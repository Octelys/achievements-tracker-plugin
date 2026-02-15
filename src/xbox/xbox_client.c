#include "xbox_client.h"

/**
 * @file xbox_client.c
 * @brief Xbox HTTP client helpers (presence, profile, title art, achievements).
 *
 * This module provides a small set of convenience functions that call Xbox Live
 * REST endpoints using the currently authenticated Xbox identity from the
 * persistent state.
 *
 * Common requirements:
 *  - Most functions require an authenticated identity to be present (see
 *    state_get_xbox_identity()).
 *  - Requests use the "Authorization: XBL3.0 x=<uhs>;<token>" header and a
 *    contract version header.
 *
 * Allocation/ownership:
 *  - Functions returning strings or linked lists allocate them with OBS
 *    allocators (bstrdup/bzalloc) and/or libc strdup.
 *  - Callers must free returned objects appropriately (e.g., bfree/free_game/
 *    free_achievement depending on the type).
 */

#include <obs-module.h>
#include <diagnostics/log.h>

#include "io/state.h"
#include "net/http/http.h"
#include "net/json/json.h"
#include "oauth/xbox-live.h"
#include "text/parsers.h"

#include <cJSON.h>
#include <cJSON_Utils.h>

#define XBOX_PRESENCE_ENDPOINT             "https://userpresence.xboxlive.com/users/xuid(%s)"
#define XBOX_PROFILE_SETTINGS_ENDPOINT     "https://profile.xboxlive.com/users/batch/profile/settings"
#define XBOX_PROFILE_CONTRACT_VERSION      "2"
#define GAMERSCORE_SETTING                 "Gamerscore"
#define GAMERPIC_SETTING                   "GameDisplayPicRaw"
#define XBOX_TITLE_HUB                     "https://titlehub.xboxlive.com/users/xuid(%s)/titles/titleId(%s)/decoration/image"
#define XBOX_ACHIEVEMENTS_ENDPOINT         "https://achievements.xboxlive.com/users/xuid(%s)/achievements?titleId=%s"

#define XBOX_GAME_COVER_DISPLAY_IMAGE      "/titles/0/displayImage"
#define XBOX_GAME_COVER_TYPE               "/titles/0/images/%d/type"
#define XBOX_GAME_COVER_URL                "/titles/0/images/%d/url"
#define XBOX_GAME_COVER_POSTER_TYPE        "poster"
#define XBOX_GAME_COVER_BOX_ART_TYPE       "boxart"

/**
 * @brief In-place substring replacement helper.
 *
 * Replaces all occurrences of @p needle in @p s with @p replacement.
 *
 * This helper is intentionally simple and works in-place, so it only supports
 * equal or shorter replacements (i.e. replacement_length <= needle_length).
 * It's used to post-process certain JSON-escaped pieces we get back from Xbox
 * services (for example: "\\u0026" sequences inside URLs).
 *
 * @param[in,out] s String buffer to mutate.
 * @param needle Substring to search for (must be non-empty).
 * @param replacement Substring to write in place of @p needle.
 *
 * @return true if at least one replacement happened, false otherwise.
 */
static bool str_replace(char *s, const char *needle, const char *replacement) {
    if (!s || !needle || !replacement) {
        return false;
    }

    const size_t needle_length      = strlen(needle);
    const size_t replacement_length = strlen(replacement);

    if (needle_length == 0 || replacement_length > needle_length) {
        return false;
    }

    bool changed = false;

    for (char *p = s; (p = strstr(p, needle)) != NULL;) {
        if (replacement_length) {
            memcpy(p, replacement, replacement_length);
        }

        if (replacement_length < needle_length) {
            memmove(p + replacement_length, p + needle_length, strlen(p + needle_length) + 1);
        }

        p += replacement_length;
        changed = true;
    }

    return changed;
}

