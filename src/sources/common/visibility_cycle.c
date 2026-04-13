#include "sources/common/visibility_cycle.h"

#include <math.h>
#include <util/platform.h>

static float clamp_non_negative(float value) {
    return value < 0.0f ? 0.0f : value;
}

void auto_visibility_add_properties(obs_properties_t *props) {

    if (!props) {
        return;
    }

    obs_properties_add_bool(props, AUTO_VISIBILITY_ENABLED_PROPERTY, "Auto show/hide");
    obs_properties_add_float_slider(props, AUTO_VISIBILITY_SHOW_PROPERTY, "Visible for (seconds)", 0.0, 120.0, 0.1);
    obs_properties_add_float_slider(props, AUTO_VISIBILITY_HIDE_PROPERTY, "Hidden for (seconds)", 0.0, 120.0, 0.1);
    obs_properties_add_float_slider(props, AUTO_VISIBILITY_FADE_PROPERTY, "Fade duration (seconds)", 0.0, 10.0, 0.05);
}

void auto_visibility_set_defaults(obs_data_t *settings) {

    if (!settings) {
        return;
    }

    obs_data_set_default_bool(settings, AUTO_VISIBILITY_ENABLED_PROPERTY, false);
    obs_data_set_default_double(settings, AUTO_VISIBILITY_SHOW_PROPERTY, AUTO_VISIBILITY_DEFAULT_SHOW_DURATION);
    obs_data_set_default_double(settings, AUTO_VISIBILITY_HIDE_PROPERTY, AUTO_VISIBILITY_DEFAULT_HIDE_DURATION);
    obs_data_set_default_double(settings, AUTO_VISIBILITY_FADE_PROPERTY, AUTO_VISIBILITY_DEFAULT_FADE_DURATION);
}

bool auto_visibility_update_properties(obs_data_t *settings, auto_visibility_config_t *config) {

    if (!settings || !config) {
        return false;
    }

    bool changed = false;

    if (obs_data_has_user_value(settings, AUTO_VISIBILITY_ENABLED_PROPERTY)) {
        bool enabled = obs_data_get_bool(settings, AUTO_VISIBILITY_ENABLED_PROPERTY);
        if (config->enabled != enabled) {
            config->enabled = enabled;
            changed         = true;
        }
    }

    if (obs_data_has_user_value(settings, AUTO_VISIBILITY_SHOW_PROPERTY)) {
        float show_duration = clamp_non_negative((float)obs_data_get_double(settings, AUTO_VISIBILITY_SHOW_PROPERTY));
        if (fabsf(config->show_duration - show_duration) > 0.0001f) {
            config->show_duration = show_duration;
            changed               = true;
        }
    }

    if (obs_data_has_user_value(settings, AUTO_VISIBILITY_HIDE_PROPERTY)) {
        float hide_duration = clamp_non_negative((float)obs_data_get_double(settings, AUTO_VISIBILITY_HIDE_PROPERTY));
        if (fabsf(config->hide_duration - hide_duration) > 0.0001f) {
            config->hide_duration = hide_duration;
            changed               = true;
        }
    }

    if (obs_data_has_user_value(settings, AUTO_VISIBILITY_FADE_PROPERTY)) {
        float fade_duration = clamp_non_negative((float)obs_data_get_double(settings, AUTO_VISIBILITY_FADE_PROPERTY));
        if (fabsf(config->fade_duration - fade_duration) > 0.0001f) {
            config->fade_duration = fade_duration;
            changed               = true;
        }
    }

    return changed;
}

float auto_visibility_get_opacity(const auto_visibility_config_t *config) {

    if (!config || !config->enabled) {
        return 1.0f;
    }

    const float show_duration = clamp_non_negative(config->show_duration);
    const float hide_duration = clamp_non_negative(config->hide_duration);
    const float fade_duration = clamp_non_negative(config->fade_duration);
    const float cycle         = show_duration + hide_duration + (2.0f * fade_duration);

    if (cycle <= 0.0f) {
        return 1.0f;
    }

    const double now_seconds = (double)os_gettime_ns() / 1000000000.0;
    float        phase_time  = (float)fmod(now_seconds, cycle);

    if (phase_time < show_duration) {
        return 1.0f;
    }
    phase_time -= show_duration;

    if (phase_time < fade_duration) {
        if (fade_duration <= 0.0f) {
            return 0.0f;
        }
        return 1.0f - (phase_time / fade_duration);
    }
    phase_time -= fade_duration;

    if (phase_time < hide_duration) {
        return 0.0f;
    }
    phase_time -= hide_duration;

    if (phase_time < fade_duration) {
        if (fade_duration <= 0.0f) {
            return 1.0f;
        }
        return phase_time / fade_duration;
    }

    return 1.0f;
}
