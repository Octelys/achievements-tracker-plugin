#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register the Xbox Achievement Icon source with OBS.
 *
 * This function registers an OBS source that displays the icon image of the most
 * recently unlocked Xbox achievement. The source automatically updates when new
 * achievements are unlocked by subscribing to Xbox monitor events.
 *
 * The source provides the following features:
 * - Displays the achievement icon as a texture
 * - Updates in real-time when achievements are unlocked
 * - Downloads and caches the icon image automatically
 * - Configurable display dimensions
 *
 * This function should be called once during plugin initialization (typically in
 * obs_module_load()) to make the source available in OBS.
 *
 * @note This function allocates resources and subscribes to Xbox monitor callbacks.
 * @note The icon image is downloaded to a temporary file and loaded as a GPU texture.
 *
 * @see xbox_achievement_name_source_register() for registering the achievement name source
 * @see xbox_achievement_description_source_register() for registering the achievement description source
 */
void xbox_achievement_icon_source_register(void);

#ifdef __cplusplus
}
#endif
