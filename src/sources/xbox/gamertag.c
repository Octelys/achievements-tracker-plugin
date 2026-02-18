#include "sources/xbox/gamertag.h"

/**
 * @file gamertag.c
 * @brief OBS source that displays the authenticated Xbox account's gamertag.
 *
 * Uses the text_source infrastructure for rendering and configuration management.
 * Updates automatically when connection state changes via Xbox monitor callbacks.
 */

#include "sources/common/text_source.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include "io/state.h"
#include "oauth/xbox-live.h"
#include "xbox/xbox_monitor.h"

/** Current gamertag text to display. */
static char g_gamertag[256];

/** Flag indicating text source needs to be reloaded. */
static bool g_must_reload;

/** Global configuration for gamertag display (font, color, size). */
static gamertag_configuration_t *g_configuration;

/**
 * @brief Update the gamertag display from Xbox identity.
 */
static void update_gamertag(void) {

    xbox_identity_t *identity = xbox_live_get_identity();

    if (!identity || !identity->gamertag) {
        snprintf(g_gamertag, sizeof(g_gamertag), "Not connected");
    } else {
        snprintf(g_gamertag, sizeof(g_gamertag), "%s", identity->gamertag);
    }

    g_must_reload = true;

    free_identity(&identity);
}

/**
 * @brief Xbox monitor callback for connection state changes.
 */
static void on_connection_changed(bool is_connected, const char *error_message) {

    UNUSED_PARAMETER(is_connected);
    UNUSED_PARAMETER(error_message);

    update_gamertag();
}

//  --------------------------------------------------------------------------------------------------------------------
//	Source callbacks
//  --------------------------------------------------------------------------------------------------------------------

static void *on_source_create(obs_data_t *settings, obs_source_t *source) {
    UNUSED_PARAMETER(settings);

    update_gamertag();

    return text_source_create(source, "Gamertag");
}

static void on_source_destroy(void *data) {
    text_source_t *source = data;
    if (source) {
        text_source_destroy(source);
    }
}

static uint32_t source_get_width(void *data) {
    return text_source_get_width(data);
}

static uint32_t source_get_height(void *data) {
    return text_source_get_height(data);
}

static void on_source_update(void *data, obs_data_t *settings) {
    UNUSED_PARAMETER(data);

    text_source_update_properties(settings, (text_source_config_t *)g_configuration, &g_must_reload);

    state_set_gamertag_configuration(g_configuration);
}

static void on_source_video_render(void *data, gs_effect_t *effect) {
    text_source_t *source = data;

    if (text_source_update_text(source,
                                &g_must_reload,
                                (const text_source_config_t *)g_configuration,
                                g_gamertag,
                                true)) {
        text_source_render(source, (const text_source_config_t *)g_configuration, effect);
    }
}

static void on_source_video_tick(void *data, float seconds) {
    text_source_tick(data, (const text_source_config_t *)g_configuration, seconds);
}

static obs_properties_t *source_get_properties(void *data) {
    UNUSED_PARAMETER(data);

    obs_properties_t *props = obs_properties_create();
    text_source_add_properties(props, false);
    return props;
}

static const char *source_get_name(void *unused) {
    UNUSED_PARAMETER(unused);

    return "Xbox Gamertag";
}

/** OBS source type definition for Xbox Gamertag display. */
static struct obs_source_info xbox_gamertag_source = {
    .id             = "xbox_gamertag_source",
    .type           = OBS_SOURCE_TYPE_INPUT,
    .output_flags   = OBS_SOURCE_VIDEO,
    .get_name       = source_get_name,
    .create         = on_source_create,
    .destroy        = on_source_destroy,
    .update         = on_source_update,
    .get_properties = source_get_properties,
    .get_width      = source_get_width,
    .get_height     = source_get_height,
    .video_tick     = on_source_video_tick,
    .video_render   = on_source_video_render,
};

//  --------------------------------------------------------------------------------------------------------------------
//	Public API
//  --------------------------------------------------------------------------------------------------------------------

void xbox_gamertag_source_register(void) {

    g_configuration = state_get_gamertag_configuration();
    state_set_gamertag_configuration(g_configuration);

    obs_register_source(&xbox_gamertag_source);

    xbox_subscribe_connected_changed(&on_connection_changed);
}

void xbox_gamertag_source_cleanup(void) {
    state_free_gamertag_configuration(&g_configuration);
}
