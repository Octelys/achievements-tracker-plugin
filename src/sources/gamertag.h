#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file gamertag.h
 * @brief OBS source type that renders the active user's gamertag / display name.
 *
 * This module registers an OBS source that displays the gamertag of the
 * currently authenticated account (Xbox Live or RetroAchievements).
 */

/**
 * @brief Register the "Gamertag" source with OBS.
 *
 * Call once during plugin/module initialization.
 */
void xbox_gamertag_source_register(void);

/**
 * @brief Clean up resources allocated by the gamertag source.
 *
 * Frees the global configuration structure and its nested allocations.
 * Should be called during plugin shutdown (obs_module_unload()).
 */
void xbox_gamertag_source_cleanup(void);

#ifdef __cplusplus
}
#endif
