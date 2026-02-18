#include "oauth/xbox-live.h"

/**
 * @file xbox-live.c
 * @brief Xbox Live authentication flow implementation for OBS plugin.
 *
 * This module implements the complete Xbox Live authentication flow using Microsoft's
 * OAuth 2.0 device code flow combined with Xbox Live's SISU (Sign-In Service Unified)
 * authentication system.
 *
 * ## Authentication Flow Overview
 *
 * The authentication process consists of three main stages:
 *
 * ### Stage 1: Microsoft User Authentication (OAuth 2.0 Device Code Flow)
 * Acquires a user access token via Microsoft's device code flow:
 * - **Cached Token**: If a valid cached access token exists, it is reused.
 * - **Refresh Token**: If the access token is expired but a refresh token exists,
 *   the access token is refreshed without user interaction.
 * - **New Authentication**: If no valid tokens exist:
 *   1. Request a device_code and user_code from Microsoft's OAuth endpoint
 *   2. Open the user's browser to the verification URL
 *   3. Poll TOKEN_ENDPOINT until the user completes authorization
 *   4. Persist the returned access_token and refresh_token
 *
 * ### Stage 2: Device Token (Proof-of-Possession)
 * Acquires a device authentication token using an emulated Xbox device identity:
 * - Uses cryptographic signing (ECDSA P-256) to prove possession of the device key
 * - The device token is required for SISU authentication
 * - Cached device tokens are reused if not expired
 * - Device identity includes UUID, serial number, and public/private key pair
 *
 * ### Stage 3: SISU Token and Xbox Identity
 * Acquires the final Xbox Live authentication token:
 * - Combines the user token and device token via signed SISU request
 * - Extracts Xbox identity information (xid, uhs, gamertag)
 * - Persists the complete Xbox identity for use by other plugin components
 *
 * ## Threading Model
 *
 * All authentication work is performed on a background pthread to avoid blocking
 * the OBS main thread. Completion is signaled via callback.
 *
 * ## Token Expiration and Refresh
 *
 * - **Access Token**: Short-lived (~1 hour), automatically refreshed using refresh token
 * - **Refresh Token**: Long-lived, stored persistently, used to obtain new access tokens
 * - **Device Token**: Medium-lived (~24 hours), cached and refreshed when expired
 * - **SISU Token**: Medium-lived, contains the final Xbox Live authorization
 *
 * ## Security Considerations
 *
 * - All tokens are URL-encoded when sent in form data to prevent injection attacks
 * - Cryptographic signatures use ECDSA P-256 with proper timestamp and nonce handling
 * - Tokens are persisted securely via the state management module
 * - Device keys are generated once and reused to maintain consistent device identity
 *
 * ## Dependencies
 *
 * This module relies on several helper subsystems:
 * - **State Management** (state_*): Token and identity persistence
 * - **HTTP Client** (http_*): OAuth and Xbox Live API communication
 * - **Cryptography** (crypto_*): Request signing and key management
 * - **Encoding** (base64_*, http_urlencode): Data encoding utilities
 * - **JSON** (cJSON): Response parsing
 * - **Browser** (open_url): User authorization flow
 *
 * ## API Usage
 *
 * ```c
 * // Initiate authentication (async)
 * xbox_live_authenticate(user_data, on_auth_completed_callback);
 *
 * // Retrieve current identity (sync, may trigger refresh)
 * xbox_identity_t *identity = xbox_live_get_identity();
 * ```
 *
 * @see https://docs.microsoft.com/en-us/azure/active-directory/develop/v2-oauth2-device-code
 * @see https://docs.microsoft.com/en-us/gaming/xbox-live/api-ref/xbox-live-rest/additional/
 */

#include "cJSON.h"
#include "cJSON_Utils.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include "net/browser/browser.h"
#include "net/http/http.h"
#include "crypto/crypto.h"
#include "encoding/base64.h"
#include "io/state.h"
#include "text/convert.h"
#include "time/time.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/** @brief Microsoft OAuth token endpoint for token acquisition and refresh */
#define TOKEN_ENDPOINT "https://login.live.com/oauth20_token.srf"

/** @brief Microsoft OAuth connect endpoint for device code flow initiation */
#define CONNECT_ENDPOINT "https://login.live.com/oauth20_connect.srf"

/** @brief Microsoft OAuth remote connect endpoint for user verification URL */
#define REGISTER_ENDPOINT "https://login.live.com/oauth20_remoteconnect.srf?otc="

/** @brief OAuth grant type for device code flow */
#define GRANT_TYPE_DEVICE_CODE "urn:ietf:params:oauth:grant-type:device_code"

/** @brief OAuth grant type for refresh token flow */
#define GRANT_TYPE_REFRESH_TOKEN "refresh_token"

/** @brief Xbox Live user authentication endpoint (deprecated, not currently used) */
#define XBOX_LIVE_AUTHENTICATE "https://user.auth.xboxlive.com/user/authenticate"

/** @brief Xbox Live device authentication endpoint for Proof-of-Possession tokens */
#define DEVICE_AUTHENTICATE "https://device.auth.xboxlive.com/device/authenticate"

/** @brief Xbox Live SISU authorization endpoint for final token acquisition */
#define SISU_AUTHENTICATE "https://sisu.xboxlive.com/authorize"

