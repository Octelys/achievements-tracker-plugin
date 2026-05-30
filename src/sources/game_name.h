#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file game_name.h
 * @brief OBS source type that renders the active game name.
 */

/**
 * @brief Register the "Game Name" source with OBS.
 */
void xbox_game_name_source_register(void);

/**
 * @brief Clean up resources allocated by the game name source.
 */
void xbox_game_name_source_cleanup(void);

#ifdef __cplusplus
}
#endif
