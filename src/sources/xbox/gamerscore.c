#include "sources/xbox/gamerscore.h"

/**
 * @file gamerscore.c
 * @brief OBS source that renders the currently authenticated Xbox account's gamerscore.
 *
 * This source displays a numeric gamerscore by drawing digits from a pre-baked
 * font sheet (atlas). Digits are extracted as subregions from the atlas texture
 * and drawn sequentially.
 *
 * Data flow:
 *  - The Xbox monitor notifies this module when connection state changes and/or
 *    achievement progress updates.
 *  - The module computes the latest gamerscore and stores it in a global.
 *  - During rendering, the current gamerscore is formatted to text and each
 *    digit is drawn from the font sheet texture.
 *
 * Threading notes:
 *  - Event handlers may be invoked from non-graphics threads.
 *  - Texture creation must happen on the OBS graphics thread; this file lazily
 *    initializes the texture in the video_render callback.
 */

#include <graphics/graphics.h>
#include <graphics/image-file.h>
#include <obs-module.h>
#include <diagnostics/log.h>
#include <curl/curl.h>

#include "oauth/xbox-live.h"
#include "xbox/xbox_client.h"
#include "xbox/xbox_monitor.h"

#define NO_FLIP 0

typedef struct xbox_account_source {
    /** OBS source instance. */
    obs_source_t *source;

    /** Output width in pixels. */
    uint32_t width;

    /** Output height in pixels. */
    uint32_t height;
} xbox_account_source_t;

/**
 * @brief Latest computed gamerscore for the authenticated account.
 */
static int64_t g_gamerscore = 0;

/**
 * @brief Configuration for rendering digits from the font sheet.
 *
 * Stored as a module-global pointer and initialized during
 * xbox_gamerscore_source_register().
 */
static gamerscore_configuration_t *g_default_configuration;

/**
 * @brief Loaded font sheet image/texture (digit atlas).
 *
 * gs_image_file_t holds decoded CPU-side image data and (after
 * gs_image_file_init_texture()) the GPU texture handle.
 */
gs_image_file_t g_font_sheet_image;

//  --------------------------------------------------------------------------------------------------------------------
//	Private functions
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Load (and decode) the configured font sheet image.
 *
 * This initializes g_font_sheet_image using the configured path.
 * Texture creation is deferred until rendering (see on_source_video_render()).
 */
static void load_font_sheet() {

    if (!g_default_configuration) {
        obs_log(LOG_ERROR, "No default configuration available for the font sheet");
        return;
    }

    obs_log(LOG_INFO, "Loading the font sheet from the configured path: %s", g_default_configuration->font_sheet_path);

    // Load an RGBA atlas image shipped with the plugin (prebaked glyphs).
    // OBS provides helpers for module files; loading the image into a texture
    // is usually done via gs_image_file_t.
    gs_image_file_init(&g_font_sheet_image, g_default_configuration->font_sheet_path);

    if (g_font_sheet_image.loaded) {
        obs_log(LOG_INFO, "The font sheet image has successfully been loaded");
    } else {
        obs_log(LOG_ERROR, "Unable to load the font sheet image");
        gs_image_file_free(&g_font_sheet_image);
    }
}

/**
 * @brief Recompute and store the latest gamerscore.
 *
 * @param gamerscore Gamerscore snapshot received from the Xbox monitor.
 */
static void update_gamerscore(const gamerscore_t *gamerscore) {

    g_gamerscore = gamerscore_compute(gamerscore);

    obs_log(LOG_INFO, "Gamerscore is %" PRId64, g_gamerscore);
}

/**
 * @brief Xbox monitor callback invoked when connection state changes.
 *
 * When connected, this refreshes the gamerscore display.
 *
 * @param is_connected Whether the account is currently connected.
 * @param gamerscore   Current gamerscore snapshot.
 * @param error_message Optional error message if disconnected (ignored here).
 */
static void on_connection_changed(bool is_connected, const gamerscore_t *gamerscore, const char *error_message) {

    UNUSED_PARAMETER(error_message);

    if (!is_connected) {
        return;
    }

    update_gamerscore(gamerscore);
}

/**
 * @brief Xbox monitor callback invoked when achievements progress.
 *
 * Recomputes the gamerscore based on the updated snapshot.
 *
 * @param gamerscore Updated gamerscore snapshot.
 * @param progress   Achievement progress details (unused).
 */
static void on_achievements_progressed(const gamerscore_t *gamerscore, const achievement_progress_t *progress) {

    UNUSED_PARAMETER(progress);

    update_gamerscore(gamerscore);
}

//  --------------------------------------------------------------------------------------------------------------------
//	Source callbacks
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief OBS callback creating a new gamerscore source instance.
 *
 * @param settings Source settings (unused).
 * @param source   OBS source instance.
 * @return Newly allocated xbox_account_source_t.
 */
static void *on_source_create(obs_data_t *settings, obs_source_t *source) {

    UNUSED_PARAMETER(settings);

    xbox_account_source_t *s = bzalloc(sizeof(*s));
    s->source                = source;
    s->width                 = 800;
    s->height                = 200;

    return s;
}

