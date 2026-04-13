#include "sources/game_cover.h"

/**
 * @file game_cover.c
 * @brief OBS source that renders the cover art for the currently played game.
 *
 * Responsibilities:
 *  - Subscribe to game-played events via the monitoring service.
 *  - Download cover art when the game changes (using the cover_url from game_t).
 *  - Load the image into an OBS gs_texture_t on the graphics thread.
 *  - Render the texture in the source's video_render callback.
 *
 * Threading notes:
 *  - Downloading happens on the calling thread of on_game_played() (currently
 *    synchronous).
 *  - Texture creation/destruction must happen on the OBS graphics thread; this
 *    file uses obs_enter_graphics()/obs_leave_graphics() to ensure that.
 */

#include <obs-module.h>
#include <diagnostics/log.h>

#include "sources/common/image_source.h"
#include "sources/common/visibility_cycle.h"
#include "integrations/monitoring_service.h"
#include "common/game.h"

/**
 * @brief Global singleton cover cache.
 *
 * This source is implemented as a singleton that stores the current cover art in
 * a global cache.
 */
static image_t                  g_game_cover;
static auto_visibility_config_t g_auto_visibility = {
    .enabled       = false,
    .show_duration = AUTO_VISIBILITY_DEFAULT_SHARED_SHOW_DURATION,
    .hide_duration = AUTO_VISIBILITY_DEFAULT_SHARED_HIDE_DURATION,
    .fade_duration = AUTO_VISIBILITY_DEFAULT_SHARED_FADE_DURATION,
};

//  --------------------------------------------------------------------------------------------------------------------
//	Event handlers
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Event handler called when a new game starts being played.
 *
 * Uses the cover_url from the game_t to download the cover art.
 *
 * @param game Currently played game information.
 */
static void on_game_played(const game_t *game) {

    if (!game) {
        obs_log(LOG_INFO, "[Game Cover] No game played");
        image_source_clear(&g_game_cover);
        return;
    }

    obs_log(LOG_INFO, "[Game Cover] Playing game %s (%s)", game->title, game->id);

    if (!game->cover_url || game->cover_url[0] == '\0') {
        obs_log(LOG_INFO, "[Game Cover] No cover URL available");
        image_source_clear(&g_game_cover);
        return;
    }

    obs_log(LOG_INFO, "[Game Cover] Cover URL is %s", game->cover_url);

    snprintf(g_game_cover.url, sizeof(g_game_cover.url), "%s", game->cover_url);
    snprintf(g_game_cover.id, sizeof(g_game_cover.id), "%s", game->id);

    image_source_download(&g_game_cover);
}

//  --------------------------------------------------------------------------------------------------------------------
//	Source callbacks
//  --------------------------------------------------------------------------------------------------------------------

/** @brief OBS callback returning the source width. */
static uint32_t source_get_width(void *data) {
    const image_source_t *s = data;
    return s->size.width;
}

/** @brief OBS callback returning the source height. */
static uint32_t source_get_height(void *data) {
    const image_source_t *s = data;
    return s->size.height;
}

/** @brief OBS callback returning the display name for the source type. */
static const char *source_get_name(void *unused) {

    UNUSED_PARAMETER(unused);

    return "Game Cover";
}

/**
 * @brief OBS callback creating a new source instance.
 *
 * @param settings OBS settings object (currently unused).
 * @param source   OBS source instance.
 * @return Newly allocated image_source_t.
 */
static void *on_source_create(obs_data_t *settings, obs_source_t *source) {

    UNUSED_PARAMETER(settings);

    image_source_t *s = bzalloc(sizeof(*s));
    s->source         = source;
    s->size.width     = 800;
    s->size.height    = 200;

    return s;
}

/**
 * @brief OBS callback destroying a source instance.
 *
 * Frees the per-instance data. Note: Global cover image (g_game_cover)
 * is cleaned up during plugin unload, not per-source-instance.
 */
static void on_source_destroy(void *data) {

    image_source_t *source = data;

    if (!source) {
        return;
    }

    bfree(source);
}

/**
 * @brief OBS callback invoked when source settings change.
 *
 * Currently unused (no editable settings).
 */
static void on_source_update(void *data, obs_data_t *settings) {

    UNUSED_PARAMETER(data);

    auto_visibility_update_toggle(settings, &g_auto_visibility);
}

static void source_get_defaults(obs_data_t *settings) {
    auto_visibility_set_defaults(settings);
}

/**
 * @brief OBS callback to render the source.
 *
 * Loads a new texture if required and draws it using draw_texture().
 */
static void on_source_video_render(void *data, gs_effect_t *effect) {

    image_source_t *source = data;

    if (!source) {
        return;
    }

    /* Load image if needed (deferred load in graphics context) */
    image_source_reload_if_needed(&g_game_cover);

    const float opacity = auto_visibility_get_opacity(&g_auto_visibility);
    image_source_render_active_with_opacity(&g_game_cover, source->size, effect, opacity);
}

static obs_properties_t *source_get_properties(void *data) {

    UNUSED_PARAMETER(data);

    obs_properties_t *p = obs_properties_create();
    auto_visibility_add_toggle_property(p);
    return p;
}

/**
 * @brief obs_source_info for the Game Cover source.
 */
static struct obs_source_info game_cover_source_info = {
    .id             = "xbox_game_cover_source",
    .type           = OBS_SOURCE_TYPE_INPUT,
    .output_flags   = OBS_SOURCE_VIDEO,
    .get_name       = source_get_name,
    .create         = on_source_create,
    .destroy        = on_source_destroy,
    .update         = on_source_update,
    .get_defaults   = source_get_defaults,
    .video_render   = on_source_video_render,
    .get_properties = source_get_properties,
    .get_width      = source_get_width,
    .get_height     = source_get_height,
    .video_tick     = NULL,
};

/**
 * @brief Get a pointer to this source type's obs_source_info.
 */
static const struct obs_source_info *game_cover_source_get(void) {
    return &game_cover_source_info;
}

//  --------------------------------------------------------------------------------------------------------------------
//      Public functions
//  --------------------------------------------------------------------------------------------------------------------

void game_cover_source_register(void) {

    snprintf(g_game_cover.display_name, sizeof(g_game_cover.display_name), "Game Cover");
    g_game_cover.id[0] = '\0';
    snprintf(g_game_cover.type, sizeof(g_game_cover.type), "game_cover");

    obs_register_source(game_cover_source_get());

    auto_visibility_register_config(&g_auto_visibility);

    monitoring_subscribe_game_played(&on_game_played);
}

void game_cover_source_cleanup(void) {
    image_source_destroy(&g_game_cover);
}
