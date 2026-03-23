#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register the Xbox Achievement Name source with OBS.
 *
 * This function registers an OBS source that displays the name and gamerscore value
 * of the most recently unlocked Xbox achievement. The source automatically updates
 * when new achievements are unlocked by subscribing to Xbox monitor events.
 *
 * The source provides the following features:
 * - Displays achievement name and gamerscore value (e.g., "50G - Master Explorer")
 * - Updates in real-time when achievements are unlocked
 * - Configurable font, text size, and color through OBS properties
 * - Automatically sorts to show the most recent achievement
 *
 * This function should be called once during plugin initialization (typically in
 * obs_module_load()) to make the source available in OBS.
 *
 * @note This function allocates resources and subscribes to Xbox monitor callbacks.
 *       The source configuration is persisted to disk via the state management system.
 *
 * @see xbox_gamerscore_source_register() for registering the gamerscore display source
 * @see state_get_achievement_name_configuration() for retrieving saved configuration
 */
void xbox_achievement_name_source_register(void);

/**
 * @brief Clean up resources allocated by the achievement name source.
 *
 * Frees the global configuration structure and its nested allocations.
 * Should be called during plugin shutdown (obs_module_unload()).
 */
void xbox_achievement_name_source_cleanup(void);

#ifdef __cplusplus
}
#endif
