#include "xbox_client.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include "io/state.h"
#include "net/http/http.h"
#include "net/json/json.h"

#include <cJSON.h>
#include <cJSON_Utils.h>

#define XBOX_PRESENCE_ENDPOINT             "https://userpresence.xboxlive.com/users/xuid(%s)"
#define XBOX_PROFILE_SETTINGS_ENDPOINT     "https://profile.xboxlive.com/users/batch/profile/settings"
#define XBOX_PROFILE_CONTRACT_VERSION      "2"
#define GAMERSCORE_SETTING                 "Gamerscore"
#define XBOX_TITLE_HUB                     "https://titlehub.xboxlive.com/users/xuid(%s)/titles/titleId(%s)/decoration/image"

#define XBOX_GAME_COVER_DISPLAY_IMAGE      "/titles/0/displayImage"
#define XBOX_GAME_COVER_TYPE               "/titles/0/images/%d/type"
#define XBOX_GAME_COVER_URL                "/titles/0/images/%d/url"
#define XBOX_GAME_COVER_POSTER_TYPE        "poster"
#define XBOX_GAME_COVER_BOX_ART_TYPE        "boxart"

char *xbox_get_game_cover(const game_t *game) {

    char *display_image_url = NULL;

    if (!game) {
        return display_image_url;
    }

    /*
     * Retrieves the user's xbox identity
     */
    xbox_identity_t *identity = state_get_xbox_identity();

    if (!identity) {
        return display_image_url;
    }

    char display_request[4096];
    snprintf(display_request, sizeof(display_request), XBOX_TITLE_HUB, identity->xid, game->id);

    obs_log(LOG_DEBUG, "Display image URL: %s", display_request);

    char headers[4096];
    snprintf(headers,
             sizeof(headers),
             "Authorization: XBL3.0 x=%s;%s\r\n"
             "x-xbl-contract-version: %s\r\n"
             "Accept-Language: en-CA\r\n", //  Must be present!
             identity->uhs,
             identity->token->value,
             XBOX_PROFILE_CONTRACT_VERSION);

    obs_log(LOG_DEBUG, "Headers: %s", headers);

    /*
     * Sends the request
     */
    long  http_code = 0;
    char *response  = http_get(display_request, headers, NULL, &http_code);

    if (http_code < 200 || http_code >= 300) {
        obs_log(LOG_ERROR, "Failed to fetch title image: received status code %d", http_code);
        goto cleanup;
    }

    if (!response) {
        obs_log(LOG_ERROR, "Failed to fetch title image: received no response");
        goto cleanup;
    }

    obs_log(LOG_DEBUG, "Response: %s", response);

    /*
     *  Process the response by trying to get the poster image URL.
     *  Otherwise falls back on the display name.
     */
    cJSON *presence_json = cJSON_Parse(response);

    for (int image_index = 0;; image_index++) {

        char image_type_key[128];
        snprintf(image_type_key, sizeof(image_type_key), XBOX_GAME_COVER_TYPE, image_index);

        cJSON *image_type_value = cJSONUtils_GetPointer(presence_json, image_type_key);

        if (!image_type_value) {
            break;
        }

        if (strcmp(image_type_value->valuestring, XBOX_GAME_COVER_POSTER_TYPE) != 0 &&
            strcmp(image_type_value->valuestring, XBOX_GAME_COVER_BOX_ART_TYPE) != 0) {
            continue;
        }

        char image_url_key[128];
        snprintf(image_url_key, sizeof(image_url_key), XBOX_GAME_COVER_URL, image_index);

        cJSON *image_url_value = cJSONUtils_GetPointer(presence_json, image_url_key);

        if (!image_url_value || strlen(image_url_value->valuestring) == 0) {
            continue;
        }

        display_image_url = bstrdup_n(image_url_value->valuestring, strlen(image_url_value->valuestring));
        obs_log(LOG_INFO, "Xbox poster image found");
        break;
    }

    if (!display_image_url) {

        obs_log(LOG_INFO, "No Xbox game poster image found: falling back on the display image");

        /* No poster image found. Let's see if we can get the display image at least */
        cJSON *display_image = cJSONUtils_GetPointer(presence_json, XBOX_GAME_COVER_DISPLAY_IMAGE);

        if (!display_image) {
            obs_log(LOG_ERROR, "Failed to fetch title image: displayName property not found");
            goto cleanup;
        }

        display_image_url = bstrdup_n(display_image->valuestring, strlen(display_image->valuestring));
        obs_log(LOG_INFO, "Xbox game display image found");
    }

cleanup:
    FREE(response);

    return display_image_url;
}

/*
 *  https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/live/rest/uri/profilev2/uri-usersbatchprofilesettingspost
 */
