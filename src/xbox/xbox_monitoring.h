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
 * Callback function type for when the connection status changes
 * @param connected true if connected, false if disconnected
 * @param error_message Error message if disconnected due to error, NULL otherwise
 */
typedef void (*on_xbox_rta_connection_status_t)(bool connected, const char *error_message);

/**
 * Start monitoring Xbox Live RTA (Real-Time Activity) endpoint
 * Uses the authorization token from the current state
 * @param on_message Callback for received messages
 * @param on_status Callback for connection status changes
 * @return true if monitoring started successfully, false otherwise
 */
bool xbox_monitoring_start(on_xbox_rta_message_received_t on_message, on_xbox_rta_connection_status_t on_status);

/**
 * Stop monitoring Xbox Live RTA endpoint
 */
void xbox_monitoring_stop(void);

/**
 * Check if monitoring is currently active
 * @return true if monitoring is active, false otherwise
 */
bool xbox_monitoring_is_active(void);

#ifdef __cplusplus
}
#endif
