#include "sources/common/achievement_cycle.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include "common/achievement.h"
#include "integrations/monitoring_service.h"

#include <stdlib.h>

/** Duration to show the last unlocked achievement (seconds). */
#define LAST_UNLOCKED_DISPLAY_DURATION 45.0f

/** Duration to show each random locked achievement (seconds). */
#define LOCKED_ACHIEVEMENT_DISPLAY_DURATION 30.0f

/** Total duration to cycle through locked achievements (seconds). */
#define LOCKED_CYCLE_TOTAL_DURATION 120.0f

/** Maximum number of subscribers that can be registered. */
#define MAX_SUBSCRIBERS 16

/**
 * @brief Display cycle phase for achievement rotation.
 */
typedef enum display_cycle_phase {
    /** Showing the last unlocked achievement. */
    DISPLAY_PHASE_LAST_UNLOCKED,
    /** Showing random locked achievements. */
    DISPLAY_PHASE_LOCKED_ROTATION,
} display_cycle_phase_t;

/** Current display cycle phase. */
static display_cycle_phase_t g_display_phase = DISPLAY_PHASE_LAST_UNLOCKED;

/** Time remaining in the current display phase (seconds). */
static float g_phase_timer = LAST_UNLOCKED_DISPLAY_DURATION;

/** Time remaining for the current locked achievement display (seconds). */
static float g_locked_display_timer = LOCKED_ACHIEVEMENT_DISPLAY_DURATION;

/** The last unlocked achievement (owned by this module). */
static achievement_t *g_last_unlocked = NULL;

/** Currently displayed achievement (pointer into g_last_unlocked or external data). */
static const achievement_t *g_current_achievement = NULL;

/** Array of registered subscriber callbacks. */
static achievement_cycle_callback_t g_subscribers[MAX_SUBSCRIBERS];

/** Number of registered subscribers. */
static int g_subscriber_count = 0;

/** Whether the module has been initialized. */
static bool g_initialized = false;

/**
 * @brief Whether the session is fully ready (achievements fetched + icons prefetched).
 *
 * Set to true by on_session_ready, reset to false by on_game_played.
 * While false, achievement_cycle_tick and reset_display_cycle are no-ops so
 * the cycle does not start before all icons are available in the local cache.
 */
static bool g_session_ready = false;

//  --------------------------------------------------------------------------------------------------------------------
//  Internal helpers
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Notify all subscribers of an achievement change.
 *
 * @param achievement The new achievement to display.
 */
static void notify_subscribers(const achievement_t *achievement) {

    g_current_achievement = achievement;

    for (int i = 0; i < g_subscriber_count; i++) {
        if (g_subscribers[i]) {
            g_subscribers[i](achievement);
        }
    }
}

/**
 * @brief Reset the display cycle to show the last unlocked achievement.
 *
 * Finds the last unlocked achievement and resets all timers to start
 * a fresh display cycle. Makes a deep copy of the achievement to avoid
 * dangling pointers when the session changes.
 */
static void reset_display_cycle(void) {

    if (!g_session_ready) {
        return;
    }

    /* Free the old cached copy */
    free_achievement(&g_last_unlocked);

    achievement_t *achievements = copy_achievement(monitoring_get_current_game_achievements());

    /* Find the last unlocked achievement */
    const achievement_t *latest_unlocked = find_latest_unlocked_achievement(achievements);
    if (latest_unlocked) {
        g_last_unlocked = copy_achievement(latest_unlocked);
    }

    free_achievement(&achievements);

    /* Reset display cycle to show the last unlocked achievement */
    g_display_phase        = DISPLAY_PHASE_LAST_UNLOCKED;
    g_phase_timer          = LAST_UNLOCKED_DISPLAY_DURATION;
    g_locked_display_timer = LOCKED_ACHIEVEMENT_DISPLAY_DURATION;

    notify_subscribers(g_last_unlocked);
}

//  --------------------------------------------------------------------------------------------------------------------
//  Monitoring service event handlers
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Monitoring service callback invoked when connection state changes.
 *
 * @param is_connected Whether the account is currently connected (unused).
 * @param error_message Optional error message if disconnected (unused).
 */
static void on_connection_changed(bool is_connected, const char *error_message) {

    UNUSED_PARAMETER(is_connected);
    UNUSED_PARAMETER(error_message);

    reset_display_cycle();
}

/**
 * @brief Event handler called when a new game starts being played.
 *
 * Clears the current display while we wait for the session to become ready
 * (icons prefetched). The actual cycle restart happens in on_session_ready.
 *
 * @param game Currently played game information.
 */
static void on_game_played(const game_t *game) {

    UNUSED_PARAMETER(game);

    /* Mark the session as not ready until icons are prefetched */
    g_session_ready = false;

    /* Clear the display while icons are being prefetched */
    g_last_unlocked = NULL;
    notify_subscribers(NULL);
}

/**
 * @brief Monitoring service callback invoked when achievements are updated.
 */
static void on_achievements_changed(void) {
    reset_display_cycle();
}

/**
 * @brief Monitoring service callback invoked when the session is fully ready.
 *
 * Called once all achievement icons have been downloaded to the local cache.
 * This is the signal to start (or restart) the achievement display cycle.
 */
static void on_session_ready(void) {

    g_session_ready = true;
    reset_display_cycle();
}

//  --------------------------------------------------------------------------------------------------------------------
//  Public API
//  --------------------------------------------------------------------------------------------------------------------