bool xbox_fetch_gamerscore(int64_t *out_gamerscore) {

    if (!out_gamerscore) {
        return false;
    }

    /*
     * Retrieves the user's xbox identity
     */
    xbox_identity_t *identity = state_get_xbox_identity();

    if (!identity) {
        return false;
    }

    bool  result          = false;
    char *json            = NULL;
    char *gamerscore_text = NULL;
    char *end             = NULL;

    /*
     * Creates the request
     */
    char json_body[4096];
    snprintf(json_body,
             sizeof(json_body),
             "{\"userIds\":[\"%s\"],\"settings\":[\"%s\"]}",
             identity->xid,
             GAMERSCORE_SETTING);

    obs_log(LOG_DEBUG, "Body: %s", json_body);

    char headers[4096];
    snprintf(headers,
             sizeof(headers),
             "Authorization: XBL3.0 x=%s;%s\r\n"
             "x-xbl-contract-version: %s\r\n",
             identity->uhs,
             identity->token->value,
             XBOX_PROFILE_CONTRACT_VERSION);

    obs_log(LOG_DEBUG, "Headers: %s", headers);

    /*
     * Sends the request
     */
    long http_code = 0;
    json           = http_post(XBOX_PROFILE_SETTINGS_ENDPOINT, json_body, headers, &http_code);

    if (http_code < 200 || http_code >= 300) {
        obs_log(LOG_ERROR, "Failed to fetch gamerscore: received status code %d", http_code);
        goto cleanup;
    }

    if (!json) {
        obs_log(LOG_ERROR, "Failed to fetch gamerscore: received no response");
        goto cleanup;
    }

    /*
     * Extracts the gamerscore from the response
     */
    gamerscore_text = json_read_string(json, "value");

    if (!gamerscore_text) {
        obs_log(LOG_ERROR, "Failed to fetch gamerscore: unable to read the value");
        goto cleanup;
    }

    long long gamerscore = strtoll(gamerscore_text, &end, 10);

    result = end && end != gamerscore_text;

    if (result) {
        *out_gamerscore = (int64_t)gamerscore;
    }

cleanup:
    FREE(json);
    FREE(gamerscore_text);
    /*FREE(end);*/

    return result;
}

game_t *xbox_get_current_game(void) {

    obs_log(LOG_INFO, "Retrieving current game");

    xbox_identity_t *identity = state_get_xbox_identity();

    if (!identity) {
        obs_log(LOG_ERROR, "Failed to fetch the current game: no identity found");
        return NULL;
    }

    char   *response_json = NULL;
    game_t *game          = NULL;

    char headers[4096];
    snprintf(headers,
             sizeof(headers),
             "Authorization: XBL3.0 x=%s;%s\r\n"
             "x-xbl-contract-version: %s\r\n",
             identity->uhs,
             identity->token->value,
             XBOX_PROFILE_CONTRACT_VERSION);

    obs_log(LOG_DEBUG, "Headers: %s", headers);

    /*
     * Sends the request
     */
    char presence_url[512];
    snprintf(presence_url, sizeof(presence_url), XBOX_PRESENCE_ENDPOINT, identity->xid);

    long http_code = 0;
    response_json  = http_get(presence_url, headers, NULL, &http_code);

    if (http_code < 200 || http_code >= 300) {
        obs_log(LOG_ERROR, "Failed to fetch the current game: received status code %d", http_code);
        goto cleanup;
    }

    if (!response_json) {
        obs_log(LOG_ERROR, "Failed to fetch the current game: received no response");
        goto cleanup;
    }

    obs_log(LOG_DEBUG, "Response: %s", response_json);

    cJSON *root = cJSON_Parse(response_json);

    if (!root) {
        obs_log(LOG_ERROR, "Failed to fetch the current game: unable to parse the JSON response");
        goto cleanup;
    }

    /* Retrieves the current state */

    char   user_state_key[512] = "/state";
    cJSON *user_state_value    = cJSONUtils_GetPointer(root, user_state_key);

    if (!user_state_value || strcmp(user_state_value->valuestring, "Offline") == 0) {
        obs_log(LOG_INFO, "User is offline at the moment.");
        goto cleanup;
    }

    char current_game_title[128];
    char current_game_id[128];

    for (int title_game_index = 0; title_game_index < 10; title_game_index++) {

        /* Finds out if there is anything at this index */
        char title_name_key[512];
        snprintf(title_name_key, sizeof(title_name_key), "/devices/0/titles/%d/name", title_game_index);
        char title_id_key[512];
        snprintf(title_id_key, sizeof(title_id_key), "/devices/0/titles/%d/id", title_game_index);
        char state_key[512];
        snprintf(state_key, sizeof(state_key), "/devices/0/titles/%d/state", title_game_index);

        cJSON *title_game_value = cJSONUtils_GetPointer(root, title_name_key);
        cJSON *title_id_value   = cJSONUtils_GetPointer(root, title_id_key);
        cJSON *state_value      = cJSONUtils_GetPointer(root, state_key);

        if (!title_game_value || !title_id_value || !state_value) {
            /* There is nothing more */
            obs_log(LOG_DEBUG, "No more game at %d", title_game_index);
            break;
        }

        if (strcmp(title_game_value->valuestring, "Home") == 0) {
            obs_log(LOG_DEBUG, "Skipping home at %d", title_game_index);
            continue;
        }

        if (strcmp(state_value->valuestring, "Active") != 0) {
            obs_log(LOG_DEBUG, "Skipping inactivated game at %d", title_game_index);
            continue;
        }

        /* Retrieve the game title and its ID */
        obs_log(LOG_DEBUG, "Game title: %s %s", title_game_value->valuestring, title_id_value->valuestring);

        snprintf(current_game_title, sizeof(current_game_title), "%s", title_game_value->valuestring);
        snprintf(current_game_id, sizeof(current_game_id), "%s", title_id_value->valuestring);
    }

    if (strlen(current_game_id) == 0) {
        obs_log(LOG_INFO, "No game found");
        goto cleanup;
    }

    obs_log(LOG_INFO, "Game is %s (%s)", current_game_title, current_game_id);

    game        = bzalloc(sizeof(game_t));
    game->id    = strdup(current_game_id);
    game->title = strdup(current_game_title);

cleanup:
    FREE(response_json);

    return game;
}

