#pragma once

#include <obs-module.h>

#include "common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUTO_VISIBILITY_ENABLED_PROPERTY "auto_visibility_enabled"
#define AUTO_VISIBILITY_SHOW_PROPERTY "auto_visibility_show_duration"
#define AUTO_VISIBILITY_HIDE_PROPERTY "auto_visibility_hide_duration"
#define AUTO_VISIBILITY_FADE_PROPERTY "auto_visibility_fade_duration"

void auto_visibility_add_properties(obs_properties_t *props);
void auto_visibility_set_defaults(obs_data_t *settings);
bool auto_visibility_update_properties(obs_data_t *settings, auto_visibility_config_t *config);
float auto_visibility_get_opacity(const auto_visibility_config_t *config);

#ifdef __cplusplus
}
#endif
