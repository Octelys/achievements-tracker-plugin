#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file gamerscore.h
 * @brief OBS source type that renders the active user's score.
 *
 * This module registers an OBS source that displays the score of the currently
 * authenticated account (Xbox Live gamerscore or RetroAchievements score).
 */

/**
 * @brief Register the "Gamerscore" source with OBS.
 *
 * Call once during plugin/module initialization.
 */
void xbox_gamerscore_source_register(void);

/**
 * @brief Clean up resources allocated by the gamerscore source.
 *
 * Frees the global configuration structure and its nested allocations.
 * Should be called during plugin shutdown (obs_module_unload()).
 */
void xbox_gamerscore_source_cleanup(void);

#ifdef __cplusplus
}
#endif
