#include "sources/xbox/game_cover.h"

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
    obs_source_t *source;
    uint32_t      width;
    uint32_t      height;
} xbox_game_cover_source_t;

/* Keeps track of the game cover image data */
typedef struct game_cover {
    char          image_path[512];
    gs_texture_t *image_texture;
    bool          must_reload;
} game_cover_t;

static game_cover_t g_game_cover;

//  --------------------------------------------------------------------------------------------------------------------
//	Private functions
//  --------------------------------------------------------------------------------------------------------------------

/**
 * Downloads the game box art from the specified URL and saves it in a temporary file.
 *
 * @param image_url
 */
static void download_box_art_from_url(const char *image_url) {

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
 *  Loads the game box art into the texture, if needed.
 *
 *  Use OBS's gs_texture_create_from_file with a temp file, or decode manually.
 *  OBS doesn't have a direct "from memory" for arbitrary formats, so we use
 *  `gs_create_texture_file_data` which can decode PNG/JPEG from memory.
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
    }

    g_game_cover.image_texture = gs_texture_create_from_file(g_game_cover.image_path);

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
 * Called when a new game is being played.
 * @param game
 */
static void on_xbox_game_played(const game_t *game) {

    char text[4096];
    snprintf(text, 4096, "Playing game %s (%s)", game->title, game->id);
    obs_log(LOG_INFO, text);

    const char *game_cover_url = xbox_get_game_cover(game);
    download_box_art_from_url(game_cover_url);
}

//  --------------------------------------------------------------------------------------------------------------------
//	Source callbacks
//  --------------------------------------------------------------------------------------------------------------------

static uint32_t source_get_width(void *data) {
    const xbox_game_cover_source_t *s = data;
    return s->width;
}

static uint32_t source_get_height(void *data) {
    const xbox_game_cover_source_t *s = data;
    return s->height;
}

static const char *source_get_name(void *unused) {

    UNUSED_PARAMETER(unused);

    return "Xbox Game Cover";
}

static void *on_source_create(obs_data_t *settings, obs_source_t *source) {

    UNUSED_PARAMETER(settings);

    xbox_game_cover_source_t *s = bzalloc(sizeof(*s));
    s->source                   = source;
    s->width                    = 800;
    s->height                   = 200;

    return s;
}

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
 *
 * @param data
 * @param settings
 */
static void on_source_update(void *data, obs_data_t *settings) {

    UNUSED_PARAMETER(settings);
    UNUSED_PARAMETER(data);

    /*
    xbox_game_cover_source_t *s = data;
    const char               *t = obs_data_get_string(settings, "text");

    bfree(s->text);
    s->text = bstrdup(t ? t : "");

    text_src_update_child(s);
    */
}

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

    /* Let the child (Text FT2) render into our source */
    /*
    if (source->child_text) {
        obs_source_video_render(source->child_text);
    }
    */
}

static obs_properties_t *source_get_properties(void *data) {

    UNUSED_PARAMETER(data);

    /* Finds out if there is a token available already */
    const xbox_identity_t *xbox_identity = state_get_xbox_identity();

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
 * Configuration of the Xbox game cover source.
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

static const struct obs_source_info *xbox_game_cover_source_get(void) {
    return &xbox_game_cover_source_info;
}

//  --------------------------------------------------------------------------------------------------------------------
//	Public functions
//  --------------------------------------------------------------------------------------------------------------------

/**
 * Registers the Xbox game cover source with the OBS source system.
 * This allows the Xbox game cover source to be available for use within OBS.
 */
void xbox_game_cover_source_register(void) {

    obs_register_source(xbox_game_cover_source_get());

    xbox_subscribe_game_played(&on_xbox_game_played);
}
