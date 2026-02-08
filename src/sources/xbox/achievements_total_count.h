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
 * @brief Register the "Xbox Achievements Total Count" source with OBS.
 *
 * Call once during plugin/module initialization.
 */
void xbox_achievements_total_count_source_register(void);

#ifdef __cplusplus
}
#endif
