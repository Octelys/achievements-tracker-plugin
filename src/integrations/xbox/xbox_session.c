#include "integrations/xbox/xbox_session.h"

#include <diagnostics/log.h>

#include "common/types.h"
#include "io/cache.h"
#include "util/bmem.h"
#include "integrations/xbox/xbox_client.h"
#include "integrations/xbox/contracts/xbox_achievement.h"
#include "integrations/xbox/contracts/xbox_achievement_progress.h"
#include "integrations/xbox/contracts/xbox_unlocked_achievement.h"

#include <errno.h>
#include <util/thread_compat.h>
#include <stdlib.h>
#include <string.h>

//  --------------------------------------------------------------------------------------------------------------------
//  Icon prefetch helpers
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Context passed to the prefetch background thread.
 */
typedef struct prefetch_context {
    /** Deep-copied achievement list. Freed by the thread when done. */
    xbox_achievement_t           *achievements;
    /** Optional callback invoked after all icons have been downloaded. */
    xbox_session_ready_callback_t on_ready;
} prefetch_context_t;

/**
 * @brief Download a single achievement icon to the local file cache.
 */
static bool download_icon_to_cache(const xbox_achievement_t *achievement) {

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
 */
static void *prefetch_icons_thread(void *arg) {

    prefetch_context_t *ctx          = arg;
    xbox_achievement_t *achievements = ctx->achievements;
    int                 count        = 0;

    for (const xbox_achievement_t *achievement = achievements; achievement != NULL; achievement = achievement->next) {
        if (download_icon_to_cache(achievement)) {
            sleep_ms(5000);
        }
        count++;
    }

    obs_log(LOG_INFO, "[Prefetch] Finished prefetching %d achievement icons", count);

    if (ctx->on_ready) {
        ctx->on_ready();
    }

    xbox_free_achievement(&achievements);
    free_memory((void **)&ctx);
    return NULL;
}

/**
 * @brief Starts a background thread to prefetch all achievement icons.
 */
static void prefetch_achievement_icons(const xbox_achievement_t *achievements, xbox_session_ready_callback_t on_ready) {

    if (!achievements) {
        if (on_ready) {
            on_ready();
        }
        return;
    }

    xbox_achievement_t *copy = xbox_copy_achievement(achievements);

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
        xbox_free_achievement(&copy);
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
 * @brief Finds an Xbox achievement definition by id.
 */
static xbox_achievement_t *find_achievement_by_id(const xbox_achievement_progress_t *progress,
                                                  xbox_achievement_t                *achievements) {

    xbox_achievement_t *current = achievements;

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

    xbox_free_achievement(&session->achievements);
    free_game(&session->game);

    if (!game) {
        if (on_ready) {
            on_ready();
        }
        return;
    }

    session->game         = copy_game(game);
    session->achievements = xbox_get_game_achievements(game);

    xbox_sort_achievements(&session->achievements);

    prefetch_achievement_icons(session->achievements, on_ready);
}

void xbox_session_unlock_achievement(xbox_session_t *session, const xbox_achievement_progress_t *progress) {

    if (!session || !progress) {
        return;
    }

    xbox_achievement_t *achievement = find_achievement_by_id(progress, session->achievements);

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

    xbox_sort_achievements(&session->achievements);

    const xbox_reward_t *reward = achievement->rewards;

    if (!reward) {
        obs_log(LOG_ERROR, "Failed to unlock achievement %s: no reward found", progress->id ? progress->id : "(null)");
        return;
    }

    obs_log(LOG_DEBUG, "Found reward %s", reward->value);

    gamerscore_t *gamerscore = session->gamerscore;

    xbox_unlocked_achievement_t *unlocked_achievement = bzalloc(sizeof(xbox_unlocked_achievement_t));
    unlocked_achievement->id                          = bstrdup(progress->id);

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

    xbox_unlocked_achievement_t *unlocked_achievements = gamerscore->unlocked_achievements;

    if (!unlocked_achievements) {
        gamerscore->unlocked_achievements = unlocked_achievement;
    } else {
        xbox_unlocked_achievement_t *last_unlocked_achievement = unlocked_achievements;
        while (last_unlocked_achievement->next) {
            last_unlocked_achievement = last_unlocked_achievement->next;
        }
        last_unlocked_achievement->next = unlocked_achievement;
    }

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

    xbox_free_achievement(&session->achievements);
    free_game(&session->game);
    free_gamerscore(&session->gamerscore);
}
