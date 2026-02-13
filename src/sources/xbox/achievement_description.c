#include "sources/xbox/achievement_description.h"

#include "sources/common/achievement_cycle.h"
#include "sources/common/text_source.h"
#include "common/achievement.h"
#include "drawing/text.h"

#include <graphics/graphics.h>
#include <obs-module.h>
#include <diagnostics/log.h>

#include "io/state.h"

#define NO_FLIP 0

static char g_achievement_description[512];
static bool g_must_reload;

/**
 * @brief Configuration for rendering the achievement description text.
 *
 * Stored as a module-global pointer and initialized during
 * xbox_achievement_description_source_register(). Contains font path, size, and color settings.
 */
static achievement_description_configuration_t *g_configuration;
static bool                                     g_is_achievement_unlocked = false;

/**
 * @brief Update and store the achievement description string.
 *
 * Extracts and stores the description text from the achievement. Sets the global
 * reload flag to trigger a text context refresh on the next render.
 *
 * @param achievement Achievement data to extract description from. If NULL, this function returns early.
 */
static void update_achievement_description(const achievement_t *achievement) {

    if (!achievement) {
        return;
    }

    bool was_unlocked         = g_is_achievement_unlocked;
    g_is_achievement_unlocked = achievement->unlocked_timestamp != 0;

    /* Force reload if the unlock state changed (color will be different) */
    if (was_unlocked != g_is_achievement_unlocked) {
        g_must_reload = true;
    }

    snprintf(g_achievement_description, sizeof(g_achievement_description), "%s", achievement->description);
    g_must_reload = true;
}

/**
 * @brief Achievement cycle callback invoked when the displayed achievement changes.
 *
 * Updates the achievement description display with the new achievement. This is called
 * by the shared achievement cycle module whenever the display should change.
 *
 * @param achievement Achievement to display. If NULL, this function returns early.
 */
static void on_achievement_changed(const achievement_t *achievement) {

    update_achievement_description(achievement);
}

//  --------------------------------------------------------------------------------------------------------------------
//	Source callbacks
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief OBS callback creating a new achievement description source instance.
 *
 * Allocates and initializes the source data structure with default dimensions.
 * The source is configured to display text at 600x200 pixels.
 *
 * @param settings Source settings (unused).
 * @param source   OBS source instance pointer.
 * @return Newly allocated text_source_base_t structure, or NULL on failure.
 */
static void *on_source_create(obs_data_t *settings, obs_source_t *source) {

    UNUSED_PARAMETER(settings);

    return text_source_create(source);
}

/**
 * @brief OBS callback destroying an achievement description source instance.
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

    text_source_destroy(source);
}

/**
 * @brief OBS callback returning the natural text width.
 *
 * @param data Source instance data.
 * @return Width in pixels.
 */
static uint32_t source_get_width(void *data) {
    text_source_base_t *s = data;
    return text_source_get_width(s);
}

/**
 * @brief OBS callback returning the natural text height.
 *
 * @param data Source instance data.
 * @return Height in pixels.
 */
static uint32_t source_get_height(void *data) {
    text_source_base_t *s = data;
    return text_source_get_height(s);
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
 * @param settings Updated OBS settings data.
 */
static void on_source_update(void *data, obs_data_t *settings) {

    UNUSED_PARAMETER(data);

    text_source_update_properties(settings, (text_source_config_t *)g_configuration, &g_must_reload);

    state_set_achievement_description_configuration(g_configuration);
}

/**
 * @brief OBS callback to render the achievement description text.
 *
 * Lazily initializes or recreates the text rendering context when needed (on first
 * render or after configuration changes). Draws the achievement description string
 * using the configured font, size, and color.
 *
 * The text context is recreated when:
 * - It hasn't been created yet (first render)
 * - Settings have changed (g_must_reload flag is set)
 * - Achievement description has been updated
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

    if (!text_source_reload(source, &g_must_reload, &render_config, g_achievement_description)) {
        return;
    }

    text_source_render(source, effect);
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
    text_source_tick(source, &render_config, seconds);

    /* Update the shared achievement display cycle */
    achievement_cycle_tick(seconds);
}

/**
 * @brief OBS callback constructing the properties UI for the achievement description source.
 *
 * Creates a properties panel with the following controls:
 * - Font dropdown: Lists all available system fonts
 * - Text color picker: RGBA color selector for unlocked achievements
 * - Locked achievement color picker: RGBA color selector for locked achievements
 * - Text size slider: Integer value from 10 to 164 pixels
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
 * @return Static string "Xbox Achievement (Description)" displayed in OBS source list.
 */
static const char *source_get_name(void *unused) {
    UNUSED_PARAMETER(unused);

    return "Xbox Achievement (Description)";
}

/**
 * @brief obs_source_info describing the Xbox Achievement Description source.
 *
 * Defines the OBS source type for displaying achievement descriptions. This structure
 * specifies the source ID, type, capabilities, and callback functions for all
 * OBS lifecycle events (creation, destruction, rendering, property management).
 */
static struct obs_source_info xbox_achievement_description_source = {
    .id             = "xbox_achievement_description_source",
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
 * @return Pointer to the xbox_achievement_description_source structure.
 */
static const struct obs_source_info *xbox_source_get(void) {
    return &xbox_achievement_description_source;
}

//  --------------------------------------------------------------------------------------------------------------------
//	Public functions
//  --------------------------------------------------------------------------------------------------------------------

void xbox_achievement_description_source_register(void) {

    g_configuration = state_get_achievement_description_configuration();

    /* Force reset font if it looks like a path or is empty */
    if (!g_configuration->font_path ||
        strlen(g_configuration->font_path) == 0 ||
        strchr(g_configuration->font_path, '/') != NULL ||
        strstr(g_configuration->font_path, ".pdf") ||
        strstr(g_configuration->font_path, ".png") ||
        strstr(g_configuration->font_path, ".ttf") ||
        strstr(g_configuration->font_path, ".otf") ||
        strstr(g_configuration->font_path, "Downloads")) {

        if (g_configuration->font_path) bfree((void*)g_configuration->font_path);
        g_configuration->font_path = bstrdup("Arial");
        obs_log(LOG_INFO, "[Achievement Description] Resetting font to 'Arial' to ensure visibility");
    }

    // Ensure config is saved
    state_set_achievement_description_configuration(g_configuration);

    obs_register_source(xbox_source_get());

    achievement_cycle_subscribe(&on_achievement_changed);
}
