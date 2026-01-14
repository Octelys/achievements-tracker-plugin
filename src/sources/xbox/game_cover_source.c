#include "sources/xbox/game_cover_source.h"

#include <graphics/graphics.h>
#include <obs-module.h>
#include <diagnostics/log.h>
#include <curl/curl.h>

#include "io/state.h"
#include "oauth/xbox-live.h"
#include "crypto/crypto.h"
#include "xbox/xbox_client.h"
#include "xbox/xbox_monitoring.h"

#include <net/http/http.h>

// Store the source reference somewhere accessible
static obs_source_t *g_xbox_game_cover_src = NULL;

typedef struct xbox_game_cover_source {
    obs_source_t *source;
    char         *text;
    obs_source_t *child_text;
    uint32_t      width;
    uint32_t      height;

    /* Image support */
    gs_texture_t *image_texture;
    uint32_t      image_width;
    uint32_t      image_height;
    char         *image_url;
    bool          image_needs_load;
} xbox_game_cover_source_t;

/**
 * Loads an image from a URL and creates a texture.
 * Must be called from the graphics thread (e.g., in video_render or via obs_enter_graphics).
 */
static void load_image_from_url(xbox_game_cover_source_t *s) {

    if (!s || !s->image_url || s->image_url[0] == '\0') {
        return;
    }

    /* Free existing texture */
    if (s->image_texture) {
        gs_texture_destroy(s->image_texture);
        s->image_texture = NULL;
    }

    s->image_width  = 0;
    s->image_height = 0;

    uint8_t *data = NULL;
    size_t   size = 0;

    if (!http_download(s->image_url, &data, &size)) {
        obs_log(LOG_WARNING, "Failed to download image from: %s", s->image_url);
        return;
    }

    /* Use OBS's gs_texture_create_from_file with a temp file, or decode manually.
     * OBS doesn't have a direct "from memory" for arbitrary formats, so we use
     * `gs_create_texture_file_data` which can decode PNG/JPEG from memory. */

    /* Decode the image using stb_image (OBS uses it internally).
     * We'll write to a temp file and use gs_texture_create_from_file. */
    char temp_path[512];
    snprintf(temp_path, sizeof(temp_path), "%s/obs_plugin_temp_image.png", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");

    FILE *f = fopen(temp_path, "wb");
    if (!f) {
        obs_log(LOG_ERROR, "Failed to create temp file for image");
        bfree(data);
        return;
    }
    fwrite(data, 1, size, f);
    fclose(f);
    bfree(data);

    /* Now load from file using OBS graphics */
    obs_enter_graphics();

    s->image_texture = gs_texture_create_from_file(temp_path);

    if (s->image_texture) {
        s->image_width  = gs_texture_get_width(s->image_texture);
        s->image_height = gs_texture_get_height(s->image_texture);
        obs_log(LOG_INFO, "Loaded image %ux%u from %s", s->image_width, s->image_height, s->image_url);
    } else {
        obs_log(LOG_WARNING, "Failed to create texture from downloaded image");
    }

    obs_leave_graphics();

    /* Clean up temp file */
    remove(temp_path);

    s->image_needs_load = false;
}

/**
 * Sets a new image URL for the source. The image will be loaded on next render.
 */
static void set_image_url(xbox_game_cover_source_t *s, const char *url) {
    if (!s)
        return;

    bfree(s->image_url);
    s->image_url        = url ? bstrdup(url) : NULL;
    s->image_needs_load = url && url[0] != '\0';
}

/**
 *
 * @param s
 */
static void text_src_update_child(xbox_game_cover_source_t *s) {
    if (!s || !s->child_text) {
        return;
    }

    obs_data_t *st = obs_data_create();
    obs_data_set_string(st, "text", s->text ? s->text : "");
    obs_data_set_int(st, "color1", 0xFFFFFFFF); /* white */
    obs_data_set_bool(st, "outline", false);
    obs_data_set_int(st, "align", 0);  /* left */
    obs_data_set_int(st, "valign", 0); /* top */
    obs_source_update(s->child_text, st);
    obs_data_release(st);
}

/**
 *
 * @param settings
 * @param source
 * @return
 */
static void *xbox_game_cover_src_create(obs_data_t *settings, obs_source_t *source) {
    g_xbox_game_cover_src = source;

    xbox_game_cover_source_t *s = bzalloc(sizeof(*s));
    s->source                   = source;

    const char *t = obs_data_get_string(settings, "text");
    s->text       = bstrdup(t ? t : "Hello from plugin");

    s->width  = 800;
    s->height = 200;

    /* Create built-in text source as a child */
    obs_data_t *st = obs_data_create();
    obs_data_set_string(st, "text", s->text);
    s->child_text = obs_source_create_private("text_ft2_source", "child_text", st);
    obs_data_release(st);

    text_src_update_child(s);
    return s;
}

static void xbox_game_cover_src_destroy(void *data) {
    xbox_game_cover_source_t *s = data;

    if (!s) {
        return;
    }

    if (s->child_text) {
        obs_source_release(s->child_text);
    }

    /* Free image resources */
    if (s->image_texture) {
        obs_enter_graphics();
        gs_texture_destroy(s->image_texture);
        obs_leave_graphics();
    }

    bfree(s->image_url);
    bfree(s->text);
    bfree(s);
}

/**
 *
 * @param data
 * @param settings
 */
static void xbox_game_cover_src_update(void *data, obs_data_t *settings) {
    xbox_game_cover_source_t *s = data;
    const char               *t = obs_data_get_string(settings, "text");

    bfree(s->text);
    s->text = bstrdup(t ? t : "");

    text_src_update_child(s);
}

/**
 * Returns the configured width of the source.
 *
 * @param data
 * @return
 */
static uint32_t xbox_game_cover_src_get_width(void *data) {
    const xbox_game_cover_source_t *s = data;
    return s->width;
}

/**
 * Returns the configured height of the source.
 *
 * @param data
 * @return
 */
static uint32_t xbox_game_cover_src_get_height(void *data) {
    const xbox_game_cover_source_t *s = data;
    return s->height;
}

/**
 * Draws the image.
 *
 * @param data
 * @param effect
 */
static void xbox_game_cover_src_video_render(void *data, gs_effect_t *effect) {

    xbox_game_cover_source_t *source = data;

    if (!source) {
        return;
    }

    /* Load image if needed (deferred load in graphics context) */
    if (source->image_needs_load) {
        load_image_from_url(source);
    }

    /* Render the image if we have a texture */
    if (source->image_texture) {

        /* Use the passed effect or get the default if NULL */
        gs_effect_t *used_effect = effect ? effect : obs_get_base_effect(OBS_EFFECT_DEFAULT);

        /* Only start our own effect loop if no effect is currently active.
         * If an effect was passed in, it's already active - just set texture and draw. */
        if (effect) {
            /* Effect already active from caller - just set texture and draw */
            gs_eparam_t *image_param = gs_effect_get_param_by_name(effect, "image");
            if (image_param)
                gs_effect_set_texture(image_param, source->image_texture);
            gs_draw_sprite(source->image_texture, 0, source->width, source->height);
        } else {
            /* No effect passed - start our own loop */
            gs_eparam_t *image_param = gs_effect_get_param_by_name(used_effect, "image");
            gs_effect_set_texture(image_param, source->image_texture);
            while (gs_effect_loop(used_effect, "Draw")) {
                gs_draw_sprite(source->image_texture, 0, source->width, source->height);
            }
        }
    }

    /* Let the child (Text FT2) render into our source */
    if (source->child_text) {
        obs_source_video_render(source->child_text);
    }
}

/**
 * Refreshes the properties page for the current Xbox achievements text source.
 * If the global source `g_xbox_achievements_text_src` is not initialized,
 * the function will return without performing any operation.
 *
 * This method signals OBS to recreate the properties UI, ensuring that
 * updates to the source are reflected in the UI. Returning true from
 * related button callbacks can also help trigger this behavior.
 */
static void refresh_page() {

    if (!g_xbox_game_cover_src)
        return;

    /*
     * Force OBS to refresh the properties UI by signaling that
     * properties need to be recreated. Returning true from the
     * button callback also helps trigger this.
     */
    obs_source_update_properties(g_xbox_game_cover_src);
}

/**
 * Sets the image URL for the Xbox game cover source.
 *
 * @param url
 */
static void source_set_image_url(const char *url) {
    if (!g_xbox_game_cover_src)
        return;

    /* Get the private data from the source */
    void *data = obs_obj_get_data(g_xbox_game_cover_src);
    if (!data)
        return;

    xbox_game_cover_source_t *s = data;
    set_image_url(s, url);
}

/**
 * Called when the Sign-out button is clicked
 *
 * Clears the tokens from the state.
 *
 * @param props
 * @param property
 * @param data
 * @return
 */
static bool on_sign_out_clicked(obs_properties_t *props, obs_property_t *property, void *data) {

    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);
    UNUSED_PARAMETER(data);

    state_clear();

    refresh_page();

    return true;
}

