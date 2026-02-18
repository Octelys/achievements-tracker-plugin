#include "sources/xbox/game_cover.h"

/**
 * @file game_cover.c
 * @brief OBS source that renders the cover art for the currently played Xbox game.
 *
 * Responsibilities:
 *  - Subscribe to Xbox game-played events.
 *  - Download cover art when the game changes.
 *  - Load the image into an OBS gs_texture_t on the graphics thread.
 *  - Render the texture in the source's video_render callback.
 *
 * Threading notes:
 *  - Downloading happens on the calling thread of on_xbox_game_played() (currently
 *    synchronous).
 *  - Texture creation/destruction must happen on the OBS graphics thread; this
 *    file uses obs_enter_graphics()/obs_leave_graphics() to ensure that.
 */

#include <obs-module.h>
#include <diagnostics/log.h>
#include <inttypes.h>

#include "oauth/xbox-live.h"
#include "sources/common/image_source.h"
#include "xbox/xbox_client.h"
#include "xbox/xbox_monitor.h"

/**
 * @brief Global singleton cover cache.
 *
 * This source is implemented as a singleton that stores the current cover art in
 * a global cache.
 */
static image_t g_game_cover;

//  --------------------------------------------------------------------------------------------------------------------
//	Event handlers
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Event handler called when a new game starts being played.
 *
 * Fetches the cover-art URL for the given game and triggers a download.
 *
 * @param game Currently played game information.
 */
static void on_xbox_game_played(const game_t *game) {

    obs_log(LOG_INFO, "Playing game %s (%s)", game->title, game->id);

    char *game_cover_url = xbox_get_game_cover(game);
    snprintf(g_game_cover.url, sizeof(g_game_cover.url), "%s", game_cover_url);
    snprintf(g_game_cover.id, sizeof(g_game_cover.id), "%s", game->id);

    image_source_download(&g_game_cover);

    free_memory((void **)&game_cover_url);
}

/**
 * @brief Xbox monitor callback invoked when connection state changes.
 *
 * When connected, this refreshes the gamerscore display.
 *
 * @param is_connected Whether the account is currently connected.
 * @param error_message Optional error message if disconnected (ignored here).
 */
static void on_connection_changed(bool is_connected, const char *error_message) {

    UNUSED_PARAMETER(error_message);

    if (is_connected) {
        obs_log(LOG_INFO, "Connected to Xbox Live - waiting for game played events");
    } else {
        image_source_clear(&g_game_cover);
    }
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

    return "Xbox Game Cover";
}

/**
 * @brief OBS callback creating a new source instance.
 *
 * @param settings OBS settings object (currently unused).
 * @param source   OBS source instance.
 * @return Newly allocated image_source_data_t.
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

    UNUSED_PARAMETER(settings);
    UNUSED_PARAMETER(data);
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

    /* Render the image if we have a texture */
    image_source_render_active(&g_game_cover, source->size, effect);
}

/**
 * @brief OBS callback to construct the properties UI.
 *
 * Shows connection status, gamerscore, and the currently played game.
 */
static obs_properties_t *source_get_properties(void *data) {

    UNUSED_PARAMETER(data);

    /* Gets or refreshes the token */
    xbox_identity_t *xbox_identity = xbox_live_get_identity();

    /* Lists all the UI components of the properties page */
    obs_properties_t *p = obs_properties_create();

    if (xbox_identity != NULL) {
        char status[4096];
        snprintf(status, 4096, "Connected to your xbox account as %s", xbox_identity->gamertag);

        int64_t gamerscore = 0;
        xbox_fetch_gamerscore(&gamerscore);

        char gamerscore_text[4096];
        snprintf(gamerscore_text, 4096, "Gamerscore %" PRId64, gamerscore);

        obs_properties_add_text(p, "connected_status_info", status, OBS_TEXT_INFO);
        obs_properties_add_text(p, "gamerscore_info", gamerscore_text, OBS_TEXT_INFO);

        const game_t *game = get_current_game();

        if (game) {
            char game_played[4096];
            snprintf(game_played, sizeof(game_played), "Playing %s (%s)", game->title, game->id);
            obs_properties_add_text(p, "game_played", game_played, OBS_TEXT_INFO);
        }
    } else {
        obs_properties_add_text(p,
                                "disconnected_status_info",
                                "You are not connected to your xbox account",
                                OBS_TEXT_INFO);
    }

    free_identity(&xbox_identity);

    return p;
}

/**
 * @brief obs_source_info for the Xbox Game Cover source.
 */
static struct obs_source_info xbox_game_cover_source_info = {
    .id             = "xbox_game_cover_source",
    .type           = OBS_SOURCE_TYPE_INPUT,
    .output_flags   = OBS_SOURCE_VIDEO,
    .get_name       = source_get_name,
    .create         = on_source_create,
    .destroy        = on_source_destroy,
    .update         = on_source_update,
    .video_render   = on_source_video_render,
    .get_properties = source_get_properties,
    .get_width      = source_get_width,
    .get_height     = source_get_height,
    .video_tick     = NULL,
};

/**
 * @brief Get a pointer to this source type's obs_source_info.
 */
static const struct obs_source_info *xbox_game_cover_source_get(void) {
    return &xbox_game_cover_source_info;
}

//  --------------------------------------------------------------------------------------------------------------------
//      Public functions
//  --------------------------------------------------------------------------------------------------------------------

void xbox_game_cover_source_register(void) {

    snprintf(g_game_cover.display_name, sizeof(g_game_cover.display_name), "Game Cover");
    g_game_cover.id[0] = '\0';
    snprintf(g_game_cover.type, sizeof(g_game_cover.type), "game_cover");

    obs_register_source(xbox_game_cover_source_get());

    xbox_subscribe_game_played(&on_xbox_game_played);
    xbox_subscribe_connected_changed(&on_connection_changed);
}

void xbox_game_cover_source_cleanup(void) {
    image_source_destroy(&g_game_cover);
}
