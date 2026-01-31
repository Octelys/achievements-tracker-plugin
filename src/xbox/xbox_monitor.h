#pragma once

#include <stdbool.h>
#include "common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file xbox_monitor.h
 * @brief Xbox Live RTA (Real-Time Activity) monitor and event fan-out.
 *
 * This module maintains a connection to the Xbox Live RTA endpoint and converts
 * incoming JSON messages into higher-level events (currently: game played,
 * achievements progressed, connection state changes).
 *
 * Threading:
 *  - Callbacks may be invoked from the monitor's networking thread.
 *  - Callbacks must return quickly and must not perform OBS graphics operations
 *    directly (use obs_enter_graphics/obs_leave_graphics or schedule work).
 *
 * Ownership/lifetime:
 *  - Pointers passed to callbacks (e.g., game_t/gamerscore_t/progress lists) are
 *    owned by the monitor and remain valid until the next update or until
 *    monitoring stops. If you need to keep them, make a deep copy.
 */

/**
 * @brief Callback invoked for every raw JSON message received from the RTA endpoint.
 *
 * @param message NUL-terminated JSON message string.
 */
typedef void (*on_xbox_rta_message_received_t)(const char *message);

/**
 * @brief Callback invoked when a (new) game is detected as being played.
 *
 * @param game Current game information.
 */
typedef void (*on_xbox_game_played_t)(const game_t *game);

/**
 * @brief Callback invoked when achievement progress updates are received.
 *
 * @param gamerscore            Current gamerscore snapshot.
 * @param achievements_progress Linked list of achievement progress items.
 */
typedef void (*on_xbox_achievements_progressed_t)(const gamerscore_t           *gamerscore,
                                                  const achievement_progress_t *achievements_progress);

/**
 * @brief Callback invoked when the connection status changes.
 *
 * @param connected     true if connected; false if disconnected.
 * @param error_message Error message if disconnected due to an error; otherwise NULL.
 */
typedef void (*on_xbox_connection_changed_t)(bool connected, const char *error_message);

/**
 * @brief Get the most recently cached gamerscore snapshot.
 *
 * Ownership/lifetime: the returned pointer is owned by the monitor and may be
 * replaced on the next incoming update. Copy it if you need to keep it.
 *
 * Threading: safe to call from any thread but the returned object may change
 * asynchronously while monitoring is running.
 *
 * @return Pointer to the current gamerscore snapshot, or NULL if unknown.
 */
const gamerscore_t *get_current_gamerscore(void);

/**
 * @brief Get the most recently detected currently played game.
 *
 * Ownership/lifetime: the returned pointer is owned by the monitor and remains
 * valid until the next update or until monitoring stops.
 *
 * @return Pointer to the current game, or NULL if none is known.
 */
const game_t *get_current_game(void);

/**
 * @brief Get the most recently cached achievements list for the current game.
 *
 * Ownership/lifetime: the returned pointer is owned by the monitor and remains
 * valid until the next update or until monitoring stops.
 *
 * @return Pointer to the cached achievements list, or NULL if not available.
 */
const achievement_t *get_current_game_achievements(void);

/**
 * @brief Start monitoring the Xbox Live RTA endpoint.
 *
 * Uses authorization data from the current persisted state.
 *
 * @return true if monitoring started successfully; false otherwise.
 */
bool xbox_monitoring_start(void);

/**
 * @brief Stop monitoring the Xbox Live RTA endpoint.
 */
void xbox_monitoring_stop(void);

/**
 * @brief Check if monitoring is currently active.
 *
 * @return true if monitoring is active; false otherwise.
 */
bool xbox_monitoring_is_active(void);

/**
 * @brief Subscribe to game-played events.
 *
 * Passing NULL clears/unsubscribes the callback.
 *
 * @param callback Callback invoked when the current game changes.
 */
void xbox_subscribe_game_played(on_xbox_game_played_t callback);

/**
 * @brief Subscribe to achievement progress events.
 *
 * Passing NULL clears/unsubscribes the callback.
 *
 * @param callback Callback invoked when achievement progress is received.
 */
void xbox_subscribe_achievements_progressed(on_xbox_achievements_progressed_t callback);

/**
 * @brief Subscribe to connection state change events.
 *
 * Passing NULL clears/unsubscribes the callback.
 *
 * @param callback Callback invoked when connectivity changes.
 */
void xbox_subscribe_connected_changed(on_xbox_connection_changed_t callback);

#ifdef __cplusplus
}
#endif
