#include "sources/common/achievement_cycle.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include "xbox/xbox_monitor.h"

/** Duration to show the last unlocked achievement (seconds). */
#define LAST_UNLOCKED_DISPLAY_DURATION 30.0f

/** Duration to show each random locked achievement (seconds). */
#define LOCKED_ACHIEVEMENT_DISPLAY_DURATION 30.0f

/** Total duration to cycle through locked achievements (seconds). */
#define LOCKED_CYCLE_TOTAL_DURATION 90.0f

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

/** Cached pointer to the last unlocked achievement. */
static const achievement_t *g_last_unlocked = NULL;

/** Currently displayed achievement. */
static const achievement_t *g_current_achievement = NULL;

/** Array of registered subscriber callbacks. */
static achievement_cycle_callback_t g_subscribers[MAX_SUBSCRIBERS];

/** Number of registered subscribers. */
static int g_subscriber_count = 0;

/** Whether the module has been initialized. */
static bool g_initialized = false;

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
 * a fresh display cycle.
 */
static void reset_display_cycle(void) {

    const achievement_t *achievements = get_current_game_achievements();

    /* Find and cache the last unlocked achievement */
    g_last_unlocked = find_latest_unlocked_achievement(achievements);

    /* Reset display cycle to show the last unlocked achievement */
    g_display_phase        = DISPLAY_PHASE_LAST_UNLOCKED;
    g_phase_timer          = LAST_UNLOCKED_DISPLAY_DURATION;
    g_locked_display_timer = LOCKED_ACHIEVEMENT_DISPLAY_DURATION;

    notify_subscribers(g_last_unlocked);
}

//  --------------------------------------------------------------------------------------------------------------------
//  Xbox monitor event handlers
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Xbox monitor callback invoked when Xbox Live connection state changes.
 *
 * @param is_connected Whether the Xbox account is currently connected (unused).
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
 * @param game Currently played game information.
 */
static void on_xbox_game_played(const game_t *game) {

    UNUSED_PARAMETER(game);

    reset_display_cycle();
}

/**
 * @brief Xbox monitor callback invoked when achievement progress is updated.
 *
 * @param gamerscore Updated gamerscore snapshot (unused).
 * @param progress   Achievement progress details (unused).
 */
static void on_achievements_progressed(const gamerscore_t *gamerscore, const achievement_progress_t *progress) {

    UNUSED_PARAMETER(gamerscore);
    UNUSED_PARAMETER(progress);

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

    xbox_subscribe_connected_changed(&on_connection_changed);
    xbox_subscribe_game_played(&on_xbox_game_played);
    xbox_subscribe_achievements_progressed(&on_achievements_progressed);

    g_initialized = true;
}

void achievement_cycle_destroy(void) {

    if (!g_initialized) {
        return;
    }

    /* TODO: Add xbox_unsubscribe_* functions if available */

    g_subscriber_count    = 0;
    g_last_unlocked       = NULL;
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

    if (!g_initialized) {
        return;
    }

    /* Get the current achievements list */
    const achievement_t *achievements = get_current_game_achievements();

    if (!achievements) {
        return;
    }

    /* Update timers */
    g_phase_timer -= seconds;

    switch (g_display_phase) {
    case DISPLAY_PHASE_LAST_UNLOCKED:
        /* Check if it's time to switch to locked achievements rotation */
        if (g_phase_timer <= 0.0f) {
            obs_log(LOG_INFO, "Achievement Cycle: Switching to locked achievements rotation");
            /* Only switch if there are locked achievements to show */
            if (count_locked_achievements(achievements) > 0) {
                g_display_phase        = DISPLAY_PHASE_LOCKED_ROTATION;
                g_phase_timer          = LOCKED_CYCLE_TOTAL_DURATION;
                g_locked_display_timer = LOCKED_ACHIEVEMENT_DISPLAY_DURATION;

                /* Show the first random locked achievement */
                const achievement_t *locked = get_random_locked_achievement(achievements);

                if (locked) {
                    obs_log(LOG_INFO, "Achievement Cycle: Showing random locked achievement: %s", locked->name);
                    notify_subscribers(locked);
                } else {
                    obs_log(LOG_WARNING, "Achievement Cycle: No locked achievements to show");
                }
            } else {
                obs_log(LOG_INFO, "Achievement Cycle: No locked achievements, keeping last unlocked");
                /* No locked achievements, keep showing last unlocked */
                g_phase_timer = LAST_UNLOCKED_DISPLAY_DURATION;
            }
        }
        break;

    case DISPLAY_PHASE_LOCKED_ROTATION:
        /* Update the locked achievement display timer */
        g_locked_display_timer -= seconds;

        if (g_locked_display_timer <= 0.0f) {
            /* Time for the next random locked achievement */
            g_locked_display_timer = LOCKED_ACHIEVEMENT_DISPLAY_DURATION;

            const achievement_t *locked = get_random_locked_achievement(achievements);
            if (locked) {
                notify_subscribers(locked);
            }
        }

        /* Check if the locked rotation phase is complete */
        if (g_phase_timer <= 0.0f) {
            obs_log(LOG_INFO, "Achievement Cycle: Locked achievements rotation complete");
            g_display_phase = DISPLAY_PHASE_LAST_UNLOCKED;
            g_phase_timer   = LAST_UNLOCKED_DISPLAY_DURATION;

            /* Switch back to the last unlocked achievement */
            if (g_last_unlocked) {
                notify_subscribers(g_last_unlocked);
            } else {
                /* Refresh the last unlocked in case it changed */
                g_last_unlocked = find_latest_unlocked_achievement(achievements);
                if (g_last_unlocked) {
                    notify_subscribers(g_last_unlocked);
                }
            }
        }
        break;
    }
}

const achievement_t *achievement_cycle_get_current(void) {
    return g_current_achievement;
}

const achievement_t *achievement_cycle_get_last_unlocked(void) {
    return g_last_unlocked;
}
