#include "sources/common/achievement_cycle.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include "common/achievement.h"
#include "integrations/monitoring_service.h"

#include <stdlib.h>

#include "common/types.h"

/** Maximum number of subscribers that can be registered. */
#define MAX_SUBSCRIBERS 16

/** Configurable duration to show the last unlocked achievement (seconds). */
static float g_last_unlocked_duration = ACHIEVEMENT_CYCLE_DEFAULT_LAST_UNLOCKED_DURATION;

/** Configurable duration to show each random locked achievement (seconds). */
static float g_locked_each_duration = ACHIEVEMENT_CYCLE_DEFAULT_LOCKED_EACH_DURATION;

/** Configurable total duration to cycle through locked achievements (seconds). */
static float g_locked_total_duration = ACHIEVEMENT_CYCLE_DEFAULT_LOCKED_TOTAL_DURATION;

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
static float g_phase_timer = ACHIEVEMENT_CYCLE_DEFAULT_LAST_UNLOCKED_DURATION;

/** Time remaining for the current locked achievement display (seconds). */
static float g_locked_display_timer = ACHIEVEMENT_CYCLE_DEFAULT_LOCKED_EACH_DURATION;

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

/** Whether the automatic display cycle is active (true by default). */
static bool g_auto_cycle_enabled = true;

/**
 * @brief Index of the currently displayed achievement in the sorted full list.
 *
 * Used by the manual navigation functions to keep track of position across
 * calls.  Reset to 0 whenever the achievement set changes (game change,
 * session reset, etc.).
 */
static int g_nav_index = 0;

/**
 * @brief Whether the session is ready to cycle achievements.
 *
 * Set to true by on_session_ready, reset to false by on_game_played.
 * While false, achievement_cycle_tick and reset_display_cycle are no-ops.
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
 * @brief Reset the display cycle to show the last unlocked achievement,
 *        or a random locked achievement if none have been unlocked yet.
 *
 * Finds the last unlocked achievement and resets all timers to start
 * a fresh display cycle. Makes a deep copy of the achievement to avoid
 * dangling pointers when the session changes.
 *
 * If there are no unlocked achievements but there are locked ones,
 * immediately starts the locked rotation so sources display something
 * straight away rather than staying blank for g_last_unlocked_duration.
 */
