#include "sources/xbox/achievement_icon.h"

#include <graphics/graphics.h>
#include <obs-module.h>
#include <diagnostics/log.h>
#include <curl/curl.h>
#include <inttypes.h>

#include "drawing/image.h"
#include "oauth/xbox-live.h"
#include "crypto/crypto.h"
#include "xbox/xbox_client.h"
#include "xbox/xbox_monitor.h"

#include <net/http/http.h>

typedef struct xbox_achievement_icon_source {
    /** OBS source instance. */
    obs_source_t *source;

    /** Source draw width in pixels (used by get_width/video_render). */
    uint32_t width;

    /** Source draw height in pixels (used by get_height/video_render). */
    uint32_t height;
} xbox_achievement_icon_source_t;

/**
 * @brief Runtime cache for the downloaded achievement icon image.
 */
typedef struct achievement_icon {
    /** Last fetched achievement icon URL (used to detect changes). */
    char image_url[1024];

    /** Temporary file path used as an intermediate for gs_texture_create_from_file(). */
    char image_path[512];

    /** GPU texture created from the downloaded image (owned by this module). */
    gs_texture_t *image_texture;

    /** If true, the next render tick should reload the texture from image_path. */
    bool must_reload;
} achievement_icon_t;

/**
 * @brief Global singleton achievement icon cache.
 *
 * This source is implemented as a singleton that stores the current achievement icon
 * in a global cache.
 */
static achievement_icon_t g_achievement_icon;

//  --------------------------------------------------------------------------------------------------------------------
//	Private functions
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Download achievement icon image from a URL into a temporary file.
 *
 * The file path is stored in g_achievement_icon.image_path and g_achievement_icon.must_reload
 * is set to true so the graphics thread can create a texture on the next render.
 *
 * @param image_url Achievement icon image URL. If NULL or empty, this function is a no-op.
 */