/** @brief Xbox Live client ID for OAuth authentication */
#define CLIENT_ID "000000004c12ae6f"

/** @brief Required OAuth scope for Xbox Live authentication */
#define SCOPE "service::user.auth.xboxlive.com::MBI_SSL"

/**
 * @struct authentication_ctx
 * @brief Context structure for managing the Xbox Live authentication flow.
 *
 * This structure maintains all state required throughout the multi-stage
 * authentication process, from initial OAuth through final SISU token acquisition.
 * It is allocated at the start of the flow and freed when complete.
 */
typedef struct authentication_ctx {
    /**
     * Input device identity (owned by caller; must outlive the flow).
     * Contains UUID, serial number, and cryptographic keys for device authentication.
     */
    device_t *device;

    /**
     * Whether to use cached tokens. Set to false to force token refresh.
     * When true, cached user, device, and SISU tokens are preferred.
     */
    bool allow_cache;

    /**
     * Background worker thread running the authentication flow.
     * Created by xbox_live_authenticate() to avoid blocking the main thread.
     */
    pthread_t thread;

    /**
     * Completion callback invoked when the flow finishes (success or error).
     * Called exactly once at the end of the authentication process.
     */
    on_xbox_live_authenticated_t on_completed;

    /**
     * Opaque user data pointer forwarded to on_completed callback.
     * Allows caller to pass context through the async operation.
     */
    void *on_completed_data;

    /**
     * Device-code flow: device_code returned by Microsoft OAuth (allocated).
     * Used to poll for token completion and stored for later token refresh.
     */
    char *device_code;

    /**
     * Device-code flow: server-provided polling interval in seconds.
     * Dictates how frequently to check if user has completed authorization.
     */
    long interval_in_seconds;

    /**
     * Sleep time (currently unused / reserved for future rate limiting).
     */
    long sleep_time;

    /**
     * Device-code flow: device-code expiry time in seconds.
     * After this duration, the device code expires and cannot be used.
     */
    long expires_in_seconds;

    /**
     * Result struct holding any error message / status for the caller.
     * Populated if authentication fails at any stage.
     */
    xbox_live_authenticate_result_t result;

    /**
     * Microsoft access token obtained for the current user.
     * Used to authenticate with Xbox Live services. Short-lived (~1 hour).
     */
    token_t *user_token;

    /**
     * Refresh token for renewing the user access token.
     * Long-lived token that persists across sessions.
     */
    token_t *refresh_token;

    /**
     * Device (Proof-of-Possession) token used for SISU/device authentication.
     * Proves the client possesses the device private key.
     */
    token_t *device_token;

} authentication_ctx_t;

//  --------------------------------------------------------------------------------------------------------------------
//  Private
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Notify the caller that the authentication flow has completed.
 *
 * This function invokes the user-provided callback (if set) to signal that the
 * authentication process has finished, either successfully or with an error.
 * The callback receives the opaque user data pointer originally passed to
 * xbox_live_authenticate().
 *
 * ## Usage Pattern
 * Called at the end of each authentication stage when:
 * - An error occurs (ctx->result.error_message is set)
 * - The final SISU token is successfully obtained
 *
 * @param ctx Authentication context containing the callback and user data
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
 * This is the final stage of the Xbox Live authentication flow. It combines the
 * Microsoft user token and Xbox device token to obtain a complete Xbox Live identity.
 *
 * ## Process
 * 1. Constructs a JSON request body containing:
 *    - User access token (from Microsoft OAuth)
 *    - Device token (from device authentication)
 *    - Client ID and proof key (device public key)
 * 2. Signs the entire request using the device private key (ECDSA P-256)
 * 3. Sends POST request to SISU_AUTHENTICATE with signature in headers
 * 4. Parses response to extract:
 *    - AuthorizationToken.Token (the Xbox Live authorization token)
 *    - xid (Xbox User ID)
 *    - uhs (Xbox User Hash - used in API authorization headers)
 *    - gtg (Gamertag)
 *    - NotAfter (token expiration timestamp in ISO8601 format)
 * 5. Creates xbox_identity_t structure and persists via state_set_xbox_identity()
 *
 * ## Security
 * The request signature proves possession of the device private key, preventing
 * token replay attacks. The signature is base64-encoded and sent in the
 * "signature" HTTP header.
 *
 * ## Error Handling
 * On any failure (signing, network, parsing), sets ctx->result.error_message
 * and calls complete(ctx) to notify the caller.
 *
 * @param ctx Authentication context containing user_token and device_token
 * @return true on success, false on failure
 *
 * @note On success or failure, this function calls complete(ctx) to signal completion.
 */
