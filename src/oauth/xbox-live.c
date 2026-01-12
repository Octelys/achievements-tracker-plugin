#include "oauth/xbox-live.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include "net/browser/browser.h"
#include "net/http/http.h"
#include "net/json/json.h"
#include "crypto/crypto.h"
#include "encoding/base64.h"
#include "io/state.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TOKEN_ENDPOINT "https://login.live.com/oauth20_token.srf"
#define AUTHORIZE_ENDPOINT "https://login.live.com/oauth20_authorize.srf"
#define CONNECT_ENDPOINT "https://login.live.com/oauth20_connect.srf"
#define REGISTER_ENDPOINT "https://login.live.com/oauth20_remoteconnect.srf?otc="
#define GRANT_TYPE "urn:ietf:params:oauth:grant-type:device_code"
#define XBOX_LIVE_AUTHENTICATE "https://user.auth.xboxlive.com/user/authenticate"
#define DEVICE_AUTHENTICATE "https://device.auth.xboxlive.com/device/authenticate"
#define SISU_AUTHENTICATE "https://sisu.xboxlive.com/authorize"
#define TITLE_AUTHENTICATE "https://title.auth.xboxlive.com/title/authenticate"

#define CLIENT_ID "000000004c12ae6f"
#define SCOPE "service::user.auth.xboxlive.com::MBI_SSL"

#if defined(_WIN32)
#include <windows.h>
static void sleep_ms(unsigned int ms) {
    Sleep(ms);
}
#else
#include <unistd.h>
static void sleep_ms(unsigned int ms) {
    usleep(ms * 1000);
}
#endif

#define CLEAR(p)	\
	if (p)			\
		p = NULL;

#define COPY_OR_FREE(src, dst)	\
	if (dst)					\
		*dst = src;				\
	else						\
		bfree(src);

/* */
typedef struct authentication_ctx {
    /* Input parameters */
    const device_t *device;

    char *device_code;
    long  interval_in_seconds;
    long  sleep_time;
    long  expires_in_seconds;

    pthread_t thread;

    xbox_live_authenticate_result_t result;

    /* Returned values */
    token_t *user_token;
    token_t *device_token;
} authentication_ctx_t;

/**
 * Retrieves the title token
 *
 * @param ctx
 */
