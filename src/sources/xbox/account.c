#include "sources/xbox/account.h"

#include <graphics/graphics.h>
#include <graphics/image-file.h>
#include <obs-module.h>
#include <diagnostics/log.h>
#include <curl/curl.h>
#include <inttypes.h>

#include "io/state.h"
#include "oauth/xbox-live.h"
#include "xbox/xbox_client.h"
#include "xbox/xbox_monitor.h"

typedef struct xbox_account_source {
    obs_source_t *source;
    uint32_t      width;
    uint32_t      height;
} xbox_account_source_t;

static void refresh_properties_on_main(void *data) {
    obs_source_t *source = data;

    if (source)
        obs_source_update_properties(source);
}

static void schedule_refresh_properties(void *data) {
    xbox_account_source_t *s = data;

    if (!s || !s->source)
        return;

    // Force OBS to call source_get_properties() again, rebuilding the info texts.
    obs_queue_task(OBS_TASK_UI, refresh_properties_on_main, s->source, false);
}

static bool on_sign_out_clicked(obs_properties_t *props, obs_property_t *property, void *data) {
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);
    UNUSED_PARAMETER(data);

    state_clear();

    schedule_refresh_properties(data);

    return true;
}

static void on_xbox_signed_in(void *data) {
    schedule_refresh_properties(data);
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

    if (!xbox_live_authenticate(device, data, &on_xbox_signed_in)) {
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

//  --------------------------------------------------------------------------------------------------------------------
//	Source callbacks
//  --------------------------------------------------------------------------------------------------------------------

static void *on_source_create(obs_data_t *settings, obs_source_t *source) {

    UNUSED_PARAMETER(settings);

    xbox_account_source_t *s = bzalloc(sizeof(*s));
    s->source                = source;
    s->width                 = 10;
    s->height                = 10;

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
}

static obs_properties_t *source_get_properties(void *data) {
    UNUSED_PARAMETER(data);

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
    } else {
        obs_properties_add_text(p, "disconnected_status_info", "You are not connected.", OBS_TEXT_INFO);
        obs_properties_add_button(p, "sign_in_xbox", "Sign in with Xbox", &on_sign_in_xbox_clicked);
    }

    return p;
}

static const char *source_get_name(void *unused) {
    UNUSED_PARAMETER(unused);

    return "Xbox Account";
}

static struct obs_source_info xbox_gamerscore_source = {
    .id             = "xbox_account_source",
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
 * Registers the Xbox account source with the system.
 *
 * This function registers the Xbox account source and initializes monitoring
 * for game activity and connection status if the user is already logged in.
 * The monitoring callbacks are set up to handle events and log the monitoring
 * status.
 */
void xbox_account_source_register(void) {

    obs_register_source(xbox_source_get());

    xbox_subscribe_game_played(&on_xbox_game_played);

    /* Starts the monitoring if the user is already logged in */
    xbox_identity_t *identity = state_get_xbox_identity();

    if (identity) {
        xbox_monitoring_start();
        obs_log(LOG_INFO, "Monitoring started");
    }
}
