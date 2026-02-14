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
 * Sets up the internal state and subscribes to Xbox monitor events.
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

#ifdef __cplusplus
}
#endif
