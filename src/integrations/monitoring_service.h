#pragma once

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

#ifdef __cplusplus
}
#endif