char *xbox_fetch_achievements_json(long *out_http_code) {
    if (out_http_code)
        *out_http_code = 0;

    return NULL;
    // const char *xid  = get_xid();
    // const char *auth = get_xsts_token();
    // if (!xid || !*xid || !auth || !*auth) {
    //     obs_log(LOG_WARNING, "Missing XUID/XSTS token; user not signed in?");
    //     return NULL;
    // }
    //
    // char url[512];
    // snprintf(url,
    //          sizeof(url),
    //          "https://achievements.xboxlive.com/users/xuid(%s)/achievements?titleId=1879711255&maxItems=1000",
    //          xid);
    //
    // char headers[2048];
    // snprintf(headers,
    //          sizeof(headers),
    //          "Authorization: %s\n"
    //          "Accept: application/json\n"
    //          "Accept-Language: en-CA\n"
    //          "Content-Type: application/json\n"
    //          "x-xbl-contract-version: 2\n",
    //          auth);
    //
    // long  code = 0;
    // char *json = http_get(url, headers, NULL, &code);
    // if (out_http_code)
    //     *out_http_code = code;
    //
    // if (!json) {
    //     obs_log(LOG_WARNING, "Achievements GET failed (no response)");
    //     return NULL;
    // }
    // if (code < 200 || code >= 300) {
    //     obs_log(LOG_WARNING, "Achievements GET HTTP %ld: %s", code, json);
    //     /* Return body to caller anyway for debugging */
    // }
    //
    // return json;
}

char *xbox_fetch_presence_json(long *out_http_code) {

    if (out_http_code)
        *out_http_code = 0;

    /*
    const char *xid  = get_xid();
    const char *auth = get_xsts_token();

    if (!xid || !*xid || !auth || !*auth) {
        obs_log(LOG_WARNING, "Missing XUID/XSTS token; user not signed in?");
        return NULL;
    }

    char url[512];
    snprintf(url, sizeof(url), "https://userpresence.xboxlive.com/users/xuid(%s)/richpresence", xid);

    char headers[2048];
    snprintf(headers,
             sizeof(headers),
             "Authorization: %s\n"
             "Accept: application/json\n"
             "Accept-Language: en-CA\n"
             "Content-Type: application/json\n"
             "x-xbl-contract-version: 2\n",
             auth);

    long  code = 0;
    char *json = http_get(url, headers, NULL, &code);
    if (out_http_code)
        *out_http_code = code;

    obs_log(LOG_WARNING, "Presence %s", json);

    if (!json) {
        obs_log(LOG_WARNING, "Presence GET failed (no response)");
        return NULL;
    }

    if (code < 200 || code >= 300)
        obs_log(LOG_WARNING, "Presence GET HTTP %ld: %s", code, json);
    return json;*/
    return NULL;
}

/*
*xuid": "2533274953419891",
"state": "Online",
"devices": [
{
"type": "Scarlett",
"titles": [
{
"id": "750323071",
"name": "Home",
"placement": "Background",
"state": "Active",
"lastModified": "2026-01-05T01:52:35.0621013"
},
{
"id": "1879711255",
"name": "The Outer Worlds 2",
"placement": "Full",
"state": "Active",
"lastModified": "2026-01-05T01:52:35.0621013"
}
]
},*/
