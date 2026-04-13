#include "sources/common/visibility_cycle.h"

#include <math.h>
#include <util/platform.h>

/** Nanoseconds-to-seconds conversion factor for os_gettime_ns(). */
#define NS_TO_SECONDS 1000000000.0

/* --------------------------------------------------------------------------
 * Module-level shared durations + registration list
 * -------------------------------------------------------------------------- */

#define MAX_REGISTERED_CONFIGS 16

static auto_visibility_durations_t g_shared_durations = {
    .show_duration = AUTO_VISIBILITY_DEFAULT_SHARED_SHOW_DURATION,
    .hide_duration = AUTO_VISIBILITY_DEFAULT_SHARED_HIDE_DURATION,
    .fade_duration = AUTO_VISIBILITY_DEFAULT_SHARED_FADE_DURATION,
};

static auto_visibility_config_t *g_registered_configs[MAX_REGISTERED_CONFIGS];
static int                       g_registered_count = 0;

/* -------------------------------------------------------------------------- */

static float clamp_non_negative(float value) {
    return value < 0.0f ? 0.0f : value;
}

static float fade_progress(float phase_time, float fade_duration, float start_opacity, float end_opacity) {

    if (fade_duration <= 0.0f) {
        return end_opacity;
    }

    const float t = phase_time / fade_duration;
    return start_opacity + ((end_opacity - start_opacity) * t);
}

/* --------------------------------------------------------------------------
 * Per-source properties helpers
 * -------------------------------------------------------------------------- */

void auto_visibility_add_toggle_property(obs_properties_t *props) {

    if (!props) {
        return;
    }

    obs_properties_add_bool(props, AUTO_VISIBILITY_ENABLED_PROPERTY, "Auto show/hide");
}

void auto_visibility_set_defaults(obs_data_t *settings) {

    if (!settings) {
        return;
    }

    obs_data_set_default_bool(settings, AUTO_VISIBILITY_ENABLED_PROPERTY, false);
}

bool auto_visibility_update_toggle(obs_data_t *settings, auto_visibility_config_t *config) {

    if (!settings || !config) {
        return false;
    }

    if (!obs_data_has_user_value(settings, AUTO_VISIBILITY_ENABLED_PROPERTY)) {
        return false;
    }

    bool enabled = obs_data_get_bool(settings, AUTO_VISIBILITY_ENABLED_PROPERTY);
    if (config->enabled != enabled) {
        config->enabled = enabled;
        return true;
    }

    return false;
}

/* --------------------------------------------------------------------------
 * Shared durations
 * -------------------------------------------------------------------------- */

void auto_visibility_apply_durations(auto_visibility_config_t *config, const auto_visibility_durations_t *durations) {

    if (!config || !durations) {
        return;
    }

    config->show_duration = durations->show_duration > 0.0f ? durations->show_duration
                                                            : AUTO_VISIBILITY_DEFAULT_SHARED_SHOW_DURATION;
    config->hide_duration = durations->hide_duration > 0.0f ? durations->hide_duration
                                                            : AUTO_VISIBILITY_DEFAULT_SHARED_HIDE_DURATION;
    config->fade_duration = durations->fade_duration > 0.0f ? durations->fade_duration
                                                            : AUTO_VISIBILITY_DEFAULT_SHARED_FADE_DURATION;
}

void auto_visibility_set_shared_durations(const auto_visibility_durations_t *durations) {

    if (!durations) {
        return;
    }

    g_shared_durations = *durations;

    /* Push to every registered per-source config */
    for (int i = 0; i < g_registered_count; i++) {
        if (g_registered_configs[i]) {
            auto_visibility_apply_durations(g_registered_configs[i], &g_shared_durations);
        }
    }
}

const auto_visibility_durations_t *auto_visibility_get_shared_durations(void) {
    return &g_shared_durations;
}

void auto_visibility_register_config(auto_visibility_config_t *config) {

    if (!config || g_registered_count >= MAX_REGISTERED_CONFIGS) {
        return;
    }

    /* Avoid double-registration */
    for (int i = 0; i < g_registered_count; i++) {
        if (g_registered_configs[i] == config) {
            return;
        }
    }

    g_registered_configs[g_registered_count++] = config;

    /* Apply current shared durations immediately */
    auto_visibility_apply_durations(config, &g_shared_durations);
}

/* --------------------------------------------------------------------------
 * Opacity calculation
 * -------------------------------------------------------------------------- */

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

    const double now_seconds = (double)os_gettime_ns() / NS_TO_SECONDS;
    float        phase_time  = (float)fmod(now_seconds, cycle);

    if (phase_time < show_duration) {
        return 1.0f;
    }
    phase_time -= show_duration;

    if (phase_time < fade_duration) {
        return fade_progress(phase_time, fade_duration, 1.0f, 0.0f);
    }
    phase_time -= fade_duration;

    if (phase_time < hide_duration) {
        return 0.0f;
    }
    phase_time -= hide_duration;

    if (phase_time < fade_duration) {
        return fade_progress(phase_time, fade_duration, 0.0f, 1.0f);
    }

    return 1.0f;
}
