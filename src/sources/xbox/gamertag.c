#include "sources/xbox/gamertag.h"

/**
 * @file gamertag.c
 * @brief OBS source that renders the currently authenticated Xbox account's gamertag.
 *
 * This source displays the gamertag (username) of the connected Xbox account.
 *
 * Data flow:
 *  - The Xbox monitor notifies this module when connection state changes.
 *  - The module retrieves the gamertag from the Xbox identity and stores it.
 *  - During rendering, the gamertag is drawn using the text rendering system.
 *
 * Threading notes:
 *  - Event handlers may be invoked from non-graphics threads.
 *  - Texture creation must happen on the OBS graphics thread; this file lazily
 *    initializes the texture in the video_render callback.
 */

#include "sources/common/text_source.h"
#include "drawing/text.h"

#include <graphics/graphics.h>
#include <obs-module.h>
#include <diagnostics/log.h>

#include "io/state.h"
#include "oauth/xbox-live.h"
#include "xbox/xbox_monitor.h"

#define NO_FLIP 0

static char            g_gamertag[256];
static bool            g_must_reload;
static text_context_t *g_text_context;

/**
 * @brief Configuration for rendering the gamertag text.
 *
 * Stored as a module-global pointer and initialized during
 * xbox_gamertag_source_register().
 */
static gamertag_configuration_t *g_configuration;

/**
 * @brief Update and store the gamertag string.
 *
 * Retrieves the gamertag from the Xbox identity and stores it. Sets the global
 * reload flag to trigger a text context refresh on the next render.
 */
static void update_gamertag(void) {

    xbox_identity_t *identity = xbox_live_get_identity();

    if (!identity || !identity->gamertag) {
        snprintf(g_gamertag, sizeof(g_gamertag), "Not connected");
    } else {
        snprintf(g_gamertag, sizeof(g_gamertag), "%s", identity->gamertag);
    }

    g_must_reload = true;

    obs_log(LOG_INFO, "Gamertag updated: %s", g_gamertag);
}

/**
 * @brief Xbox monitor callback invoked when Xbox Live connection state changes.
 *
 * Retrieves the current gamertag and updates the display.
 *
 * @param is_connected Whether the Xbox account is currently connected.
 * @param error_message Optional error message if disconnected (unused).
 */
static void on_connection_changed(bool is_connected, const char *error_message) {

    UNUSED_PARAMETER(is_connected);
    UNUSED_PARAMETER(error_message);

    update_gamertag();
}

//  --------------------------------------------------------------------------------------------------------------------
//	Source callbacks
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief OBS callback creating a new gamertag source instance.
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

    return text_source_create(source, (source_size_t){600, 200});
}

/**
 * @brief OBS callback destroying a gamertag source instance.
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

/** @brief OBS callback returning the configured source width. */
static uint32_t source_get_width(void *data) {
    const text_source_base_t *s = data;
    return s ? s->size.width : 0;
}

/** @brief OBS callback returning the configured source height. */
static uint32_t source_get_height(void *data) {
    const text_source_base_t *s = data;
    return s ? s->size.height : 0;
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

    state_set_gamertag_configuration(g_configuration);
}

/**
 * @brief OBS callback to render the gamertag text.
 *
 * Lazily initializes or recreates the text rendering context when needed (on first
 * render or after configuration changes). Draws the gamertag string using the
 * configured font, size, and color.
 *
 * The text context is recreated when:
 * - It hasn't been created yet (first render)
 * - Settings have changed (g_must_reload flag is set)
 * - Gamertag has been updated
 *
 * @param data   Source instance data containing width and height.
 * @param effect Effect to use when rendering. If NULL, OBS default effect is used.
 */
static void on_source_video_render(void *data, gs_effect_t *effect) {

    text_source_base_t *source = data;

    if (!source) {
        return;
    }

    if (!text_source_reload_if_needed(&g_text_context,
                                      &g_must_reload,
                                      (const text_source_config_t *)g_configuration,
                                      source,
                                      g_gamertag)) {
        return;
    }

    text_source_render_unscaled(g_text_context, effect);
}

/**
 * @brief OBS callback constructing the properties UI for the gamertag source.
 *
 * Creates a properties panel with the following controls:
 * - Font dropdown: Lists all available system fonts
 * - Text color picker: RGBA color selector for the text
 * - Text size slider: Integer value from 10 to 164 pixels
 * - Text alignment dropdown: Left or Right alignment
 *
 * @param data Source instance data (unused).
 * @return Newly created obs_properties_t structure containing the UI controls.
 */
static obs_properties_t *source_get_properties(void *data) {

    UNUSED_PARAMETER(data);

    obs_properties_t *p = obs_properties_create();
    text_source_add_properties(p);

    return p;
}

/** @brief OBS callback returning the display name for this source type. */
static const char *source_get_name(void *unused) {
    UNUSED_PARAMETER(unused);

    return "Xbox Gamertag";
}

/**
 * @brief obs_source_info describing the Xbox Gamertag source.
 *
 * Defines the OBS source type for displaying gamertags. This structure
 * specifies the source ID, type, capabilities, and callback functions for all
 * OBS lifecycle events (creation, destruction, rendering, property management).
 */
static struct obs_source_info xbox_gamertag_source = {
    .id             = "xbox_gamertag_source",
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
 * @return Pointer to the xbox_gamertag_source structure.
 */
static const struct obs_source_info *xbox_source_get(void) {
    return &xbox_gamertag_source;
}

//  --------------------------------------------------------------------------------------------------------------------
//	Public functions
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Register the Xbox Gamertag source with OBS.
 *
 * Initializes the gamertag source by:
 * - Allocating and loading the configuration from persistent state
 * - Setting a default font path if none is configured
 * - Registering the source type with OBS
 * - Subscribing to Xbox monitor callbacks for connection changes
 *
 * This function should be called once during plugin initialization to make the source
 * available in OBS. The source will automatically update when the Xbox connection changes.
 */
void xbox_gamertag_source_register(void) {

    g_configuration = state_get_gamertag_configuration();

    /* Set a default font path if none is configured */
    if (!g_configuration->font_path || strlen(g_configuration->font_path) == 0) {
        g_configuration->font_path = "/System/Library/Fonts/Supplemental/Arial.ttf";
    }

    obs_register_source(xbox_source_get());

    xbox_subscribe_connected_changed(&on_connection_changed);
}
