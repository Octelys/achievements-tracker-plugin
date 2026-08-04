#include "integrations/twitch/stream_status.h"

#include "cJSON.h"
#include "cJSON_Utils.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include "net/http/http.h"
#include "common/memory.h"
#include "integrations/twitch/twitch_client.h"
#include "integrations/twitch/oauth/twitch-oauth.h"

#include <string.h>

bool twitch_is_stream_live(const char *user_id) {

    if (!user_id || strlen(user_id) == 0) {
        return false;
    }

    twitch_identity_t *identity = twitch_get_identity();

    if (!identity) {
        return false;
    }

    char headers[1024];
    snprintf(headers, sizeof(headers), "Authorization: Bearer %s\nClient-Id: %s\n", identity->token->value,
             TWITCH_CLIENT_ID);

    char url[512];
    snprintf(url, sizeof(url), "https://api.twitch.tv/helix/streams?user_id=%s", user_id);

    long  http_code = 0;
    char *response  = http_get(url, headers, NULL, &http_code);

    free_twitch_identity(&identity);

    if (!response) {
        obs_log(LOG_ERROR, "[TwitchStreamStatus] No response from server");
        return false;
    }

    if (http_code < 200 || http_code >= 300) {
        obs_log(LOG_ERROR, "[TwitchStreamStatus] Unable to check live status: HTTP %ld", http_code);
        free_memory((void **)&response);
        return false;
    }

    cJSON *json = cJSON_Parse(response);
    free_memory((void **)&response);

    if (!json) {
        obs_log(LOG_ERROR, "[TwitchStreamStatus] Unable to parse the JSON response");
        return false;
    }

    cJSON *data_node = cJSONUtils_GetPointer(json, "/data");
    bool live = data_node && (data_node->type & 0xFF) == cJSON_Array && cJSON_GetArraySize(data_node) > 0;

    free_json_memory((void **)&json);

    return live;
}