/**
 * @brief OBS callback destroying a gamerscore source instance.
 */
static void on_source_destroy(void *data) {

    xbox_account_source_t *source = data;

    if (!source) {
        return;
    }

    bfree(source);
}

/** @brief OBS callback returning the configured source width. */
static uint32_t source_get_width(void *data) {
    const xbox_account_source_t *s = data;
    return s->width;
}

/** @brief OBS callback returning the configured source height. */
static uint32_t source_get_height(void *data) {
    const xbox_account_source_t *s = data;
    return s->height;
}

/**
 * @brief OBS callback invoked when settings change.
 *
 * Currently unused.
 */
static void on_source_update(void *data, obs_data_t *settings) {
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(settings);
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
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(effect);

    //  Number 0 is at (x,y) (offset_x,offset_y)
    //  Number 1 is at (x,y) (offset_x + font_width,offset_y)
    //  Number 2 is at (x,y) (offset_x + 2 * font_width,offset_y)

    if (!g_default_configuration) {
        return;
    }

    if (!g_font_sheet_image.loaded) {
        return;
    }

    /* Ensures texture is loaded */
    if (!g_font_sheet_image.texture) {
        gs_image_file_init_texture(&g_font_sheet_image);
    }

    /* Prints the gamerscore in a string */
    char gamerscore_text[128];
    snprintf(gamerscore_text, sizeof(gamerscore_text), "%" PRId64, g_gamerscore);

    /* Retrieves the configured parameters of the font sheet */
    const uint32_t font_width  = g_default_configuration->font_width;
    const uint32_t font_height = g_default_configuration->font_height;
    const uint32_t offset_x    = g_default_configuration->offset_x;
    const uint32_t offset_y    = g_default_configuration->offset_y;

    /* Retrieves the texture of the font sheet */
    gs_texture_t *tex = g_font_sheet_image.texture;

    /* Draw using the stock "Draw" technique. */
    gs_effect_t *used_effect = effect ? effect : obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_eparam_t *image       = gs_effect_get_param_by_name(used_effect, "image");

    gs_effect_set_texture(image, tex);

    float x = (float)offset_x;
    float y = (float)offset_y;

    for (const char *p = gamerscore_text; *p; ++p) {

        if (*p < '0' || *p > '9') {
            continue;
        }

        const uint32_t digit = (uint32_t)(*p - '0');

        const uint32_t src_x = offset_x + digit * font_width;
        const uint32_t src_y = offset_y;

        // Draw subregion at current position.
        gs_matrix_push();
        gs_matrix_translate3f(x, y, 0.0f);
        gs_draw_sprite_subregion(tex, NO_FLIP, src_x, src_y, font_width, font_height);
        gs_matrix_pop();

        x += (float)font_width;
    }
}

/**
 * @brief OBS callback constructing the properties UI.
 *
 * Exposes configuration of the font sheet path and digit glyph metrics.
 */
static obs_properties_t *source_get_properties(void *data) {

    UNUSED_PARAMETER(data);

    /* Lists all the UI components of the properties page */
    obs_properties_t *p = obs_properties_create();

    obs_properties_add_path(p,
                            "font_sheet_path",  // setting key
                            "Font sheet image", // display name
                            OBS_PATH_FILE,      // file chooser
                            "Image Files (*.png *.jpg *.jpeg);;All Files (*.*)",
                            NULL // default path (optional)
    );

    obs_properties_add_text(p, "offset_x", "Initial X", OBS_TEXT_DEFAULT);
    obs_properties_add_text(p, "offset_y", "Initial Y", OBS_TEXT_DEFAULT);
    obs_properties_add_text(p, "font_width", "Font Width", OBS_TEXT_DEFAULT);
    obs_properties_add_text(p, "font_height", "Font Height", OBS_TEXT_DEFAULT);

    return p;
}

/** @brief OBS callback returning the display name for this source type. */
static const char *source_get_name(void *unused) {
    UNUSED_PARAMETER(unused);

    return "Xbox Gamerscore";
}

/**
 * @brief obs_source_info describing the Xbox Gamerscore source.
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
    .video_tick     = NULL,
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

/**
 * @brief Register the Xbox Gamerscore source with OBS.
 *
 * Registers the source type and subscribes to Xbox monitor events so the
 * displayed gamerscore stays up to date.
 */
void xbox_gamerscore_source_register(void) {

    g_default_configuration = bzalloc(sizeof(gamerscore_configuration_t));

    /* TODO A default font sheet path should be embedded with the plugin */
    g_default_configuration->font_sheet_path = "/Users/christophe/Downloads/font_sheet.png";
    g_default_configuration->offset_x        = 0;
    g_default_configuration->offset_y        = 0;
    g_default_configuration->font_width      = 148;
    g_default_configuration->font_height     = 226;

    obs_register_source(xbox_source_get());

    load_font_sheet();

    xbox_subscribe_connected_changed(&on_connection_changed);
    xbox_subscribe_achievements_progressed(&on_achievements_progressed);
}
