#include "parsers.h"

#include "text/convert.h"

#include <obs-module.h>
#include <cJSON.h>
#include <cJSON_Utils.h>
#include <string.h>
#include <common/types.h>
#include <diagnostics/log.h>

/**
 * @file parsers.c
 * @brief Implementation of lightweight JSON message classifiers and parsers.
 *
 * This module parses a couple of known Xbox JSON message shapes using cJSON.
 * It is used for:
 *  - Detecting whether a message is a presence message or an achievement message.
 *  - Extracting the currently played game (title/id) from presence messages.
 *  - Parsing achievement progression updates.
 *  - Parsing achievement metadata including media assets and Gamerscore rewards.
 *
 * Allocation/ownership:
 *  - Returned structs are allocated with bzalloc().
 *  - Most strings returned by helpers are duplicated on the heap (bstrdup/strdup).
 *  - The caller owns returned objects and is responsible for freeing them.
 */

//  --------------------------------------------------------------------------------------------------------------------
//  Private functions
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Read a string value from an achievement object at a given index.
 *
 * This helper builds a cJSON pointer of the form:
 *   /achievements/<achievement_index>/<property_name>
 * and returns a duplicated string.
 *
 * @param json_root          Parsed JSON root object.
 * @param achievement_index Index in the "achievements" array.
 * @param property_name     Property name to read.
 * @return Newly allocated string (caller must bfree()), or NULL if missing.
 */
static const char *get_node_string(cJSON *json_root, int achievement_index, const char *property_name) {

    char property_key[512] = "";
    snprintf(property_key, sizeof(property_key), "/achievements/%d/%s", achievement_index, property_name);

    cJSON *property_node = cJSONUtils_GetPointer(json_root, property_key);

    if (!property_node || !property_node->valuestring) {
        return NULL;
    }

    return bstrdup(property_node->valuestring);
}

/**
 * @brief Read a boolean value stored as a string from an achievement object.
 *
 * Some responses encode booleans as strings. This helper reads the string value
 * and returns true if it equals "true".
 *
 * @param json_root          Parsed JSON root object.
 * @param achievement_index Index in the "achievements" array.
 * @param property_name     Property name to read.
 * @return true if the underlying value equals "true"; false otherwise.
 */
static bool get_node_bool(cJSON *json_root, int achievement_index, const char *property_name) {

    const char *property_value = get_node_string(json_root, achievement_index, property_name);

    if (!property_value) {
        return false;
    }

    return strcmp(property_value, "true") == 0;
}

/**
 * @brief Read a boolean value stored as a string from an achievement object.
 *
 * Some responses encode booleans as strings. This helper reads the string value
 * and returns true if it equals "true".
 *
 * @param json_root          Parsed JSON root object.
 * @param achievement_index Index in the "achievements" array.
 * @param property_name     Property name to read.
 * @return true if the underlying value equals "true"; false otherwise.
 */
static int64_t get_node_unix_timestamp(cJSON *json_root, int achievement_index, const char *property_name) {

    const char *property_value = get_node_string(json_root, achievement_index, property_name);

    obs_log(LOG_INFO, "%s=%s", property_name, property_value);

    if (!property_value || strlen(property_value) == 0) {
        return 0;
    }

    int32_t fraction       = 0;
    int64_t unix_timestamp = 0;

    if (!convert_iso8601_utc_to_unix(property_value, &unix_timestamp, &fraction)) {
        obs_log(LOG_ERROR,
                "Unable to convert property '%s' as a unix timestamp. Value: %s",
                property_name,
                property_value);
        return 0;
    }

    obs_log(LOG_INFO, "%s=%" PRId64, property_name, unix_timestamp);

    return unix_timestamp;
}

/**
 * @brief Check whether a given JSON pointer exists in the document.
 *
 * Parses @p json_string and uses cJSONUtils_GetPointer with @p node_key.
 *
 * @param json_string JSON message text.
 * @param node_key    cJSON pointer string (e.g. "/presenceDetails").
 * @return true if the node exists; false otherwise.
 */
