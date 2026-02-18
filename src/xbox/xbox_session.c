#include "xbox/xbox_session.h"

#include <diagnostics/log.h>

#include "common/types.h"
#include "util/bmem.h"
#include "xbox/xbox_client.h"

#include <errno.h>
#include <stdlib.h>

//  --------------------------------------------------------------------------------------------------------------------
//  Private functions.
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Finds an achievement definition by id.
 *
 * Performs a case-insensitive search of the @p achievements linked list for an
 * entry whose @c id matches @c progress->id.
 *
 * @param progress Progress item containing the achievement id to look up.
 * @param achievements Head of the achievements linked list.
 *
 * @return Pointer to the matching achievement node within @p achievements, or
 *         NULL if not found.
 */
static achievement_t *find_achievement_by_id(const achievement_progress_t *progress, achievement_t *achievements) {

    achievement_t *current = achievements;

    while (current) {

        if (strcasecmp(current->id, progress->id) == 0) {
            return current;
        }

        current = current->next;
    }

    return NULL;
}

//  --------------------------------------------------------------------------------------------------------------------
//  Public API
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

    /* Sort the achievements from the most recent unlocked to the locked ones */
    sort_achievements(&session->achievements);
}

void xbox_session_unlock_achievement(xbox_session_t *session, const achievement_progress_t *progress) {

    if (!session || !progress) {
        return;
    }

    /* TODO Let's make sure the progress is achieved */

    achievement_t *achievement = find_achievement_by_id(progress, session->achievements);

    if (!achievement) {
        obs_log(LOG_ERROR,
                "Failed to unlock achievement %s: not found in the game's achievements",
                progress->id ? progress->id : "(null)");
        return;
    }

    /* Updates the achievement status */
    free_memory((void **)&achievement->progress_state);
    achievement->progress_state     = bstrdup(progress->progress_state);
    achievement->unlocked_timestamp = progress->unlocked_timestamp;

    /* Sort the achievements from the most recent unlocked to the locked ones */
    sort_achievements(&session->achievements);

    const reward_t *reward = achievement->rewards;

    if (!reward) {
        obs_log(LOG_ERROR, "Failed to unlock achievement %s: no reward found", progress->id ? progress->id : "(null)");
        return;
    }

    obs_log(LOG_DEBUG, "Found reward %s", reward->value);

    gamerscore_t *gamerscore = session->gamerscore;

    unlocked_achievement_t *unlocked_achievement = bzalloc(sizeof(unlocked_achievement_t));
    unlocked_achievement->id                     = bstrdup(progress->id);

    long  parsed_value = 0;
    char *endptr       = NULL;
    errno              = 0;

    if (reward->value) {
        parsed_value = strtol(reward->value, &endptr, 10);
    }

    if (errno != 0 || endptr == reward->value || (endptr && *endptr != '\0')) {
        obs_log(LOG_WARNING,
                "Unable to parse gamerscore value '%s' for achievement %s; defaulting to 0",
                reward->value ? reward->value : "(null)",
                progress->id ? progress->id : "(null)");
        parsed_value = 0;
    }

    unlocked_achievement->value = (int)parsed_value;

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

    /* Sort achievements from the most recent unlocked achievement first and then locked achievements */

    obs_log(LOG_INFO,
            "New achievement unlocked: %s (%d G)! Gamerscore is now %d",
            achievement->name,
            unlocked_achievement->value,
            xbox_session_compute_gamerscore(session));
}

void xbox_session_clear(xbox_session_t *session) {

    if (!session) {
        return;
    }

    free_achievement(&session->achievements);
    free_game(&session->game);
    free_gamerscore(&session->gamerscore);
}