static bool retrieve_sisu_token(authentication_ctx_t *ctx) {

    bool     succeeded           = false;
    uint8_t *signature           = NULL;
    char    *signature_b64       = NULL;
    char    *sisu_token_response = NULL;
    char    *proof_key           = NULL;

    /* Creates the request */
    proof_key = crypto_to_string(ctx->device->keys, false);

    if (!proof_key) {
        ctx->result.error_message = "Unable retrieve a sisu token: could not serialize proof key";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    char json_body[16384];
    snprintf(json_body,
             sizeof(json_body),
             "{\"AccessToken\":\"t=%s\",\"AppId\":\"%s\",\"DeviceToken\":\"%s\",\"Sandbox\":\"RETAIL\",\"UseModernGamertag\":true},\"SiteName\":\"user.auth.xboxlive.com\",\"RelyingParty\":\"http://xboxlive.com\",\"ProofKey\":\"%s\"}",
             ctx->user_token->value,
             CLIENT_ID,
             ctx->device_token->value,
             proof_key);

    obs_log(LOG_DEBUG, "Body: %s", json_body);

    /* Signs the request */
    size_t signature_len = 0;
    signature            = crypto_sign(ctx->device->keys, SISU_AUTHENTICATE, "", json_body, &signature_len);

    if (!signature) {
        ctx->result.error_message = "Unable retrieve a sisu token: signing failed";
        goto cleanup;
    }

    /* Encodes the signature */
    signature_b64 = base64_encode(signature, signature_len);

    if (!signature_b64) {
        ctx->result.error_message = "Unable retrieve a sisu token: encoding of the signature failed";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    obs_log(LOG_DEBUG, "Signature (base64): %s", signature_b64);

    /* Sets up the headers */
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
    long http_code      = 0;
    sisu_token_response = http_post(SISU_AUTHENTICATE, json_body, extra_headers, &http_code);

    if (!sisu_token_response) {
        ctx->result.error_message = "Unable to retrieve a sisu token: received no response from the server";
        goto cleanup;
    }

    obs_log(LOG_DEBUG, "Received response with status code %d: %s", http_code, sisu_token_response);

    if (http_code < 200 || http_code >= 300) {
        ctx->result.error_message = "Unable to retrieve a sisu token: received error from the server";
        obs_log(LOG_ERROR, "Unable to retrieve a sisu token: received status code '%d'", http_code);
        goto cleanup;
    }

    cJSON *sisu_token_json = cJSON_Parse(sisu_token_response);

    if (!sisu_token_json) {
        ctx->result.error_message = "Unable retrieve a sisu token: unable to parse the JSON response";
        goto cleanup;
    }

    /* Extracts the token */
    cJSON *token_node = cJSONUtils_GetPointer(sisu_token_json, "/AuthorizationToken/Token");

    if (!token_node) {
        ctx->result.error_message = "Unable to retrieve a sisu token: no token found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    /* Extracts the Xbox ID */
    cJSON *xid_node = cJSONUtils_GetPointer(sisu_token_json, "/AuthorizationToken/DisplayClaims/xui/0/xid");

    if (!xid_node) {
        ctx->result.error_message = "Unable to retrieve the xid: no value found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    /* Extracts the Xbox User Hash */
    cJSON *uhs_node = cJSONUtils_GetPointer(sisu_token_json, "/AuthorizationToken/DisplayClaims/xui/0/uhs");

    if (!uhs_node) {
        ctx->result.error_message = "Unable to retrieve the uhs: no value found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    /* Extracts the token expiration */
    cJSON *not_after_date_node = cJSONUtils_GetPointer(sisu_token_json, "/AuthorizationToken/NotAfter");

    if (!not_after_date_node) {
        ctx->result.error_message = "Unable to retrieve the NotAfter: no value found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    int32_t fraction       = 0;
    int64_t unix_timestamp = 0;

    if (!convert_iso8601_utc_to_unix(not_after_date_node->valuestring, &unix_timestamp, &fraction)) {
        ctx->result.error_message = "Unable retrieve a device token: unable to read the NotAfter date";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    /* Extracts the gamertag */
    cJSON *gtg_node = cJSONUtils_GetPointer(sisu_token_json, "/AuthorizationToken/DisplayClaims/xui/0/gtg");

    if (!gtg_node) {
        ctx->result.error_message = "Unable to retrieve the gtg: no value found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    obs_log(LOG_INFO, "Sisu authentication succeeded!");

    obs_log(LOG_DEBUG, "gtg: %s", gtg_node->valuestring);
    obs_log(LOG_DEBUG, "XID: %s", xid_node->valuestring);
    obs_log(LOG_DEBUG, "Hash: %s", uhs_node->valuestring);
    obs_log(LOG_DEBUG, "Now: %d", now());
    obs_log(LOG_DEBUG, "Expires: %d (%s)", unix_timestamp, not_after_date_node->valuestring);

    /* Creates the Xbox identity */
    token_t *xbox_token = bzalloc(sizeof(token_t));
    xbox_token->value   = bstrdup(token_node->valuestring);
    xbox_token->expires = unix_timestamp;

    xbox_identity_t *identity = bzalloc(sizeof(xbox_identity_t));
    identity->gamertag        = bstrdup(gtg_node->valuestring);
    identity->xid             = bstrdup(xid_node->valuestring);
    identity->uhs             = bstrdup(uhs_node->valuestring);
    identity->token           = xbox_token;

    /* Saves the identity */
    state_set_xbox_identity(identity);

    succeeded = true;

cleanup:
    free_memory((void **)&proof_key);
    free_memory((void **)&signature);
    free_memory((void **)&signature_b64);
    free_memory((void **)&sisu_token_response);
    free_json_memory((void **)&sisu_token_json);

    complete(ctx);

    return succeeded;
}

/**
 * @brief Retrieve the device Proof-of-Possession (PoP) token required for SISU.
 *
 * This implements Xbox Live's device authentication protocol, which proves the
 * client possesses a specific device private key.
 *
 * ## Caching Behavior
 * If allow_cache is true and a valid cached device token exists, it is reused
 * and the flow proceeds directly to retrieve_sisu_token().
 *
 * ## Device Authentication Process
 * 1. Constructs a JSON request containing:
 *    - AuthMethod: "ProofOfPossession"
 *    - Device ID (UUID), SerialNumber, DeviceType ("iOS"), Version
 *    - ProofKey: The device's public key in JWK format
 * 2. Signs the request using the device private key (ECDSA P-256)
 * 3. Sends POST to DEVICE_AUTHENTICATE with signature in headers
 * 4. Parses response for:
 *    - Token: The device authentication token (JWT)
 *    - NotAfter: Token expiration timestamp (ISO8601)
 * 5. Persists the token via state_set_device_token()
 *
 * ## Security
 * The cryptographic signature proves that the client controls the private key
 * corresponding to the ProofKey. This prevents device spoofing.
 *
 * ## Error Handling
 * On failure (signing, network, parsing), sets ctx->result.error_message and
 * calls complete(ctx).
 *
 * @param ctx Authentication context containing device identity
 * @return true if successful and proceeding to next stage, false on failure
 *
 * @note On success, proceeds to retrieve_sisu_token(). On failure, calls complete(ctx).
 */
static bool retrieve_device_token(struct authentication_ctx *ctx) {

    /* Finds out if a device token already exists */
    token_t *existing_device_token = state_get_device_token();

    if (ctx->allow_cache && existing_device_token) {
        obs_log(LOG_INFO, "Using cached device token");
        ctx->device_token = existing_device_token;
        return retrieve_sisu_token(ctx);
    }

    bool     succeeded             = false;
    char    *encoded_signature     = NULL;
    char    *device_token_response = NULL;
    uint8_t *signature             = NULL;
    char    *proof_key             = NULL;

    obs_log(LOG_INFO, "No device token cached found. Requesting a new device token");

    /* Builds the device token request */
    proof_key = crypto_to_string(ctx->device->keys, false);

    if (!proof_key) {
        ctx->result.error_message = "Unable retrieve a device token: could not serialize proof key";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    char json_body[8192];
    snprintf(json_body,
             sizeof(json_body),
             "{\"Properties\":{\"AuthMethod\":\"ProofOfPossession\",\"Id\":\"{%s}\",\"DeviceType\":\"iOS\",\"SerialNumber\":\"{%s}\",\"Version\":\"1.0.0\",\"ProofKey\":%s},\"RelyingParty\":\"http://auth.xboxlive.com\",\"TokenType\":\"JWT\"}",
             ctx->device->uuid,
             ctx->device->serial_number,
             proof_key);

    obs_log(LOG_DEBUG, "Device token request is: %s", json_body);

    /* Signs the request */
    size_t signature_len = 0;
    signature            = crypto_sign(ctx->device->keys, DEVICE_AUTHENTICATE, "", json_body, &signature_len);

    if (!signature) {
        ctx->result.error_message = "Unable retrieve a device token: signing failed";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    /* Encodes the signature */
    encoded_signature = base64_encode(signature, signature_len);

    if (!encoded_signature) {
        ctx->result.error_message = "Unable retrieve a device token: signature encoding failed";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    obs_log(LOG_DEBUG, "Encoded signature: %s", encoded_signature);

    /* Creates the headers */
    char extra_headers[4096];
    snprintf(extra_headers,
             sizeof(extra_headers),
             "signature: %s\r\n"
             "Cache-Control: no-store, must-revalidate, no-cache\r\n"
             "Content-Type: text/plain;charset=UTF-8\r\n"
             "x-xbl-contract-version: 1\r\n",
             encoded_signature);

    /* Sends the request */
    long http_code        = 0;
    device_token_response = http_post(DEVICE_AUTHENTICATE, json_body, extra_headers, &http_code);

    if (!device_token_response) {
        ctx->result.error_message = "Unable retrieve a device token: server returned no response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        return false;
    }

    obs_log(LOG_DEBUG, "Received response with status code %d: %s", http_code, device_token_response);

    if (http_code < 200 || http_code >= 300) {
        ctx->result.error_message = "Unable retrieve a device token: server returned an error";
        obs_log(LOG_ERROR, "Unable retrieve a device token: server returned status code %d", http_code);
        free_memory((void **)&device_token_response);
        return false;
    }

    /* Retrieves the device token */
    cJSON *device_token_json = cJSON_Parse(device_token_response);

    if (!device_token_json) {
        ctx->result.error_message = "Unable retrieve a device token: unable to parse the JSON response";
        goto cleanup;
    }

    cJSON *token_node = cJSONUtils_GetPointer(device_token_json, "/Token");

    if (!token_node) {
        ctx->result.error_message = "Unable retrieve a device token: unable to read the token from the response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    cJSON *not_after_date_node = cJSONUtils_GetPointer(device_token_json, "/NotAfter");

    if (!not_after_date_node) {
        ctx->result.error_message =
            "Unable retrieve a device token: unable to read the NotAfter field from the response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    int32_t fraction       = 0;
    int64_t unix_timestamp = 0;

    if (!convert_iso8601_utc_to_unix(not_after_date_node->valuestring, &unix_timestamp, &fraction)) {
        ctx->result.error_message = "Unable retrieve a device token: unable to read the NotAfter date";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    /* Saves the device token */
    obs_log(LOG_INFO, "Device authentication succeeded!");

    token_t *device = bzalloc(sizeof(token_t));
    device->value   = bstrdup(token_node->valuestring);
    device->expires = unix_timestamp;

    state_set_device_token(device);

    ctx->device_token = device;
    succeeded         = true;

cleanup:
    free_memory((void **)&proof_key);
    free_memory((void **)&signature);
    free_memory((void **)&encoded_signature);
    free_memory((void **)&device_token_response);
    free_json_memory((void **)&device_token_json);

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
 * This implements the OAuth 2.0 refresh token flow to obtain a new access token
 * without requiring user interaction.
 *
 * ## Process
 * 1. URL-encodes the refresh token and scope to prevent injection
 * 2. Constructs form-urlencoded POST data with:
 *    - client_id: The Xbox Live client ID
 *    - refresh_token: The previously obtained refresh token
 *    - scope: Required permission scope (service::user.auth.xboxlive.com::MBI_SSL)
 *    - grant_type: "refresh_token"
 * 3. Sends POST to TOKEN_ENDPOINT
 * 4. Parses response for:
 *    - access_token: New access token for API calls
 *    - refresh_token: New refresh token (may rotate)
 *    - expires_in: Token lifetime in milliseconds
 * 5. Persists both tokens via state_set_user_token()
 * 6. Calculates absolute expiration time (current time + expires_in)
 *
 * ## Security Considerations
 * Both the refresh token and scope are URL-encoded to prevent injection attacks
 * when constructing form-urlencoded data. The refresh token is long-lived and
 * should be stored securely.
 *
 * ## Error Handling
 * On failure (network error, HTTP error, parse error), sets ctx->result.error_message
 * and calls complete(ctx). Common failures include expired refresh tokens.
 *
 * @param ctx Authentication context containing refresh_token and device_code
 * @return true if successful and proceeding to device token retrieval, false on failure
 *
 * @note On success, proceeds to retrieve_device_token(). On failure, calls complete(ctx).
 */
static bool refresh_user_token(authentication_ctx_t *ctx) {

    bool  succeeded              = false;
    char *refresh_token_response = NULL;

    /* URL-encode both refresh_token and scope to prevent injection attacks
     * and ensure proper form-urlencoded format. The refresh_token may contain
     * special characters like +, /, = that must be percent-encoded.
     * The scope contains : characters that encode to %3A. */
    char *encoded_refresh_token = http_urlencode(ctx->refresh_token->value);
    char *encoded_scope         = http_urlencode(SCOPE);

    char refresh_token_form_url_encoded[8192];

    snprintf(refresh_token_form_url_encoded,
             sizeof(refresh_token_form_url_encoded),
             "client_id=%s&refresh_token=%s&scope=%s&grant_type=%s",
             CLIENT_ID,
             encoded_refresh_token,
             encoded_scope,
             GRANT_TYPE_REFRESH_TOKEN);

    obs_log(LOG_DEBUG, "URL: %s", refresh_token_form_url_encoded);

    long http_code         = 0;
    refresh_token_response = http_get(TOKEN_ENDPOINT, NULL, refresh_token_form_url_encoded, &http_code);

    if (http_code < 200 || http_code > 300) {
        ctx->result.error_message = "Unable to refresh the user token: server returned an error";
        obs_log(LOG_ERROR,
                "Unable to refresh the user token: server returned a status %d. Content: %s",
                http_code,
                refresh_token_response);
        goto cleanup;
    }

    if (!refresh_token_response) {
        ctx->result.error_message = "Unable to refresh the user token: server returned no response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    obs_log(LOG_DEBUG, "Response received: %s", refresh_token_response);

    cJSON *refresh_token_json = cJSON_Parse(refresh_token_response);

    if (!refresh_token_json) {
        ctx->result.error_message = "Unable to refresh the user token: unable to parse the JSON response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    cJSON *access_token_node  = cJSONUtils_GetPointer(refresh_token_json, "/access_token");
    cJSON *refresh_token_node = cJSONUtils_GetPointer(refresh_token_json, "/refresh_token");
    cJSON *expires_in_node    = cJSONUtils_GetPointer(refresh_token_json, "/expires_in");

    if (!access_token_node) {
        ctx->result.error_message = "Unable to refresh the user token: no access_token field found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    if (!refresh_token_node) {
        ctx->result.error_message = "Unable to refresh the user token: no refresh_token field found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    if (!expires_in_node) {
        ctx->result.error_message = "Unable to refresh the user token: no expires_in field found";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    /* The token has been found and is saved in the context */
    token_t *user_token = bzalloc(sizeof(token_t));
    user_token->value   = bstrdup(access_token_node->valuestring);
    user_token->expires = time(NULL) + expires_in_node->valueint / 1000;

    token_t *refresh_token = bzalloc(sizeof(token_t));
    refresh_token->value   = bstrdup(refresh_token_node->valuestring);

    ctx->user_token = user_token;

    /* And in the persistence */
    state_set_user_token(ctx->device_code, user_token, refresh_token);

    succeeded = true;

    obs_log(LOG_INFO, "User & refresh token received");

cleanup:
    free_memory((void **)&encoded_refresh_token);
    free_memory((void **)&encoded_scope);
    free_memory((void **)&refresh_token_response);
    free_json_memory((void **)&refresh_token_json);

    /* Either complete the process if an error has been encountered or go to the next step */
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
 * This implements the polling phase of OAuth 2.0 device code flow. After the user
 * has been shown a verification URL and code, this function repeatedly checks if
 * they've completed authorization.
 *
 * ## Polling Behavior
 * 1. Constructs form-urlencoded GET parameters with:
 *    - client_id: Xbox Live client ID
 *    - device_code: The device code from the initial request
 *    - grant_type: "urn:ietf:params:oauth:grant-type:device_code"
 * 2. Sleeps for the server-specified interval (typically 5 seconds)
 * 3. Sends GET request to TOKEN_ENDPOINT
 * 4. Handles responses:
 *    - HTTP 200: Parse access_token, refresh_token, expires_in and exit loop
 *    - HTTP 4xx: User hasn't authorized yet, continue polling
 *    - HTTP 5xx: Server error, continue polling (may want to abort)
 * 5. Repeats until:
 *    - Success (HTTP 200 with tokens)
 *    - Timeout (expires_in_seconds elapsed)
 *    - Parse error
 *
 * ## Token Persistence
 * On success, creates token_t structures for both access and refresh tokens,
 * calculates absolute expiration time, and persists via state_set_user_token().
 *
 * ## Threading
 * This function blocks the worker thread for the entire polling duration
 * (potentially several minutes). It should never be called on the main thread.
 *
 * ## Error Handling
 * On timeout or parse error, exits without setting ctx->user_token. The caller
 * checks this condition and calls complete(ctx) with an error.
 *
 * @param ctx Authentication context containing device_code and polling parameters
 *
 * @note On success, proceeds to retrieve_device_token(). On failure, calls complete(ctx).
 */
static void poll_for_user_token(authentication_ctx_t *ctx) {

    /* Creates the request */
    char get_token_form_url_encoded[8192];
    snprintf(get_token_form_url_encoded,
             sizeof(get_token_form_url_encoded),
             "client_id=%s&device_code=%s&grant_type=%s",
             CLIENT_ID,
             ctx->device_code,
             GRANT_TYPE_DEVICE_CODE);

    obs_log(LOG_INFO, "Waiting for the user to validate the code");
    obs_log(LOG_DEBUG, "URL: %s", get_token_form_url_encoded);

    /* Polls the server at a regular interval (as instructed by the server) */
    time_t       start_time = time(NULL);
    long         code       = 0;
    unsigned int interval   = (unsigned int)ctx->interval_in_seconds * 1000;
    long         expires_in = ctx->expires_in_seconds * 1000;

    while (time(NULL) - start_time < expires_in) {

        sleep_ms(interval);

        char *token_response = http_get(TOKEN_ENDPOINT, NULL, get_token_form_url_encoded, &code);

        if (code != 200) {
            obs_log(LOG_INFO,
                    "Device not validated yet. Received status code %d, Waiting %d second before retrying...",
                    code,
                    interval);
        } else {

            obs_log(LOG_DEBUG, "Response received: %s", token_response);

            cJSON *token_json = cJSON_Parse(token_response);

            if (!token_json) {
                obs_log(LOG_ERROR, "Failed to retrieve the user token: unable to parse the JSON response");
                free_memory((void **)&token_response);
                break;
            }

            cJSON *access_token_node     = cJSONUtils_GetPointer(token_json, "/access_token");
            cJSON *refresh_token_node    = cJSONUtils_GetPointer(token_json, "/refresh_token");
            cJSON *token_expires_in_node = cJSONUtils_GetPointer(token_json, "/expires_in");

            if (access_token_node && refresh_token_node && token_expires_in_node) {

                /* The token has been found and is saved in the context */
                token_t *user_token = bzalloc(sizeof(token_t));
                user_token->value   = bstrdup(access_token_node->valuestring);
                user_token->expires = time(NULL) + token_expires_in_node->valueint;

                token_t *refresh_token = bzalloc(sizeof(token_t));
                refresh_token->value   = bstrdup(refresh_token_node->valuestring);

                ctx->user_token = user_token;

                /* And in the persistence */
                state_set_user_token(ctx->device_code, user_token, refresh_token);

                obs_log(LOG_INFO, "User & refresh token received");
                free_json_memory((void **)&token_json);
                break;
            }

            ctx->result.error_message = "Could not parse access_token from token response";
            obs_log(LOG_ERROR, ctx->result.error_message);
            free_json_memory((void **)&token_json);
        }

        free_memory((void **)&token_response);
    }

    /* Either complete the process if an error has been encountered or go to the next step */
    if (!ctx->user_token) {
        complete(ctx);
    } else {
        retrieve_device_token(ctx);
    }
}

/**
 * @brief Worker thread entry point running the full authentication flow.
 *
 * This is the main entry point for the background authentication thread. It
 * implements a cascading fallback strategy to obtain a user access token, then
 * proceeds through device and SISU authentication.
 *
 * ## Token Acquisition Priority
 * The function attempts to obtain a user token in this order:
 * 1. **Cached Access Token**: If a valid (non-expired) cached user token exists, reuse it
 * 2. **Refresh Token**: If the access token is expired but a refresh token exists, refresh it
 * 3. **Device Code Flow**: If no tokens exist, perform full OAuth device code flow:
 *    - Request device_code and user_code from CONNECT_ENDPOINT
 *    - Open browser to verification URL
 *    - Poll TOKEN_ENDPOINT until user authorizes
 *
 * ## Flow Continuation
 * After obtaining a user token (by any method), the flow proceeds to:
 * 1. retrieve_device_token() - Get device Proof-of-Possession token
 * 2. retrieve_sisu_token() - Get final Xbox Live authorization
 *
 * ## Threading Context
 * This function runs entirely on a background pthread created by
 * xbox_live_authenticate(). It must not block the OBS main thread.
 *
 * ## Error Handling
 * Any failure in the device code request phase sets ctx->result.error_message
 * and calls complete(ctx). Failures in later stages are handled by those
 * respective functions.
 *
 * ## Memory Management
 * The authentication_ctx_t is freed at the end of this function. All allocated
 * resources (device, tokens, JSON) are cleaned up via the goto cleanup pattern.
 *
 * @param param Opaque pointer to authentication_ctx_t
 * @return Always returns (void *)false (return value currently unused)
 */
static void *start_authentication_flow(void *param) {

    char *scope_enc      = NULL;
    char *token_response = NULL;

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
        ctx->device_code   = state_get_device_code();
        ctx->refresh_token = refresh_token;
        refresh_user_token(ctx);
        goto cleanup;
    }

    obs_log(LOG_INFO, "Starting Xbox sign-in in browser");

    /* Builds the www-form-url-encoded */
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

    /* Requests a device code from the connect endpoint */
    long http_code = 0;
    token_response = http_post_form(CONNECT_ENDPOINT, form_url_encoded, &http_code);

    if (!token_response) {
        ctx->result.error_message = "Unable to retrieve a user token: received no response from the server";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    if (http_code < 200 || http_code >= 300) {
        ctx->result.error_message = "Unable to retrieve a user token: received an error from the server";
        obs_log(LOG_ERROR, "Unable to retrieve a user token:  %ld", http_code);
        goto cleanup;
    }

    obs_log(LOG_DEBUG, "Response received: %s", token_response);

    cJSON *token_json = cJSON_Parse(token_response);

    if (!token_json) {
        obs_log(LOG_ERROR, "Failed to retrieve the user token: unable to parse the JSON response");
        goto cleanup;
    }

    /* Retrieves the information from the response */
    cJSON *user_code_node = cJSONUtils_GetPointer(token_json, "/user_code");

    if (!user_code_node || strlen(user_code_node->valuestring) == 0) {
        ctx->result.error_message = "Unable to received a user token: could not parse the user_code from the response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    cJSON *device_code_node = cJSONUtils_GetPointer(token_json, "/device_code");

    if (!device_code_node || strlen(device_code_node->valuestring) == 0) {
        ctx->result.error_message =
            "Unable to received a user token: could not parse the device_code from the response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    cJSON *interval_node = cJSONUtils_GetPointer(token_json, "/interval");

    if (!interval_node) {
        ctx->result.error_message = "Unable to received a user token: could not parse the interval from token response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    cJSON *expires_in_node = cJSONUtils_GetPointer(token_json, "/expires_in");

    if (!expires_in_node) {
        ctx->result.error_message =
            "Unable to received a user token: could not parse the expires_in from token response";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    ctx->device_code         = bstrdup(device_code_node->valuestring);
    ctx->interval_in_seconds = interval_node->valueint;
    ctx->expires_in_seconds  = expires_in_node->valueint;

    /* Open the browser to the verification URL */
    char verification_uri[4096];
    snprintf(verification_uri, sizeof(verification_uri), "%s%s", REGISTER_ENDPOINT, user_code_node->valuestring);

    obs_log(LOG_DEBUG, "Open browser for OAuth verification at URL: %s", verification_uri);

    if (!open_url(verification_uri)) {
        ctx->result.error_message = "Unable to received a user token: could not open the browser";
        obs_log(LOG_ERROR, ctx->result.error_message);
        goto cleanup;
    }

    /* Starts the loop waiting for the token */
    poll_for_user_token(ctx);

cleanup:
    free_memory((void **)&scope_enc);
    free_memory((void **)&token_response);
    free_json_memory((void **)&token_json);

    if (!ctx->result.error_message) {
        retrieve_device_token(ctx);
    } else {
        complete(ctx);
    }

    free_memory((void **)&ctx);

    return (void *)false;
}

//  --------------------------------------------------------------------------------------------------------------------
//  Public API
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Initiate Xbox Live authentication flow asynchronously.
 *
 * This function starts the complete Xbox Live authentication process on a background
 * thread. It attempts to authenticate the user through a multi-stage process involving
 * Microsoft OAuth, device authentication, and SISU token acquisition.
 *
 * ## Authentication Process
 * The function will attempt authentication in this priority order:
 * 1. Use cached access token if available and valid
 * 2. Refresh access token using cached refresh token if available
 * 3. Perform full OAuth device code flow (opens browser for user authorization)
 *
 * After obtaining a user token, it automatically proceeds to:
 * - Acquire device Proof-of-Possession token
 * - Acquire final SISU token and Xbox identity
 *
 * ## Threading
 * All network operations and token acquisition occur on a background pthread to
 * avoid blocking the OBS main thread. The callback is invoked when the process
 * completes (success or failure).
 *
 * ## Callback Behavior
 * The provided callback is invoked exactly once when authentication finishes.
 * The callback receives the opaque data pointer passed to this function.
 * Check the result via xbox_live_get_identity() after callback invocation.
 *
 * ## Error Conditions
 * - Returns false immediately if no device identity is available
 * - Returns false if pthread creation fails
 * - Asynchronous failures are reported via the callback
 *
 * ## Example Usage
 * ```c
 * void on_auth_complete(void *user_data) {
 *     xbox_identity_t *identity = xbox_live_get_identity();
 *     if (identity) {
 *         obs_log(LOG_INFO, "Authenticated as: %s", identity->gamertag);
 *     } else {
 *         obs_log(LOG_ERROR, "Authentication failed");
 *     }
 * }
 *
 * xbox_live_authenticate(my_context, on_auth_complete);
 * ```
 *
 * @param data Opaque user data pointer forwarded to the callback
 * @param callback Function to call when authentication completes (may be NULL)
 * @return true if authentication thread was started successfully, false on immediate failure
 *
 * @note The device identity must be initialized via state_get_device() before calling this.
 * @see xbox_live_get_identity() to retrieve the authenticated identity
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
 * @brief Retrieve the current Xbox Live identity, refreshing if expired.
 *
 * This function returns the authenticated Xbox identity for API usage. It automatically
 * handles token expiration by refreshing tokens when necessary.
 *
 * ## Behavior
 * 1. Retrieves the cached Xbox identity from persistent storage
 * 2. If no identity exists, returns NULL (user must authenticate first)
 * 3. If identity exists but SISU token is not expired, returns it immediately
 * 4. If SISU token is expired:
 *    - Attempts to refresh using the cached refresh token
 *    - Performs full re-authentication flow (user, device, SISU tokens)
 *    - Returns NULL if refresh fails
 *
 * ## Token Expiration Handling
 * The function checks the SISU token's NotAfter timestamp. If expired, it
 * synchronously refreshes all tokens (user, device, SISU) before returning.
 * This ensures the returned identity always has valid authorization.
 *
 * ## Synchronous vs Asynchronous
 * Unlike xbox_live_authenticate(), this function is **synchronous**. Token refresh
 * blocks until complete. Use this for API calls where immediate identity is needed.
 * For initial authentication or background refresh, use xbox_live_authenticate().
 *
 * ## Thread Safety
 * This function should be called from the main thread or properly synchronized.
 * Internal token refresh operations use synchronous HTTP calls.
 *
 * ## Return Value
 * The returned xbox_identity_t* pointer is owned by the state management system.
 * Do not free it manually. It remains valid until the next authentication or
 * state modification.
 *
 * ## Identity Contents
 * When successful, the identity contains:
 * - **gamertag**: Xbox gamertag (display name)
 * - **xid**: Xbox User ID (unique identifier)
 * - **uhs**: Xbox User Hash (used in Authorization headers)
 * - **token**: SISU authorization token with expiration timestamp
 *
 * ## Example Usage
 * ```c
 * xbox_identity_t *identity = xbox_live_get_identity();
 * if (!identity) {
 *     // User not authenticated or refresh failed
 *     xbox_live_authenticate(ctx, on_complete);
 *     return;
 * }
 *
 * // Use identity for API calls
 * char auth_header[512];
 * snprintf(auth_header, sizeof(auth_header),
 *          "XBL3.0 x=%s;%s", identity->uhs, identity->token->value);
 * ```
 *
 * @return Pointer to xbox_identity_t if authenticated and valid, NULL otherwise
 *
 * @note Returns NULL if:
 *       - User has never authenticated
 *       - Token refresh fails (expired refresh token, network error)
 *       - No device identity is available for refresh
 *
 * @see xbox_live_authenticate() for initial authentication
 * @see token_is_expired() for token expiration checking
 */
xbox_identity_t *xbox_live_get_identity(void) {

    xbox_identity_t *identity = state_get_xbox_identity();

    if (!identity) {
        obs_log(LOG_INFO, "No identity found");
        return identity;
    }

    /* Checks if the Sisu token is expired */
    if (!token_is_expired(identity->token)) {
        obs_log(LOG_DEBUG, "Token is NOT expired, reusing existing identity");
        return identity;
    }

    obs_log(LOG_INFO, "Sisu token is expired. Retrieving device information.");

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
    ctx->refresh_token        = state_get_user_refresh_token();

    /* All the tokens (User, Device and Sisu) will be retrieved */
    if (!refresh_user_token(ctx)) {
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
