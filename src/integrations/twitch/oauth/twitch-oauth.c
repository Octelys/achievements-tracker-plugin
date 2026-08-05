#include "integrations/twitch/oauth/twitch-oauth.h"

/**
 * @file twitch-oauth.c
 * @brief Twitch authentication flow implementation for the OBS plugin.
 *
 * Implements Twitch's OAuth 2.0 device code grant
 * (https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/#device-code-grant-flow),
 * modeled after the Xbox Live device-code flow in
 * src/integrations/xbox/oauth/xbox-live.c. One deliberate difference: Twitch's
 * verification page does not accept an embedded code in the URL (unlike Xbox's
 * REGISTER_ENDPOINT+otc trick) — the user must type the short user_code shown
 * on screen, so that code is surfaced to the caller via on_twitch_device_code_ready_t
 * before the browser opens.
 */

#include "cJSON.h"
#include "cJSON_Utils.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include "net/browser/browser.h"
#include "net/http/http.h"
#include "io/state.h"
#include "common/memory.h"
#include "integrations/twitch/twitch_client.h"

#include <util/thread_compat.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/** @brief Twitch OAuth device-code request endpoint. */
#define TWITCH_DEVICE_CODE_ENDPOINT "https://id.twitch.tv/oauth2/device"

/** @brief Twitch OAuth token endpoint (polling + refresh). */
#define TWITCH_TOKEN_ENDPOINT "https://id.twitch.tv/oauth2/token"

/** @brief Twitch Helix endpoint used to resolve the authenticated user's profile. */
#define TWITCH_USERS_ENDPOINT "https://api.twitch.tv/helix/users"

/** @brief OAuth grant type for device code flow. */
#define GRANT_TYPE_DEVICE_CODE "urn:ietf:params:oauth:grant-type:device_code"

/** @brief OAuth grant type for refresh token flow. */
#define GRANT_TYPE_REFRESH_TOKEN "refresh_token"

/** @brief Scope required to post chat messages as the authenticated user. */
#define TWITCH_SCOPE "user:write:chat"

typedef struct twitch_authentication_ctx {
    on_twitch_device_code_ready_t on_code_ready;
    on_twitch_authenticated_t     on_completed;
    void                         *on_completed_data;

    char *device_code;
    int   interval_in_seconds;
    long  expires_in_seconds;

    pthread_t thread;
} twitch_authentication_ctx_t;

static void complete(twitch_authentication_ctx_t *ctx) {
    if (ctx->on_completed) {
        ctx->on_completed(ctx->on_completed_data);
    }
}

/**
 * @brief Resolve the authenticated user's Twitch profile (id/login/display_name).
 */
static bool fetch_twitch_user_profile(const char *access_token_value, char **out_user_id, char **out_login,
                                      char **out_display_name) {

    char headers[1024];
    snprintf(headers,
             sizeof(headers),
             "Authorization: Bearer %s\nClient-Id: %s\n",
             access_token_value,
             TWITCH_CLIENT_ID);

    long  http_code = 0;
    char *response  = http_get(TWITCH_USERS_ENDPOINT, headers, NULL, &http_code);

    if (!response) {
        obs_log(LOG_ERROR, "[TwitchAuth] Unable to fetch user profile: no response from server");
        return false;
    }

    if (http_code < 200 || http_code >= 300) {
        obs_log(LOG_ERROR, "[TwitchAuth] Unable to fetch user profile: server returned HTTP %ld", http_code);
        free_memory((void **)&response);
        return false;
    }

    cJSON *json = cJSON_Parse(response);
    free_memory((void **)&response);

    if (!json) {
        obs_log(LOG_ERROR, "[TwitchAuth] Unable to fetch user profile: could not parse the JSON response");
        return false;
    }

    cJSON *id_node           = cJSONUtils_GetPointer(json, "/data/0/id");
    cJSON *login_node        = cJSONUtils_GetPointer(json, "/data/0/login");
    cJSON *display_name_node = cJSONUtils_GetPointer(json, "/data/0/display_name");

    bool succeeded = id_node && login_node && display_name_node;

    if (succeeded) {
        *out_user_id      = bstrdup(id_node->valuestring);
        *out_login        = bstrdup(login_node->valuestring);
        *out_display_name = bstrdup(display_name_node->valuestring);
    } else {
        obs_log(LOG_ERROR, "[TwitchAuth] Unable to fetch user profile: missing fields in response");
    }

    free_json_memory((void **)&json);
    return succeeded;
}

/**
 * @brief Resolve the user's profile and persist the full Twitch identity.
 */
static bool complete_sign_in(const char *access_token_value, int64_t expires, const char *refresh_token_value) {

    char *user_id      = NULL;
    char *login        = NULL;
    char *display_name = NULL;

    if (!fetch_twitch_user_profile(access_token_value, &user_id, &login, &display_name)) {
        return false;
    }

    token_t           token    = {.value = (char *)access_token_value, .expires = expires};
    twitch_identity_t identity = {
        .login         = login,
        .display_name  = display_name,
        .user_id       = user_id,
        .token         = &token,
        .refresh_token = (char *)refresh_token_value,
    };

    state_set_twitch_identity(&identity);

    obs_log(LOG_INFO, "[TwitchAuth] Signed in as %s", login);

    free_memory((void **)&user_id);
    free_memory((void **)&login);
    free_memory((void **)&display_name);

    return true;
}

