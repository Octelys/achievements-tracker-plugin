#include "sources/game_name.h"

#include "sources/common/text_source.h"
#include "sources/common/visibility_cycle.h"

#include <obs-module.h>

#include "common/game.h"
#include "integrations/monitoring_service.h"
#include "io/state.h"

static char g_game_name[512];
static bool g_must_reload;

static game_name_configuration_t *g_configuration;
static text_source_config_t       g_render_config;

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

static void update_game_name(const game_t *game) {
    if (!game || !game->title || game->title[0] == '\0') {
        if (g_game_name[0] != '\0') {
            g_game_name[0] = '\0';
            g_must_reload  = true;
        }
        return;
    }

    snprintf(g_game_name, sizeof(g_game_name), "%s", game->title);
    g_must_reload = true;
}

static void on_game_played(const game_t *game) {
    update_game_name(game);
}

static void *on_source_create(obs_data_t *settings, obs_source_t *source) {
    UNUSED_PARAMETER(settings);

    update_game_name(monitoring_get_current_active_game());
    return text_source_create(source, "Game Name");
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

    state_set_game_name_configuration(g_configuration);
}

static void on_source_video_render(void *data, gs_effect_t *effect) {
    text_source_t *source = data;

    if (text_source_update_text(source, &g_must_reload, &g_render_config, g_game_name, true)) {
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

    return "Game Name";
}

static struct obs_source_info xbox_game_name_source = {
    .id             = "xbox_game_name_source",
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

void xbox_game_name_source_register(void) {

    g_configuration = state_get_game_name_configuration();
    state_set_game_name_configuration(g_configuration);
    update_render_config();

    obs_register_source(&xbox_game_name_source);

    auto_visibility_register_config(&g_render_config.auto_visibility);

    monitoring_subscribe_game_played(on_game_played);
}

void xbox_game_name_source_cleanup(void) {
    state_free_game_name_configuration(&g_configuration);
}
