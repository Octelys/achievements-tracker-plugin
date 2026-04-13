#include "sources/gamertag.h"

/**
 * @file gamertag.c
 * @brief OBS source that displays the authenticated user's gamertag / display name.
 *
 * Subscribes to the monitoring service's active-identity event so it works for
 * both Xbox Live and RetroAchievements sessions.
 */

#include "sources/common/text_source.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include "io/state.h"
#include "integrations/monitoring_service.h"

/** Current gamertag text to display. */
static char g_gamertag[256];

/** Flag indicating the text source needs to be reloaded. */
static bool g_must_reload;

/** Global configuration for gamertag display (font, color, size). */
static gamertag_configuration_t *g_configuration;
static text_source_config_t      g_render_config;

static void update_render_config(void) {
    g_render_config.font_face             = g_configuration->font_face;
    g_render_config.font_style            = g_configuration->font_style;
    g_render_config.font_size             = g_configuration->font_size;
    g_render_config.active_top_color      = g_configuration->top_color;
    g_render_config.active_bottom_color   = g_configuration->bottom_color;
    g_render_config.inactive_top_color    = g_configuration->top_color;
    g_render_config.inactive_bottom_color = g_configuration->bottom_color;
    g_render_config.auto_visibility       = g_configuration->auto_visibility;
}

/**
 * @brief Update the gamertag display from the active identity.
 *
 * When an identity becomes available the source fades in with the new name.
 * When the identity is lost the source fades out to blank, preserving the
 * previous name as the "current" text so the text_source transition system
 * can fade it out gracefully before replacing it with an empty string.
 */
static void update_gamertag(const identity_t *identity) {

    if (!identity || !identity->name || identity->name[0] == '\0') {
        /* Lost identity: only trigger a reload (fade to blank) if something
         * was previously displayed. */
        if (g_gamertag[0] != '\0') {
            g_gamertag[0] = '\0';
            g_must_reload = true;
        }
    } else {
        snprintf(g_gamertag, sizeof(g_gamertag), "%s", identity->name);
        g_must_reload = true;
    }
}

/**
 * @brief Monitoring service callback for active identity changes.
 */
static void on_active_identity_changed(const identity_t *identity) {
    update_gamertag(identity);
}

//  --------------------------------------------------------------------------------------------------------------------
//	Source callbacks
//  --------------------------------------------------------------------------------------------------------------------

static void *on_source_create(obs_data_t *settings, obs_source_t *source) {
    UNUSED_PARAMETER(settings);

    /* Populate immediately with the current identity, so the source does not
     * briefly show "Not connected" when added to a scene after the monitor
     * has already connected.  The subscription callback won't fire again for
     * an already-established identity. */
    update_gamertag(monitoring_get_current_active_identity());

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

    text_source_update_properties(settings, &g_render_config, &g_must_reload);

    g_configuration->font_face       = g_render_config.font_face;
    g_configuration->font_style      = g_render_config.font_style;
    g_configuration->font_size       = g_render_config.font_size;
    g_configuration->top_color       = g_render_config.active_top_color;
    g_configuration->bottom_color    = g_render_config.active_bottom_color;
    g_configuration->auto_visibility = g_render_config.auto_visibility;

    state_set_gamertag_configuration(g_configuration);
}

static void on_source_video_render(void *data, gs_effect_t *effect) {
    text_source_t *source = data;

    if (text_source_update_text(source,
                                &g_must_reload,
                                &g_render_config,
                                g_gamertag,
                                true)) {
        text_source_render(source, &g_render_config, effect);
    }
}

static void on_source_video_tick(void *data, float seconds) {
    text_source_tick(data, &g_render_config, seconds);
}

static obs_properties_t *source_get_properties(void *data) {
    UNUSED_PARAMETER(data);

    obs_properties_t *props = obs_properties_create();
    text_source_add_properties(props, false);
    return props;
}

static const char *source_get_name(void *unused) {
    UNUSED_PARAMETER(unused);

    return "Gamertag";
}

/** OBS source type definition for the Gamertag display. */
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
    update_render_config();

    obs_register_source(&xbox_gamertag_source);

    monitoring_subscribe_active_identity(on_active_identity_changed);
}

void xbox_gamertag_source_cleanup(void) {
    state_free_gamertag_configuration(&g_configuration);
}