static bool contains_node(const char *json_string, const char *node_key) {

    cJSON *json_message  = NULL;
    cJSON *node          = NULL;
    bool   contains_node = false;

    if (!json_string || strlen(json_string) == 0) {
        goto cleanup;
    }

    json_message = cJSON_Parse(json_string);

    if (!json_message) {
        goto cleanup;
    }

    node = cJSONUtils_GetPointer(json_message, node_key);

    contains_node = node != NULL;

cleanup:
    FREE_JSON(json_message);

    return contains_node;
}

//  --------------------------------------------------------------------------------------------------------------------
//  Public functions
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Determine if a message looks like an achievement message.
 *
 * Currently implemented as the presence of the top-level "/serviceConfigId" node.
 */
bool is_achievement_message(const char *json_string) {

    return contains_node(json_string, "/serviceConfigId");
}

/**
 * @brief Determine if a message looks like a presence message.
 *
 * Currently implemented as the presence of the top-level "/presenceDetails" node.
 */
bool is_presence_message(const char *json_string) {

    return contains_node(json_string, "/presenceDetails");
}

/**
 * @brief Parse the played game information out of a presence message.
 *
 * This inspects up to the first few entries of "/presenceDetails" and returns
 * the first entry marked as a game ("isGame" true), then reads presenceText and
 * titleId.
 *
 * @param json_string Presence JSON message.
 * @return Newly allocated game_t on success; NULL if no game is found or parsing fails.
 */
game_t *parse_game(const char *json_string) {

    cJSON  *json_root = NULL;
    game_t *game      = NULL;

    if (!json_string || strlen(json_string) == 0) {
        return NULL;
    }

    json_root = cJSON_Parse(json_string);

    if (!json_root) {
        return NULL;
    }

    char current_game_title[128] = "";
    char current_game_id[128]    = "";

    for (int detail_index = 0; detail_index < 3; detail_index++) {

        /* Finds out if there is anything at this index */
        char is_game_key[512] = "";
        snprintf(is_game_key, sizeof(is_game_key), "/presenceDetails/%d/isGame", detail_index);

        cJSON *is_game_value = cJSONUtils_GetPointer(json_root, is_game_key);

        if (!is_game_value) {
            /* There is nothing more */
            obs_log(LOG_DEBUG, "No more game at %d", detail_index);
            break;
        }

        if (is_game_value->type == cJSON_False) {
            /* This is not a game: most likely the xbox home */
            obs_log(LOG_DEBUG, "No game at %d. Is game = %s", detail_index, is_game_value->valuestring);
            continue;
        }

        obs_log(LOG_DEBUG, "Game at %d. Is game = %s", detail_index, is_game_value->valuestring);

        /* Retrieve the game title and its ID */
        char game_title_key[512];
        snprintf(game_title_key, sizeof(game_title_key), "/presenceDetails/%d/presenceText", detail_index);

        cJSON *game_title_value = cJSONUtils_GetPointer(json_root, game_title_key);

        obs_log(LOG_DEBUG, "Game title: %s %s", game_title_value->string, game_title_value->valuestring);

        char game_id_key[512];
        snprintf(game_id_key, sizeof(game_id_key), "/presenceDetails/%d/titleId", detail_index);

        cJSON *game_id_value = cJSONUtils_GetPointer(json_root, game_id_key);

        obs_log(LOG_DEBUG, "Game ID: %s %s", game_id_value->string, game_id_value->valuestring);

        snprintf(current_game_title, sizeof(current_game_title), "%s", game_title_value->valuestring);
        snprintf(current_game_id, sizeof(current_game_id), "%s", game_id_value->valuestring);
    }

    if (strlen(current_game_id) == 0) {
        obs_log(LOG_DEBUG, "No game found");
        goto cleanup;
    }

    obs_log(LOG_DEBUG, "Game is %s (%s)", current_game_title, current_game_id);

    game        = bzalloc(sizeof(game_t));
    game->id    = strdup(current_game_id);
    game->title = strdup(current_game_title);

cleanup:
    FREE_JSON(json_root);

    return game;
}

/**
 * @brief Parse achievement progression updates.
 *
 * Iterates the "/progression" array and builds a linked list of
 * achievement_progress_t elements.
 *
 * @param json_string Achievement progression JSON message.
 * @return Head of a newly allocated linked list, or NULL on failure/no items.
 */
