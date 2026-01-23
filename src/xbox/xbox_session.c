#include "xbox/xbox_session.h"

#include <diagnostics/log.h>

#include "common/types.h"
#include "util/bmem.h"
#include "xbox/xbox_client.h"

//  --------------------------------------------------------------------------------------------------------------------
//  Private functions.
//  --------------------------------------------------------------------------------------------------------------------

static const achievement_t *find_achievement(const achievement_progress_t *progress,
                                             const achievement_t          *achievements) {

    const achievement_t *current = achievements;

    while (current) {

        if (strcasecmp(current->id, progress->id) == 0) {
            return current;
        }

        current = current->next;
    }

    return NULL;
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

    free_achievement(&session->achievements);
    free_game(&session->game);

    /* Let's get the achievements of the game */
    if (!game) {
        return;
    }

    session->game         = copy_game(game);
    session->achievements = xbox_get_game_achievements(game);
}

void xbox_session_unlock_achievement(const xbox_session_t *session, const achievement_progress_t *progress) {

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
    unlocked_achievement->id                     = bstrdup(progress->id);
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