static void download_achievement_icon_from_url(const char *image_url) {

    if (!image_url || image_url[0] == '\0') {
        return;
    }

    obs_log(LOG_INFO, "Downloading Xbox Achievement Icon image from URL: %s", image_url);

    /* Downloads the image in memory */
    uint8_t *data = NULL;
    size_t   size = 0;

    if (!http_download(image_url, &data, &size)) {
        obs_log(LOG_WARNING, "Unable to download Achievement Icon image from URL: %s", image_url);
        return;
    }

    obs_log(LOG_INFO, "Downloaded %zu bytes for Achievement Icon image", size);

    /* Write the bytes to a temp file and use gs_texture_create_from_file() on the render thread */
    snprintf(g_achievement_icon.image_path,
             sizeof(g_achievement_icon.image_path),
             "%s/obs_plugin_temp_achievement_icon.png",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");

    FILE *temp_file = fopen(g_achievement_icon.image_path, "wb");

    if (!temp_file) {
        obs_log(LOG_ERROR, "Failed to create temp file for Achievement Icon image");
        bfree(data);
        return;
    }
    fwrite(data, 1, size, temp_file);
    fclose(temp_file);
    bfree(data);

    /* Force its reload into a texture on the next render */
    g_achievement_icon.must_reload = true;
}

/**
 * @brief Load the downloaded achievement icon image into a gs_texture_t.
 *
 * If g_achievement_icon.must_reload is false, this function does nothing.
 *
 * This must be called from a context where entering/leaving graphics is allowed
 * (typically from video_render).
 */
static void load_texture_from_file() {

    if (!g_achievement_icon.must_reload) {
        return;
    }

    /* Now load the image from the temporary file using OBS graphics */
    obs_enter_graphics();

    /* Free existing texture */
    if (g_achievement_icon.image_texture) {
        gs_texture_destroy(g_achievement_icon.image_texture);
        g_achievement_icon.image_texture = NULL;
    }

    if (g_achievement_icon.image_path[0] != '\0') {
        g_achievement_icon.image_texture = gs_texture_create_from_file(g_achievement_icon.image_path);
    }

    obs_leave_graphics();

    g_achievement_icon.must_reload = false;

    /* Clean up temp file */
    remove(g_achievement_icon.image_path);

    if (g_achievement_icon.image_texture) {
        obs_log(LOG_INFO, "New Achievement Icon texture has been successfully loaded");
    } else {
        obs_log(LOG_WARNING, "Failed to create Achievement Icon texture from the downloaded file");
    }
}

//  --------------------------------------------------------------------------------------------------------------------
//	Event handlers
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Xbox monitor callback invoked when Xbox Live connection state changes.
 *
 * Retrieves the current game's most recent achievement and updates the icon display.
 * This ensures the achievement icon source reflects the latest data when the
 * connection is established or re-established.
 *
 * @param is_connected Whether the Xbox account is currently connected.
 * @param error_message Optional error message if disconnected (unused).
 */
static void on_connection_changed(bool is_connected, const char *error_message) {

    UNUSED_PARAMETER(error_message);

    if (is_connected) {
        obs_log(LOG_INFO, "Connected to Xbox Live - fetching achievement icon URL");

        const achievement_t *achievement = get_current_game_achievements();

        if (achievement && achievement->icon_url) {
            if (strcasecmp(achievement->icon_url, g_achievement_icon.image_url) == 0) {
                /* Icon URL hasn't changed, no need to download/reload */
                return;
            }

            snprintf(g_achievement_icon.image_url, sizeof(g_achievement_icon.image_url), "%s", achievement->icon_url);
            download_achievement_icon_from_url(achievement->icon_url);
        } else {
            /* No achievement or empty URL: clear cached URL and force texture to be freed on the next render */
            g_achievement_icon.image_url[0]  = '\0';
            g_achievement_icon.image_path[0] = '\0';
            g_achievement_icon.must_reload   = true;
        }
    } else {
        g_achievement_icon.image_url[0]  = '\0';
        g_achievement_icon.image_path[0] = '\0';
        g_achievement_icon.must_reload   = true;
    }
}

/**
 * @brief Xbox monitor callback invoked when achievement progress is updated.
 *
 * Retrieves the current game's most recent achievement and updates the icon display
 * to show the newly unlocked achievement's icon. This callback ensures the source
 * always displays the latest achievement that was unlocked.
 *
 * @param gamerscore Updated gamerscore snapshot (unused).
 * @param progress   Achievement progress details (unused).
 */
static void on_achievements_progressed(const gamerscore_t *gamerscore, const achievement_progress_t *progress) {

    UNUSED_PARAMETER(gamerscore);
    UNUSED_PARAMETER(progress);

    const achievement_t *achievement = get_current_game_achievements();

    if (achievement && achievement->icon_url) {
        if (strcasecmp(achievement->icon_url, g_achievement_icon.image_url) == 0) {
            /* Icon URL hasn't changed, no need to download/reload */
            return;
        }

        /* TODO Move to constant */
        snprintf(g_achievement_icon.image_url, sizeof(g_achievement_icon.image_url), "%s&w=128&h=128&format=png", achievement->icon_url);
        download_achievement_icon_from_url(achievement->icon_url);
    } else {
        /* No achievement or empty URL: clear cached URL and force texture to be freed on the next render */
        g_achievement_icon.image_url[0]  = '\0';
        g_achievement_icon.image_path[0] = '\0';
        g_achievement_icon.must_reload   = true;
    }
}

//  --------------------------------------------------------------------------------------------------------------------
//	Source callbacks
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief OBS callback returning the configured source width.
 *
 * @param data Source instance data.
 * @return Width in pixels (200).
 */
static uint32_t source_get_width(void *data) {
    const xbox_achievement_icon_source_t *s = data;
    return s->width;
}

/**
 * @brief OBS callback returning the configured source height.
 *
 * @param data Source instance data.
 * @return Height in pixels (200).
 */
static uint32_t source_get_height(void *data) {
    const xbox_achievement_icon_source_t *s = data;
    return s->height;
}

/**
 * @brief OBS callback returning the display name for this source type.
 *
 * @param unused Unused parameter.
 * @return Static string "Xbox Achievement (Icon)" displayed in OBS source list.
 */
static const char *source_get_name(void *unused) {

    UNUSED_PARAMETER(unused);

    return "Xbox Achievement (Icon)";
}

/**
 * @brief OBS callback creating a new achievement icon source instance.
 *
 * Allocates and initializes the source data structure with default dimensions.
 * The source is configured to display the icon at 200x200 pixels.
 *
 * @param settings Source settings (unused).
 * @param source   OBS source instance pointer.
 * @return Newly allocated xbox_achievement_icon_source_t structure, or NULL on failure.
 */
static void *on_source_create(obs_data_t *settings, obs_source_t *source) {

    UNUSED_PARAMETER(settings);

    xbox_achievement_icon_source_t *s = bzalloc(sizeof(*s));
    s->source                         = source;
    s->width                          = 200;
    s->height                         = 200;

    return s;
}

/**
 * @brief OBS callback destroying an achievement icon source instance.
 *
 * Frees the text rendering context and source data structure. Safe to call
 * with NULL data pointer.
 *
 * @param data Source instance data to destroy.
 */
static void on_source_destroy(void *data) {

    xbox_achievement_icon_source_t *source = data;

    if (!source) {
        return;
    }

    /* Free image resources */
    if (g_achievement_icon.image_texture) {
        obs_enter_graphics();
        gs_texture_destroy(g_achievement_icon.image_texture);
        obs_leave_graphics();
        g_achievement_icon.image_texture = NULL;
    }

    bfree(source);
}

/**
 * @brief OBS callback invoked when source settings are updated.
 *
 * Currently unused as the achievement icon source has no editable settings.
 *
 * @param data Source instance data (unused).
 * @param settings Updated OBS settings data (unused).
 */
static void on_source_update(void *data, obs_data_t *settings) {

    UNUSED_PARAMETER(settings);
    UNUSED_PARAMETER(data);
}

/**
 * @brief OBS callback to render the achievement icon image.
 *
 * Loads a new texture if required and draws it using draw_texture().
 * The texture is lazily loaded from the downloaded icon file on the first
 * render after an achievement unlock.
 *
 * @param data   Source instance data containing width and height.
 * @param effect Effect to use when rendering. If NULL, OBS default effect is used.
 */
static void on_source_video_render(void *data, gs_effect_t *effect) {

    xbox_achievement_icon_source_t *source = data;

    if (!source) {
        return;
    }

    /* Load image if needed (deferred load in graphics context) */
    load_texture_from_file();

    /* Render the image if we have a texture */
    if (g_achievement_icon.image_texture) {
        draw_texture(g_achievement_icon.image_texture, source->width, source->height, effect);
    }
}

/**
 * @brief OBS callback constructing the properties UI for the achievement icon source.
 *
 * Shows connection status to Xbox Live. Currently provides no editable settings.
 *
 * @param data Source instance data (unused).
 * @return Newly created obs_properties_t structure containing the UI controls.
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
        obs_properties_add_text(p, "connected_status_info", status, OBS_TEXT_INFO);
    } else {
        obs_properties_add_text(p,
                                "disconnected_status_info",
                                "You are not connected to your xbox account",
                                OBS_TEXT_INFO);
    }

    return p;
}

/**
 * @brief obs_source_info describing the Xbox Achievement Icon source.
 *
 * Defines the OBS source type for displaying achievement icons. This structure
 * specifies the source ID, type, capabilities, and callback functions for all
 * OBS lifecycle events (creation, destruction, rendering, property management).
 */
static struct obs_source_info xbox_achievement_icon_source_info = {
    .id             = "xbox_achievement_icon_source",
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
 * @brief Get the obs_source_info for registration.
 *
 * Returns a pointer to the static source info structure for use with obs_register_source().
 *
 * @return Pointer to the xbox_achievement_icon_source_info structure.
 */
static const struct obs_source_info *xbox_achievement_icon_source_get(void) {
    return &xbox_achievement_icon_source_info;
}

//  --------------------------------------------------------------------------------------------------------------------
//	Public functions
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Register the Xbox Achievement Icon source with OBS.
 *
 * Initializes the achievement icon source by:
 * - Registering the source type with OBS
 * - Subscribing to Xbox monitor callbacks for connection changes and achievement progress
 *
 * This function should be called once during plugin initialization to make the source
 * available in OBS. The source will automatically update when achievements are unlocked.
 *
 * @note The global g_achievement_icon cache persists for the lifetime of the plugin.
 */
void xbox_achievement_icon_source_register(void) {

    obs_register_source(xbox_achievement_icon_source_get());

    xbox_subscribe_connected_changed(&on_connection_changed);
    xbox_subscribe_achievements_progressed(&on_achievements_progressed);
}
