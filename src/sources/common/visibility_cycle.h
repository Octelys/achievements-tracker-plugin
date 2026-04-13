#pragma once

#include <obs-module.h>

#include "common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUTO_VISIBILITY_ENABLED_PROPERTY "auto_visibility_enabled"

/* The show / hide / fade duration sliders are only shown in the global
 * Achievement Tracker dialog, not in per-source property panels.
 * These macro names are kept for use by the global dialog and state helpers. */
#define AUTO_VISIBILITY_SHOW_PROPERTY "auto_visibility_show_duration"
#define AUTO_VISIBILITY_HIDE_PROPERTY "auto_visibility_hide_duration"
#define AUTO_VISIBILITY_FADE_PROPERTY "auto_visibility_fade_duration"

/**
 * @brief Add only the "Auto show/hide" enabled toggle to a per-source properties panel.
 *
 * The show / hide / fade duration sliders are configured globally via the
 * Achievement Tracker dialog and are therefore NOT added here.
 *
 * @param props Properties panel to add the toggle to.
 */
void auto_visibility_add_toggle_property(obs_properties_t *props);

/**
 * @brief Set per-source defaults (enabled = false only; durations come from global state).
 */
void auto_visibility_set_defaults(obs_data_t *settings);

/**
 * @brief Update a config from OBS settings, reading only the enabled flag.
 *
 * The duration fields of @p config must be filled from the global durations
 * (via auto_visibility_apply_durations()) before this is called; this function
 * only touches the @c enabled field.
 *
 * @return @c true if the enabled flag changed.
 */
bool auto_visibility_update_toggle(obs_data_t *settings, auto_visibility_config_t *config);

/**
 * @brief Apply the global shared durations into a per-source config.
 *
 * Call this whenever the global durations change (e.g. after the user saves
 * the Achievement Tracker dialog) to propagate the new values to every source.
 *
 * @param config    Per-source config to update.
 * @param durations Global duration values to apply.
 */
void auto_visibility_apply_durations(auto_visibility_config_t *config, const auto_visibility_durations_t *durations);

/**
 * @brief Update the module-level shared durations and apply them to all
 *        registered source configs.
 *
 * Stores the durations as the module-global defaults and immediately pushes
 * them to every config previously registered via auto_visibility_register_config().
 *
 * @param durations New duration values.
 */
void auto_visibility_set_shared_durations(const auto_visibility_durations_t *durations);

/**
 * @brief Return the current module-level shared durations.
 *
 * The returned pointer is valid until the next call to
 * auto_visibility_set_shared_durations() or until plugin unload.
 * Do NOT free it.
 */
const auto_visibility_durations_t *auto_visibility_get_shared_durations(void);

/**
 * @brief Register a per-source config pointer so it automatically receives
 *        future shared-duration updates.
 *
 * Call once from each source's _register() function.
 *
 * @param config Per-source config to register.
 */
void auto_visibility_register_config(auto_visibility_config_t *config);

float auto_visibility_get_opacity(const auto_visibility_config_t *config);

#ifdef __cplusplus
}
#endif
