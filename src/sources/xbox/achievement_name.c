/**
 * @file achievement_name.c
 * @brief OBS source for displaying Xbox achievement names.
 *
 * This module implements an OBS video source that renders the current achievement's
 * name and gamerscore as text (e.g., "50G - Master Explorer"). The source automatically
 * updates when the achievement cycle selects a new achievement to display.
 *
 * Key features:
 * - Automatic text updates via achievement_cycle subscription
 * - Configurable font, size, and colors (separate for locked/unlocked states)
 * - Fade transitions when the displayed achievement changes
 * - Persistent configuration via state management
 *
 * Architecture:
 * - Global configuration (g_configuration) stores user preferences
 * - Cached render config (g_render_config) avoids per-frame reconstruction
 * - Achievement cycle module manages which achievement to display
 *
 * @see achievement_cycle.h for the shared achievement rotation logic
 * @see text_source.h for the common text rendering infrastructure
 */

#include "sources/xbox/achievement_name.h"

#include "sources/common/achievement_cycle.h"
#include "sources/common/text_source.h"
#include "common/achievement.h"

#include <graphics/graphics.h>
#include <obs-module.h>
#include <diagnostics/log.h>

#include "io/state.h"

#define NO_FLIP 0

/**
 * @brief Buffer holding the formatted achievement name string.
 *
 * Contains the display text in the format "XG - Achievement Name" where X is the
 * gamerscore value. Updated by update_achievement_name() when achievements change.
 */
static char g_achievement_name[512];

/**
 * @brief Flag indicating the text context needs to be recreated.
 *
 * Set to true when:
 * - Achievement name changes
 * - Achievement unlock state changes (affects color)
 * - User modifies source settings (font, size, colors)
 *
 * Cleared by text_source_update_text() after the context is recreated.
 */
static bool g_must_reload;

/**
 * @brief User configuration for achievement name rendering.
 *
 * Module-global pointer initialized during xbox_achievement_name_source_register().
 * Contains font face, font size, and color settings for both locked and unlocked
 * achievement states. This configuration is loaded from persistent storage and
 * updated when the user modifies source properties in OBS.
 *
 * @note The pointer is owned by the state management system; do not free directly.
 */
static achievement_name_configuration_t *g_configuration;

/**
 * @brief Tracks whether the currently displayed achievement is unlocked.
 *
 * Used to determine which color scheme (active vs inactive) to apply when rendering.
 * When this state changes, g_must_reload is set to trigger a text context refresh
 * with the appropriate colors.
 */
static bool g_is_achievement_unlocked = false;

/**
 * @brief Cached render configuration derived from g_configuration.
 *
 * Updated by update_render_config() whenever g_configuration changes. This avoids
 * reconstructing the configuration structure on every frame (60+ fps), improving
 * performance. Used by on_source_video_render() and on_source_video_tick().
 */
static text_source_config_t g_render_config;

/**
 * @brief Synchronize the cached render config with the global configuration.
 *
 * Copies all rendering parameters from g_configuration to g_render_config. This
 * optimization prevents reconstructing the configuration structure on every frame
 * (60+ fps). Must be called whenever g_configuration is modified:
 * - During source initialization
 * - When user settings change via the OBS properties UI
 *
 * @pre g_configuration must be non-NULL and fully initialized.
 * @post g_render_config reflects the current g_configuration values.
 */
static void update_render_config(void) {
    g_render_config.font_face             = g_configuration->font_face;
    g_render_config.font_size             = g_configuration->font_size;
    g_render_config.active_top_color      = g_configuration->active_top_color;
    g_render_config.active_bottom_color   = g_configuration->active_bottom_color;
    g_render_config.inactive_top_color    = g_configuration->inactive_top_color;
    g_render_config.inactive_bottom_color = g_configuration->inactive_bottom_color;
}

/**
 * @brief Format and store the achievement name string for display.
 *
 * Constructs the display text in one of these formats:
 * - "XG - Achievement Name" (if gamerscore reward is present)
 * - "Achievement Name" (if no gamerscore reward)
 *
 * Also tracks the unlock state to apply the correct color scheme:
 * - Unlocked achievements use active colors
 * - Locked achievements use inactive colors
 *
 * @param achievement Achievement data to format. If NULL, returns immediately.
 *
 * @post g_achievement_name contains the formatted string.
 * @post g_is_achievement_unlocked reflects the achievement's unlock state.
 * @post g_must_reload is set to true, triggering a text context refresh.
 */
