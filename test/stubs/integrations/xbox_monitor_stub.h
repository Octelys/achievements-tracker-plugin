#pragma once

/**
 * @file xbox_monitor_stub.h
 * @brief Test controls for the xbox_monitor / xbox_client stubs.
 *
 * Call these from setUp / individual tests to drive the monitoring_service
 * callbacks without a real WebSocket connection or HTTP client.
 */

#include "common/game.h"
#include "integrations/xbox/xbox_monitor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set the xbox_identity that state_get_xbox_identity() will return.
 *
 * The stub takes ownership of the provided identity; pass NULL to make the
 * stub return NULL (simulates "not logged in").
 */
void mock_xbox_monitor_set_identity(xbox_identity_t *identity);

/**
 * @brief Simulate an Xbox connection-changed event.
 *
 * Invokes the callback registered via xbox_subscribe_connected_changed().
 */
void mock_xbox_monitor_fire_connection_changed(bool connected, const char *error_message);

/**
 * @brief Simulate an Xbox game-played event.
 *
 * Invokes the callback registered via xbox_subscribe_game_played().
 */
void mock_xbox_monitor_fire_game_played(const game_t *game);

/**
 * @brief Reset all stub state (callbacks, stored identity, etc.).
 *
 * Call from tearDown() to prevent state leaking between tests.
 */
void mock_xbox_monitor_reset(void);

#ifdef __cplusplus
}
#endif