static void retrieve_sisu_token(struct authentication_ctx *ctx) {

    char json_body[16384];
    snprintf(json_body,
             sizeof(json_body),
             "{\"AccessToken\":\"t=%s\",\"AppId\":\"%s\",\"DeviceToken\":\"%s\",\"Sandbox\":\"RETAIL\",\"UseModernGamertag\":true},\"SiteName\":\"user.auth.xboxlive.com\",\"RelyingParty\":\"http://xboxlive.com\",\"ProofKey\":\"%s\"}",
             ctx->user_token->value,
             CLIENT_ID,
             ctx->device_token->value,
             crypto_to_string(ctx->device->keys, false));

    obs_log(LOG_INFO, "Body: %s", json_body);

    size_t   signature_len = 0;
    uint8_t *signature     = crypto_sign(ctx->device->keys, SISU_AUTHENTICATE, "", json_body, &signature_len);

    if (!signature) {
        ctx->result.error_message = "Unable retrieve a sisu token: signing failed";
        obs_log(LOG_ERROR, ctx->result.error_message);
        return;
    }

    char *signature_b64 = base64_encode(signature, signature_len);

    if (!signature_b64) {
        ctx->result.error_message = "Unable retrieve a sisu token: encoding of the signature failed";
        obs_log(LOG_ERROR, ctx->result.error_message);
        return;
    }

    obs_log(LOG_INFO, "Signature (base64): %s", signature_b64);

    char extra_headers[4096];
    snprintf(extra_headers,
             sizeof(extra_headers),
             "signature: %s\r\n"
             "Cache-Control: no-store, must-revalidate, no-cache\r\n"
             "Content-Type: text/plain;charset=UTF-8\r\n"
             "x-xbl-contract-version: 1\r\n",
             signature_b64);

    obs_log(LOG_INFO, "Sending request for sisu token: %s", json_body);

    long  http_code       = 0;
    char *sisu_token_json = http_post(SISU_AUTHENTICATE, json_body, extra_headers, &http_code);

    bfree(signature_b64);

    if (!sisu_token_json) {
        ctx->result.error_message = "Unable to retrieve a sisu token: received no response from the server";
        obs_log(LOG_ERROR, ctx->result.error_message);
        return;
    }

    if (http_code < 200 || http_code >= 300) {
        ctx->result.error_message = "Unable to retrieve a sisu token: received error from the server";
        obs_log(LOG_ERROR, "Unable to retrieve a sisu token: received status code '%d'", http_code);
        bfree(sisu_token_json);
        return;
    }

    obs_log(LOG_INFO, "Response\n%s", sisu_token_json);

    char *sisu_token = json_read_string_from_path(sisu_token_json, "AuthorizationToken.Token");

    if (!sisu_token) {
        ctx->result.error_message = "Unable to retrieve a sisu token: no token found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        bfree(sisu_token_json);
        return;
    }

    char *xid = json_read_string(sisu_token_json, "xid");

    if (!xid) {
        ctx->result.error_message = "Unable to retrieve the xid: no value found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        bfree(sisu_token_json);
        bfree(sisu_token);
        return;
    }

    char *uhs = json_read_string(sisu_token_json, "uhs");

    if (!uhs) {
        ctx->result.error_message = "Unable to retrieve the uhs: no value found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        bfree(sisu_token_json);
        bfree(sisu_token);
        bfree(xid);
        return;
    }

    char *not_after = json_read_string(sisu_token_json, "NotAfter");

    if (!not_after) {
        ctx->result.error_message = "Unable to retrieve the NotAfter: no value found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        bfree(sisu_token_json);
        bfree(sisu_token);
        bfree(xid);
        bfree(uhs);
        return;
    }

    char *gtg = json_read_string(sisu_token_json, "gtg");

    if (!gtg) {
        ctx->result.error_message = "Unable to retrieve the gtg: no value found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        bfree(sisu_token_json);
        bfree(sisu_token);
        return;
    }

    obs_log(LOG_INFO, "sisu authentication succeeded!");

    obs_log(LOG_INFO, "gtg: %s\n", gtg);
    obs_log(LOG_INFO, "XID: %s\n", xid);
    obs_log(LOG_INFO, "Hash: %s\n", uhs);

    token_t *xbox_token = bzalloc(sizeof(token_t));
    xbox_token->value   = sisu_token;
    xbox_token->expires = 100;

    xbox_identity_t *identity = bzalloc(sizeof(xbox_identity_t));
    identity->gamertag        = gtg;
    identity->xid             = xid;
    identity->token           = xbox_token;

    state_set_xbox_identity(identity);

    bfree(sisu_token_json);
    bfree(sisu_token);
    bfree(xid);
    bfree(uhs);
    bfree(not_after);
}

/**
 * Retrieves the device token
 *
 * @param ctx
 */
