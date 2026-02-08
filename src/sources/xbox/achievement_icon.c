#include "sources/xbox/achievement_icon.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include "oauth/xbox-live.h"
#include "sources/common/image_source.h"
#include "xbox/xbox_client.h"
#include "xbox/xbox_monitor.h"

/**
 * @brief Global singleton achievement icon cache.
 *
 * This source is implemented as a singleton that stores the current achievement icon
 * in a global cache.
 */
static image_source_cache_t g_achievement_icon;

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
            image_source_download_if_changed(&g_achievement_icon, achievement->icon_url);
        } else {
            image_source_clear(&g_achievement_icon);
        }
    } else {
        image_source_clear(&g_achievement_icon);
    }
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

    if (achievement && achievement->icon_url) {
        image_source_download_if_changed(&g_achievement_icon, achievement->icon_url);
    } else {
        image_source_clear(&g_achievement_icon);
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
        image_source_download_if_changed(&g_achievement_icon, achievement->icon_url);
    } else {
        image_source_clear(&g_achievement_icon);
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
    const image_source_data_t *s = data;
    return s->size.width;
}

/**
 * @brief OBS callback returning the configured source height.
 *
 * @param data Source instance data.
 * @return Height in pixels (200).
 */
static uint32_t source_get_height(void *data) {
    const image_source_data_t *s = data;
    return s->size.height;
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
 * @return Newly allocated image_source_data_t structure, or NULL on failure.
 */
static void *on_source_create(obs_data_t *settings, obs_source_t *source) {

    UNUSED_PARAMETER(settings);

    image_source_data_t *s = bzalloc(sizeof(*s));
    s->source              = source;
    s->size.width          = 200;
    s->size.height         = 200;

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

    image_source_data_t *source = data;

    if (!source) {
        return;
    }

    image_source_destroy(&g_achievement_icon);

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

    image_source_data_t *source = data;

    if (!source) {
        return;
    }

    /* Load image if needed (deferred load in graphics context) */
    image_source_reload_if_needed(&g_achievement_icon);

    /* Render the image if we have a texture */
    image_source_render(&g_achievement_icon, source->size, effect);
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
//      Public functions
//  --------------------------------------------------------------------------------------------------------------------

void xbox_achievement_icon_source_register(void) {

    image_source_cache_init(&g_achievement_icon, "Achievement Icon", "achievement_icon");

    obs_register_source(xbox_achievement_icon_source_get());

    xbox_subscribe_connected_changed(&on_connection_changed);
    xbox_subscribe_game_played(&on_xbox_game_played);
    xbox_subscribe_achievements_progressed(&on_achievements_progressed);
}
