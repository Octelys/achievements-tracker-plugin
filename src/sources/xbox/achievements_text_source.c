#include "sources/xbox/achievements_text_source.h"

#include <graphics/graphics.h>
#include <graphics/image-file.h>
#include <obs-module.h>
#include <diagnostics/log.h>
#include <curl/curl.h>
#include <inttypes.h>

#include "io/state.h"
#include "oauth/xbox-live.h"
#include "crypto/crypto.h"
#include "xbox/xbox_client.h"
#include "xbox/xbox_monitoring.h"

// Store the source reference somewhere accessible
static obs_source_t *g_xbox_game_cover_src = NULL;

struct xbox_game_cover_source {
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
};

/* -------------------------------------------------------------------------
 * Image download helpers
 * ------------------------------------------------------------------------- */

struct image_buffer {
    uint8_t *data;
    size_t   size;
    size_t   capacity;
};

static size_t image_download_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    struct image_buffer *buf   = (struct image_buffer *)userdata;
    size_t               total = size * nmemb;

    if (buf->size + total > buf->capacity) {
        size_t new_cap = (buf->capacity == 0) ? 65536 : buf->capacity * 2;
        while (new_cap < buf->size + total)
            new_cap *= 2;
        uint8_t *new_data = brealloc(buf->data, new_cap);
        if (!new_data)
            return 0;
        buf->data     = new_data;
        buf->capacity = new_cap;
    }

    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    return total;
}

/**
 * Downloads an image from a URL into memory.
 * Caller must bfree(*out_data) when done.
 * Returns true on success.
 */
static bool download_image_to_memory(const char *url, uint8_t **out_data, size_t *out_size) {
    if (!url || !out_data || !out_size)
        return false;

    *out_data = NULL;
    *out_size = 0;

    CURL *curl = curl_easy_init();
    if (!curl) {
        obs_log(LOG_ERROR, "Failed to init curl for image download");
        return false;
    }

    struct image_buffer buf = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, image_download_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "OBS-Plugin/1.0");

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        obs_log(LOG_ERROR, "Image download failed: %s", curl_easy_strerror(res));
        bfree(buf.data);
        return false;
    }

    *out_data = buf.data;
    *out_size = buf.size;
    return true;
}

/**
 * Loads an image from a URL and creates a texture.
 * Must be called from the graphics thread (e.g., in video_render or via obs_enter_graphics).
 */
static void load_image_from_url(struct xbox_game_cover_source *s) {
    if (!s || !s->image_url || s->image_url[0] == '\0')
        return;

    /* Free existing texture */
    if (s->image_texture) {
        gs_texture_destroy(s->image_texture);
        s->image_texture = NULL;
    }
    s->image_width  = 0;
    s->image_height = 0;

    uint8_t *data = NULL;
    size_t   size = 0;

    if (!download_image_to_memory(s->image_url, &data, &size)) {
        obs_log(LOG_WARNING, "Failed to download image from: %s", s->image_url);
        return;
    }

    /* Use OBS's gs_texture_create_from_file with a temp file, or decode manually.
     * OBS doesn't have a direct "from memory" for arbitrary formats, so we use
     * gs_create_texture_file_data which can decode PNG/JPEG from memory. */

    /* Decode image using stb_image (OBS uses it internally).
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
static void set_image_url(struct xbox_game_cover_source *s, const char *url) {
    if (!s)
        return;

    bfree(s->image_url);
    s->image_url        = url ? bstrdup(url) : NULL;
    s->image_needs_load = (url && url[0] != '\0');
}

static void text_src_update_child(struct xbox_game_cover_source *s) {
    if (!s || !s->child_text)
        return;

    obs_data_t *st = obs_data_create();
    obs_data_set_string(st, "text", s->text ? s->text : "");
    obs_data_set_int(st, "color1", 0xFFFFFFFF); /* white */
    obs_data_set_bool(st, "outline", false);
    obs_data_set_int(st, "align", 0);  /* left */
    obs_data_set_int(st, "valign", 0); /* top */
    obs_source_update(s->child_text, st);
    obs_data_release(st);
}

