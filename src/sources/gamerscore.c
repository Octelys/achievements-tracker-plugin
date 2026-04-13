#include "sources/gamerscore.h"

/**
 * @file gamerscore.c
 * @brief OBS source that renders the active user's score.
 *
 * Subscribes to the monitoring service's active-identity event so it works for
 * both Xbox Live (gamerscore) and RetroAchievements (higher of hardcore vs
 * softcore score, resolved by identity_from_retro()).
 */

#include "sources/common/text_source.h"

#include <graphics/graphics.h>
#include <obs-module.h>
#include <diagnostics/log.h>

#include "io/state.h"
#include "integrations/monitoring_service.h"

#define NO_FLIP 0

static char g_gamerscore[64];
static bool g_must_reload;

static gamerscore_configuration_t *g_default_configuration;
static text_source_config_t        g_render_config;

static void update_render_config(void) {
    g_render_config.font_face             = g_default_configuration->font_face;
    g_render_config.font_style            = g_default_configuration->font_style;
    g_render_config.font_size             = g_default_configuration->font_size;
    g_render_config.active_top_color      = g_default_configuration->top_color;
    g_render_config.active_bottom_color   = g_default_configuration->bottom_color;
    g_render_config.inactive_top_color    = g_default_configuration->top_color;
    g_render_config.inactive_bottom_color = g_default_configuration->bottom_color;
    g_render_config.auto_visibility       = g_default_configuration->auto_visibility;
}

/**
 * @brief Update the score display from the active identity.
 */
static void update_gamerscore(const identity_t *identity) {

    if (!identity) {
        g_gamerscore[0] = '\0';
    } else if (identity->source == IDENTITY_SOURCE_XBOX) {
        snprintf(g_gamerscore, sizeof(g_gamerscore), "%u G", identity->score);
        obs_log(LOG_INFO, "[Gamerscore] Xbox score: %uG", identity->score);
    } else {
        snprintf(g_gamerscore, sizeof(g_gamerscore), "%u", identity->score);
        obs_log(LOG_INFO, "[Gamerscore] Retro score: %u Hardcore", identity->score);
    }

    g_must_reload = true;
}

/**
 * @brief Monitoring service callback for active identity changes.
 */
static void on_active_identity_changed(const identity_t *identity) {
    update_gamerscore(identity);
}

//  --------------------------------------------------------------------------------------------------------------------
//	Source callbacks
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief OBS callback creating a new gamerscore source instance.
 *
 * @param settings Source settings (unused).
 * @param source   OBS source instance.
 * @return Newly allocated text_source_t.
 */
static void *on_source_create(obs_data_t *settings, obs_source_t *source) {

    UNUSED_PARAMETER(settings);

    return text_source_create(source, "Gamerscore");
}

/**
 * @brief OBS callback destroying a gamerscore source instance.
 */
static void on_source_destroy(void *data) {

    text_source_t *source = data;

    if (!source) {
        return;
    }

    text_source_destroy(source);
}

/** @brief OBS callback returning the natural text width. */
static uint32_t source_get_width(void *data) {
    text_source_t *s = data;
    return text_source_get_width(s);
}

/** @brief OBS callback returning the natural text height. */
static uint32_t source_get_height(void *data) {
    text_source_t *s = data;
    return text_source_get_height(s);
}

/**
 * @brief OBS callback invoked when settings change.
 *
 * Currently unused.
 */
static void on_source_update(void *data, obs_data_t *settings) {

    UNUSED_PARAMETER(data);

    text_source_update_properties(settings, &g_render_config, &g_must_reload);

    g_default_configuration->font_face       = g_render_config.font_face;
    g_default_configuration->font_style      = g_render_config.font_style;
    g_default_configuration->font_size       = g_render_config.font_size;
    g_default_configuration->top_color       = g_render_config.active_top_color;
    g_default_configuration->bottom_color    = g_render_config.active_bottom_color;
    g_default_configuration->auto_visibility = g_render_config.auto_visibility;

    state_set_gamerscore_configuration(g_default_configuration);
}

/**
 * @brief OBS callback to render the gamerscore digits.
 *
 * Lazily initializes the font sheet texture (if needed) and draws each digit of
 * the current gamerscore using gs_draw_sprite_subregion().
 *
 * @param data   Source instance data (unused).
 * @param effect Effect to use when rendering. If NULL, OBS default effect is used.
 */
static void on_source_video_render(void *data, gs_effect_t *effect) {

    text_source_t *source = data;

    if (!source) {
        return;
    }

    if (!text_source_update_text(source,
                                 &g_must_reload,
                                 &g_render_config,
                                 g_gamerscore,
                                 true)) {
        return;
    }

    text_source_render(source, &g_render_config, effect);
}

/**
 * @brief OBS callback for animation tick.
 *
 * Updates fade transition animations.
 */
static void on_source_video_tick(void *data, float seconds) {

    text_source_t *source = data;

    if (!source) {
        return;
    }

    text_source_tick(source, &g_render_config, seconds);
}

/**
 * @brief OBS callback constructing the properties UI.
 *
 * Exposes configuration of the font sheet path and digit glyph metrics.
 */
static obs_properties_t *source_get_properties(void *data) {

    UNUSED_PARAMETER(data);

    obs_properties_t *p = obs_properties_create();
    text_source_add_properties(p, false);

    return p;
}

/** @brief OBS callback returning the display name for this source type. */
static const char *source_get_name(void *unused) {
    UNUSED_PARAMETER(unused);

    return "Gamerscore";
}

/**
 * @brief obs_source_info describing the Gamerscore source.
 */
static struct obs_source_info xbox_gamerscore_source = {
    .id             = "xbox_gamerscore_source",
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

/**
 * @brief Get the obs_source_info for registration.
 */
static const struct obs_source_info *xbox_source_get(void) {
    return &xbox_gamerscore_source;
}

//  --------------------------------------------------------------------------------------------------------------------
//	Public functions
//  --------------------------------------------------------------------------------------------------------------------

void xbox_gamerscore_source_register(void) {

    g_default_configuration = state_get_gamerscore_configuration();
    state_set_gamerscore_configuration(g_default_configuration);
    update_render_config();

    obs_register_source(xbox_source_get());

    monitoring_subscribe_active_identity(on_active_identity_changed);
}

void xbox_gamerscore_source_cleanup(void) {
    state_free_gamerscore_configuration(&g_default_configuration);
}
