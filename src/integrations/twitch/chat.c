#include "integrations/twitch/chat.h"

#include "cJSON.h"

#include <obs-module.h>
#include <diagnostics/log.h>
#include <util/thread_compat.h>

#include "net/http/http.h"
#include "common/memory.h"
#include "integrations/twitch/twitch_client.h"
#include "integrations/twitch/oauth/twitch-oauth.h"

#include <string.h>
#include <time.h>

/** @brief Twitch Helix chat-send endpoint. */
#define TWITCH_CHAT_MESSAGES_ENDPOINT "https://api.twitch.tv/helix/chat/messages"

/**
 * @brief Minimum spacing (ms) enforced between consecutive chat sends.
 *
 * Twitch's per-user chat rate limit is generous (dozens of messages per 30s),
 * but achievement unlocks can arrive in quick bursts (e.g. a full RetroAchievements
 * set load); a modest client-side floor keeps the plugin well clear of it without
 * needing a full token-bucket implementation.
 */
#define MIN_SEND_INTERVAL_MS 1500

static pthread_mutex_t g_rate_limit_mutex = PTHREAD_MUTEX_INITIALIZER;
static int64_t         g_last_send_time  = 0;

/**
 * @brief Sleep off any remaining gap since the last send, then record this send's time.
 */
static void throttle_send(void) {

    pthread_mutex_lock(&g_rate_limit_mutex);

    int64_t elapsed_ms = (int64_t)(time(NULL) - g_last_send_time) * 1000;

    if (g_last_send_time != 0 && elapsed_ms < MIN_SEND_INTERVAL_MS) {
        sleep_ms((unsigned int)(MIN_SEND_INTERVAL_MS - elapsed_ms));
    }

    g_last_send_time = time(NULL);

    pthread_mutex_unlock(&g_rate_limit_mutex);
}

/**
 * @brief Post a single chat message attempt and return the HTTP status code.
 */
static long send_message_once(const twitch_identity_t *identity, const char *message) {

    cJSON *body = cJSON_CreateObject();
    cJSON_AddItemToObject(body, "broadcaster_id", cJSON_CreateString(identity->user_id));
    cJSON_AddItemToObject(body, "sender_id", cJSON_CreateString(identity->user_id));
    cJSON_AddItemToObject(body, "message", cJSON_CreateString(message));

    char *json_body = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    char headers[1024];
    snprintf(headers, sizeof(headers), "Authorization: Bearer %s\nClient-Id: %s\n", identity->token->value,
             TWITCH_CLIENT_ID);

    long  http_code = 0;
    char *response  = http_post_json(TWITCH_CHAT_MESSAGES_ENDPOINT, json_body, headers, &http_code);

    if (http_code < 200 || http_code >= 300) {
        obs_log(LOG_ERROR, "[TwitchChat] POST failed: HTTP %ld. Response: %s", http_code,
                response ? response : "(none)");
    }

    free(json_body);
    free_memory((void **)&response);

    return http_code;
}

bool twitch_send_chat_message(const char *message) {

    if (!message || strlen(message) == 0) {
        return false;
    }

    twitch_identity_t *identity = twitch_get_identity();

    if (!identity) {
        obs_log(LOG_WARNING, "[TwitchChat] Cannot post message: not signed in");
        return false;
    }

    throttle_send();

    long http_code = send_message_once(identity, message);

    if (http_code == 401) {
        /* Token may have just expired between twitch_get_identity() and the send;
         * force one refresh + retry rather than surfacing a spurious failure. */
        free_twitch_identity(&identity);
        identity = twitch_get_identity();

        if (!identity) {
            return false;
        }

        http_code = send_message_once(identity, message);
    }

    free_twitch_identity(&identity);

    return http_code >= 200 && http_code < 300;
}
