#include "sources/xbox/gamerscore.h"

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
    obs_source_t *source;
    uint32_t      width;
    uint32_t      height;
} xbox_account_source_t;

static int64_t                     g_gamerscore = 0;
static gamerscore_configuration_t *g_default_configuration;

gs_image_file_t g_font_sheet_image;

//  --------------------------------------------------------------------------------------------------------------------
//	Private functions
//  --------------------------------------------------------------------------------------------------------------------

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

static void update_gamerscore(const gamerscore_t *gamerscore) {

    g_gamerscore = gamerscore_compute(gamerscore);

    obs_log(LOG_INFO, "Gamerscore is %" PRId64, g_gamerscore);
}

static void on_connection_changed(bool is_connected, const gamerscore_t *gamerscore, const char *error_message) {

    UNUSED_PARAMETER(error_message);

    if (!is_connected) {
        return;
    }

    update_gamerscore(gamerscore);
}

static void on_achievements_progressed(const gamerscore_t *gamerscore, const achievement_progress_t *progress) {

    UNUSED_PARAMETER(progress);

    update_gamerscore(gamerscore);
}

//  --------------------------------------------------------------------------------------------------------------------
//	Source callbacks
//  --------------------------------------------------------------------------------------------------------------------

static void *on_source_create(obs_data_t *settings, obs_source_t *source) {

    UNUSED_PARAMETER(settings);

    xbox_account_source_t *s = bzalloc(sizeof(*s));
    s->source                = source;
    s->width                 = 800;
    s->height                = 200;

    return s;
}

static void on_source_destroy(void *data) {

    xbox_account_source_t *source = data;

    if (!source) {
        return;
    }

    bfree(source);
}

static uint32_t source_get_width(void *data) {
    const xbox_account_source_t *s = data;
    return s->width;
}

static uint32_t source_get_height(void *data) {
    const xbox_account_source_t *s = data;
    return s->height;
}

static void on_source_update(void *data, obs_data_t *settings) {
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(settings);
}

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

static const char *source_get_name(void *unused) {
    UNUSED_PARAMETER(unused);

    return "Xbox Gamerscore";
}

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

static const struct obs_source_info *xbox_source_get(void) {
    return &xbox_gamerscore_source;
}

//  --------------------------------------------------------------------------------------------------------------------
//	Public functions
//  --------------------------------------------------------------------------------------------------------------------

/**
 *
 */
void xbox_gamerscore_source_register(void) {

    g_default_configuration                  = bzalloc(sizeof(gamerscore_configuration_t));
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
