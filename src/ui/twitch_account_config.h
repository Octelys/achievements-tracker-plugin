#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register the global Twitch account configuration entry in the OBS Tools menu.
 */
void twitch_account_config_register(void);

/**
 * @brief Tear down any global Twitch account configuration UI state.
 */
void twitch_account_config_unregister(void);

#ifdef __cplusplus
}
#endif
