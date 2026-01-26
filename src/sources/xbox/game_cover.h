#pragma once

#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file game_cover.h
 * @brief OBS source type that renders an Xbox game's cover art.
 *
 * This module registers an OBS source that can display cover art for the
 * currently selected/active Xbox title.
 */

/**
 * @brief Register the "Xbox Game Cover" source with OBS.
 *
 * Call once during plugin/module initialization.
 */
void xbox_game_cover_source_register(void);

#ifdef __cplusplus
}
#endif
