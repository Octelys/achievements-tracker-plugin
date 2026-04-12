#pragma once

#include "common/achievement.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file achievement_cycle.h
 * @brief Shared achievement display cycling logic for OBS sources.
 *
 * This module provides centralized management for the achievement display cycle
 * that alternates between showing the last unlocked achievement and rotating
 * through random locked achievements. Multiple sources can subscribe to receive
 * notifications when the displayed achievement changes, ensuring all sources
 * stay synchronized.
 *
 * The display cycle follows this pattern:
 * - Show the last unlocked achievement for 60 seconds
 * - Rotate through random locked achievements (15 seconds each) for 60 seconds
 * - Repeat
 */

/**
 * @brief Callback type for achievement change notifications.
 *
 * Sources register callbacks of this type to be notified when the currently
 * displayed achievement changes (either due to timer expiration, user action,
 * or external events like new achievements being unlocked).
 *
 * @param achievement The achievement that should now be displayed.
 *                    May be NULL if no achievement is available.
 */
typedef void (*achievement_cycle_callback_t)(const achievement_t *achievement);

/**
 * @brief Initialize the achievement cycle module.
 *
 * Sets up the internal state and subscribes to monitoring service events.
 * Call this once during plugin initialization.
 */
void achievement_cycle_init(void);

/**
 * @brief Clean up the achievement cycle module.
 *
 * Frees resources and unsubscribes from events.
 * Call this during plugin shutdown.
 */
void achievement_cycle_destroy(void);

/**
 * @brief Subscribe to achievement change notifications.
 *
 * Registers a callback to be invoked whenever the displayed achievement changes.
 * Multiple callbacks can be registered; they will all be called in registration order.
 *
 * @param callback Function to call when the displayed achievement changes.
 */
void achievement_cycle_subscribe(achievement_cycle_callback_t callback);

/**
 * @brief Unsubscribe from achievement change notifications.
 *
 * Removes a previously registered callback. Safe to call even if the callback
 * was not registered.
 *
 * @param callback The callback to remove.
 */
void achievement_cycle_unsubscribe(achievement_cycle_callback_t callback);

/**
 * @brief Update the achievement display cycle.
 *
 * Call this from the video_tick callback of sources that participate in the
 * achievement cycling. Only one source needs to call this per frame; the module
 * uses internal tracking to prevent multiple updates per frame.
 *
 * @param seconds Time elapsed since the last tick (in seconds).
 */
void achievement_cycle_tick(float seconds);

/**
 * @brief Get the currently displayed achievement.
 *
 * Returns the achievement that should currently be displayed based on the
 * cycle state. This is useful for sources that need to query the current
 * achievement without waiting for a callback.
 *
 * @return The currently displayed achievement, or NULL if none.
 */
const achievement_t *achievement_cycle_get_current(void);

/**
 * @brief Get the last unlocked achievement.
 *
 * Returns the cached last unlocked achievement. This is useful for sources
 * that need to know the last unlocked achievement specifically.
 *
 * @return The last unlocked achievement, or NULL if none.
 */
const achievement_t *achievement_cycle_get_last_unlocked(void);

/**
 * @brief Update the display-duration settings used by the achievement cycle.
 *
 * Changes take effect at the start of the next phase; currently-running timers
 * are not truncated.  Values <= 0 are silently clamped to 1 second.
 *
 * @param last_unlocked_secs  Seconds to show the last-unlocked achievement.
 * @param locked_each_secs    Seconds to show each random locked achievement.
 * @param locked_total_secs   Total seconds to spend in the locked-rotation phase.
 */
void achievement_cycle_set_timings(float last_unlocked_secs, float locked_each_secs, float locked_total_secs);

/**
 * @brief Advance to the next achievement in the sorted list.
 *
 * Immediately displays the next achievement (wrapping around at the end of the
 * list) and resets the phase timer so it stays visible for a full interval.
 * No-op if the session is not ready or no achievements are available.
 */
void achievement_cycle_navigate_next(void);

/**
 * @brief Go back to the previous achievement in the sorted list.
 *
 * Immediately displays the previous achievement (wrapping around at the
 * beginning of the list) and resets the phase timer so it stays visible for a
 * full interval.  No-op if the session is not ready or no achievements are
 * available.
 */
void achievement_cycle_navigate_previous(void);

/**
 * @brief Jump directly to the first locked achievement in the sorted list.
 *
 * The sorted order places unlocked achievements first; locked achievements
 * follow. This function jumps to the first entry in the locked section.
 * No-op if the session is not ready, there are no achievements, or all
 * achievements are already unlocked.
 */
void achievement_cycle_navigate_first_locked(void);

/**
 * @brief Jump directly to the first unlocked achievement in the sorted list.
 *
 * Equivalent to jumping to index 0 of the sorted list, which holds the most
 * recently unlocked achievement.
 * No-op if the session is not ready, there are no achievements, or no
 * achievement has been unlocked yet.
 */
void achievement_cycle_navigate_first_unlocked(void);

/**
 * @brief Enable or disable the automatic achievement rotation.
 *
 * When disabled, @ref achievement_cycle_tick is a no-op: the cycle freezes on
 * the achievement currently being displayed.  Manual navigation via
 * @ref achievement_cycle_navigate_next / @ref achievement_cycle_navigate_previous
 * remains functional.
 *
 * The setting defaults to @c true and survives until the next call.
 *
 * @param enabled @c true to resume automatic cycling, @c false to pause it.
 */
void achievement_cycle_set_auto_cycle(bool enabled);

/**
 * @brief Return whether the automatic achievement rotation is currently active.
 *
 * @return @c true if the cycle advances automatically, @c false if it is paused.
 */
bool achievement_cycle_is_auto_cycle_enabled(void);

#ifdef __cplusplus
}
#endif
