#include "sources/xbox/achievement_name.h"

#include "sources/common/achievement_cycle.h"
#include "sources/common/text_source.h"
#include "common/achievement.h"
#include "drawing/text.h"

#include <graphics/graphics.h>
#include <obs-module.h>
#include <diagnostics/log.h>

#include "io/state.h"

#define NO_FLIP 0

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
static bool                              g_is_achievement_unlocked = false;

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

    bool was_unlocked         = g_is_achievement_unlocked;
    g_is_achievement_unlocked = achievement->unlocked_timestamp != 0;

    /* Force reload if the unlock state changed (color will be different) */
    if (was_unlocked != g_is_achievement_unlocked) {
        g_must_reload = true;
    }

    if (achievement->rewards && achievement->rewards->value) {
        snprintf(g_achievement_name,
                 sizeof(g_achievement_name),
                 "%sG - %s",
                 achievement->rewards->value,
                 achievement->name);
    } else {
        snprintf(g_achievement_name, sizeof(g_achievement_name), "%s", achievement->name);
    }

    g_must_reload = true;
}

/**
 * @brief Achievement cycle callback invoked when the displayed achievement changes.
 *
 * Updates the achievement name display with the new achievement. This is called
 * by the shared achievement cycle module whenever the display should change.
 *
 * @param achievement Achievement to display. If NULL, this function returns early.
 */
static void on_achievement_changed(const achievement_t *achievement) {

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
 * @return Newly allocated text_source_base_t structure, or NULL on failure.
 */
static void *on_source_create(obs_data_t *settings, obs_source_t *source) {

    UNUSED_PARAMETER(settings);

    return text_source_create(source, (source_size_t){600, 200});
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

    text_source_base_t *source = data;

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
    const text_source_base_t *s = data;
    return s ? s->size.width : 0;
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
    const text_source_base_t *s = data;
    return s ? s->size.height : 0;
}

/**
 * @brief OBS callback invoked when source settings are updated.
 *
 * Processes change to text color, size, font, and alignment from the OBS properties UI.
 * When settings change, updates the global configuration and triggers a text
 * context reload. The updated configuration is persisted to disk via the state
 * management system.
 *
 * @param data Source instance data (unused).
 * @param settings Updated OBS settings data containing text_color, text_size, text_font, and text_align.
 */
static void on_source_update(void *data, obs_data_t *settings) {

    UNUSED_PARAMETER(data);

    text_source_update_properties(settings, (text_source_config_t *)g_configuration, &g_must_reload);

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

    text_source_base_t *source = data;

    if (!source) {
        return;
    }

    /*
     * Use alternate color for locked achievements.
     * We create a temporary config with the appropriate color.
     */
    text_source_config_t render_config = {
        .font_path = g_configuration->font_path,
        .font_size = g_configuration->font_size,
        .color     = g_is_achievement_unlocked ? g_configuration->color : g_configuration->alternate_color,
        .align     = g_configuration->align,
    };

    bool texture_loaded =
        text_source_reload(&g_text_context, &g_must_reload, &render_config, source, g_achievement_name);

    if (!texture_loaded) {
        return;
    }

    text_source_render(g_text_context, source, effect);
}

/**
 * @brief OBS callback for animation tick.
 *
 * Updates fade transition animations and delegates achievement display cycle
 * management to the shared achievement_cycle module.
 */
static void on_source_video_tick(void *data, float seconds) {

    text_source_base_t *source = data;

    if (!source) {
        return;
    }

    /*
     * Use alternate color for locked achievements.
     * This config must match the one used in on_source_video_render.
     */
    text_source_config_t render_config = {
        .font_path = g_configuration->font_path,
        .font_size = g_configuration->font_size,
        .color     = g_is_achievement_unlocked ? g_configuration->color : g_configuration->alternate_color,
        .align     = g_configuration->align,
    };

    /* Update fade transition animations */
    text_source_tick(source, &g_text_context, &render_config, seconds);

    /* Update the shared achievement display cycle */
    achievement_cycle_tick(seconds);
}

/**
 * @brief OBS callback constructing the properties UI for the achievement name source.
 *
 * Creates a properties panel with the following controls:
 * - Font dropdown: Lists all available system fonts
 * - Text color picker: RGBA color selector for unlocked achievements
 * - Locked achievement color picker: RGBA color selector for locked achievements
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

    obs_properties_t *p = obs_properties_create();
    text_source_add_properties(p);
    text_source_add_alternate_color_property(p);

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
    .video_tick     = on_source_video_tick,
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

void xbox_achievement_name_source_register(void) {

    g_configuration = bzalloc(sizeof(gamerscore_configuration_t));

    g_configuration = state_get_achievement_name_configuration();

    /* TODO A default font sheet path should be embedded with the plugin */
    if (!g_configuration->font_path) {
        g_configuration->font_path = "/Users/christophe/Downloads/font_sheet.png";
    }

    obs_register_source(xbox_source_get());

    achievement_cycle_subscribe(&on_achievement_changed);
}