static void reset_display_cycle(void) {

    if (!g_session_ready) {
        return;
    }

    /* Reset the navigation index so manual navigation restarts from a clean position */
    g_nav_index = 0;

    /* Free the old cached copy */
    free_achievement(&g_last_unlocked);

    achievement_t *achievements = copy_achievement(monitoring_get_current_game_achievements());

    /* Find the last unlocked achievement */
    const achievement_t *latest_unlocked = find_latest_unlocked_achievement(achievements);
    if (latest_unlocked) {
        g_last_unlocked = copy_achievement(latest_unlocked);
    }

    if (g_last_unlocked) {
        /* There is a last-unlocked achievement — start with the normal phase */
        g_display_phase        = DISPLAY_PHASE_LAST_UNLOCKED;
        g_phase_timer          = g_last_unlocked_duration;
        g_locked_display_timer = g_locked_each_duration;

        notify_subscribers(g_last_unlocked);
    } else {
        /* No unlocked achievements yet — immediately show a random locked one
         * so sources are never blank at session start. */
        const achievement_t *locked = get_random_locked_achievement(achievements);
        if (locked) {
            g_last_unlocked = copy_achievement(locked);

            g_display_phase        = DISPLAY_PHASE_LOCKED_ROTATION;
            g_phase_timer          = g_locked_total_duration;
            g_locked_display_timer = g_locked_each_duration;

            obs_log(LOG_DEBUG, "Achievement Cycle: No unlocked achievements, showing random locked: %s", locked->name);
            notify_subscribers(g_last_unlocked);
        } else {
            /* No achievements at all (empty list) — clear the display */
            g_display_phase        = DISPLAY_PHASE_LAST_UNLOCKED;
            g_phase_timer          = g_last_unlocked_duration;
            g_locked_display_timer = g_locked_each_duration;

            notify_subscribers(NULL);
        }
    }

    free_achievement(&achievements);
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
    free_achievement(&g_last_unlocked);
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
 * Called once achievements are loaded for the active game.
 * This is the signal to start (or restart) the achievement display cycle.
 */
static void on_session_ready(void) {

    g_session_ready = true;
    reset_display_cycle();
}

//  --------------------------------------------------------------------------------------------------------------------
//  Manual navigation helpers
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Move the display to an adjacent achievement in the sorted list.
 *
 * @param direction +1 for next, -1 for previous.
 */
static void navigate(int direction) {

    if (!g_initialized || !g_session_ready) {
        return;
    }

    achievement_t *achievements = copy_achievement(monitoring_get_current_game_achievements());
    if (!achievements) {
        return;
    }

    int count = count_achievements(achievements);
    if (count == 0) {
        free_achievement(&achievements);
        return;
    }

    /* Sort for a stable, deterministic traversal order (unlocked first, then
     * by timestamp descending; locked achievements follow). */
    sort_achievements(&achievements);

    /* Advance or rewind, wrapping at the boundaries. */
    g_nav_index = ((g_nav_index + direction) % count + count) % count;

    /* Walk the linked list to the target index. */
    const achievement_t *target = achievements;
    for (int i = 0; i < g_nav_index && target->next; i++) {
        target = target->next;
    }

    /* Update the cached copy and reset the phase so the achievement is
     * visible for a full interval before the automatic cycle resumes. */
    free_achievement(&g_last_unlocked);
    g_last_unlocked = copy_achievement(target);

    g_display_phase        = DISPLAY_PHASE_LAST_UNLOCKED;
    g_phase_timer          = g_last_unlocked_duration;
    g_locked_display_timer = g_locked_each_duration;

    obs_log(LOG_DEBUG,
            "Achievement Cycle: Manual navigation to index %d: %s",
            g_nav_index,
            target->name ? target->name : "(null)");

    notify_subscribers(g_last_unlocked);
    free_achievement(&achievements);
}

/**
 * @brief Jump directly to a specific index in the sorted achievement list.
 *
 * @param target_index Absolute index to jump to (0-based, from sorted list).
 */
static void navigate_to_index(int target_index) {

    if (!g_initialized || !g_session_ready) {
        return;
    }

    achievement_t *achievements = copy_achievement(monitoring_get_current_game_achievements());
    if (!achievements) {
        return;
    }

    int count = count_achievements(achievements);
    if (count == 0 || target_index < 0 || target_index >= count) {
        free_achievement(&achievements);
        return;
    }

    sort_achievements(&achievements);

    /* Walk the linked list to the target index. */
    const achievement_t *target = achievements;
    for (int i = 0; i < target_index && target->next; i++) {
        target = target->next;
    }

    g_nav_index = target_index;

    free_achievement(&g_last_unlocked);
    g_last_unlocked = copy_achievement(target);

    g_display_phase        = DISPLAY_PHASE_LAST_UNLOCKED;
    g_phase_timer          = g_last_unlocked_duration;
    g_locked_display_timer = g_locked_each_duration;

    obs_log(LOG_DEBUG, "Achievement Cycle: Jumped to index %d: %s", g_nav_index, target->name ? target->name : "(null)");

    notify_subscribers(g_last_unlocked);
    free_achievement(&achievements);
}

//  --------------------------------------------------------------------------------------------------------------------
//  Public API
//  --------------------------------------------------------------------------------------------------------------------

void achievement_cycle_init(void) {

    if (g_initialized) {
        return;
    }

    g_display_phase        = DISPLAY_PHASE_LAST_UNLOCKED;
    g_phase_timer          = g_last_unlocked_duration;
    g_locked_display_timer = g_locked_each_duration;
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

    if (!g_initialized || !g_session_ready || !g_auto_cycle_enabled) {
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
                g_phase_timer          = g_locked_total_duration;
                g_locked_display_timer = g_locked_each_duration;

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
                g_phase_timer = g_last_unlocked_duration;
            }
        }
        break;

    case DISPLAY_PHASE_LOCKED_ROTATION:
        g_locked_display_timer -= seconds;

        if (g_locked_display_timer <= 0.0f) {
            g_locked_display_timer = g_locked_each_duration;

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
            g_phase_timer   = g_last_unlocked_duration;

            /* Always re-derive the last unlocked achievement from the live list.
             * g_last_unlocked is overwritten with locked achievements during the
             * rotation phase and cannot be trusted to hold the correct value here. */
            free_achievement(&g_last_unlocked);
            const achievement_t *latest_unlocked = find_latest_unlocked_achievement(achievements);
            if (latest_unlocked) {
                g_last_unlocked = copy_achievement(latest_unlocked);
                notify_subscribers(g_last_unlocked);
            } else {
                /* Still no unlocked achievements — keep showing a random locked one
                 * rather than going blank for the entire unlocked phase. */
                const achievement_t *locked = get_random_locked_achievement(achievements);
                if (locked) {
                    g_last_unlocked = copy_achievement(locked);
                    notify_subscribers(g_last_unlocked);
                } else {
                    notify_subscribers(NULL);
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

void achievement_cycle_navigate_next(void) {
    navigate(+1);
}

void achievement_cycle_navigate_previous(void) {
    navigate(-1);
}

void achievement_cycle_navigate_first_locked(void) {

    if (!g_initialized || !g_session_ready) {
        return;
    }

    achievement_t *achievements = copy_achievement(monitoring_get_current_game_achievements());
    if (!achievements) {
        return;
    }

    sort_achievements(&achievements);

    /* In the sorted list, unlocked achievements come first. The first locked
     * achievement is therefore at index == count_unlocked_achievements(). */
    int first_locked_index = count_unlocked_achievements(achievements);
    int total              = count_achievements(achievements);

    free_achievement(&achievements);

    if (first_locked_index >= total) {
        obs_log(LOG_DEBUG, "Achievement Cycle: No locked achievements to jump to");
        return;
    }

    obs_log(LOG_DEBUG, "Achievement Cycle: Jumping to first locked achievement at index %d", first_locked_index);
    navigate_to_index(first_locked_index);
}

void achievement_cycle_navigate_first_unlocked(void) {

    if (!g_initialized || !g_session_ready) {
        return;
    }

    achievement_t *achievements = copy_achievement(monitoring_get_current_game_achievements());
    if (!achievements) {
        return;
    }

    int unlocked_count = count_unlocked_achievements(achievements);
    free_achievement(&achievements);

    if (unlocked_count == 0) {
        obs_log(LOG_DEBUG, "Achievement Cycle: No unlocked achievements to jump to");
        return;
    }

    obs_log(LOG_DEBUG, "Achievement Cycle: Jumping to first unlocked achievement (index 0)");
    navigate_to_index(0);
}

void achievement_cycle_set_auto_cycle(bool enabled) {
    g_auto_cycle_enabled = enabled;
    obs_log(LOG_DEBUG, "Achievement Cycle: auto-cycle %s", enabled ? "enabled" : "disabled");
}

bool achievement_cycle_is_auto_cycle_enabled(void) {
    return g_auto_cycle_enabled;
}

void achievement_cycle_set_timings(float last_unlocked_secs, float locked_each_secs, float locked_total_secs) {

    g_last_unlocked_duration = last_unlocked_secs >= ACHIEVEMENT_CYCLE_MIN_DURATION ? last_unlocked_secs
                                                                                    : ACHIEVEMENT_CYCLE_MIN_DURATION;

    g_locked_each_duration = locked_each_secs >= ACHIEVEMENT_CYCLE_MIN_DURATION ? locked_each_secs
                                                                                : ACHIEVEMENT_CYCLE_MIN_DURATION;

    /* The total locked rotation must fit at least one locked achievement. */
    g_locked_total_duration = locked_total_secs >= g_locked_each_duration ? locked_total_secs : g_locked_each_duration;

    obs_log(LOG_DEBUG,
            "Achievement Cycle: timings updated — last_unlocked=%.0fs, locked_each=%.0fs, locked_total=%.0fs",
            g_last_unlocked_duration,
            g_locked_each_duration,
            g_locked_total_duration);
}
