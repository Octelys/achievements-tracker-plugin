#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file achievements_unlocked_count.h
 * @brief OBS source type that renders the number of unlocked achievements for the current game.
 *
 * This module registers an OBS source that displays how many achievements have been
 * unlocked for the currently played Xbox game.
 */

/**
 * @brief Register the "Xbox Achievements Unlocked Count" source with OBS.
 *
 * Call once during plugin/module initialization.
 */
void xbox_achievements_unlocked_count_source_register(void);

#ifdef __cplusplus
}
#endif
