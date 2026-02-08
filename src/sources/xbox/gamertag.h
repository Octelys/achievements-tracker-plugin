#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file gamertag.h
 * @brief OBS source type that renders an Xbox user's gamertag.
 *
 * This module registers an OBS source that displays the currently authenticated
 * Xbox account's gamertag.
 */

/**
 * @brief Register the "Xbox Gamertag" source with OBS.
 *
 * Call once during plugin/module initialization.
 */
void xbox_gamertag_source_register(void);

#ifdef __cplusplus
}
#endif
