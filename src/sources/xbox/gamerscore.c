#include "sources/xbox/gamerscore.h"

#include <graphics/graphics.h>
#include <graphics/image-file.h>
#include <obs-module.h>
#include <diagnostics/log.h>
#include <curl/curl.h>
#include <inttypes.h>

#include "io/state.h"
#include "oauth/xbox-live.h"
#include "xbox/xbox_client.h"
#include "xbox/xbox_monitoring.h"

// Store the source reference somewhere accessible
static obs_source_t *g_game_cover_source = NULL;

typedef struct xbox_gamerscore_source {
    obs_source_t *source;
    uint32_t      width;
    uint32_t      height;
} xbox_gamerscore_source_t;

static void refresh_page() {
    if (!g_game_cover_source)
        return;

    /*
     * Force OBS to refresh the properties UI by signaling that
     * properties need to be recreated. Returning true from the
     * button callback also helps trigger this.
     */
    obs_source_update_properties(g_game_cover_source);
}

/**
 * Called when the Sign-out button is clicked
 *
 * Clears the tokens from the state.
 *
 * @param props
 * @param property
 * @param data
 * @return
 */
static bool on_sign_out_clicked(obs_properties_t *props, obs_property_t *property, void *data) {
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);
    UNUSED_PARAMETER(data);

    state_clear();

    refresh_page();

    return true;
}

static void on_xbox_sign_in_completed() {
    refresh_page();
}

/**
 * Called when the Sign-in button is called.
 *
 * The method triggers the device oauth flow to register the device with Xbox
 * live.
 *
 * @param props
 * @param property
 * @param data
 *
 * @return
 */
static bool on_sign_in_xbox_clicked(obs_properties_t *props, obs_property_t *property, void *data) {
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);
    UNUSED_PARAMETER(data);

    device_t *device = state_get_device();

    if (!xbox_live_get_authenticate(device, &on_xbox_sign_in_completed)) {
        obs_log(LOG_WARNING, "Xbox sign-in failed");
        return false;
    }

    return true;
}

static void on_xbox_game_played(const game_t *game) {
    char text[4096];
    snprintf(text, 4096, "Playing game %s (%s)", game->title, game->id);
    obs_log(LOG_WARNING, text);
}

static void on_xbox_monitoring_connection_status_changed(bool connected, const char *error_message) {
    if (connected) {
        obs_log(LOG_WARNING, "Connected to Real-Time Activity endpoint");
        xbox_subscribe();
    } else {
        obs_log(LOG_WARNING, error_message);
    }
}

static bool on_monitoring_clicked(obs_properties_t *props, obs_property_t *property, void *data) {
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);
    UNUSED_PARAMETER(data);

    xbox_monitoring_start(&on_xbox_game_played, &on_xbox_monitoring_connection_status_changed);
    obs_log(LOG_WARNING, "Monitoring started!");

    return true;
}

//  --------------------------------------------------------------------------------------------------------------------
//	Source callbacks
//  --------------------------------------------------------------------------------------------------------------------

void *on_source_create(obs_data_t *settings, obs_source_t *source) {

    UNUSED_PARAMETER(settings);

    xbox_gamerscore_source_t *s = bzalloc(sizeof(*s));
    s->source                   = source;
    s->width                    = 800;
    s->height                   = 200;

    return s;
}

void on_source_destroy(void *data) {

    g_game_cover_source = NULL;

    xbox_gamerscore_source_t *source = data;

    if (!source) {
        return;
    }

    bfree(source);
}

uint32_t source_get_width(void *data) {
    const xbox_gamerscore_source_t *s = data;
    return s->width;
}

uint32_t source_get_height(void *data) {
    const xbox_gamerscore_source_t *s = data;
    return s->height;
}

static void on_source_update(void *data, obs_data_t *settings) {
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(settings);
}

void on_source_video_render(void *data, gs_effect_t *effect) {
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(effect);
}

/**
 * Configures the properties page of the plugin
 *
 * @param data
 * @return
 */
obs_properties_t *source_get_properties(void *data) {
    UNUSED_PARAMETER(data);

    /* TODO Share the logic */

    /* Finds out if there is a token available already */
    const xbox_identity_t *xbox_identity = state_get_xbox_identity();

    /* Lists all the UI components of the properties page */
    obs_properties_t *p = obs_properties_create();

    if (xbox_identity != NULL) {
        char status[4096];
        snprintf(status, 4096, "Signed in as %s", xbox_identity->gamertag);

        int64_t gamerscore = 0;
        xbox_fetch_gamerscore(&gamerscore);

        char gamerscore_text[4096];
        snprintf(gamerscore_text, 4096, "Gamerscore %" PRId64, gamerscore);

        obs_properties_add_text(p, "connected_status_info", status, OBS_TEXT_INFO);
        obs_properties_add_text(p, "gamerscore_info", gamerscore_text, OBS_TEXT_INFO);

        const game_t *game = get_current_game();

        if (game) {
            char game_played[4096];
            snprintf(game_played, sizeof(game_played), "Playing %s (%s)", game->title, game->id);
            obs_properties_add_text(p, "game_played", game_played, OBS_TEXT_INFO);
        }

        obs_properties_add_button(p, "sign_out_xbox", "Sign out from Xbox", &on_sign_out_clicked);

        /* Temporary */
        obs_properties_add_button(p, "monitor", "Start monitoring", &on_monitoring_clicked);
    } else {
        obs_properties_add_text(p, "disconnected_status_info", "You are not connected.", OBS_TEXT_INFO);
        obs_properties_add_button(p, "sign_in_xbox", "Sign in with Xbox", &on_sign_in_xbox_clicked);
    }

    return p;
}

/**
 * Gets the name of the source
 *
 * @param unused
 * @return
 */
const char *source_get_name(void *unused) {
    UNUSED_PARAMETER(unused);

    return "Xbox Gamerscore";
}

static struct obs_source_info xbox_gamerscore_score_info = {
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
 *
 * @return
 */
static const struct obs_source_info *xbox_gamerscore_source_get(void) {
    return &xbox_gamerscore_score_info;
}

//  --------------------------------------------------------------------------------------------------------------------
//	Public functions
//  --------------------------------------------------------------------------------------------------------------------

/**
 *
 */
void xbox_gamerscore_source_register(void) {

    obs_register_source(xbox_gamerscore_source_get());

    /* TODO Conflict: both sources can't start the monitoring at the same time! */

    /* Starts the monitoring if the user is already logged in */
    xbox_identity_t *identity = state_get_xbox_identity();

    if (identity) {
        xbox_monitoring_start(&on_xbox_game_played, &on_xbox_monitoring_connection_status_changed);
        obs_log(LOG_INFO, "Monitoring started");
    }
}
