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
 * @brief Sorts achievements with unlocked ones first (by timestamp descending), then locked ones.
 *
 * This function reorders the achievements linked list in-place so that:
 * - Unlocked achievements (unlocked_timestamp != 0) appear first, ordered by
 *   timestamp descending (most recently unlocked first)
 * - Locked achievements (unlocked_timestamp == 0) appear after all unlocked ones
 *
 * Uses an in-place insertion sort algorithm with no memory allocation.
 *
 * @param achievements Pointer to the head of the achievements list. The head may be
 *        updated if the first achievement changes position.
 */
static void sort_achievements(achievement_t **achievements) {

    if (!achievements || !*achievements || !(*achievements)->next) {
        return;
    }

    achievement_t *sorted  = NULL;
    achievement_t *current = *achievements;

    /* Insertion sort: take each node from the original list and insert it in sorted order */
    while (current) {
        achievement_t *next = current->next;

        /* Insert current into the sorted list at the correct position */
        if (!sorted) {
            /* First node in sorted list */
            sorted       = current;
            sorted->next = NULL;
        } else {
            /* Determine if current should go before sorted head */
            bool should_insert_before_head = false;

            if (sorted->unlocked_timestamp == 0 && current->unlocked_timestamp != 0) {
                /* Current is unlocked, head is locked */
                should_insert_before_head = true;
            } else if (current->unlocked_timestamp != 0 && sorted->unlocked_timestamp != 0 &&
                       current->unlocked_timestamp > sorted->unlocked_timestamp) {
                /* Both unlocked, current has more recent timestamp */
                should_insert_before_head = true;
            }

            if (should_insert_before_head) {
                /* Insert at head */
                current->next = sorted;
                sorted        = current;
            } else {
                /* Find the correct position in the sorted list */
                achievement_t *search = sorted;
                while (search->next) {
                    bool should_insert_here = false;

                    if (search->next->unlocked_timestamp == 0 && current->unlocked_timestamp != 0) {
                        /* Current is unlocked, next is locked */
                        should_insert_here = true;
                    } else if (current->unlocked_timestamp != 0 && search->next->unlocked_timestamp != 0 &&
                               current->unlocked_timestamp > search->next->unlocked_timestamp) {
                        /* Both unlocked, current has more recent timestamp */
                        should_insert_here = true;
                    }

                    if (should_insert_here) {
                        break;
                    }
                    search = search->next;
                }

                /* Insert current after search */
                current->next = search->next;
                search->next  = current;
            }
        }

        current = next;
    }

    *achievements = sorted;
}

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
//  Public functions.
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Determines whether the session is currently tracking the given game.
 *
 * Compares the game identifiers case-insensitively.
 *
 * @param session Session to inspect (may be NULL).
 * @param game Game to compare against (may be NULL).
 *
 * @return True if both the session has a current game and its id matches
 *         @p game->id, false otherwise.
 */
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

/**
 * @brief Switches the session to a new game.
 *
 * Frees any existing achievements and game stored in the session. If @p game is
 * non-NULL, stores a copy of it and fetches the associated achievements list.
 *
 * @param session Session to update (must not be NULL).
 * @param game New game to set. If NULL, the session is cleared.
 */
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

/**
 * @brief Applies an achievement progress update to the current session.
 *
 * Looks up the achievement by id and, if it has a reward, appends a new entry to
 * the session's @c gamerscore->unlocked_achievements list.
 *
 * Current behavior/assumptions:
 * - The function assumes the first reward's @c value is a numeric gamerscore
 *   amount and will parse it via @c atoi().
 * - No de-duplication is performed; callers should avoid sending the same unlock
 *   multiple times.
 *
 * @param session Session to update (may be NULL).
 * @param progress Progress update indicating which achievement was unlocked
 *        (may be NULL).
 */
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

/**
 * @brief Clears all cached state in the session.
 *
 * Frees heap allocations owned by the session and leaves it in an empty state:
 * - clears the cached achievements list
 * - clears the current game
 * - clears the cached gamerscore (including unlocked achievements list)
 *
 * This does not free the @p session object itself.
 *
 * @param session Session to clear (may be NULL).
 */
void xbox_session_clear(xbox_session_t *session) {

    if (!session) {
        return;
    }

    free_achievement(&session->achievements);
    free_game(&session->game);
    free_gamerscore(&session->gamerscore);
}
