#pragma once

#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register the "Xbox Gamerpic" OBS source.
 *
 * This source renders the currently authenticated user's Xbox gamerpic (avatar).
 * The gamerpic is fetched via Xbox profile settings (GameDisplayPicRaw) and cached
 * locally as a GPU texture.
 *
 * Call this once from the plugin/module entry point during OBS module load.
 */
void xbox_gamerpic_source_register(void);

/**
 * @brief Clean up resources allocated by the gamerpic source.
 *
 * Frees the global gamerpic cache and destroys associated textures.
 * Should be called during plugin shutdown (obs_module_unload()).
 */
void xbox_gamerpic_source_cleanup(void);

#ifdef __cplusplus
}
#endif