achievement_progress_t *parse_achievement_progress(const char *json_string) {

    cJSON                  *json_root            = NULL;
    achievement_progress_t *achievement_progress = NULL;

    if (!json_string || strlen(json_string) == 0) {
        return NULL;
    }

    json_root = cJSON_Parse(json_string);

    if (!json_root) {
        return NULL;
    }

    cJSON *service_config_node = cJSONUtils_GetPointer(json_root, "/serviceConfigId");

    if (!service_config_node) {
        goto cleanup;
    }

    char current_service_config_id[128] = "";
    snprintf(current_service_config_id, sizeof(current_service_config_id), "%s", service_config_node->valuestring);

    for (int detail_index = 0; detail_index < 3; detail_index++) {

        /* Finds out if there is anything at this index */
        char id_key[512] = "";
        snprintf(id_key, sizeof(id_key), "/progression/%d/id", detail_index);

        cJSON *id_node = cJSONUtils_GetPointer(json_root, id_key);

        if (!id_node) {
            /* There is nothing more */
            obs_log(LOG_DEBUG, "No more progression at %d", detail_index);
            break;
        }

        char progress_state_key[512] = "";
        snprintf(progress_state_key, sizeof(progress_state_key), "/progression/%d/progressState", detail_index);

        cJSON *progress_state_node = cJSONUtils_GetPointer(json_root, progress_state_key);

        if (!progress_state_node) {
            /* This is not a game: most likely the xbox home */
            obs_log(LOG_DEBUG, "No progress at %d. No progress state", detail_index);
            continue;
        }

        char time_unlocked_key[512] = "";
        snprintf(time_unlocked_key, sizeof(time_unlocked_key), "/progression/%d/timeUnlocked", detail_index);

        cJSON *time_unlocked_node = cJSONUtils_GetPointer(json_root, time_unlocked_key);

        if (!time_unlocked_node) {
            obs_log(LOG_ERROR, "No time unlocked at %d", detail_index);
            continue;
        }

        int32_t fraction = 0;
        int64_t unlocked_timestamp = 0;

        if (!convert_iso8601_utc_to_unix(time_unlocked_node->valuestring, &unlocked_timestamp, &fraction)) {
            obs_log(LOG_ERROR, "No time unlocked at %d", detail_index);
            continue;
        }

        achievement_progress_t *progress = bzalloc(sizeof(achievement_progress_t));
        progress->service_config_id      = strdup(current_service_config_id);
        progress->id                     = strdup(id_node->valuestring);
        progress->progress_state         = strdup(progress_state_node->valuestring);
        progress->unlocked_timestamp     = unlocked_timestamp;
        progress->next                   = NULL;

        if (!achievement_progress) {
            achievement_progress = progress;
        } else {
            achievement_progress_t *last_progress = achievement_progress;
            while (last_progress->next) {
                last_progress = last_progress->next;
            }
            last_progress->next = progress;
        }
    }

cleanup:
    FREE_JSON(json_root);

    return achievement_progress;
}

/**
 * @brief Parse full achievement metadata.
 *
 * Iterates over the "/achievements" array and builds a linked list of
 * achievement_t elements.
 *
 * For each achievement it extracts:
 *  - basic metadata (id, serviceConfigId, name, descriptions, progressState)
 *  - isSecret
 *  - mediaAssets[] (urls)
 *  - rewards[] filtered to Gamerscore rewards
 *
 * @param json_string Achievements JSON message.
 * @return Head of a newly allocated linked list, or NULL on failure/no items.
 */
