#include "oauth/xbox-live.h"

/**
 * @file xbox-live.c
 * @brief Xbox Live authentication flow implementation.
 *
 * High-level flow:
 *  1) Acquire a user access token via Microsoft device-code flow.
 *     - Prefer cached access token if available.
 *     - Otherwise, prefer cached refresh token.
 *     - Otherwise, request a device_code/user_code and open the browser for the user.
 *     - Poll TOKEN_ENDPOINT until the user authorizes and tokens are returned.
 *  2) Acquire a device token (Proof-of-Possession) using the emulated device identity.
 *     - Prefer cached device token if available and not expired.
 *  3) Acquire a SISU token and persist the Xbox identity data.
 *
 * The work is performed on a background pthread; completion is reported via the
 * callback provided to xbox_live_authenticate().
 *
 * @note This module relies on other helpers for persistence (state_*), JSON
 *       extraction (json_*), HTTP (http_*), signing (crypto_sign), and opening a
 *       browser (open_url).
 */

#include <obs-module.h>
#include <diagnostics/log.h>

#include "net/browser/browser.h"
#include "net/http/http.h"
#include "net/json/json.h"
#include "crypto/crypto.h"
#include "encoding/base64.h"
#include "io/state.h"
#include "time/time.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TOKEN_ENDPOINT "https://login.live.com/oauth20_token.srf"
#define CONNECT_ENDPOINT "https://login.live.com/oauth20_connect.srf"
#define REGISTER_ENDPOINT "https://login.live.com/oauth20_remoteconnect.srf?otc="
#define GRANT_TYPE "urn:ietf:params:oauth:grant-type:device_code"
#define XBOX_LIVE_AUTHENTICATE "https://user.auth.xboxlive.com/user/authenticate"
#define DEVICE_AUTHENTICATE "https://device.auth.xboxlive.com/device/authenticate"
#define SISU_AUTHENTICATE "https://sisu.xboxlive.com/authorize"

#define CLIENT_ID "000000004c12ae6f"
#define SCOPE "service::user.auth.xboxlive.com::MBI_SSL"

typedef struct authentication_ctx {
    /** Input device identity (owned by caller; must outlive the flow). */
    device_t *device;
    bool      allow_cache;

    /** Background worker thread running the flow. */
    pthread_t thread;

    /** Completion callback invoked when the flow finishes (success or error). */
    on_xbox_live_authenticated_t on_completed;

    /** Opaque pointer forwarded to on_completed. */
    void *on_completed_data;

    /** Device-code flow: device_code (allocated). */
    char *device_code;

    /** Device-code flow: server-provided polling interval in seconds. */
    long interval_in_seconds;

    /** Sleep time (currently unused / reserved). */
    long sleep_time;

    /** Device-code flow: device-code expiry in seconds. */
    long expires_in_seconds;

    /** Result struct holding any error message / status for the caller. */
    xbox_live_authenticate_result_t result;

    /** Access token obtained for the current user (allocated/persisted elsewhere). */
    token_t *user_token;

    /** Refresh token for the current user (allocated/persisted elsewhere). */
    token_t *refresh_token;

    /** Device (PoP) token used for SISU/device authentication (allocated/persisted elsewhere). */
    token_t *device_token;

} authentication_ctx_t;

//  --------------------------------------------------------------------------------------------------------------------
//  Private
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Notify the caller that the authentication flow has completed.
 *
 * This simply invokes the caller-provided callback (if any).
 */
static void complete(authentication_ctx_t *ctx) {

    if (!ctx->on_completed) {
        return;
    }

    ctx->on_completed(ctx->on_completed_data);
}

/**
 * @brief Retrieve the SISU token and persist Xbox identity data.
 *
 * This creates and signs a SISU_AUTHENTICATE request using the device PoP key,
 * then extracts:
 *  - AuthorizationToken.Token (the Xbox token)
 *  - xid, uhs, gtg (identity)
 *  - NotAfter (expiry)
 *
 * The resulting identity is persisted using state_set_xbox_identity().
 *
 * On success or failure, this function calls complete(ctx).
 */
