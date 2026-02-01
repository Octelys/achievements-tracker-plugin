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

#include <graphics/graphics.h>
#include <obs-module.h>
#include <diagnostics/log.h>
#include <curl/curl.h>
#include <inttypes.h>

#include "drawing/image.h"
#include "io/state.h"
#include "oauth/xbox-live.h"
#include "crypto/crypto.h"
#include "xbox/xbox_client.h"
#include "xbox/xbox_monitor.h"

#include <net/http/http.h>

typedef struct xbox_game_cover_source {
    /** OBS source instance. */
    obs_source_t *source;

    /** Source draw width in pixels (used by get_width/video_render). */
    uint32_t width;

    /** Source draw height in pixels (used by get_height/video_render). */
    uint32_t height;
} xbox_game_cover_source_t;

/**
 * @brief Runtime cache for the downloaded cover art image.
 */
typedef struct game_cover {
    /** Temporary file path used as an intermediate for gs_texture_create_from_file(). */
    char image_path[512];

    /** GPU texture created from the downloaded image (owned by this module). */
    gs_texture_t *image_texture;

    /** If true, the next render tick should reload the texture from image_path. */
    bool must_reload;
} game_cover_t;

/**
 * @brief Global singleton cover cache.
 *
 * This source is implemented as a singleton that stores the current cover art in
 * a global cache.
 */
static game_cover_t g_game_cover;

//  --------------------------------------------------------------------------------------------------------------------
//	Private functions
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Download cover art from an URL into a temporary file.
 *
 * The file path is stored in g_game_cover.image_path and g_game_cover.must_reload
 * is set to true so the graphics thread can create a texture on the next render.
 *
 * @param image_url Cover art URL. If NULL or empty, this function is a no-op.
 */
static void download_game_cover_from_url(const char *image_url) {

    if (!image_url || image_url[0] == '\0') {
        return;
    }

    obs_log(LOG_INFO, "Loading Xbox game box art from URL: %s", image_url);

    /* Downloads the image in memory */
    uint8_t *data = NULL;
    size_t   size = 0;

    if (!http_download(image_url, &data, &size)) {
        obs_log(LOG_WARNING, "Unable to download box art from URL: %s", image_url);
        return;
    }

    /* Write the bytes to a temp file and use gs_texture_create_from_file of the render thread */
    snprintf(g_game_cover.image_path,
             sizeof(g_game_cover.image_path),
             "%s/obs_plugin_temp_image.png",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");

    FILE *temp_file = fopen(g_game_cover.image_path, "wb");

    if (!temp_file) {
        obs_log(LOG_ERROR, "Failed to create temp file for image");
        bfree(data);
        return;
    }
    fwrite(data, 1, size, temp_file);
    fclose(temp_file);
    bfree(data);

    /* Force its reload into a texture on the next render */
    g_game_cover.must_reload = true;
}

/**
 * @brief Load the downloaded cover image into a gs_texture_t.
 *
 * If g_game_cover.must_reload is false, this function does nothing.
 *
 * This must be called from a context where entering/leaving graphics is allowed
 * (typically from video_render).
 */
static void load_texture_from_file() {

    if (!g_game_cover.must_reload) {
        return;
    }

    /* Now load the image from the temporary file using OBS graphics */
    obs_enter_graphics();

    /* Free existing texture */
    if (g_game_cover.image_texture) {
        gs_texture_destroy(g_game_cover.image_texture);
        g_game_cover.image_texture = NULL;
    }

    if (strlen(g_game_cover.image_path) > 0) {
        g_game_cover.image_texture = gs_texture_create_from_file(g_game_cover.image_path);
    }

    obs_leave_graphics();

    g_game_cover.must_reload = false;

    /* Clean up temp file */
    remove(g_game_cover.image_path);

    if (g_game_cover.image_texture) {
        obs_log(LOG_INFO, "New image has been successfully loaded from the file");
    } else {
        obs_log(LOG_WARNING, "Failed to create texture from the file");
    }
}

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

    char text[4096];
    snprintf(text, 4096, "Playing game %s (%s)", game->title, game->id);
    obs_log(LOG_INFO, text);

    const char *game_cover_url = xbox_get_game_cover(game);
    download_game_cover_from_url(game_cover_url);
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
        g_game_cover.image_path[0] = '\0';
        g_game_cover.must_reload   = true;
    }
}

//  --------------------------------------------------------------------------------------------------------------------
//	Source callbacks
//  --------------------------------------------------------------------------------------------------------------------

/** @brief OBS callback returning the source width. */
static uint32_t source_get_width(void *data) {
    const xbox_game_cover_source_t *s = data;
    return s->width;
}

/** @brief OBS callback returning the source height. */
static uint32_t source_get_height(void *data) {
    const xbox_game_cover_source_t *s = data;
    return s->height;
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
 * @return Newly allocated xbox_game_cover_source_t.
 */
static void *on_source_create(obs_data_t *settings, obs_source_t *source) {

    UNUSED_PARAMETER(settings);

    xbox_game_cover_source_t *s = bzalloc(sizeof(*s));
    s->source                   = source;
    s->width                    = 800;
    s->height                   = 200;

    return s;
}

/**
 * @brief OBS callback destroying a source instance.
 *
 * Frees the instance data and any global image resources.
 */
static void on_source_destroy(void *data) {

    xbox_game_cover_source_t *source = data;

    if (!source) {
        return;
    }

    /* Free image resources */
    if (g_game_cover.image_texture) {
        obs_enter_graphics();
        gs_texture_destroy(g_game_cover.image_texture);
        obs_leave_graphics();
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

    xbox_game_cover_source_t *source = data;

    if (!source) {
        return;
    }

    /* Load image if needed (deferred load in graphics context) */
    load_texture_from_file();

    /* Render the image if we have a texture */
    if (g_game_cover.image_texture) {
        draw_texture(g_game_cover.image_texture, source->width, source->height, effect);
    }
}

/**
 * @brief OBS callback to construct the properties UI.
 *
 * Shows connection status, gamerscore, and the currently played game.
 */
static obs_properties_t *source_get_properties(void *data) {

    UNUSED_PARAMETER(data);

    /* Gets or refreshes the token */
    const xbox_identity_t *xbox_identity = xbox_live_get_identity();

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
//	Public functions
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Register the Xbox Game Cover source with OBS.
 *
 * Registers the source type, then subscribes to Xbox game-played events so the
 * cover art gets refreshed when the current game changes.
 */
void xbox_game_cover_source_register(void) {

    obs_register_source(xbox_game_cover_source_get());

    xbox_subscribe_game_played(&on_xbox_game_played);
    xbox_subscribe_connected_changed(&on_connection_changed);
}
