#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file achievements_total_count.h
 * @brief OBS source type that renders the total number of achievements for the current game.
 *
 * This module registers an OBS source that displays the total number of achievements
 * available for the currently played Xbox game.
 */

/**
 * @brief Register the "Xbox Achievements Count" source with OBS.
 *
 * Call once during plugin/module initialization.
 */
void xbox_achievements_count_source_register(void);

/**
 * @brief Clean up resources allocated by the achievements count source.
 *
 * Frees the global configuration structure and its nested allocations.
 * Should be called during plugin shutdown (obs_module_unload()).
 */
void xbox_achievements_count_source_cleanup(void);

#ifdef __cplusplus
}
#endif
