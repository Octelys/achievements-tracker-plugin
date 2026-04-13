#include "sources/achievements_count.h"

/**
 * @file achievements_total_count.c
 * @brief OBS source that renders the total number of achievements for the current game.
 *
 * This source displays the total count of achievements available for the currently
 * played game. The count is updated when the game changes or achievements progress.
 *
 * Data flow:
 *  - The monitoring service notifies this module when the connection state changes,
 *    when the game changes, or when achievements are updated.
 *  - The module counts total achievements and stores the result in a global.
 *  - During rendering, the count is formatted to text and rendered.
 *
 * Threading notes:
 *  - Event handlers may be invoked from non-graphics threads.
 *  - Texture creation must happen on the OBS graphics thread; this file lazily
 *    initializes the texture in the video_render callback.
 */

#include "sources/common/text_source.h"

#include <graphics/graphics.h>
#include <obs-module.h>
#include <diagnostics/log.h>

#include "common/achievement.h"
#include "io/state.h"
#include "integrations/monitoring_service.h"

#define NO_FLIP 0

static char g_total_count[64];
static bool g_must_reload;

/**
 * @brief Configuration for the total count display.
 *
 * Stored as a module-global pointer and initialized during
 * xbox_achievements_total_count_source_register().
 */
static achievements_count_configuration_t *g_configuration;
static text_source_config_t               g_render_config;

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
 * @brief Recompute and store the total achievements count.
 */
static void update_count(void) {
    const achievement_t *achievements = monitoring_get_current_game_achievements();

    int unlocked = count_unlocked_achievements(achievements);
    int total    = count_achievements(achievements);

    if (unlocked != total) {
        snprintf(g_total_count, sizeof(g_total_count), "%d / %d", unlocked, total);
    } else if (total > 0) {
        snprintf(g_total_count, sizeof(g_total_count), "Mastered");
    } else {
        g_total_count[0] = '\0';
    }

    g_must_reload = true;

    obs_log(LOG_INFO, "[Achievements Counter] %d achievements unlocked out of %d", unlocked, total);
}

/**
 * @brief Monitoring service callback invoked when a new game is played.
 *
 * @param game Current game information.
 */
static void on_game_played(const game_t *game) {

    if (game) {
        update_count();
    } else {
        g_total_count[0] = '\0';
        g_must_reload    = true;
    }
}

/**
 * @brief Monitoring service callback invoked when achievements change.
 */
static void on_achievements_changed(void) {
    update_count();
}

//  --------------------------------------------------------------------------------------------------------------------
//	Source callbacks
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief OBS callback creating a new source instance.
 *
 * @param settings Source settings (unused).
 * @param source   OBS source instance.
 * @return Newly allocated text_source_base_t.
 */
static void *on_source_create(obs_data_t *settings, obs_source_t *source) {
    UNUSED_PARAMETER(settings);

    return text_source_create(source, "Achievement total count");
}

/**
 * @brief OBS callback destroying a source instance.
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
 */
static void on_source_update(void *data, obs_data_t *settings) {
    UNUSED_PARAMETER(data);

    text_source_update_properties(settings, &g_render_config, &g_must_reload);

    g_configuration->font_face       = g_render_config.font_face;
    g_configuration->font_style      = g_render_config.font_style;
    g_configuration->font_size       = g_render_config.font_size;
    g_configuration->top_color       = g_render_config.active_top_color;
    g_configuration->bottom_color    = g_render_config.active_bottom_color;
    g_configuration->auto_visibility = g_render_config.auto_visibility;

    state_set_achievements_count_configuration(g_configuration);
}

/**
 * @brief OBS callback to render the total count.
 *
 * @param data   Source instance data.
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
                                 g_total_count,
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

    text_source_tick(data, &g_render_config, seconds);
}

/**
 * @brief OBS callback constructing the properties UI.
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

    return "Achievements Count";
}

/**
 * @brief obs_source_info describing the Achievements Total Count source.
 */
static struct obs_source_info xbox_achievements_count_source = {
    .id             = "xbox_achievements_count_source",
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
    return &xbox_achievements_count_source;
}

//  --------------------------------------------------------------------------------------------------------------------
//	Public functions
//  --------------------------------------------------------------------------------------------------------------------

void xbox_achievements_count_source_register(void) {

    g_configuration = state_get_achievements_count_configuration();
    state_set_achievements_count_configuration(g_configuration);
    update_render_config();

    obs_register_source(xbox_source_get());

    monitoring_subscribe_achievements_changed(&on_achievements_changed);
    monitoring_subscribe_game_played(&on_game_played);
}

void xbox_achievements_count_source_cleanup(void) {
    state_free_achievements_count_configuration(&g_configuration);
}
