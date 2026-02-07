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

#include "drawing/text.h"

#include <graphics/graphics.h>
#include <graphics/image-file.h>
#include <obs-module.h>
#include <diagnostics/log.h>
#include <curl/curl.h>
#include <util/platform.h>

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

static char g_gamerscore[64];
static bool g_must_reload;
static text_context_t *g_text_context;

/**
 * @brief Configuration for rendering digits from the font sheet.
 *
 * Stored as a module-global pointer and initialized during
 * xbox_gamerscore_source_register().
 */
static gamerscore_configuration_t *g_default_configuration;

static uint32_t color_argb_to_rgba(uint32_t argb) {
    const uint8_t a = (uint8_t)((argb >> 24) & 0xFF);
    const uint8_t r = (uint8_t)((argb >> 16) & 0xFF);
    const uint8_t g = (uint8_t)((argb >> 8) & 0xFF);
    const uint8_t b = (uint8_t)(argb & 0xFF);
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | a;
}

/**
 * @brief Recompute and store the latest gamerscore.
 *
 * @param gamerscore Gamerscore snapshot received from the Xbox monitor.
 */
static void update_gamerscore(const gamerscore_t *gamerscore) {

    int value = gamerscore_compute(gamerscore);

    snprintf(g_gamerscore, sizeof(g_gamerscore), "%d", value);
    g_must_reload = true;

    obs_log(LOG_INFO, "Gamerscore is %" PRId64, value);
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

    UNUSED_PARAMETER(is_connected);
    UNUSED_PARAMETER(error_message);

    const gamerscore_t *gamerscore = get_current_gamerscore();

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

    if (g_text_context) {
        text_context_destroy(g_text_context);
        g_text_context = NULL;
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

    if (obs_data_has_user_value(settings, "text_color")) {
        const uint32_t argb = (uint32_t)obs_data_get_int(settings, "text_color");
        --> Do not work: colors are not the same.
        g_default_configuration->color = color_argb_to_rgba(argb);
        g_must_reload = true;
    }
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

    xbox_account_source_t *source = data;

    if (!source) {
        return;
    }

    if (g_must_reload || !g_text_context) {

        if (g_text_context) {
            text_context_destroy(g_text_context);
            g_text_context = NULL;
        }

        g_text_context = text_context_create(
            "/Library/Fonts/SF-Pro.ttf",
            source->width,
            source->height,
            g_gamerscore,
            128,
            g_default_configuration->color
        );

        g_must_reload = false;
    }

    if (!g_text_context) {
       return;
    }

    text_context_draw(g_text_context, effect);
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

    obs_properties_add_color(p, "text_color", "Text color");

    /*
    obs_properties_add_path(p,
                            "font_sheet_path",  // setting key
                            "Font sheet image", // display name
                            OBS_PATH_FILE,      // file chooser
                            "Image Files (*.png *.jpg *.jpeg);;All Files (*.*)",
                            NULL // default path (optional)
    );
    */

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

    obs_register_source(xbox_source_get());

    xbox_subscribe_connected_changed(&on_connection_changed);
    xbox_subscribe_achievements_progressed(&on_achievements_progressed);
}
