#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file gamerscore.h
 * @brief OBS source type that renders an Xbox user's gamerscore.
 *
 * This module registers an OBS source that displays the currently authenticated
 * Xbox account's gamerscore.
 */

/**
 * @brief Register the "Xbox Gamerscore" source with OBS.
 *
 * Call once during plugin/module initialization.
 */
void xbox_gamerscore_source_register(void);

#ifdef __cplusplus
}
#endif
