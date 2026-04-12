#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register the Achievement Tracker configuration entry in the OBS Tools
 *        menu and set up the navigation hotkeys.
 *
 * Registers two OBS frontend hotkeys:
 *   - "Previous Achievement" (default: Shift+Left)
 *   - "Next Achievement"     (default: Shift+Right)
 *
 * Adds an "Achievement Tracker" item to the OBS Tools menu where the user
 * can see the current hotkey bindings.
 */
void achievement_tracker_config_register(void);

/**
 * @brief Tear down the Achievement Tracker configuration UI state.
 */
void achievement_tracker_config_unregister(void);

#ifdef __cplusplus
}
#endif
