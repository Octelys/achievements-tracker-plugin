#include "sources/xbox/gamerpic.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include "oauth/xbox-live.h"
#include "sources/common/image_source.h"
#include "common/memory.h"
#include "xbox/xbox_client.h"
#include "xbox/xbox_monitor.h"

/**
 * @brief Global singleton gamerpic cache.
 *
 * This source is implemented as a singleton that stores the current user gamerpic
 * in a global cache.
 */
static image_t g_gamerpic;

//  --------------------------------------------------------------------------------------------------------------------
//	Event handlers
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Xbox monitor callback invoked when connection state changes.
 *
 * When connected, this refreshes the gamerscore display.
 *
 * @param is_connected Whether the account is currently connected.
 * @param error_message Optional error message if disconnected (ignored here).
 */
static void on_connection_changed(bool is_connected, const char *error_message) {

    UNUSED_PARAMETER(error_message);

    if (!is_connected) {
        obs_log(LOG_INFO, "[Gamerpic] Not connected - clearing");
        image_source_clear(&g_gamerpic);
        return;
    }

    obs_log(LOG_INFO, "[Gamerpic] Connected to Xbox Live - fetching Gamerpic URL %s", g_gamerpic.type);

    char *gamerpic_url = xbox_fetch_gamerpic();

    if (!gamerpic_url || gamerpic_url[0] == '\0') {
        obs_log(LOG_INFO, "[Gamerpic] No Gamerpic URL - clearing");
        image_source_clear(&g_gamerpic);
        goto cleanup;
    }

    if (strcasecmp(gamerpic_url, g_gamerpic.url) != 0) {
        obs_log(LOG_INFO, "[Gamerpic] Gamerpic URL changed - downloading");
        snprintf(g_gamerpic.url, sizeof(g_gamerpic.url), "%s", gamerpic_url);
        snprintf(g_gamerpic.id, sizeof(g_gamerpic.id), "%s", "default");
        image_source_download(&g_gamerpic);
    }

cleanup:
    free_memory((void **)&gamerpic_url);
}

//  --------------------------------------------------------------------------------------------------------------------
//	Source callbacks
//  --------------------------------------------------------------------------------------------------------------------

/** @brief OBS callback returning the source width. */
static uint32_t source_get_width(void *data) {
    const image_source_t *s = data;
    return s->size.width;
}

/** @brief OBS callback returning the source height. */
static uint32_t source_get_height(void *data) {
    const image_source_t *s = data;
    return s->size.height;
}

/** @brief OBS callback returning the display name for the source type. */
static const char *source_get_name(void *unused) {

    UNUSED_PARAMETER(unused);

    return "Xbox Gamerpic";
}

/**
 * @brief OBS callback creating a new source instance.
 *
 * @param settings OBS settings object (currently unused).
 * @param source   OBS source instance.
 * @return Newly allocated image_source_data_t.
 */
static void *on_source_create(obs_data_t *settings, obs_source_t *source) {

    UNUSED_PARAMETER(settings);

    image_source_t *s = bzalloc(sizeof(*s));
    s->source         = source;
    s->size.width     = 800;
    s->size.height    = 200;

    return s;
}

/**
 * @brief OBS callback destroying a source instance.
 *
 * Frees the per-instance data. Note: Global gamerpic (g_gamerpic)
 * is cleaned up during plugin unload, not per-source-instance.
 */
static void on_source_destroy(void *data) {

    image_source_t *source = data;

    if (!source) {
        return;
    }

    bfree(source);
}

/**
 * @brief OBS callback invoked when source settings change.
 *
 * Currently unused (no editable settings).
 */
static void on_source_update(void *data, obs_data_t *settings) {

    UNUSED_PARAMETER(settings);
    UNUSED_PARAMETER(data);
}

/**
 * @brief OBS callback to render the source.
 *
 * Loads a new texture if required and draws it using draw_texture().
 */
static void on_source_video_render(void *data, gs_effect_t *effect) {

    image_source_t *source = data;

    if (!source) {
        return;
    }

    /* Load image if needed (deferred load in graphics context) */
    image_source_reload_if_needed(&g_gamerpic);

    /* Render the image if we have a texture */
    image_source_render_active(&g_gamerpic, source->size, effect);
}

/**
 * @brief OBS callback to construct the properties UI.
 *
 * Shows connection status, gamerscore, and the currently played game.
 */
static obs_properties_t *source_get_properties(void *data) {

    UNUSED_PARAMETER(data);

    /* Gets or refreshes the token */
    xbox_identity_t *xbox_identity = xbox_live_get_identity();

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

    free_identity(&xbox_identity);

    return p;
}

/**
 * @brief obs_source_info for the Xbox Gamerpic source.
 */
static struct obs_source_info xbox_gamerpic_source_info = {
    .id             = "xbox_gamerpic_source",
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
 * @brief Get a pointer to this source type's obs_source_info.
 */
static const struct obs_source_info *xbox_gamerpic_source_get(void) {
    return &xbox_gamerpic_source_info;
}

//  --------------------------------------------------------------------------------------------------------------------
//      Public functions
//  --------------------------------------------------------------------------------------------------------------------

void xbox_gamerpic_source_register(void) {

    snprintf(g_gamerpic.display_name, sizeof(g_gamerpic.display_name), "Gamerpic");
    snprintf(g_gamerpic.id, sizeof(g_gamerpic.id), "default");
    snprintf(g_gamerpic.type, sizeof(g_gamerpic.type), "gamerpic");

    obs_register_source(xbox_gamerpic_source_get());

    xbox_subscribe_connected_changed(&on_connection_changed);
}

void xbox_gamerpic_source_cleanup(void) {
    image_source_destroy(&g_gamerpic);
}