char *xbox_get_game_cover(const game_t *game) {

    char            *display_image_url = NULL;
    xbox_identity_t *identity          = NULL;
    char            *titlehub_response = NULL;
    cJSON           *titlehub_json     = NULL;

    if (!game) {
        goto cleanup;
    }

    /* Retrieves the user's xbox identity */
    identity = xbox_live_get_identity();

    if (!identity) {
        goto cleanup;
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
    long http_code    = 0;
    titlehub_response = http_get(display_request, headers, NULL, &http_code);

    if (http_code < 200 || http_code >= 300) {
        obs_log(LOG_ERROR, "Failed to fetch title image: received status code %d", http_code);
        goto cleanup;
    }

    if (!titlehub_response) {
        obs_log(LOG_ERROR, "Failed to fetch title image: received no response");
        goto cleanup;
    }

    obs_log(LOG_DEBUG, "Response: %s", titlehub_response);

    /*
     *  Process the response by trying to get the poster image URL.
     *  Otherwise, it falls back on the display name.
     */
    titlehub_json = cJSON_Parse(titlehub_response);

    for (int image_index = 0;; image_index++) {

        char image_type_key[128];
        snprintf(image_type_key, sizeof(image_type_key), XBOX_GAME_COVER_TYPE, image_index);

        cJSON *image_type_value = cJSONUtils_GetPointer(titlehub_json, image_type_key);

        if (!image_type_value) {
            break;
        }

        if (strcmp(image_type_value->valuestring, XBOX_GAME_COVER_POSTER_TYPE) != 0 &&
            strcmp(image_type_value->valuestring, XBOX_GAME_COVER_BOX_ART_TYPE) != 0) {
            continue;
        }

        char image_url_key[128];
        snprintf(image_url_key, sizeof(image_url_key), XBOX_GAME_COVER_URL, image_index);

        cJSON *image_url_value = cJSONUtils_GetPointer(titlehub_json, image_url_key);

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
        cJSON *display_image = cJSONUtils_GetPointer(titlehub_json, XBOX_GAME_COVER_DISPLAY_IMAGE);

        if (!display_image) {
            obs_log(LOG_ERROR, "Failed to fetch title image: displayName property not found");
            goto cleanup;
        }

        display_image_url = bstrdup_n(display_image->valuestring, strlen(display_image->valuestring));
        obs_log(LOG_INFO, "Xbox game display image found");
    }

cleanup:
    free_memory((void **)&identity);
    free_memory((void **)&titlehub_response);
    free_json_memory((void **)&titlehub_json);

    return display_image_url;
}

bool xbox_fetch_gamerscore(int64_t *out_gamerscore) {

    if (!out_gamerscore) {
        return false;
    }

    /* Retrieves the user's xbox identity */
    xbox_identity_t *identity = state_get_xbox_identity();

    if (!identity) {
        return false;
    }

    bool  result                    = false;
    char *profile_settings_response = NULL;
    char *gamerscore_text           = NULL;
    char *end                       = NULL;

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
    long http_code            = 0;
    profile_settings_response = http_post(XBOX_PROFILE_SETTINGS_ENDPOINT, json_body, headers, &http_code);

    if (http_code < 200 || http_code >= 300) {
        obs_log(LOG_ERROR, "Failed to fetch gamerscore: received status code %d", http_code);
        goto cleanup;
    }

    if (!profile_settings_response) {
        obs_log(LOG_ERROR, "Failed to fetch gamerscore: received no response");
        goto cleanup;
    }

    /*
     * Extracts the gamerscore from the response
     */
    gamerscore_text = json_read_string(profile_settings_response, "value");

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
    free_memory((void **)&identity);
    free_memory((void **)&profile_settings_response);
    free_memory((void **)&gamerscore_text);

    return result;
}

char *xbox_fetch_gamerpic() {

    xbox_identity_t *identity = state_get_xbox_identity();

    if (!identity) {
        return NULL;
    }

    char  *profile_response      = NULL;
    cJSON *profile_settings_json = NULL;
    char  *gamerpic_url          = NULL;

    /* Creates the request */
    char json_body[4096];
    snprintf(json_body,
             sizeof(json_body),
             "{\"userIds\":[\"%s\"],\"settings\":[\"%s\"]}",
             identity->xid,
             GAMERPIC_SETTING);

    obs_log(LOG_DEBUG, "Profile settings request body: %s", json_body);

    char headers[4096];
    snprintf(headers,
             sizeof(headers),
             "Authorization: XBL3.0 x=%s;%s\r\n"
             "x-xbl-contract-version: %s\r\n",
             identity->uhs,
             identity->token->value,
             XBOX_PROFILE_CONTRACT_VERSION);

    obs_log(LOG_DEBUG, "Profile settings request headers: %s", headers);

    /* Sends the request */
    long http_code   = 0;
    profile_response = http_post(XBOX_PROFILE_SETTINGS_ENDPOINT, json_body, headers, &http_code);

    if (http_code < 200 || http_code >= 300) {
        obs_log(LOG_ERROR, "Failed to fetch the user's Gamerpic: received status code %ld", http_code);
        goto cleanup;
    }

    if (!profile_response) {
        obs_log(LOG_ERROR, "Failed to fetch the user's Gamerpic: received no response");
        goto cleanup;
    }

    obs_log(LOG_DEBUG, "Profile settings response: %s", profile_response);

    profile_settings_json = cJSON_Parse(profile_response);

    if (!profile_settings_json) {
        obs_log(LOG_ERROR, "Failed to fetch the user's gamerpic: unable to parse the JSON response");
        goto cleanup;
    }

    /* Retrieves the value at the specified key: we assume it is at the first item (for now) */
    cJSON *user_gamerpic_url = cJSONUtils_GetPointer(profile_settings_json, "/profileUsers/0/settings/0/value");

    if (!user_gamerpic_url || !user_gamerpic_url->valuestring || user_gamerpic_url->valuestring[0] == '\0') {
        obs_log(LOG_INFO, "Failed to fetch the user's gamerpic: no value found.");
        goto cleanup;
    }

    gamerpic_url = bstrdup(user_gamerpic_url->valuestring);

    /* cJSON doesn't automatically unescape literal unicode sequences (\\uXXXX) inside strings.
     * Xbox sometimes returns URLs containing "\\u0026" for '&'. Fix it up for curl/http. */
    str_replace(gamerpic_url, "u0026", "&");

    obs_log(LOG_INFO, "User gamerpic URL is '%s'", gamerpic_url);

cleanup:
    free_json_memory((void **)&profile_settings_json);
    free_memory((void **)&identity);
    free_memory((void **)&profile_response);

    return gamerpic_url;
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
        /* Retry? */
        obs_log(LOG_ERROR, "Failed to fetch the current game: received status code %d", http_code);
        goto cleanup;
    }

    if (!response_json) {
        obs_log(LOG_ERROR, "Failed to fetch the current game: received no response");
        goto cleanup;
    }

    obs_log(LOG_INFO, "Response: %s", response_json);

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

    obs_log(LOG_INFO, "Game is '%s' (%s)", current_game_title, current_game_id);

    game        = bzalloc(sizeof(game_t));
    game->id    = strdup(current_game_id);
    game->title = strdup(current_game_title);

cleanup:
    // FREE(response_json);

    return game;
}