void achievement_cycle_init(void) {

    if (g_initialized) {
        return;
    }

    g_display_phase        = DISPLAY_PHASE_LAST_UNLOCKED;
    g_phase_timer          = LAST_UNLOCKED_DISPLAY_DURATION;
    g_locked_display_timer = LOCKED_ACHIEVEMENT_DISPLAY_DURATION;
    g_last_unlocked        = NULL;
    g_current_achievement  = NULL;
    g_subscriber_count     = 0;

    monitoring_subscribe_connection_changed(&on_connection_changed);
    monitoring_subscribe_game_played(&on_game_played);
    monitoring_subscribe_achievements_changed(&on_achievements_changed);
    monitoring_subscribe_session_ready(&on_session_ready);

    g_initialized = true;
}

void achievement_cycle_destroy(void) {

    if (!g_initialized) {
        return;
    }

    /* TODO: Add monitoring_unsubscribe_* functions if available */

    /* Free the owned achievement copy */
    free_achievement(&g_last_unlocked);

    g_subscriber_count    = 0;
    g_current_achievement = NULL;
    g_initialized         = false;
}

void achievement_cycle_subscribe(achievement_cycle_callback_t callback) {

    if (!callback) {
        return;
    }

    if (g_subscriber_count >= MAX_SUBSCRIBERS) {
        obs_log(LOG_WARNING, "Achievement cycle: Maximum subscribers reached");
        return;
    }

    /* Check for duplicates */
    for (int i = 0; i < g_subscriber_count; i++) {
        if (g_subscribers[i] == callback) {
            return;
        }
    }

    g_subscribers[g_subscriber_count++] = callback;

    /* If the session is already ready, immediately notify the new subscriber
     * with the current achievement so it does not miss the initial state. */
    if (g_session_ready) {
        callback(g_current_achievement);
    }
}

void achievement_cycle_unsubscribe(achievement_cycle_callback_t callback) {

    if (!callback) {
        return;
    }

    for (int i = 0; i < g_subscriber_count; i++) {
        if (g_subscribers[i] == callback) {
            /* Shift the remaining callbacks down */
            for (int j = i; j < g_subscriber_count - 1; j++) {
                g_subscribers[j] = g_subscribers[j + 1];
            }
            g_subscriber_count--;
            g_subscribers[g_subscriber_count] = NULL;
            return;
        }
    }
}

void achievement_cycle_tick(float seconds) {

    if (!g_initialized || !g_session_ready) {
        return;
    }

    /* Get the current achievements */
    achievement_t *achievements = copy_achievement(monitoring_get_current_game_achievements());

    if (!achievements) {
        return;
    }

    /* Update timers */
    g_phase_timer -= seconds;

    switch (g_display_phase) {
    case DISPLAY_PHASE_LAST_UNLOCKED:
        if (g_phase_timer <= 0.0f) {
            obs_log(LOG_DEBUG, "Achievement Cycle: Switching to locked achievements rotation");
            if (count_locked_achievements(achievements) > 0) {
                g_display_phase        = DISPLAY_PHASE_LOCKED_ROTATION;
                g_phase_timer          = LOCKED_CYCLE_TOTAL_DURATION;
                g_locked_display_timer = LOCKED_ACHIEVEMENT_DISPLAY_DURATION;

                const achievement_t *locked = get_random_locked_achievement(achievements);

                if (locked) {
                    obs_log(LOG_DEBUG, "Achievement Cycle: Showing random locked achievement: %s", locked->name);
                    /* Store a copy so g_current_achievement survives beyond this tick */
                    free_achievement(&g_last_unlocked);
                    g_last_unlocked = copy_achievement(locked);
                    notify_subscribers(g_last_unlocked);
                } else {
                    obs_log(LOG_WARNING, "Achievement Cycle: No locked achievements to show");
                }
            } else {
                obs_log(LOG_DEBUG, "Achievement Cycle: No locked achievements, keeping last unlocked");
                g_phase_timer = LAST_UNLOCKED_DISPLAY_DURATION;
            }
        }
        break;

    case DISPLAY_PHASE_LOCKED_ROTATION:
        g_locked_display_timer -= seconds;

        if (g_locked_display_timer <= 0.0f) {
            g_locked_display_timer = LOCKED_ACHIEVEMENT_DISPLAY_DURATION;

            const achievement_t *locked = get_random_locked_achievement(achievements);
            if (locked) {
                free_achievement(&g_last_unlocked);
                g_last_unlocked = copy_achievement(locked);
                notify_subscribers(g_last_unlocked);
            }
        }

        if (g_phase_timer <= 0.0f) {
            obs_log(LOG_DEBUG, "Achievement Cycle: Locked achievements rotation complete");
            g_display_phase = DISPLAY_PHASE_LAST_UNLOCKED;
            g_phase_timer   = LAST_UNLOCKED_DISPLAY_DURATION;

            if (g_last_unlocked) {
                notify_subscribers(g_last_unlocked);
            } else {
                const achievement_t *latest_unlocked = find_latest_unlocked_achievement(achievements);
                if (latest_unlocked) {
                    g_last_unlocked = copy_achievement(latest_unlocked);
                    notify_subscribers(g_last_unlocked);
                }
            }
        }
        break;
    }

    free_achievement(&achievements);
}

const achievement_t *achievement_cycle_get_current(void) {
    return g_current_achievement;
}

const achievement_t *achievement_cycle_get_last_unlocked(void) {
    return g_last_unlocked;
}
