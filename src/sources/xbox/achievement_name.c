#include "sources/xbox/achievement_name.h"

#include "drawing/text.h"

#include <graphics/graphics.h>
#include <graphics/image-file.h>
#include <graphics/matrix4.h>
#include <obs-module.h>
#include <diagnostics/log.h>
#include <curl/curl.h>
#include <util/platform.h>

#include "drawing/color.h"
#include "io/state.h"
#include "oauth/xbox-live.h"
#include "system/font.h"
#include "xbox/xbox_monitor.h"

#define NO_FLIP 0

typedef struct xbox_account_source {
    /** OBS source instance. */
    obs_source_t *source;

    /** Canvas width in pixels. */
    uint32_t width;

    /** Canvas height in pixels. */
    uint32_t height;

} xbox_account_source_t;

static char            g_achievement_name[512];
static bool            g_must_reload;
static text_context_t *g_text_context;

/**
 * @brief Configuration for rendering the achievement name text.
 *
 * Stored as a module-global pointer and initialized during
 * xbox_achievement_name_source_register(). Contains font path, size, and color settings.
 */
static achievement_name_configuration_t *g_configuration;

/**
 * @brief Update and store the formatted achievement name string.
 *
 * Formats the achievement information as "XG - Achievement Name" where X is the
 * gamerscore value from the first reward. Sets the global reload flag to trigger
 * a text context refresh on the next render.
 *
 * @param achievement Achievement data to format. If NULL, this function returns early.
 */
static void update_achievement_name(const achievement_t *achievement) {

    if (!achievement) {
        return;
    }

    snprintf(g_achievement_name, sizeof(g_achievement_name), "%sG - %s", achievement->rewards->value, achievement->name);
    g_must_reload = true;
}

/**
 * @brief Xbox monitor callback invoked when Xbox Live connection state changes.
 *
 * Retrieves the current game's most recent achievement and updates the display.
 * This ensures the achievement name source reflects the latest data when the
 * connection is established or re-established.
 *
 * @param is_connected Whether the Xbox account is currently connected (unused).
 * @param error_message Optional error message if disconnected (unused).
 */
static void on_connection_changed(bool is_connected, const char *error_message) {

    UNUSED_PARAMETER(is_connected);
    UNUSED_PARAMETER(error_message);

    const achievement_t *achievement = get_current_game_achievements();

    update_achievement_name(achievement);
}

/**
 * @brief Event handler called when a new game starts being played.
 *
 * Fetches the cover-art URL for the given game and triggers a download.
 *
 * @param game Currently played game information.
 */
static void on_xbox_game_played(const game_t *game) {

    UNUSED_PARAMETER(game);

    const achievement_t *achievement = get_current_game_achievements();

    update_achievement_name(achievement);
}

/**
 * @brief Xbox monitor callback invoked when achievement progress is updated.
 *
 * Retrieves the current game's most recent achievement and updates the display
 * to show the newly unlocked achievement. This callback ensures the source always
 * displays the latest achievement that was unlocked.
 *
 * @param gamerscore Updated gamerscore snapshot (unused).
 * @param progress   Achievement progress details (unused).
 */
static void on_achievements_progressed(const gamerscore_t *gamerscore, const achievement_progress_t *progress) {

    UNUSED_PARAMETER(gamerscore);
    UNUSED_PARAMETER(progress);

    const achievement_t *achievement = get_current_game_achievements();

    update_achievement_name(achievement);
}

//  --------------------------------------------------------------------------------------------------------------------
//	Source callbacks
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief OBS callback creating a new achievement name source instance.
 *
 * Allocates and initializes the source data structure with a default canvas size.
 * The canvas provides a fixed bounding box; text renders at actual size within it.
 *
 * @param settings Source settings (unused).
 * @param source   OBS source instance pointer.
 * @return Newly allocated xbox_account_source_t structure, or NULL on failure.
 */
