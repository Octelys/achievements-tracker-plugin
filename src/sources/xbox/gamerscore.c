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

#include "sources/common/text_source.h"
#include "drawing/text.h"

#include <graphics/graphics.h>
#include <obs-module.h>
#include <diagnostics/log.h>

#include "io/state.h"
#include "oauth/xbox-live.h"
#include "xbox/xbox_monitor.h"

#define NO_FLIP 0

static char            g_gamerscore[64];
static bool            g_must_reload;
static text_context_t *g_text_context;

/**
 * @brief Configuration for rendering digits from the font sheet.
 *
 * Stored as a module-global pointer and initialized during
 * xbox_gamerscore_source_register().
 */
static gamerscore_configuration_t *g_default_configuration;

/**
 * @brief Recompute and store the latest gamerscore.
 *
 * @param gamerscore Gamerscore snapshot received from the Xbox monitor.
 */
static void update_gamerscore(const gamerscore_t *gamerscore) {

    int total_gamerscore = gamerscore_compute(gamerscore);

    //  Computes the total gamerscore and activate the switch to reload the texture with the new number.
    snprintf(g_gamerscore, sizeof(g_gamerscore), "%dG", total_gamerscore);
    g_must_reload = true;

    obs_log(LOG_INFO, "Gamerscore is %" PRId64, total_gamerscore);
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

    return text_source_create(source, (source_size_t){600, 200});
}

/**
 * @brief OBS callback destroying a gamerscore source instance.
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
 * @brief OBS callback invoked when settings change.
 *
 * Currently unused.
 */
static void on_source_update(void *data, obs_data_t *settings) {

    UNUSED_PARAMETER(data);

    text_source_update_properties(settings, (text_source_config_t *)g_default_configuration, &g_must_reload);

    state_set_gamerscore_configuration(g_default_configuration);
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

    text_source_base_t *source = data;

    if (!source) {
        return;
    }

    if (!text_source_reload(&g_text_context,
                                      &g_must_reload,
                                      (const text_source_config_t *)g_default_configuration,
                                      source,
                                      g_gamerscore)) {
        return;
    }

    text_source_render(g_text_context, source, effect);
}

/**
 * @brief OBS callback for animation tick.
 *
 * Updates fade transition animations.
 */
static void on_source_video_tick(void *data, float seconds) {

    text_source_base_t *source = data;

    if (!source) {
        return;
    }

    text_source_tick(source,
                     &g_text_context,
                     (const text_source_config_t *)g_default_configuration,
                     seconds);
}

/**
 * @brief OBS callback constructing the properties UI.
 *
 * Exposes configuration of the font sheet path and digit glyph metrics.
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
    .video_tick     = on_source_video_tick,
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

void xbox_gamerscore_source_register(void) {

    g_default_configuration = bzalloc(sizeof(gamerscore_configuration_t));

    g_default_configuration = state_get_gamerscore_configuration();

    /* TODO A default font sheet path should be embedded with the plugin */
    if (!g_default_configuration->font_path) {
        g_default_configuration->font_path = "/Users/christophe/Downloads/font_sheet.png";
    }

    obs_register_source(xbox_source_get());

    xbox_subscribe_connected_changed(&on_connection_changed);
    xbox_subscribe_achievements_progressed(&on_achievements_progressed);
}