/**
 * Handles the completion of Xbox sign-in.
 *
 * This function is triggered once the Xbox sign-in process is completed.
 * It invokes the refresh_page function to update the OBS properties UI
 * associated with the Xbox achievements text source.
 */
static void on_xbox_sign_in_completed() {
    refresh_page();
}

/**
 * Called when the Sign-in button is clicked.
 *
 * The method triggers the device oauth flow to register the device with Xbox
 * live.
 *
 * @param props
 * @param property
 * @param data
 *
 * @return
 */
static bool on_sign_in_xbox_clicked(obs_properties_t *props, obs_property_t *property, void *data) {
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);
    UNUSED_PARAMETER(data);

    device_t *device = state_get_device();

    if (!xbox_live_get_authenticate(device, &on_xbox_sign_in_completed)) {
        obs_log(LOG_WARNING, "Xbox sign-in failed");
        return false;
    }

    return true;
}

/**
 * Called when a new game is being played.
 * @param game
 */
static void on_xbox_game_played(const game_t *game) {

    char text[4096];
    snprintf(text, 4096, "Playing game %s (%s)", game->title, game->id);
    obs_log(LOG_WARNING, text);

    const char *game_cover_url = xbox_get_game_cover(game);

    source_set_image_url(game_cover_url);

    refresh_page();
}

