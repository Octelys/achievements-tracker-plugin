#pragma once

#include <stdbool.h>
#include "common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Callback function type for when a message is received from the RTA endpoint
 * @param message The JSON message received
 */
typedef void (*on_xbox_rta_message_received_t)(const char *message);

/**
 * Callback function type for when a game is being played
 */
typedef void (*on_xbox_game_played_t)(const game_t *game);

typedef void (*on_xbox_achievements_progressed_t)(const gamerscore_t           *gamerscore,
                                                  const achievement_progress_t *achievements_progress);

/**
 * Callback function type for when the connection status changes
 * @param connected true if connected, false if disconnected
 * @param gamerscore
 * @param error_message Error message if disconnected due to error, NULL otherwise
 */
typedef void (*on_xbox_connection_changed_t)(bool connected, const gamerscore_t *gamerscore, const char *error_message);

const game_t *get_current_game();

const achievement_t *get_current_game_achievements();

/**
 * Start monitoring Xbox Live RTA (Real-Time Activity) endpoint
 * Uses the authorization token from the current state
 * @return true if monitoring started successfully, false otherwise
 */
bool xbox_monitoring_start();

/**
 * Stop monitoring Xbox Live RTA endpoint
 */
void xbox_monitoring_stop(void);

/**
 * Check if monitoring is currently active
 * @return true if monitoring is active, false otherwise
 */
bool xbox_monitoring_is_active(void);

void xbox_subscribe_game_played(on_xbox_game_played_t callback);

void xbox_subscribe_achievements_progressed(on_xbox_achievements_progressed_t callback);

void xbox_subscribe_connected_changed(on_xbox_connection_changed_t callback);

#ifdef __cplusplus
}
#endif