achievement_t *xbox_get_game_achievements(const game_t *game) {

    if (!game) {
        return NULL;
    }

    xbox_identity_t *identity = state_get_xbox_identity();

    if (!identity) {
        obs_log(LOG_ERROR, "Failed to fetch the game's achievements: no identity found");
        return NULL;
    }

    achievement_t *all_achievements   = NULL;
    achievement_t *last_achievement   = NULL;
    char          *continuation_token = NULL;

    char headers[4096];
    snprintf(headers,
             sizeof(headers),
             "Authorization: XBL3.0 x=%s;%s\r\n"
             "x-xbl-contract-version: %s\r\n",
             identity->uhs,
             identity->token->value,
             XBOX_PROFILE_CONTRACT_VERSION);

    obs_log(LOG_DEBUG, "Headers: %s", headers);

    /* Pagination loop: keep fetching until no continuation token */
    do {
        char          *response_json     = NULL;
        achievement_t *page_achievements = NULL;

        /* Build the URL with or without continuation token */
        char achievements_url[1024];
        if (continuation_token) {
            snprintf(achievements_url,
                     sizeof(achievements_url),
                     XBOX_ACHIEVEMENTS_ENDPOINT "&continuationToken=%s",
                     identity->xid,
                     game->id,
                     continuation_token);
            bfree(continuation_token);
            continuation_token = NULL;
        } else {
            snprintf(achievements_url, sizeof(achievements_url), XBOX_ACHIEVEMENTS_ENDPOINT, identity->xid, game->id);
        }

        long http_code = 0;
        response_json  = http_get(achievements_url, headers, NULL, &http_code);

        if (http_code < 200 || http_code >= 300) {
            obs_log(LOG_ERROR, "Failed to fetch the games achievements: received status code %ld", http_code);
            FREE(response_json);
            break;
        }

        if (!response_json) {
            obs_log(LOG_ERROR, "Failed to fetch the games achievements: received no response");
            break;
        }

        obs_log(LOG_DEBUG, "Response length: %zu bytes", strlen(response_json));

        /* Parse achievements from this page */
        page_achievements = parse_achievements(response_json);

        if (page_achievements) {
            /* Append to the full list */
            if (!all_achievements) {
                all_achievements = page_achievements;
            } else {
                last_achievement->next = page_achievements;
            }

            /* Find the new last achievement */
            last_achievement = page_achievements;
            while (last_achievement->next) {
                last_achievement = last_achievement->next;
            }
        }

        /* Check for continuation token in pagingInfo */
        cJSON *root = cJSON_Parse(response_json);
        if (root) {
            cJSON *paging_info = cJSONUtils_GetPointer(root, "/pagingInfo/continuationToken");
            if (paging_info && paging_info->valuestring && paging_info->valuestring[0] != '\0') {
                continuation_token = bstrdup(paging_info->valuestring);
                obs_log(LOG_DEBUG, "Found continuation token, fetching next page...");
            }
            cJSON_Delete(root);
        }

        FREE(response_json);

    } while (continuation_token);

    obs_log(LOG_INFO, "Received %d achievements for game %s", count_achievements(all_achievements), game->title);

    return all_achievements;
}