static void *on_source_create(obs_data_t *settings, obs_source_t *source) {

    UNUSED_PARAMETER(settings);

    xbox_account_source_t *s = bzalloc(sizeof(*s));
    s->source                = source;
    s->width                 = 600;
    s->height                = 200;

    return s;
}

/**
 * @brief OBS callback destroying an achievement name source instance.
 *
 * Frees the text rendering context and source data structure. Safe to call
 * with NULL data pointer.
 *
 * @param data Source instance data to destroy.
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

/**
 * @brief OBS callback returning the source canvas width.
 *
 * Returns a fixed canvas width that provides stable dimensions for OBS transforms.
 * The text texture may be smaller and is drawn at actual size within this canvas.
 *
 * @param data Source instance data.
 * @return Canvas width in pixels.
 */
static uint32_t source_get_width(void *data) {
    const xbox_account_source_t *s = data;
    return s ? s->width : 16;
}

/**
 * @brief OBS callback returning the source canvas height.
 *
 * Returns a fixed canvas height that provides stable dimensions for OBS transforms.
 * The text texture may be smaller and is drawn at actual size within this canvas.
 *
 * @param data Source instance data.
 * @return Canvas height in pixels.
 */
static uint32_t source_get_height(void *data) {
    const xbox_account_source_t *s = data;
    return s ? s->height : 16;
}

/**
 * @brief OBS callback invoked when source settings are updated.
 *
 * Processes changes to text color, size, font, and alignment from the OBS properties UI.
 * When settings change, updates the global configuration and triggers a text
 * context reload. The updated configuration is persisted to disk via the state
 * management system.
 *
 * @param data Source instance data (unused).
 * @param settings Updated OBS settings data containing text_color, text_size, text_font, and text_align.
 */
static void on_source_update(void *data, obs_data_t *settings) {

    UNUSED_PARAMETER(data);

    if (obs_data_has_user_value(settings, "text_color")) {
        const uint32_t argb    = (uint32_t)obs_data_get_int(settings, "text_color");
        g_configuration->color = color_argb_to_rgba(argb);
        g_must_reload          = true;
    }

    if (obs_data_has_user_value(settings, "text_size")) {
        g_configuration->size = (uint32_t)obs_data_get_int(settings, "text_size");
        g_must_reload         = true;
    }

    if (obs_data_has_user_value(settings, "text_font")) {
        g_configuration->font_path = obs_data_get_string(settings, "text_font");
        g_must_reload              = true;
    }

    if (obs_data_has_user_value(settings, "text_align")) {
        g_configuration->align = (uint32_t)obs_data_get_int(settings, "text_align");
        g_must_reload          = true;
    }

    state_set_achievement_name_configuration(g_configuration);
}

/**
 * @brief OBS callback to render the achievement name text.
 *
 * Lazily initializes or recreates the text rendering context when needed (on first
 * render or after configuration changes). Draws the formatted achievement name string
 * using the configured font, size, and color.
 *
 * The text context is recreated when:
 * - It hasn't been created yet (first render)
 * - Settings have changed (g_must_reload flag is set)
 * - Achievement name has been updated
 *
 * @param data   Source instance data containing width and height.
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

        // Create texture at exact source dimensions to prevent scaling.
        // Text renders at actual font size within this fixed canvas.
        g_text_context = text_context_create(g_configuration->font_path,
                                             source->width,
                                             source->height,
                                             g_achievement_name,
                                             g_configuration->size,
                                             g_configuration->color,
                                             (text_align_t)g_configuration->align);

        g_must_reload = false;
    }

    if (!g_text_context) {
        return;
    }

    // Get the current transformation matrix to extract translation
    struct matrix4 current_matrix;
    gs_matrix_get(&current_matrix);

    // Extract translation from the matrix
    float trans_x = current_matrix.t.x;
    float trans_y = current_matrix.t.y;

    // Build a new matrix: translation only (no scaling)
    gs_matrix_push();
    gs_matrix_identity();
    gs_matrix_translate3f(trans_x, trans_y, 0.0f);

    text_context_draw(g_text_context, effect);

    gs_matrix_pop();
}

/**
 * @brief OBS callback constructing the properties UI for the achievement name source.
 *
 * Creates a properties panel with the following controls:
 * - Font dropdown: Lists all available system fonts
 * - Text color picker: RGBA color selector for the text
 * - Text size slider: Integer value from 10 to 164 pixels
 * - Text alignment dropdown: Left or Right alignment
 *
 * Source dimensions automatically adjust to fit the rendered text.
 *
 * @param data Source instance data (unused).
 * @return Newly created obs_properties_t structure containing the UI controls.
 */
