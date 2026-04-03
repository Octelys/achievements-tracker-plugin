#pragma once

#include "common/achievement.h"
#include "common/game.h"
#include "common/identity.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file monitoring_service.h
 * @brief Unified entry point that starts and stops all integration monitors.
 *
 * Wraps @ref xbox_monitoring_start / @ref xbox_monitoring_stop and
 * @ref retro_achievements_monitor_start / @ref retro_achievements_monitor_stop
 * so that callers do not need to depend on each integration directly.
 */

/**
 * @brief Callback invoked when the connection status of any monitor changes.
 *
 * Mirrors the individual monitor connection-changed signatures so a single
 * handler can cover both Xbox and RetroAchievements.
 *
 * @param connected     true if the monitor just connected; false if it
 *                      disconnected.
 * @param error_message Human-readable error description when @p connected is
 *                      false and the disconnect was caused by an error; NULL
 *                      for a clean disconnect or a successful connection.
 */
typedef void (*on_monitoring_connection_changed_t)(bool connected, const char *error_message);

/**
 * @brief Callback invoked when the active identity changes.
 *
 * Fired whenever either integration receives a new-game-playing notification.
 * The identity is resolved from whichever source produced the game event.
 * May be called with NULL when no identity is available for that source.
 *
 * @param identity  The currently active identity, or NULL if unavailable.
 */
typedef void (*on_monitoring_active_identity_changed_t)(const identity_t *identity);

/**
 * @brief Callback invoked when the current game changes.
 *
 * Fired by whichever integration detects a new game being played.
 * May be called with NULL when no game is active.
 *
 * @param game The currently played game, or NULL.
 */
typedef void (*on_monitoring_game_played_t)(const game_t *game);

/**
 * @brief Callback invoked when the achievements list for the current game changes.
 *
 * Fired whenever an integration receives new or updated achievements (e.g.
 * after an unlock or when the full list is first fetched).
 */
typedef void (*on_monitoring_achievements_changed_t)(void);

/**
 * @brief Callback invoked when the session is fully ready.
 *
 * "Ready" means the current game's achievements have been fetched and all
 * achievement icons have been prefetched to the local cache.  This is the
 * appropriate moment to start the achievement display cycle.
 */
typedef void (*on_monitoring_session_ready_t)(void);

/**
 * @brief Start all integration monitors.
 *
 * Starts the Xbox Live RTA monitor and the RetroAchievements WebSocket
 * monitor. Safe to call even if a monitor is already running (each
 * individual monitor is idempotent on double-start).
 */
void monitoring_start(void);

/**
 * @brief Stop all integration monitors.
 *
 * Stops the RetroAchievements WebSocket monitor and the Xbox Live RTA
 * monitor. Safe to call when monitors are not running.
 */
void monitoring_stop(void);

/**
 * @brief Subscribe to connection-state change events from any monitor.
 *
 * The same callback is registered with both the Xbox and RetroAchievements
 * monitors. Passing NULL unsubscribes from both.
 *
 * @param callback Function to invoke on any connection change, or NULL to
 *                 unsubscribe.
 */
void monitoring_subscribe_connection_changed(on_monitoring_connection_changed_t callback);

/**
 * @brief Subscribe to active identity change events.
 *
 * The callback is fired whenever either integration receives a new-game-playing
 * notification, carrying the identity associated with that source.
 * Passing NULL unsubscribes.
 *
 * @param callback Function to invoke when the active identity changes, or NULL
 *                 to unsubscribe.
 */
void monitoring_subscribe_active_identity(on_monitoring_active_identity_changed_t callback);

/**
 * @brief Subscribe to game-played events from any integration.
 *
 * The callback is fired whenever either integration detects a new game.
 * Passing NULL unsubscribes.
 *
 * @param callback Function to invoke when the current game changes, or NULL
 *                 to unsubscribe.
 */
void monitoring_subscribe_game_played(on_monitoring_game_played_t callback);

/**
 * @brief Subscribe to achievements-changed events from any integration.
 *
 * The callback is fired whenever the cached achievements list is updated
 * (new game, unlock, progress). Passing NULL unsubscribes.
 *
 * @param callback Function to invoke when achievements change, or NULL
 *                 to unsubscribe.
 */
void monitoring_subscribe_achievements_changed(on_monitoring_achievements_changed_t callback);

/**
 * @brief Subscribe to session-ready events from any integration.
 *
 * The callback is fired once per game change after all achievement icons have
 * been prefetched. Passing NULL unsubscribes.
 *
 * @param callback Function to invoke when the session is ready, or NULL
 *                 to unsubscribe.
 */
void monitoring_subscribe_session_ready(on_monitoring_session_ready_t callback);

/**
 * @brief Get the currently active identity, if any.
 *
 * Returns the same identity that would be delivered to active-identity
 * subscribers right now. Useful for sources that need to seed their initial
 * state at creation time, after the monitor has already connected.
 *
 * Ownership/lifetime: the returned pointer is owned by the monitoring service
 * and may be replaced on the next identity update. Do not free it.
 *
 * @return The active identity, or NULL if no session is established.
 */
const identity_t *monitoring_get_current_active_identity(void);

/**
 * @brief Get the cached generic achievements list for the current game.
 *
 * Returns the achievements converted to generic @ref achievement_t form,
 * regardless of which integration provided them.
 *
 * Ownership/lifetime: the returned pointer is owned by the monitoring service
 * and may be replaced on the next update. Copy if you need to keep it.
 *
 * @return Head of the generic achievements linked list, or NULL if unavailable.
 */
const achievement_t *monitoring_get_current_game_achievements(void);

#ifdef __cplusplus
}
#endif