static void update_achievement_name(const achievement_t *achievement) {

    if (!achievement) {
        return;
    }

    bool was_unlocked         = g_is_achievement_unlocked;
    g_is_achievement_unlocked = achievement->unlocked_timestamp != 0;

    /* Force reload if the unlocked state changed (color will be different) */
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
 * @brief Callback invoked by the achievement cycle when the display should update.
 *
 * Registered via achievement_cycle_subscribe() during source initialization.
 * Called whenever the shared achievement rotation selects a new achievement
 * to display across all subscribed sources.
 *
 * @param achievement New achievement to display. If NULL, returns immediately.
 *
 * @see achievement_cycle_subscribe()
 */
static void on_achievement_changed(const achievement_t *achievement) {

    update_achievement_name(achievement);
}

//  --------------------------------------------------------------------------------------------------------------------
//  OBS Source Callbacks
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

    return text_source_create(source, "Achievement name");
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

    text_source_t *source = data;

    if (!source) {
        return;
    }

    text_source_destroy(source);
}

/**
 * @brief OBS callback returning the natural text width.
 *
 * Queries the actual rendered text width from the FreeType source.
 * This allows OBS to scale the source properly without distortion.
 *
 * @param data Source instance data.
 * @return Text width in pixels, or 0 if no text is rendered.
 */
static uint32_t source_get_width(void *data) {
    text_source_t *s = data;
    return text_source_get_width(s);
}

/**
 * @brief OBS callback returning the natural text height.
 *
 * Queries the actual rendered text height from the FreeType source.
 * This allows OBS to scale the source properly without distortion.
 *
 * @param data Source instance data.
 * @return Text height in pixels, or 0 if no text is rendered.
 */
static uint32_t source_get_height(void *data) {
    text_source_t *s = data;
    return text_source_get_height(s);
}

/**
 * @brief OBS callback invoked when source settings are updated.
 *
 * Processes change to text color, size, font, and alignment from the OBS properties UI.
 * When settings change, updates the global configuration, refreshes the cached render
 * config, and triggers a text context reload. The updated configuration is persisted
 * to disk via the state management system.
 *
 * @param data Source instance data (unused).
 * @param settings Updated OBS settings data containing text_color, text_size, text_font, and text_align.
 */
static void on_source_update(void *data, obs_data_t *settings) {

    UNUSED_PARAMETER(data);

    text_source_update_properties(settings, (text_source_config_t *)g_configuration, &g_must_reload);

    update_render_config();

    state_set_achievement_name_configuration(g_configuration);
}

/**
 * @brief OBS callback to render the achievement name text.
 *
 * Lazily initializes or recreates the text rendering context when needed (on first
 * render or after configuration changes). Draws the formatted achievement name string
 * using the cached render configuration (g_render_config).
 *
 * The text context is recreated when:
 * - It hasn't been created yet (first render)
 * - Settings have changed (g_must_reload flag is set)
 * - Achievement name has been updated
 *
 * Performance: Uses cached g_render_config to avoid reconstructing the configuration
 * struct on every frame.
 *
 * @param data   Source instance data containing width and height.
 * @param effect Effect to use when rendering. If NULL, OBS default effect is used.
 */
static void on_source_video_render(void *data, gs_effect_t *effect) {

    text_source_t *source = data;

    if (!source) {
        return;
    }

    bool use_active_color = g_is_achievement_unlocked;
    bool updated =
        text_source_update_text(source, &g_must_reload, &g_render_config, g_achievement_name, use_active_color);

    if (!updated) {
        return;
    }

    text_source_render(source, &g_render_config, effect);
}

/**
 * @brief OBS callback for animation tick.
 *
 * Updates fade transition animations using the cached render configuration and
 * delegates achievement display cycle management to the shared achievement_cycle module.
 *
 * Performance: Uses cached g_render_config to avoid reconstructing the configuration
 * struct on every frame.
 *
 * @param data    Source instance data.
 * @param seconds Time elapsed since last tick in seconds.
 */
static void on_source_video_tick(void *data, float seconds) {

    text_source_t *source = data;

    if (!source) {
        return;
    }

    /* Update fade transition animations */
    text_source_tick(source, &g_render_config, seconds);

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
    text_source_add_properties(p, true);

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
//  Public API
//  --------------------------------------------------------------------------------------------------------------------

void xbox_achievement_name_source_register(void) {

    g_configuration = state_get_achievement_name_configuration();
    state_set_achievement_name_configuration(g_configuration);

    update_render_config();

    obs_register_source(xbox_source_get());

    achievement_cycle_subscribe(&on_achievement_changed);
}

void xbox_achievement_name_source_cleanup(void) {
    state_free_achievement_name_configuration(&g_configuration);
}