achievement_t *parse_achievements(const char *json_string) {

    cJSON         *json_root    = NULL;
    achievement_t *achievements = NULL;

    if (!json_string || strlen(json_string) == 0) {
        return NULL;
    }

    json_root = cJSON_Parse(json_string);

    if (!json_root) {
        return NULL;
    }

    for (int achievement_index = 0;; achievement_index++) {

        const char *id = get_node_string(json_root, achievement_index, "id");

        if (!id) {
            /* There is nothing more */
            obs_log(LOG_DEBUG, "No more achievement at %d", achievement_index);
            break;
        }

        achievement_t *achievement      = bzalloc(sizeof(achievement_t));
        achievement->id                 = id;
        achievement->service_config_id  = get_node_string(json_root, achievement_index, "serviceConfigId");
        achievement->name               = get_node_string(json_root, achievement_index, "name");
        achievement->progress_state     = get_node_string(json_root, achievement_index, "progressState");
        achievement->description        = get_node_string(json_root, achievement_index, "description");
        achievement->locked_description = get_node_string(json_root, achievement_index, "lockedDescription");
        achievement->is_secret          = get_node_bool(json_root, achievement_index, "isSecret");
        achievement->unlocked_timestamp = get_node_unix_timestamp(json_root, achievement_index, "progression/timeUnlocked");
        achievement->icon_url = get_node_string(json_root, achievement_index, "mediaAssets/0/url");

        /* Reads the media assets */
        media_asset_t *media_assets = NULL;

        for (int media_asset_index = 0;; media_asset_index++) {

            char media_asset_url_key[512] = "";
            snprintf(media_asset_url_key,
                     sizeof(media_asset_url_key),
                     "/achievements/%d/mediaAssets/%d/url",
                     achievement_index,
                     media_asset_index);

            cJSON *media_asset_url_node = cJSONUtils_GetPointer(json_root, media_asset_url_key);

            if (!media_asset_url_node) {
                /* There is nothing more */
                obs_log(LOG_DEBUG, "No more media asset at %d/%d", achievement_index, media_asset_index);
                break;
            }

            media_asset_t *media_asset = bzalloc(sizeof(media_asset_t));
            media_asset->url           = bstrdup(media_asset_url_node->valuestring);
            media_asset->next          = NULL;

            if (!media_assets) {
                media_assets = media_asset;
            } else {
                media_asset_t *last_media_asset = media_assets;
                while (last_media_asset->next) {
                    last_media_asset = last_media_asset->next;
                }
                last_media_asset->next = media_asset;
            }
        }

        achievement->media_assets = media_assets;

        /* Reads the rewards */
        reward_t *rewards = NULL;

        for (int reward_index = 0;; reward_index++) {

            char reward_type_key[512] = "";
            snprintf(reward_type_key,
                     sizeof(reward_type_key),
                     "/achievements/%d/rewards/%d/type",
                     achievement_index,
                     reward_index);

            cJSON *reward_type_node = cJSONUtils_GetPointer(json_root, reward_type_key);

            if (!reward_type_node) {
                /* There is nothing more */
                obs_log(LOG_DEBUG, "No more reward at %d/%d", achievement_index, reward_index);
                break;
            }

            if (!reward_type_node->type || strcasecmp(reward_type_node->valuestring, "Gamerscore") != 0) {
                /* Ignores the non-gamerscore reward */
                obs_log(LOG_DEBUG, "Not a Gamerscore reward at %d/%d", achievement_index, reward_index);
                continue;
            }

            char reward_value_key[512] = "";
            snprintf(reward_value_key,
                     sizeof(reward_value_key),
                     "/achievements/%d/rewards/%d/value",
                     achievement_index,
                     reward_index);

            cJSON *reward_value_node = cJSONUtils_GetPointer(json_root, reward_value_key);

            if (!reward_value_node) {
                obs_log(LOG_DEBUG, "No value in reward at %d/%d", achievement_index, reward_index);
                continue;
            }

            reward_t *reward = bzalloc(sizeof(reward_t));
            reward->value    = bstrdup(reward_value_node->valuestring);

            if (!rewards) {
                rewards = reward;
            } else {
                reward_t *last_reward = rewards;
                while (last_reward->next) {
                    last_reward = last_reward->next;
                }
                last_reward->next = reward;
            }
        }

        achievement->rewards = rewards;

        obs_log(LOG_INFO,
                "%s | Achievement %s (%s G) is %s",
                achievement->service_config_id,
                achievement->name,
                achievement->rewards ? achievement->rewards->value : "no reward",
                achievement->progress_state);

        if (!achievements) {
            achievements = achievement;
        } else {
            achievement_t *last_achievement = achievements;
            while (last_achievement->next) {
                last_achievement = last_achievement->next;
            }
            last_achievement->next = achievement;
        }
    }

    FREE_JSON(json_root);

    return achievements;
}
