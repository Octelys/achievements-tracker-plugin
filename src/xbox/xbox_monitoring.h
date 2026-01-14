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

/**
 * Callback function type for when the connection status changes
 * @param connected true if connected, false if disconnected
 * @param error_message Error message if disconnected due to error, NULL otherwise
 */
typedef void (*on_xbox_rta_connection_status_t)(bool connected, const char *error_message);

const game_t *get_current_game();

/**
 * Start monitoring Xbox Live RTA (Real-Time Activity) endpoint
 * Uses the authorization token from the current state
 * @param on_game_played Callback for received messages
 * @param on_status Callback for connection status changes
 * @return true if monitoring started successfully, false otherwise
 */
bool xbox_monitoring_start(on_xbox_game_played_t on_game_played, on_xbox_rta_connection_status_t on_status);

/**
 * Stop monitoring Xbox Live RTA endpoint
 */
void xbox_monitoring_stop(void);

/**
 * Check if monitoring is currently active
 * @return true if monitoring is active, false otherwise
 */
bool xbox_monitoring_is_active(void);

/**
 * Start subscribing to Xbox Live RTA notifications
 * Sends the subscription message over the WebSocket connection
 * @return true if subscription request was sent, false otherwise
 */
bool xbox_subscribe();

/**
 * Stop subscribing to Xbox Live RTA notifications
 * Sends the unsubscribe message over the WebSocket connection
 * @param subscription_id The subscription ID returned from the subscribe response
 * @return true if unsubscribe request was sent, false otherwise
 */
bool xbox_unsubscribe(const char *subscription_id);

#ifdef __cplusplus
}
#endif
