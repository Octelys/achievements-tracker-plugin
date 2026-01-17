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

typedef struct xbox_account_source {
    obs_source_t *source;
    uint32_t      width;
    uint32_t      height;
} xbox_account_source_t;

//  --------------------------------------------------------------------------------------------------------------------
//	Private functions
//  --------------------------------------------------------------------------------------------------------------------

static void on_xbox_game_played(const game_t *game) {

    UNUSED_PARAMETER(game);

    /* Saves the game information to load the cover on the next update */
}

//  --------------------------------------------------------------------------------------------------------------------
//	Source callbacks
//  --------------------------------------------------------------------------------------------------------------------

static void *on_source_create(obs_data_t *settings, obs_source_t *source) {

    UNUSED_PARAMETER(settings);

    xbox_account_source_t *s = bzalloc(sizeof(*s));
    s->source                = source;
    s->width                 = 800;
    s->height                = 200;

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

    /* TODO Share the logic */

    /* Finds out if there is a token available already */
    const xbox_identity_t *xbox_identity = state_get_xbox_identity();

    /* Lists all the UI components of the properties page */
    obs_properties_t *p = obs_properties_create();

    if (xbox_identity != NULL) {
        char status[4096];
        snprintf(status, 4096, "Signed in to your xbox account as %s", xbox_identity->gamertag);

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

    } else {
        obs_properties_add_text(p,
                                "disconnected_status_info",
                                "You are not connected to your xbox account.",
                                OBS_TEXT_INFO);
    }

    return p;
}

/**
 * Gets the name of the source
 *
 * @param unused
 * @return
 */
static const char *source_get_name(void *unused) {
    UNUSED_PARAMETER(unused);

    return "Xbox Gamerscore";
}

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
    .video_tick     = NULL,
    .video_render   = on_source_video_render,
};

/**
 *
 * @return
 */
static const struct obs_source_info *xbox_source_get(void) {
    return &xbox_gamerscore_source;
}

//  --------------------------------------------------------------------------------------------------------------------
//	Public functions
//  --------------------------------------------------------------------------------------------------------------------

/**
 *
 */
void xbox_gamerscore_source_register(void) {

    obs_register_source(xbox_source_get());

    /* TODO Register for game played notifications */
}