void *xbox_game_cover_src_create(obs_data_t *settings, obs_source_t *source) {
    g_xbox_game_cover_src = source;

    struct xbox_game_cover_source *s = bzalloc(sizeof(*s));
    s->source                        = source;

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

void xbox_game_cover_src_destroy(void *data) {
    struct xbox_game_cover_source *s = data;

    if (!s)
        return;

    if (s->child_text)
        obs_source_release(s->child_text);

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

void xbox_game_cover_src_update(void *data, obs_data_t *settings) {
    struct xbox_game_cover_source *s = data;
    const char                    *t = obs_data_get_string(settings, "text");

    bfree(s->text);
    s->text = bstrdup(t ? t : "");

    text_src_update_child(s);
}

uint32_t xbox_achievements_text_src_get_width(void *data) {
    struct xbox_game_cover_source *s = data;
    /* Use image width if available, otherwise default */
    if (s->image_texture && s->image_width > 0)
        return s->image_width;
    return s->width;
}

uint32_t xbox_game_cover_src_get_height(void *data) {
    struct xbox_game_cover_source *s = data;
    /* Use image height if available, otherwise default */
    if (s->image_texture && s->image_height > 0)
        return s->image_height;
    return s->height;
}

void xbox_game_cover_src_video_render(void *data, gs_effect_t *effect) {
    struct xbox_game_cover_source *s = data;
    if (!s)
        return;

    /* Load image if needed (deferred load in graphics context) */
    if (s->image_needs_load) {
        load_image_from_url(s);
    }

    /* Render the image if we have a texture */
    if (s->image_texture) {
        /* Use the passed effect, or get the default if NULL */
        gs_effect_t *eff = effect ? effect : obs_get_base_effect(OBS_EFFECT_DEFAULT);

        /* Only start our own effect loop if no effect is currently active.
         * If an effect was passed in, it's already active - just set texture and draw. */
        if (effect) {
            /* Effect already active from caller - just set texture and draw */
            gs_eparam_t *image_param = gs_effect_get_param_by_name(effect, "image");
            if (image_param)
                gs_effect_set_texture(image_param, s->image_texture);
            gs_draw_sprite(s->image_texture, 0, s->image_width, s->image_height);
        } else {
            /* No effect passed - start our own loop */
            gs_eparam_t *image_param = gs_effect_get_param_by_name(eff, "image");
            gs_effect_set_texture(image_param, s->image_texture);
            while (gs_effect_loop(eff, "Draw")) {
                gs_draw_sprite(s->image_texture, 0, s->image_width, s->image_height);
            }
        }
    }

    /* Let the child (Text FT2) render into our source */
    if (s->child_text) {
        obs_source_video_render(s->child_text);
    }
}

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

static void on_xbox_sign_in_completed() {
    refresh_page();
}

/**
 * Called when the Sign-in button is called.
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

static void on_xbox_game_played(const game_t *game) {
    char text[4096];
    snprintf(text, 4096, "Playing game %s (%s)", game->title, game->id);
    obs_log(LOG_WARNING, text);

    const char *game_cover_url = xbox_get_game_cover(game);

    xbox_achievements_text_source_set_image_url(game_cover_url);

    refresh_page();
}

static void on_xbox_monitoring_connection_status_changed(bool connected, const char *error_message) {
    if (connected) {
        obs_log(LOG_WARNING, "Connected to Real-Time Activity endpoint");
        xbox_subscribe();
    } else {
        obs_log(LOG_WARNING, error_message);
    }
}

static bool on_monitoring_clicked(obs_properties_t *props, obs_property_t *property, void *data) {
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);
    UNUSED_PARAMETER(data);

    xbox_monitoring_start(&on_xbox_game_played, &on_xbox_monitoring_connection_status_changed);
    obs_log(LOG_WARNING, "Monitoring started!");

    return true;
}

/**
 * Configures the properties page of the plugin
 *
 * @param data
 * @return
 */
obs_properties_t *xbox_achievements_get_properties(void *data) {
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
        snprintf(gamerscore_text, 4096, "Gamerscore %" PRId64, gamerscore);

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

/**
 * Gets the name of the source
 *
 * @param unused
 * @return
 */
const char *xbox_achievements_text_src_get_name(void *unused) {
    UNUSED_PARAMETER(unused);

    return "Achievements Tracker";
}

static struct obs_source_info xbox_achievements_text_src_info = {
    .id             = "xbox_achievements_text_source",
    .type           = OBS_SOURCE_TYPE_INPUT,
    .output_flags   = OBS_SOURCE_VIDEO,
    .get_name       = xbox_achievements_text_src_get_name,
    .create         = xbox_game_cover_src_create,
    .destroy        = xbox_game_cover_src_destroy,
    .update         = xbox_game_cover_src_update,
    .get_properties = xbox_achievements_get_properties,
    .get_width      = xbox_achievements_text_src_get_width,
    .get_height     = xbox_game_cover_src_get_height,
    .video_tick     = NULL,
    .video_render   = xbox_game_cover_src_video_render,
};

//	Public methods

/**
 * Returns the configuration of the plugin
 *
 * @return
 */
const struct obs_source_info *xbox_achievements_text_source_get(void) {
    return &xbox_achievements_text_src_info;
}

void xbox_achievements_text_source_register(void) {
    obs_register_source(xbox_achievements_text_source_get());
}

void xbox_achievements_text_source_set_image_url(const char *url) {
    if (!g_xbox_game_cover_src)
        return;

    /* Get the private data from the source */
    void *data = obs_obj_get_data(g_xbox_game_cover_src);
    if (!data)
        return;

    struct xbox_game_cover_source *s = data;
    set_image_url(s, url);
}