/**
 * Called when the xbox monitor's status changes.
 *
 * @param connected
 * @param error_message
 */
static void on_xbox_monitoring_connection_status_changed(const bool connected, const char *error_message) {
    if (connected) {
        obs_log(LOG_WARNING, "Connected to Real-Time Activity endpoint");
        xbox_subscribe();
    } else {
        obs_log(LOG_WARNING, error_message);
    }
}

/**
 * Called when the monitoring button is clicked.
 *
 * @param props
 * @param property
 * @param data
 * @return
 */
static bool on_monitoring_clicked(obs_properties_t *props, obs_property_t *property, void *data) {
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);
    UNUSED_PARAMETER(data);

    xbox_monitoring_start(&on_xbox_game_played, &on_xbox_monitoring_connection_status_changed);
    obs_log(LOG_WARNING, "Monitoring started!");

    return true;
}

/**
 * Configuration of the Xbox game cover source.
 */
static obs_properties_t *xbox_game_cover_get_properties(void *data) {
    UNUSED_PARAMETER(data);

    /* Finds out if there is a token available already */
    const xbox_identity_t *xbox_identity = state_get_xbox_identity();

    /* Lists all the UI components of the properties page */
    obs_properties_t *p = obs_properties_create();

    if (xbox_identity != NULL) {
        char status[4096];
        snprintf(status, 4096, "Signed in as %s", xbox_identity->gamertag);

        int64_t gamerscore = 0;
        xbox_fetch_gamerscore(&gamerscore);

        char gamerscore_text[4096];
        snprintf(gamerscore_text, 4096, "Gamerscore %lld", gamerscore);

        obs_properties_add_text(p, "connected_status_info", status, OBS_TEXT_INFO);
        obs_properties_add_text(p, "gamerscore_info", gamerscore_text, OBS_TEXT_INFO);

        const game_t *game = get_current_game();

        if (game) {
            char game_played[4096];
            snprintf(game_played, sizeof(game_played), "Playing %s (%s)", game->title, game->id);
            obs_properties_add_text(p, "game_played", game_played, OBS_TEXT_INFO);
        }

        obs_properties_add_button(p, "sign_out_xbox", "Sign out from Xbox", &on_sign_out_clicked);

        /* Temporary */
        obs_properties_add_button(p, "monitor", "Start monitoring", &on_monitoring_clicked);
    } else {
        obs_properties_add_text(p, "disconnected_status_info", "You are not connected.", OBS_TEXT_INFO);
        obs_properties_add_button(p, "sign_in_xbox", "Sign in with Xbox", &on_sign_in_xbox_clicked);
    }

    return p;
}

//  --------------------------------------------------------------------------------------------------------------------
//	Public methods
//  --------------------------------------------------------------------------------------------------------------------

/**
 *  Gets the name of the source.
 */
const char *xbox_game_cover_src_get_name(void *unused) {

    UNUSED_PARAMETER(unused);

    return "Game Cover";
}

/**
 * Registers the Xbox game cover source with the OBS source system.
 * This allows the Xbox game cover source to be available for use within OBS.
 */
void xbox_game_cover_source_register(void) {
    obs_register_source(xbox_game_cover_source_get());
}

/**
 * Configuration of the Xbox game cover source.
 */
static struct obs_source_info xbox_game_cover_source_info = {
    .id             = "xbox_game_cover_source",
    .type           = OBS_SOURCE_TYPE_INPUT,
    .output_flags   = OBS_SOURCE_VIDEO,
    .get_name       = xbox_game_cover_src_get_name,
    .create         = xbox_game_cover_src_create,
    .destroy        = xbox_game_cover_src_destroy,
    .update         = xbox_game_cover_src_update,
    .get_properties = xbox_game_cover_get_properties,
    .get_width      = xbox_game_cover_src_get_width,
    .get_height     = xbox_game_cover_src_get_height,
    .video_tick     = NULL,
    .video_render   = xbox_game_cover_src_video_render,
};

/**
 * Returns the configuration of the Xbox game cover source.
 *
 * @return The configuration of the Xbox game cover source.
 */
const struct obs_source_info *xbox_game_cover_source_get(void) {
    return &xbox_game_cover_source_info;
}
