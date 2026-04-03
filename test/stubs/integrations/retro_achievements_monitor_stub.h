#pragma once

/**
 * @file retro_achievements_monitor_stub.h
 * @brief Test controls for the retro_achievements_monitor stub.
 *
 * Lets tests drive monitoring_service.c's retro callbacks without a real
 * WebSocket connection to the RetroArch server.
 */

#include "integrations/retro-achievements/retro_achievements_monitor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Simulate a RetroAchievements connection-changed event.
 */
void mock_retro_monitor_fire_connection_changed(bool connected, const char *error_message);

/**
 * @brief Simulate a "user" message arriving from the RetroArch server.
 */
void mock_retro_monitor_fire_user(const retro_user_t *user);

/**
 * @brief Simulate a "no_user" message arriving from the RetroArch server.
 */
void mock_retro_monitor_fire_no_user(void);

/**
 * @brief Simulate a "game_playing" message arriving from the RetroArch server.
 */
void mock_retro_monitor_fire_game_playing(const retro_game_t *game);

/**
 * @brief Simulate a "no_game" message arriving from the RetroArch server.
 */
void mock_retro_monitor_fire_no_game(void);

/**
 * @brief Reset all stub state. Call from tearDown().
 */
void mock_retro_monitor_reset(void);

#ifdef __cplusplus
}
#endif
