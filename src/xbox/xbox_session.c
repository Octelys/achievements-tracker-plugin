#include "xbox/xbox_session.h"

#include <diagnostics/log.h>

#include "util/bmem.h"
#include "xbox/xbox_client.h"

#include <_strings.h>

//  --------------------------------------------------------------------------------------------------------------------
//  Private functions.
//  --------------------------------------------------------------------------------------------------------------------

static const achievement_t *find_achievement(const achievements_progress_t *progress,
                                             const achievement_t           *achievements) {

    const achievement_t *current = achievements;

    while (current) {

        if (strcasecmp(current->id, progress->id) == 0) {
            return current;
        }

        current = current->next;
    }

    return NULL;
}

static void free_rewards(reward_t *reward) {

    if (!reward) {
        return;
    }

    reward_t *current = reward;

    while (current) {
        reward_t *next = current->next;
        FREE(current->value);
        FREE(current);
        current = next;
    }
}

static void free_media_assets(media_asset_t *media_asset) {

    if (!media_asset) {
        return;
    }

    media_asset_t *current = media_asset;

    while (current) {
        media_asset_t *next = current->next;
        FREE(current->url);
        FREE(current);
        current = next;
    }
}

static void free_achievements(achievement_t *achievements) {

    if (!achievements) {
        return;
    }

    achievement_t *current = achievements;

    while (current) {
        achievement_t *next = current->next;

        FREE(current->service_config_id);
        FREE(current->id);
        FREE(current->name);
        FREE(current->description);
        FREE(current->locked_description);
        FREE(current->progress_state)
        free_media_assets((void *)current->media_assets);
        free_rewards((void *)current->rewards);
        FREE(current);

        current = next;
    }
}

//  --------------------------------------------------------------------------------------------------------------------
//  Public functions.
//  --------------------------------------------------------------------------------------------------------------------

bool xbox_session_is_game_played(xbox_session_t *session, const game_t *game) {

    if (!session) {
        return false;
    }

    game_t *current_game = session->game;

    if (!current_game || !game) {
        return false;
    }

    return strcasecmp(current_game->id, game->id) == 0;
}

void xbox_session_change_game(xbox_session_t *session, game_t *game) {

    if (!session) {
        obs_log(LOG_ERROR, "Failed to change game: session is NULL");
        return;
    }

    free_achievements(session->achievements);
    session->achievements = NULL;

    FREE(session->game);
    session->game = game;

    /* Let's get the achievements of the game */
    if (game) {
        session->achievements = xbox_get_game_achievements(game);
    }
}

int xbox_session_compute_gamerscore(const xbox_session_t *session) {

    if (!session) {
        return 0;
    }

    const gamerscore_t *gamerscore = session->gamerscore;

    if (!gamerscore) {
        return 0;
    }

    int total_value = gamerscore->base_value;

    const unlocked_achievement_t *current = gamerscore->unlocked_achievements;

    while (current) {
        total_value += current->value;
        current = current->next;
    }

    return total_value;
}

void xbox_session_unlock_achievement(const xbox_session_t *session, const achievements_progress_t *progress) {

    if (!session || !progress) {
        return;
    }

    /* TODO Let's make sure the progress is achieved */

    const achievement_t *achievement = find_achievement(progress, session->achievements);

    if (!achievement) {
        obs_log(LOG_ERROR, "Failed to unlock achievement %d: not found in the game's achievements", progress->id);
        return;
    }

    const reward_t *reward = achievement->rewards;

    if (!reward) {
        obs_log(LOG_ERROR, "Failed to unlock achievement %d: no reward found", progress->id);
        return;
    }

    gamerscore_t *gamerscore = session->gamerscore;

    unlocked_achievement_t *unlocked_achievement = bzalloc(sizeof(unlocked_achievement_t));
    unlocked_achievement->id                     = progress->id;
    unlocked_achievement->value                  = atoi(reward->value);

    unlocked_achievement_t *unlocked_achievements = gamerscore->unlocked_achievements;

    /* Appends the unlocked achievement to the list */
    if (!unlocked_achievements) {
        gamerscore->unlocked_achievements = unlocked_achievement;
    } else {
        unlocked_achievement_t *last_unlocked_achievement = unlocked_achievements;
        while (last_unlocked_achievement->next) {
            last_unlocked_achievement = last_unlocked_achievement->next;
        }
        last_unlocked_achievement->next = unlocked_achievement;
    }

    obs_log(LOG_INFO, "New achievement unlocked! Gamerscore is now %d", xbox_session_compute_gamerscore(session));
}