static bool retrieve_sisu_token(authentication_ctx_t *ctx) {

    bool     succeeded       = false;
    uint8_t *signature       = NULL;
    char    *signature_b64   = NULL;
    char    *sisu_token_json = NULL;
    char    *sisu_token      = NULL;
    char    *xid             = NULL;
    char    *uhs             = NULL;
    char    *not_after_date  = NULL;
    char    *gtg             = NULL;

    /*
     * Creates the request
     */
    char json_body[16384];
    snprintf(json_body,
             sizeof(json_body),
             "{\"AccessToken\":\"t=%s\",\"AppId\":\"%s\",\"DeviceToken\":\"%s\",\"Sandbox\":\"RETAIL\",\"UseModernGamertag\":true},\"SiteName\":\"user.auth.xboxlive.com\",\"RelyingParty\":\"http://xboxlive.com\",\"ProofKey\":\"%s\"}",
             ctx->user_token->value,
             CLIENT_ID,
             ctx->device_token->value,
             crypto_to_string(ctx->device->keys, false));

    obs_log(LOG_DEBUG, "Body: %s", json_body);

    /*
     * Signs the request
     */
    size_t signature_len = 0;
    signature            = crypto_sign(ctx->device->keys, SISU_AUTHENTICATE, "", json_body, &signature_len);

    if (!signature) {
        ctx->result.error_message = "Unable retrieve a sisu token: signing failed";
        goto cleanup;
    }

    /*
     * Encodes the signature
     */
    signature_b64 = base64_encode(signature, signature_len);

    if (!signature_b64) {
        ctx->result.error_message = "Unable retrieve a sisu token: encoding of the signature failed";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    obs_log(LOG_DEBUG, "Signature (base64): %s", signature_b64);

    /*
     * Sets up the headers
     */
    char extra_headers[4096];
    snprintf(extra_headers,
             sizeof(extra_headers),
             "signature: %s\r\n"
             "Cache-Control: no-store, must-revalidate, no-cache\r\n"
             "Content-Type: text/plain;charset=UTF-8\r\n"
             "x-xbl-contract-version: 1\r\n",
             signature_b64);

    obs_log(LOG_DEBUG, "Sending request for sisu token: %s", json_body);

    /*
     * Sends the request
     */
    long http_code  = 0;
    sisu_token_json = http_post(SISU_AUTHENTICATE, json_body, extra_headers, &http_code);

    if (!sisu_token_json) {
        ctx->result.error_message = "Unable to retrieve a sisu token: received no response from the server";
        goto cleanup;
    }

    obs_log(LOG_DEBUG, "Received response with status code %d: %s", http_code, sisu_token_json);

    if (http_code < 200 || http_code >= 300) {
        ctx->result.error_message = "Unable to retrieve a sisu token: received error from the server";
        obs_log(LOG_ERROR, "Unable to retrieve a sisu token: received status code '%d'", http_code);
        goto cleanup;
    }

    /*
     * Extracts the token
     */
    sisu_token = json_read_string_from_path(sisu_token_json, "AuthorizationToken.Token");

    if (!sisu_token) {
        ctx->result.error_message = "Unable to retrieve a sisu token: no token found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    /*
     * Extracts the Xbox ID
     */
    xid = json_read_string(sisu_token_json, "xid");

    if (!xid) {
        ctx->result.error_message = "Unable to retrieve the xid: no value found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    /*
     * Extracts the Xbox User Hash
     */
    uhs = json_read_string(sisu_token_json, "uhs");

    if (!uhs) {
        ctx->result.error_message = "Unable to retrieve the uhs: no value found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    /*
     * Extracts the token expiration
     */
    not_after_date = json_read_string_from_path(sisu_token_json, "AuthorizationToken.NotAfter");

    if (!not_after_date) {
        ctx->result.error_message = "Unable to retrieve the NotAfter: no value found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    int32_t fraction       = 0;
    int64_t unix_timestamp = 0;

    if (!time_iso8601_utc_to_unix(not_after_date, &unix_timestamp, &fraction)) {
        ctx->result.error_message = "Unable retrieve a device token: unable to read the NotAfter date";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    /*
     * Extracts the gamertag
     */
    gtg = json_read_string(sisu_token_json, "gtg");

    if (!gtg) {
        ctx->result.error_message = "Unable to retrieve the gtg: no value found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    obs_log(LOG_INFO, "Sisu authentication succeeded!");

    obs_log(LOG_INFO, "gtg: %s", gtg);
    obs_log(LOG_INFO, "XID: %s", xid);
    obs_log(LOG_INFO, "Hash: %s", uhs);
    obs_log(LOG_INFO, "Now: %d", now());
    obs_log(LOG_INFO, "Expires: %d (%s)", unix_timestamp, not_after_date);

    /*
     * Creates the Xbox identity
     */
    token_t *xbox_token = bzalloc(sizeof(token_t));
    xbox_token->value   = sisu_token;
    xbox_token->expires = unix_timestamp;

    xbox_identity_t *identity = bzalloc(sizeof(xbox_identity_t));
    identity->gamertag        = gtg;
    identity->xid             = xid;
    identity->uhs             = uhs;
    identity->token           = xbox_token;

    /*
     * Saves the identity
     */
    state_set_xbox_identity(identity);

    succeeded = true;

cleanup:
    FREE(sisu_token_json);
    FREE(sisu_token);
    FREE(xid);
    FREE(uhs);
    FREE(not_after_date);

    complete(ctx);

    return succeeded;
}

/**
 * @brief Retrieve the device PoP token required for SISU.
 *
 * If a cached device token exists, it is reused. Otherwise this signs and sends
 * a request to DEVICE_AUTHENTICATE and parses Token/NotAfter from the response.
 *
 * On success, the device token is persisted and the flow proceeds to
 * retrieve_sisu_token(). On failure it calls complete(ctx).
 */
static bool retrieve_device_token(struct authentication_ctx *ctx) {

    /*
     * Finds out if a device token already exists
     */
    token_t *existing_device_token = state_get_device_token();

    if (ctx->allow_cache && existing_device_token) {
        obs_log(LOG_INFO, "Using cached device token");
        ctx->device_token = existing_device_token;
        return retrieve_sisu_token(ctx);
    }

    bool     succeeded         = false;
    char    *encoded_signature = NULL;
    char    *not_after_date    = NULL;
    char    *token             = NULL;
    char    *device_token_json = NULL;
    uint8_t *signature         = NULL;

    obs_log(LOG_INFO, "No device token cached found. Requesting a new device token");

    /*
     * Builds the device token request
     */
    char json_body[8192];
    snprintf(json_body,
             sizeof(json_body),
             "{\"Properties\":{\"AuthMethod\":\"ProofOfPossession\",\"Id\":\"{%s}\",\"DeviceType\":\"iOS\",\"SerialNumber\":\"{%s}\",\"Version\":\"1.0.0\",\"ProofKey\":%s},\"RelyingParty\":\"http://auth.xboxlive.com\",\"TokenType\":\"JWT\"}",
             ctx->device->uuid,
             ctx->device->serial_number,
             crypto_to_string(ctx->device->keys, false));

    obs_log(LOG_DEBUG, "Device token request is: %s", json_body);

    /*
     * Signs the request
     */
    size_t signature_len = 0;
    signature            = crypto_sign(ctx->device->keys, DEVICE_AUTHENTICATE, "", json_body, &signature_len);

    if (!signature) {
        ctx->result.error_message = "Unable retrieve a device token: signing failed";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    /*
     * Encodes the signature
     */
    encoded_signature = base64_encode(signature, signature_len);

    if (!encoded_signature) {
        ctx->result.error_message = "Unable retrieve a device token: signature encoding failed";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    obs_log(LOG_DEBUG, "Encoded signature: %s", encoded_signature);

    /*
     * Creates the headers
     */
    char extra_headers[4096];
    snprintf(extra_headers,
             sizeof(extra_headers),
             "signature: %s\r\n"
             "Cache-Control: no-store, must-revalidate, no-cache\r\n"
             "Content-Type: text/plain;charset=UTF-8\r\n"
             "x-xbl-contract-version: 1\r\n",
             encoded_signature);

    /*
     * Sends the request
     */
    long http_code    = 0;
    device_token_json = http_post(DEVICE_AUTHENTICATE, json_body, extra_headers, &http_code);

    if (!device_token_json) {
        ctx->result.error_message = "Unable retrieve a device token: server returned no response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        return false;
        ;
    }

    obs_log(LOG_DEBUG, "Received response with status code %d: %s", http_code, device_token_json);

    if (http_code < 200 || http_code >= 300) {
        ctx->result.error_message = "Unable retrieve a device token: server returned an error";
        obs_log(LOG_ERROR, "Unable retrieve a device token: server returned status code %d", http_code);
        FREE(device_token_json);
        return false;
    }

    /*
     * Retrieves the device token
     */
    token = json_read_string(device_token_json, "Token");

    if (!token) {
        ctx->result.error_message = "Unable retrieve a device token: unable to read the token from the response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    not_after_date = json_read_string(device_token_json, "NotAfter");

    if (!not_after_date) {
        ctx->result.error_message =
            "Unable retrieve a device token: unable to read the NotAfter field from the response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    int32_t fraction       = 0;
    int64_t unix_timestamp = 0;

    if (!time_iso8601_utc_to_unix(not_after_date, &unix_timestamp, &fraction)) {
        ctx->result.error_message = "Unable retrieve a device token: unable to read the NotAfter date";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    /*
     * Saves the device token
     */
    obs_log(LOG_INFO, "Device authentication succeeded!");

    token_t *device = bzalloc(sizeof(token_t));
    device->value   = bstrdup_n(token, strlen(token));
    device->expires = unix_timestamp;

    state_set_device_token(device);

    ctx->device_token = device;
    succeeded         = true;

cleanup:
    free_memory((void **)&signature);
    free_memory((void **)&encoded_signature);
    free_memory((void **)&device_token_json);
    free_memory((void **)&token);
    free_memory((void **)&not_after_date);

    if (ctx->result.error_message) {
        complete(ctx);
    }

    if (!succeeded) {
        return false;
    }

    return retrieve_sisu_token(ctx);
}

/**
 * @brief Refresh the user access token using a cached refresh token.
 *
 * Sends a request to TOKEN_ENDPOINT and extracts access_token, refresh_token and
 * expires_in. On success it persists the tokens and proceeds to
 * retrieve_device_token(). On failure it calls complete(ctx).
 */
static bool refresh_user_token(authentication_ctx_t *ctx) {

    bool  succeeded           = false;
    char *response_json       = NULL;
    char *access_token_value  = NULL;
    char *refresh_token_value = NULL;
    long *token_expires_in    = NULL;

    /*
     * Creates the request.
     */
    char refresh_token_form_url_encoded[8192];
    snprintf(refresh_token_form_url_encoded,
             sizeof(refresh_token_form_url_encoded),
             "client_id=%s&refresh_token=%s&grant_type=%s",
             CLIENT_ID,
             ctx->refresh_token->value,
             GRANT_TYPE);

    long http_code = 0;
    response_json  = http_get(TOKEN_ENDPOINT, NULL, refresh_token_form_url_encoded, &http_code);

    if (http_code < 200 || http_code > 300) {
        ctx->result.error_message = "Unable to refresh the user token: server returned an error";
        obs_log(LOG_ERROR, "Unable to refresh the user token: server returned an error. Received status %d", http_code);
        goto cleanup;
    }

    if (!response_json) {
        ctx->result.error_message = "Unable to refresh the user token: server returned no response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    obs_log(LOG_WARNING, "Response received: %s", response_json);

    access_token_value  = json_read_string(response_json, "access_token");
    refresh_token_value = json_read_string(response_json, "refresh_token");
    token_expires_in    = json_read_long(response_json, "expires_in");

    if (!access_token_value) {
        ctx->result.error_message = "Unable to refresh the user token: no access_token field found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    if (!refresh_token_value) {
        ctx->result.error_message = "Unable to refresh the user token: no refresh_token field found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    if (!token_expires_in) {
        ctx->result.error_message = "Unable to refresh the user token: no expires_in field found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    /*
     *  The token has been found and is saved in the context
     */
    token_t *user_token = bzalloc(sizeof(token_t));
    user_token->value   = bstrdup_n(access_token_value, strlen(access_token_value));
    user_token->expires = time(NULL) + *token_expires_in / 1000;

    token_t *refresh_token = bzalloc(sizeof(token_t));
    refresh_token->value   = bstrdup_n(refresh_token_value, strlen(refresh_token_value));

    ctx->user_token = user_token;

    /* And in the persistence */
    state_set_user_token(user_token, refresh_token);

    succeeded = true;

    obs_log(LOG_INFO, "User & refresh token received");

cleanup:
    free_memory((void **)&access_token_value);
    free_memory((void **)&refresh_token_value);
    free_memory((void **)&token_expires_in);
    free_memory((void **)&response_json);

    /*
     * Either complete the process if an error has been encountered or go to the next step
     */
    if (!ctx->user_token) {
        complete(ctx);
        return false;
    }

    if (!succeeded) {
        return false;
    }

    return retrieve_device_token(ctx);
}

/**
 * @brief Poll TOKEN_ENDPOINT until the user completes device-code verification.
 *
 * This repeatedly sleeps for the server-provided interval and performs a GET to
 * TOKEN_ENDPOINT with device_code and grant_type. When a 200 response includes
 * access_token/refresh_token/expires_in, the tokens are persisted and the flow
 * proceeds to retrieve_device_token().
 *
 * On timeout/expiry or parse failure, the flow completes with an error.
 */
static void poll_for_user_token(authentication_ctx_t *ctx) {

    /*
     * Creates the request.
     */
    char get_token_form_url_encoded[8192];
    snprintf(get_token_form_url_encoded,
             sizeof(get_token_form_url_encoded),
             "client_id=%s&device_code=%s&grant_type=%s",
             CLIENT_ID,
             ctx->device_code,
             GRANT_TYPE);

    obs_log(LOG_INFO, "Waiting for the user to validate the code");

    /*
     *  Polls the server at a regular interval (as instructed by the server)
     */
    time_t       start_time = time(NULL);
    long         code       = 0;
    unsigned int interval   = (unsigned int)ctx->interval_in_seconds * 1000;
    long         expires_in = ctx->expires_in_seconds * 1000;

    while (time(NULL) - start_time < expires_in) {

        sleep_ms(interval);

        char *json = http_get(TOKEN_ENDPOINT, NULL, get_token_form_url_encoded, &code);

        if (code != 200) {
            obs_log(LOG_INFO,
                    "Device not validated yet. Received status code %d, Waiting %d second before retrying...",
                    code,
                    interval);
        } else {

            obs_log(LOG_WARNING, "Response received: %s", json);

            char *access_token_value  = json_read_string(json, "access_token");
            char *refresh_token_value = json_read_string(json, "refresh_token");
            long *token_expires_in    = json_read_long(json, "expires_in");

            if (access_token_value && refresh_token_value && token_expires_in) {
                /*
                 *  The token has been found and is saved in the context
                 */
                token_t *user_token = (token_t *)bzalloc(sizeof(token_t));
                user_token->value   = bstrdup_n(access_token_value, strlen(access_token_value));
                // user_token->expires = time(NULL) + *token_expires_in;
                user_token->expires = time(NULL) + 30;

                token_t *refresh_token = (token_t *)bzalloc(sizeof(token_t));
                refresh_token->value   = bstrdup_n(refresh_token_value, strlen(refresh_token_value));

                ctx->user_token = user_token;

                /* And in the persistence */
                state_set_user_token(user_token, refresh_token);

                obs_log(LOG_INFO, "User & refresh token received");
                FREE(access_token_value);
                FREE(refresh_token_value);
                FREE(token_expires_in);
                FREE(json);
                break;
            }

            ctx->result.error_message = "Could not parse access_token from token response";
            obs_log(LOG_ERROR, ctx->result.error_message);
            FREE(access_token_value);
            FREE(refresh_token_value);
            FREE(token_expires_in);
        }

        FREE(json);
    }

    /*
     * Either complete the process if an error has been encountered or go to the next step
     */
    if (!ctx->user_token) {
        complete(ctx);
    } else {
        retrieve_device_token(ctx);
    }
}

/**
 * @brief Worker thread entry point running the full authentication flow.
 *
 * Order of preference:
 *  1) If a non-expired cached user token exists, reuse it.
 *  2) Else if a refresh token exists, refresh the user token.
 *  3) Else perform device-code flow: request codes, open browser, poll.
 *
 * Final step after user token is available: retrieve device token + SISU token.
 */
static void *start_authentication_flow(void *param) {

    long *expires_in  = NULL;
    long *interval    = NULL;
    char *scope_enc   = NULL;
    char *token_json  = NULL;
    char *device_code = NULL;
    char *user_code   = NULL;

    authentication_ctx_t *ctx = param;

    token_t *user_token = state_get_user_token();

    if (user_token) {
        obs_log(LOG_INFO, "Using cached user token");
        ctx->user_token = user_token;
        goto cleanup;
    }

    token_t *refresh_token = state_get_user_refresh_token();

    if (refresh_token) {
        obs_log(LOG_INFO, "Using refresh token");
        ctx->refresh_token = refresh_token;
        refresh_user_token(ctx);
        goto cleanup;
    }

    obs_log(LOG_INFO, "Starting Xbox sign-in in browser");

    /*
     * Builds the www-form-url-encoded
     */
    scope_enc = http_urlencode(SCOPE);

    if (!scope_enc) {
        obs_log(LOG_WARNING, ctx->result.error_message);
        goto cleanup;
    }

    char form_url_encoded[8192];
    snprintf(form_url_encoded,
             sizeof(form_url_encoded),
             "client_id=%s&response_type=device_code&scope=%s",
             CLIENT_ID,
             scope_enc);

    /*
     * Requests a device code from the connect endpoint
     */
    long http_code = 0;
    token_json     = http_post_form(CONNECT_ENDPOINT, form_url_encoded, &http_code);

    if (!token_json) {
        ctx->result.error_message = "Unable to retrieve a user token: received no response from the server";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    if (http_code < 200 || http_code >= 300) {
        ctx->result.error_message = "Unable to retrieve a user token: received an error from the server";
        obs_log(LOG_ERROR, "Unable to retrieve a user token:  %ld", http_code);
        goto cleanup;
    }

    /*
     * Retrieves the information from the response
     */
    user_code = json_read_string(token_json, "user_code");

    if (!user_code) {
        ctx->result.error_message = "Unable to received a user token: could not parse the user_code from the response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    device_code = json_read_string(token_json, "device_code");

    if (!device_code) {
        ctx->result.error_message =
            "Unable to received a user token: could not parse the device_code from the response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    interval = json_read_long(token_json, "interval");

    if (!interval) {
        ctx->result.error_message = "Unable to received a user token: could not parse the interval from token response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    expires_in = json_read_long(token_json, "expires_in");

    if (!expires_in) {
        ctx->result.error_message =
            "Unable to received a user token: could not parse the expires_in from token response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    ctx->device_code         = device_code;
    ctx->interval_in_seconds = *interval;
    ctx->expires_in_seconds  = *expires_in;

    /*
     * Open the browser to the verification URL
     */
    char verification_uri[4096];
    snprintf(verification_uri, sizeof(verification_uri), "%s%s", REGISTER_ENDPOINT, user_code);

    obs_log(LOG_DEBUG, "Open browser for OAuth verification at URL: %s", verification_uri);

    if (!open_url(verification_uri)) {
        ctx->result.error_message = "Unable to received a user token: could not open the browser";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    /*
     * Starts the loop waiting for the token
     */
    poll_for_user_token(ctx);

cleanup:
    free_memory((void **)&scope_enc);
    free_memory((void **)&token_json);
    free_memory((void **)&device_code);
    free_memory((void **)&expires_in);
    free_memory((void **)&interval);
    free_memory((void **)&user_code);

    if (!ctx->result.error_message) {
        retrieve_device_token(ctx);
    } else {
        complete(ctx);
    }

    /* TODO ctx! */

    return (void *)false;
}

//  --------------------------------------------------------------------------------------------------------------------
//  Public
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Start Xbox Live authentication on a background thread.
 *
 * Allocates an internal authentication context and launches a pthread.
 * Completion is signaled via @p callback.
 *
 * @param data     Opaque pointer passed back to @p callback.
 * @param callback Completion callback (must be non-NULL).
 *
 * @return true if the worker thread was successfully created; false otherwise.
 */
bool xbox_live_authenticate(void *data, on_xbox_live_authenticated_t callback) {

    device_t *device = state_get_device();

    if (!device) {
        obs_log(LOG_ERROR, "Unable to authenticate: no device identity found");
        return false;
    }

    /* Defines the structure that will filled up by the different authentication steps */
    authentication_ctx_t *ctx = bzalloc(sizeof(authentication_ctx_t));
    ctx->device               = device;
    ctx->on_completed         = callback;
    ctx->on_completed_data    = data;
    ctx->allow_cache          = true;

    return pthread_create(&ctx->thread, NULL, start_authentication_flow, ctx) == 0;
}

/**
 * @brief Get the currently stored Xbox identity, refreshing tokens if needed.
 *
 * This is a convenience helper around the state subsystem:
 *  - If an identity is already present and its token is not expired, it is
 *    returned immediately.
 *  - If the token is expired, this function attempts to refresh authentication
 *    using the persisted device identity and refresh token.
 *
 * Side-effects:
 *  - May perform network requests to refresh tokens.
 *  - May persist updated tokens/identity via the state subsystem.
 *
 * Threading:
 *  - This function is synchronous and may block while performing network I/O.
 *  - Call from a worker thread (not the OBS render thread).
 *
 * Ownership:
 *  - Returns an xbox_identity_t allocated/returned by state_get_xbox_identity().
 *    The caller owns the returned object and must free it consistently with the
 *    state subsystem contract.
 *
 * @return Xbox identity on success; NULL if no identity is available or refresh
 *         fails.
 */
xbox_identity_t *xbox_live_get_identity(void) {

    xbox_identity_t *identity = state_get_xbox_identity();

    if (!identity) {
        obs_log(LOG_INFO, "No identity found");
        return identity;
    }

    /* Checks if the Sisu token is expired */
    if (!token_is_expired(identity->token)) {
        obs_log(LOG_INFO, "Token is NOT expired, reusing existing identity");
        return identity;
    }

    obs_log(LOG_INFO, "Sisu token is expired, refreshing...");

    device_t *device = state_get_device();

    if (!device) {
        obs_log(LOG_ERROR, "No device found for Xbox token refresh");
        return false;
    }

    authentication_ctx_t *ctx = bzalloc(sizeof(authentication_ctx_t));
    ctx->device               = device;
    ctx->on_completed         = NULL;
    ctx->on_completed_data    = NULL;
    ctx->allow_cache          = false;

    /* Retrieves the user token */
    token_t *user_token = state_get_user_token();

    if (!user_token) {
        obs_log(LOG_ERROR, "No user token found for Xbox token refresh");
        identity = NULL;
        goto cleanup;
    }

    if (token_is_expired(user_token)) {
        /* All the tokens (User, Device and Sisu) will be retrieved */
        if (!refresh_user_token(ctx)) {
            identity = NULL;
            goto cleanup;
        }
        identity = state_get_xbox_identity();
        goto cleanup;
    }

    ctx->user_token = user_token;

    /* Retrieves the device token */
    token_t *device_token = state_get_device_token();

    if (!device_token) {
        obs_log(LOG_ERROR, "No device token found for Xbox token refresh");
        identity = NULL;
        goto cleanup;
    }

    if (token_is_expired(device_token)) {
        /* All the tokens (Device and Sisu) will be retrieved */
        if (!retrieve_device_token(ctx)) {
            identity = NULL;
            goto cleanup;
        }
        identity = state_get_xbox_identity();
        goto cleanup;
    }

    ctx->device_token = device_token;

    /* Defines the structure that will filled up by the different authentication steps */
    if (!retrieve_sisu_token(ctx)) {
        identity = NULL;
        goto cleanup;
    }

    identity = state_get_xbox_identity();

cleanup:
    free_memory((void **)&ctx->device);
    free_memory((void **)&ctx->user_token);
    free_memory((void **)&ctx->device_token);
    free_memory((void **)&ctx);

    return identity;
}
