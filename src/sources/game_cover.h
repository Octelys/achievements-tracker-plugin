#pragma once

#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file game_cover.h
 * @brief OBS source type that renders the cover art for the currently played game.
 *
 * This module registers an OBS source that displays cover art for the active
 * game. It subscribes to the monitoring service and works with any integration
 * (Xbox, RetroAchievements, etc.) through the common @c game_t type.
 */

/**
 * @brief Register the "Game Cover" source with OBS.
 *
 * Call once during plugin/module initialization.
 */
void game_cover_source_register(void);

/**
 * @brief Clean up resources allocated by the game cover source.
 *
 * Frees the global cover image cache and destroys associated textures.
 * Should be called during plugin shutdown (obs_module_unload()).
 */
void game_cover_source_cleanup(void);

#ifdef __cplusplus
}
#endif