static obs_properties_t *source_get_properties(void *data) {

    UNUSED_PARAMETER(data);

    /* Lists all the UI components of the properties page */
    obs_properties_t *p = obs_properties_create();

    // Font dropdown.
    obs_property_t *font_list_prop =
        obs_properties_add_list(p, "text_font", "Font", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

    size_t  font_count = 0;
    font_t *fonts      = font_list_available(&font_count);

    if (fonts) {
        for (size_t i = 0; i < font_count; i++) {
            if (fonts[i].name && fonts[i].path) {
                // Display name shown in UI, path stored as value.
                obs_property_list_add_string(font_list_prop, fonts[i].name, fonts[i].path);
            }
        }
        font_list_free(fonts, font_count);
    }

    obs_properties_add_color(p, "text_color", "Text color");
    obs_properties_add_int(p, "text_size", "Text size", 10, 164, 1);

    // Text alignment dropdown.
    obs_property_t *align_list =
        obs_properties_add_list(p, "text_align", "Text alignment", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(align_list, "Left", TEXT_ALIGN_LEFT);
    obs_property_list_add_int(align_list, "Right", TEXT_ALIGN_RIGHT);

    return p;
}

/**
 * @brief OBS callback returning the display name for this source type.
 *
 * @param unused Unused parameter.
 * @return Static string "Xbox Achievement (Name)" displayed in OBS source list.
 */
static const char *source_get_name(void *unused) {
    UNUSED_PARAMETER(unused);

    return "Xbox Achievement (Name)";
}

/**
 * @brief obs_source_info describing the Xbox Achievement Name source.
 *
 * Defines the OBS source type for displaying achievement names. This structure
 * specifies the source ID, type, capabilities, and callback functions for all
 * OBS lifecycle events (creation, destruction, rendering, property management).
 */
static struct obs_source_info xbox_achievement_name_source = {
    .id             = "xbox_achievement_name_source",
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
 *
 * Returns a pointer to the static source info structure for use with obs_register_source().
 *
 * @return Pointer to the xbox_achievement_name_source structure.
 */
static const struct obs_source_info *xbox_source_get(void) {
    return &xbox_achievement_name_source;
}

//  --------------------------------------------------------------------------------------------------------------------
//	Public functions
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Register the Xbox Achievement Name source with OBS.
 *
 * Initializes the achievement name source by:
 * - Allocating and loading the configuration from persistent state
 * - Setting a default font path if none is configured
 * - Registering the source type with OBS
 * - Subscribing to Xbox monitor callbacks for connection changes and achievement progress
 *
 * This function should be called once during plugin initialization to make the source
 * available in OBS. The source will automatically update when achievements are unlocked.
 *
 * @note Allocates g_configuration which persists for the lifetime of the plugin.
 * @note TODO: A default font path should be embedded with the plugin rather than hardcoded.
 */
void xbox_achievement_name_source_register(void) {

    g_configuration = bzalloc(sizeof(gamerscore_configuration_t));

    g_configuration = state_get_achievement_name_configuration();

    /* TODO A default font sheet path should be embedded with the plugin */
    if (!g_configuration->font_path) {
        g_configuration->font_path = "/Users/christophe/Downloads/font_sheet.png";
    }

    obs_register_source(xbox_source_get());

    xbox_subscribe_connected_changed(&on_connection_changed);
    xbox_subscribe_game_played(&on_xbox_game_played);
    xbox_subscribe_achievements_progressed(&on_achievements_progressed);
}