/**
 * @brief Persist refreshed tokens, keeping the previously-resolved profile fields.
 */
static void persist_refreshed_tokens(const char *access_token_value, int64_t expires, const char *refresh_token_value) {

    twitch_identity_t *existing = state_get_twitch_identity();

    if (!existing) {
        obs_log(LOG_ERROR, "[TwitchAuth] Cannot persist refreshed tokens: no existing identity found");
        return;
    }

    token_t           token   = {.value = (char *)access_token_value, .expires = expires};
    twitch_identity_t updated = *existing;
    updated.token             = &token;
    updated.refresh_token     = (char *)refresh_token_value;

    state_set_twitch_identity(&updated);

    free_twitch_identity(&existing);
}

/**
 * @brief Refresh the Twitch access token using the persisted refresh token.
 *
 * Twitch rotates the refresh token on every use — the newly-returned refresh
 * token (not the old one) must always be the one persisted.
 */
static bool refresh_twitch_token(void) {

    twitch_identity_t *identity = state_get_twitch_identity();

    if (!identity || !identity->refresh_token || strlen(identity->refresh_token) == 0) {
        obs_log(LOG_ERROR, "[TwitchAuth] No refresh token available");
        free_twitch_identity(&identity);
        return false;
    }

    char *encoded_refresh_token = http_urlencode(identity->refresh_token);
    free_twitch_identity(&identity);

    char form_url_encoded[8192];
    snprintf(form_url_encoded,
             sizeof(form_url_encoded),
             "client_id=%s&refresh_token=%s&grant_type=%s",
             TWITCH_CLIENT_ID,
             encoded_refresh_token,
             GRANT_TYPE_REFRESH_TOKEN);
    free_memory((void **)&encoded_refresh_token);

    long  http_code = 0;
    char *response  = http_post_form(TWITCH_TOKEN_ENDPOINT, form_url_encoded, &http_code);

    if (!response) {
        obs_log(LOG_ERROR, "[TwitchAuth] Unable to refresh token: no response from server");
        return false;
    }

    if (http_code < 200 || http_code >= 300) {
        obs_log(LOG_ERROR, "[TwitchAuth] Unable to refresh token: server returned HTTP %ld", http_code);
        free_memory((void **)&response);
        return false;
    }

    cJSON *json = cJSON_Parse(response);
    free_memory((void **)&response);

    if (!json) {
        obs_log(LOG_ERROR, "[TwitchAuth] Unable to refresh token: could not parse the JSON response");
        return false;
    }

    cJSON *access_token_node  = cJSONUtils_GetPointer(json, "/access_token");
    cJSON *refresh_token_node = cJSONUtils_GetPointer(json, "/refresh_token");
    cJSON *expires_in_node    = cJSONUtils_GetPointer(json, "/expires_in");

    bool succeeded = access_token_node && refresh_token_node && expires_in_node;

    if (succeeded) {
        int64_t expires = time(NULL) + expires_in_node->valueint;
        persist_refreshed_tokens(access_token_node->valuestring, expires, refresh_token_node->valuestring);
        obs_log(LOG_INFO, "[TwitchAuth] Access token refreshed");
    } else {
        obs_log(LOG_ERROR, "[TwitchAuth] Unable to refresh token: missing fields in response");
    }

    free_json_memory((void **)&json);
    return succeeded;
}

/**
 * @brief Poll TWITCH_TOKEN_ENDPOINT until the user completes device-code verification.
 *
 * Blocks the calling (background) thread for the full polling duration.
 */
static void poll_for_token(twitch_authentication_ctx_t *ctx) {

    char form_url_encoded[8192];
    snprintf(form_url_encoded,
             sizeof(form_url_encoded),
             "client_id=%s&device_code=%s&grant_type=%s",
             TWITCH_CLIENT_ID,
             ctx->device_code,
             GRANT_TYPE_DEVICE_CODE);

    obs_log(LOG_INFO, "[TwitchAuth] Waiting for the user to validate the code");

    time_t       start_time = time(NULL);
    unsigned int interval   = (unsigned int)ctx->interval_in_seconds * 1000;
    bool         succeeded  = false;

    while (time(NULL) - start_time < ctx->expires_in_seconds) {

        sleep_ms(interval);

        long  http_code = 0;
        char *response  = http_post_form(TWITCH_TOKEN_ENDPOINT, form_url_encoded, &http_code);

        if (http_code != 200) {
            obs_log(LOG_INFO,
                    "[TwitchAuth] Not validated yet (HTTP %ld), retrying in %d second(s)...",
                    http_code,
                    ctx->interval_in_seconds);
            free_memory((void **)&response);
            continue;
        }

        cJSON *json = cJSON_Parse(response);
        free_memory((void **)&response);

        if (!json) {
            obs_log(LOG_ERROR, "[TwitchAuth] Unable to parse the token response");
            continue;
        }

        cJSON *access_token_node  = cJSONUtils_GetPointer(json, "/access_token");
        cJSON *refresh_token_node = cJSONUtils_GetPointer(json, "/refresh_token");
        cJSON *expires_in_node    = cJSONUtils_GetPointer(json, "/expires_in");

        if (access_token_node && refresh_token_node && expires_in_node) {
            int64_t expires = time(NULL) + expires_in_node->valueint;
            succeeded = complete_sign_in(access_token_node->valuestring, expires, refresh_token_node->valuestring);
            free_json_memory((void **)&json);
            break;
        }

        obs_log(LOG_ERROR, "[TwitchAuth] Token response missing expected fields");
        free_json_memory((void **)&json);
    }

    if (!succeeded) {
        obs_log(LOG_ERROR, "[TwitchAuth] Sign-in did not complete (timed out or failed)");
    }
}