static void retrieve_device_token(struct authentication_ctx *ctx) {

    token_t *existing_device_token = state_get_device_token();

    if (existing_device_token) {
        obs_log(LOG_INFO, "Using cached device token");
        ctx->device_token = existing_device_token;
        retrieve_sisu_token(ctx);
        return;
    }

    obs_log(LOG_INFO, "No device token cached found");

    char json_body[8192];
    snprintf(json_body,
             sizeof(json_body),
             "{\"Properties\":{\"AuthMethod\":\"ProofOfPossession\",\"Id\":\"{%s}\",\"DeviceType\":\"iOS\",\"SerialNumber\":\"{%s}\",\"Version\":\"1.0.0\",\"ProofKey\":%s},\"RelyingParty\":\"http://auth.xboxlive.com\",\"TokenType\":\"JWT\"}",
             ctx->device->uuid,
             ctx->device->serial_number,
             crypto_to_string(ctx->device->keys, false));

    obs_log(LOG_INFO, "Requesting device token: %s", json_body);

    size_t   signature_len = 0;
    uint8_t *signature     = crypto_sign(ctx->device->keys, DEVICE_AUTHENTICATE, "", json_body, &signature_len);

    if (!signature) {
        ctx->result.error_message = "Unable retrieve a device token: signing failed";
        obs_log(LOG_ERROR, "Unable to sign the request for a device token");
        return;
    }

    char *encoded_signature = base64_encode(signature, signature_len);

    if (!encoded_signature) {
        ctx->result.error_message = "Unable retrieve a device token: signature encoding failed";
        obs_log(LOG_ERROR, "Unable to base64-encode the request signature");
        return;
    }

    obs_log(LOG_INFO, "Encoded signature: %s", encoded_signature);

    char extra_headers[4096];
    snprintf(extra_headers,
             sizeof(extra_headers),
             "signature: %s\r\n"
             "Cache-Control: no-store, must-revalidate, no-cache\r\n"
             "Content-Type: text/plain;charset=UTF-8\r\n"
             "x-xbl-contract-version: 1\r\n",
             encoded_signature);

    long  http_code         = 0;
    char *device_token_json = http_post(DEVICE_AUTHENTICATE, json_body, extra_headers, &http_code);

    bfree(encoded_signature);

    if (!device_token_json) {
        ctx->result.error_message = "Unable retrieve a device token: server returned no response";
        obs_log(LOG_ERROR, "Device authentication failed (no response)");
        return;
    }

    if (http_code < 200 || http_code >= 300) {
        ctx->result.error_message = "Unable retrieve a device token: server returned an error";
        obs_log(LOG_ERROR, "Device authentication failed. Received status code: %d", http_code);
        bfree(device_token_json);
        return;
    }

    char *token = json_read_string(device_token_json, "Token");

    if (!token) {
        ctx->result.error_message = "Unable retrieve a device token: unable to read the token from the response";
        obs_log(LOG_ERROR, "Unable parse the device token from the response: %s", device_token_json);
        bfree(device_token_json);
        return;
    }

    //	TODO
    //	Need to retrieve the NotAfter datetime
    //	"IssueInstant": "2026-01-11T01:20:09.7849404Z",
    //	"NotAfter": "2026-01-25T01:20:09.7849404Z",

    obs_log(LOG_INFO, "Device authentication succeeded!");

    token_t *device = (token_t *)bzalloc(sizeof(token_t));
    device->value   = bstrdup_n(token, strlen(token));

    state_set_device_token(device);

    bfree(token);

    ctx->device_token = device;

    retrieve_sisu_token(ctx);
}

/**
 *
 * @param ctx
 * @return
 */
static void wait_for_user_token_loop(authentication_ctx_t *ctx) {

    char get_token_form_url_encoded[8192];
    snprintf(get_token_form_url_encoded,
             sizeof(get_token_form_url_encoded),
             "client_id=%s&device_code=%s&grant_type=%s",
             CLIENT_ID,
             ctx->device_code,
             GRANT_TYPE);

    obs_log(LOG_INFO, "Waiting for the user to validate the code");

    time_t       start_time = time(NULL);
    long         code       = 0;
    unsigned int interval   = (unsigned int)ctx->interval_in_seconds * 1000;
    long         expires_in = ctx->expires_in_seconds * 1000;

    while (time(NULL) - start_time < expires_in) {

        sleep_ms(interval);

        char *json = http_get(TOKEN_ENDPOINT, NULL, get_token_form_url_encoded, &code);

        if (code != 200) {
            obs_log(LOG_INFO,
                    "Device not validated yet. Received code %d, Waiting %d second before retrying...",
                    code,
                    interval);
        } else {

            char *token = json_read_string(json, "access_token");

            if (token) {
                token_t *user_token = (token_t *)bzalloc(sizeof(token_t));
                user_token->value   = bstrdup_n(token, strlen(token));
                ctx->user_token     = user_token;

                /* Saves the user token */
                state_set_user_token(user_token);

                bfree(token);
                obs_log(LOG_INFO, "User token received");
                bfree(json);
                break;
            }

            ctx->result.error_message = "Could not parse access_token from token response";
            obs_log(LOG_ERROR, ctx->result.error_message);
        }

        if (json) {
            bfree(json);
        }
    }

    if (!ctx->user_token) {
        return;
    }

    retrieve_device_token(ctx);
}

/**
 *
 * @param param
 * @return
 */
