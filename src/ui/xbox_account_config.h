#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register the global Xbox account configuration entry in the OBS Tools menu.
 */
void xbox_account_config_register(void);

/**
 * @brief Tear down any global Xbox account configuration UI state.
 */
void xbox_account_config_unregister(void);

#ifdef __cplusplus
}
#endif