static void *start_twitch_authentication_flow(void *param) {

    twitch_authentication_ctx_t *ctx = param;

    char *encoded_scope = http_urlencode(TWITCH_SCOPE);

    char form_url_encoded[8192];
    snprintf(form_url_encoded, sizeof(form_url_encoded), "client_id=%s&scope=%s", TWITCH_CLIENT_ID, encoded_scope);
    free_memory((void **)&encoded_scope);

    long  http_code = 0;
    char *response  = http_post_form(TWITCH_DEVICE_CODE_ENDPOINT, form_url_encoded, &http_code);

    if (!response || http_code < 200 || http_code >= 300) {
        obs_log(LOG_ERROR, "[TwitchAuth] Unable to request a device code (HTTP %ld)", http_code);
        free_memory((void **)&response);
        goto cleanup;
    }

    cJSON *json = cJSON_Parse(response);
    free_memory((void **)&response);

    if (!json) {
        obs_log(LOG_ERROR, "[TwitchAuth] Unable to parse the device code response");
        goto cleanup;
    }

    {
        cJSON *device_code_node      = cJSONUtils_GetPointer(json, "/device_code");
        cJSON *user_code_node        = cJSONUtils_GetPointer(json, "/user_code");
        cJSON *verification_uri_node = cJSONUtils_GetPointer(json, "/verification_uri");
        cJSON *interval_node         = cJSONUtils_GetPointer(json, "/interval");
        cJSON *expires_in_node       = cJSONUtils_GetPointer(json, "/expires_in");

        if (!device_code_node || !user_code_node || !verification_uri_node || !interval_node || !expires_in_node) {
            obs_log(LOG_ERROR, "[TwitchAuth] Device code response missing expected fields");
            free_json_memory((void **)&json);
            goto cleanup;
        }

        ctx->device_code         = bstrdup(device_code_node->valuestring);
        ctx->interval_in_seconds = interval_node->valueint;
        ctx->expires_in_seconds  = expires_in_node->valueint;

        if (ctx->on_code_ready) {
            ctx->on_code_ready(user_code_node->valuestring, verification_uri_node->valuestring, ctx->on_completed_data);
        }

        if (!open_url(verification_uri_node->valuestring)) {
            obs_log(LOG_WARNING,
                    "[TwitchAuth] Could not open the browser automatically; the user_code above is "
                    "still valid to enter manually.");
        }
    }

    free_json_memory((void **)&json);

    poll_for_token(ctx);

cleanup:
    complete(ctx);
    free_memory((void **)&ctx->device_code);
    free_memory((void **)&ctx);

    return NULL;
}

bool twitch_authenticate(void *data, on_twitch_device_code_ready_t on_code_ready,
                         on_twitch_authenticated_t on_completed) {

    if (!on_completed) {
        obs_log(LOG_ERROR, "[TwitchAuth] Unable to authenticate: on_completed callback is required");
        return false;
    }

    twitch_authentication_ctx_t *ctx = bzalloc(sizeof(twitch_authentication_ctx_t));
    ctx->on_code_ready               = on_code_ready;
    ctx->on_completed                = on_completed;
    ctx->on_completed_data           = data;

    if (pthread_create(&ctx->thread, NULL, start_twitch_authentication_flow, ctx) != 0) {
        obs_log(LOG_ERROR, "[TwitchAuth] Unable to start the authentication thread");
        free_memory((void **)&ctx);
        return false;
    }

    pthread_detach(ctx->thread);
    return true;
}

twitch_identity_t *twitch_get_identity(void) {

    twitch_identity_t *identity = state_get_twitch_identity();

    if (!identity) {
        obs_log(LOG_INFO, "[TwitchAuth] No identity found");
        return NULL;
    }

    if (!token_is_expired(identity->token)) {
        return identity;
    }

    obs_log(LOG_INFO, "[TwitchAuth] Access token expired, refreshing");
    free_twitch_identity(&identity);

    if (!refresh_twitch_token()) {
        return NULL;
    }

    return state_get_twitch_identity();
}