static void *start_authentication_flow(void *param) {

    authentication_ctx_t *ctx = (authentication_ctx_t *)param;

    token_t *user_token = state_get_user_token();

    if (user_token) {
        obs_log(LOG_INFO, "Using cached user token");
        ctx->user_token = user_token;
        retrieve_device_token(ctx);
        return (void *)false;
    }

    obs_log(LOG_INFO, "Starting Xbox sign-in in browser");

    /* ******************************* */
    /* Builds the www-form-url-encoded */
    /* ******************************* */
    char *scope_enc = http_urlencode(SCOPE);

    if (!scope_enc) {
        obs_log(LOG_WARNING, ctx->result.error_message);
        return (void *)false;
    }

    char form_url_encoded[8192];
    snprintf(form_url_encoded,
             sizeof(form_url_encoded),
             "client_id=%s&response_type=device_code&scope=%s",
             CLIENT_ID,
             scope_enc);

    bfree(scope_enc);

    /* ************************************************ */
    /* Request a device code from the connect endpoint. */
    /* ************************************************ */
    long http_code = 0;

    char *token_json = http_post_form(CONNECT_ENDPOINT, form_url_encoded, &http_code);

    if (!token_json) {
        ctx->result.error_message = "Unable to retrieve a user token: received no response from the server";
        obs_log(LOG_ERROR, ctx->result.error_message);
        return (void *)false;
    }

    if (http_code < 200 || http_code >= 300) {
        ctx->result.error_message = "Unable to retrieve a user token: received an error from the server";
        obs_log(LOG_ERROR, "Unable to retrieve a user token:  %ld", http_code);
        bfree(token_json);
        return (void *)false;
    }

    /* ******************************************* */
    /* Retrieves the information from the response */
    /* ******************************************* */
    char *user_code = json_read_string(token_json, "user_code");

    if (!user_code) {
        ctx->result.error_message =
            "Unable to received a user token: could not parse the user_code from token response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        bfree(token_json);
        return (void *)false;
    }

    char *device_code = json_read_string(token_json, "device_code");

    if (!device_code) {
        ctx->result.error_message =
            "Unable to received a user token: could not parse the device_code from token response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        bfree(token_json);
        bfree(user_code);
        return (void *)false;
    }

    long *interval = json_get_long_value(token_json, "interval");

    if (!interval) {
        ctx->result.error_message = "Unable to received a user token: could not parse the interval from token response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        bfree(interval);
        bfree(token_json);
        bfree(user_code);
        return (void *)false;
    }

    long *expires_in = json_get_long_value(token_json, "expires_in");

    if (!expires_in) {
        ctx->result.error_message =
            "Unable to received a user token: could not parse the expires_in from token response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        bfree(expires_in);
        bfree(interval);
        bfree(token_json);
        bfree(user_code);
        return (void *)false;
    }

    ctx->device_code         = device_code;
    ctx->interval_in_seconds = *interval;
    ctx->expires_in_seconds  = *expires_in;

    /* ***************************************** */
    /* Open the browser to the verification URL. */
    /* ***************************************** */
    char verification_uri[4096];
    snprintf(verification_uri, sizeof(verification_uri), "%s%s", REGISTER_ENDPOINT, user_code);

    obs_log(LOG_DEBUG, "Open browser for OAuth verification at URL: %s", verification_uri);

    if (!open_url(verification_uri)) {
        ctx->result.error_message = "Unable to received a user token: could not open the browser";
        obs_log(LOG_ERROR, ctx->result.error_message);
        bfree(expires_in);
        bfree(interval);
        bfree(token_json);
        bfree(user_code);
        return (void *)false;
    }

    /* ************************************* */
    /* Starts the loop waiting for the token */
    /* ************************************* */
    wait_for_user_token_loop(ctx);

    free(expires_in);
    bfree(interval);
    bfree(token_json);
    bfree(user_code);

    return (void *)true;
}

bool xbox_live_get_authenticate(const device_t *device) {

    /* Defines the structure that will filled up by the different authentication steps */
    authentication_ctx_t *ctx = bzalloc(sizeof(authentication_ctx_t));
    ctx->device               = device;

    return pthread_create(&ctx->thread, NULL, start_authentication_flow, ctx) == 0;
}
