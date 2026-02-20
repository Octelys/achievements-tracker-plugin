#include "xbox/xbox_session.h"

#include <diagnostics/log.h>

#include "common/types.h"
#include "io/cache.h"
#include "util/bmem.h"
#include "xbox/xbox_client.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

//  --------------------------------------------------------------------------------------------------------------------
//  Icon prefetch helpers
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Context passed to the prefetch background thread.
 *
 * Bundles the deep-copied achievements list (owned by the thread) and the
 * optional completion callback.
 */
typedef struct prefetch_context {
    /** Deep-copied achievement list. Freed by the thread when done. */
    achievement_t                *achievements;
    /** Optional callback invoked after all icons have been downloaded. */
    xbox_session_ready_callback_t on_ready;
} prefetch_context_t;

/**
 * @brief Download a single achievement icon to the local file cache.
 *
 * Uses the same cache-path convention as image_source_download() so that later
 * display requests hit the on-disk cache instead of making another HTTP call.
 *
 * @param achievement
 */
static bool download_icon_to_cache(const achievement_t *achievement) {

    if (!achievement->icon_url || achievement->icon_url[0] == '\0') {
        return false;
    }

    /* Build the composite id matching what achievement_icon.c uses */
    char id[256];
    snprintf(id, sizeof(id), "%s_%s", achievement->service_config_id, achievement->id);

    return cache_download(achievement->icon_url, "achievement_icon", id, NULL, 0);
}

/**
 * @brief Background thread entry point: prefetches all achievement icons.
 *
 * Receives a prefetch_context_t (deep-copied achievement list + optional
 * callback), downloads every icon to the local cache, invokes the callback,
 * then frees all resources.  The thread is created detached, so no join is
 * required.
 *
 * @param arg Pointer to a heap-allocated prefetch_context_t. Ownership is
 *            transferred to this thread.
 * @return NULL (unused).
 */
static void *prefetch_icons_thread(void *arg) {

    prefetch_context_t *ctx          = arg;
    achievement_t      *achievements = ctx->achievements;
    int                 count        = 0;

    for (const achievement_t *achievement = achievements; achievement != NULL; achievement = achievement->next) {
        if (download_icon_to_cache(achievement)) {
            sleep_ms(5000);
        }
        count++;
    }

    obs_log(LOG_INFO, "[Prefetch] Finished prefetching %d achievement icons", count);

    if (ctx->on_ready) {
        ctx->on_ready();
    }

    free_achievement(&achievements);
    free_memory((void **)&ctx);
    return NULL;
}

/**
 * @brief Starts a background thread to prefetch all achievement icons.
 *
 * Deep-copies the given achievements list and passes ownership to a detached
 * pthread that downloads each icon to the local file cache.  When finished,
 * @p on_ready is invoked (if non-NULL) from the background thread.
 *
 * @param achievements Head of the achievements list to prefetch icons for.
 *                     The caller retains ownership; the function makes its own copy.
 * @param on_ready     Optional callback invoked when all icons have been downloaded.
 */
static void prefetch_achievement_icons(const achievement_t *achievements, xbox_session_ready_callback_t on_ready) {

    if (!achievements) {
        if (on_ready) {
            on_ready();
        }
        return;
    }

    achievement_t *copy = copy_achievement(achievements);

    if (!copy) {
        obs_log(LOG_WARNING, "[Prefetch] Failed to copy achievements for icon prefetch");
        if (on_ready) {
            on_ready();
        }
        return;
    }

    prefetch_context_t *ctx = bzalloc(sizeof(prefetch_context_t));
    ctx->achievements       = copy;
    ctx->on_ready           = on_ready;

    pthread_t thread;
    if (pthread_create(&thread, NULL, prefetch_icons_thread, ctx) == 0) {
        pthread_detach(thread);
        obs_log(LOG_INFO, "[Prefetch] Started background icon prefetch thread");
    } else {
        obs_log(LOG_ERROR, "[Prefetch] Failed to create icon prefetch thread");
        free_achievement(&copy);
        free_memory((void **)&ctx);
        if (on_ready) {
            on_ready();
        }
    }
}

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

void xbox_session_change_game(xbox_session_t *session, game_t *game, xbox_session_ready_callback_t on_ready) {

    if (!session) {
        obs_log(LOG_ERROR, "Failed to change game: session is NULL");
        return;
    }

    free_achievement(&session->achievements);
    free_game(&session->game);

    /* Let's get the achievements of the game */
    if (!game) {
        if (on_ready) {
            on_ready();
        }
        return;
    }

    session->game         = copy_game(game);
    session->achievements = xbox_get_game_achievements(game);

    /* Sort the achievements from the most recent unlocked to the locked ones */
    sort_achievements(&session->achievements);

    /* Prefetch all achievement icons in the background; on_ready fires when done */
    prefetch_achievement_icons(session->achievements, on_ready);
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
