#include "sources/xbox_achievements_text_source.h"

#include <graphics/graphics.h>
#include <obs-module.h>
#include <diagnostics/log.h>

#include "io/state.h"
#include "oauth/xbox-live.h"
#include "crypto/crypto.h"
#include "xbox/xbox_client.h"

// Store the source reference somewhere accessible
static obs_source_t *g_xbox_achievements_text_src = NULL;

struct xbox_achievements_text_src {
    obs_source_t *source;
    char         *text;
    obs_source_t *child_text;
    uint32_t      width;
    uint32_t      height;
};

static void text_src_update_child(struct xbox_achievements_text_src *s) {
    if (!s || !s->child_text)
        return;

    obs_data_t *st = obs_data_create();
    obs_data_set_string(st, "text", s->text ? s->text : "");
    obs_data_set_int(st, "color1", 0xFFFFFFFF); /* white */
    obs_data_set_bool(st, "outline", false);
    obs_data_set_int(st, "align", 0);  /* left */
    obs_data_set_int(st, "valign", 0); /* top */
    obs_source_update(s->child_text, st);
    obs_data_release(st);
}

void *xbox_achievements_text_src_create(obs_data_t *settings, obs_source_t *source) {
    g_xbox_achievements_text_src = source;

    struct xbox_achievements_text_src *s = bzalloc(sizeof(*s));
    s->source                            = source;

    const char *t = obs_data_get_string(settings, "text");
    s->text       = bstrdup(t ? t : "Hello from plugin");

    s->width  = 800;
    s->height = 200;

    /* Create built-in text source as a child */
    obs_data_t *st = obs_data_create();
    obs_data_set_string(st, "text", s->text);
    s->child_text = obs_source_create_private("text_ft2_source", "child_text", st);
    obs_data_release(st);

    text_src_update_child(s);
    return s;
}

void xbox_achievements_text_src_destroy(void *data) {
    struct xbox_achievements_text_src *s = data;

    if (!s)
        return;

    if (s->child_text)
        obs_source_release(s->child_text);

    bfree(s->text);
    bfree(s);
}

void xbox_achievements_text_src_update(void *data, obs_data_t *settings) {
    struct xbox_achievements_text_src *s = data;
    const char                        *t = obs_data_get_string(settings, "text");

    bfree(s->text);
    s->text = bstrdup(t ? t : "");

    text_src_update_child(s);
}

uint32_t xbox_achievements_text_src_get_width(void *data) {
    struct xbox_achievements_text_src *s = data;
    return s->width;
}

uint32_t xbox_achievements_text_src_get_height(void *data) {
    struct xbox_achievements_text_src *s = data;
    return s->height;
}

void xbox_achievements_text_src_video_render(void *data, gs_effect_t *effect) {
    UNUSED_PARAMETER(effect);
    struct xbox_achievements_text_src *s = data;
    if (!s || !s->child_text)
        return;

    /* Let the child (Text FT2) render into our source */
    obs_source_video_render(s->child_text);
}

static void refresh_page() {
    if (!g_xbox_achievements_text_src)
        return;

    /*
     * Force OBS to refresh the properties UI by signaling that
     * properties need to be recreated. Returning true from the
     * button callback also helps trigger this.
     */
    obs_source_update_properties(g_xbox_achievements_text_src);
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

/**
 * Configures the properties page of the plugin
 *
 * @param data
 * @return
 */
obs_properties_t *xbox_achievements_get_properties(void *data) {
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
        snprintf(gamerscore_text, 4096, "Gamerscore %lld", gamerscore);

        obs_properties_add_text(p, "connected_status_info", status, OBS_TEXT_INFO);
        obs_properties_add_text(p, "gamerscore_info", gamerscore_text, OBS_TEXT_INFO);
        obs_properties_add_button(p, "sign_out_xbox", "Sign out from Xbox", &on_sign_out_clicked);
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
const char *xbox_achievements_text_src_get_name(void *unused) {
    UNUSED_PARAMETER(unused);

    return "Achievements Tracker";
}

static struct obs_source_info xbox_achievements_text_src_info = {
    .id             = "xbox_achievements_text_source",
    .type           = OBS_SOURCE_TYPE_INPUT,
    .output_flags   = OBS_SOURCE_VIDEO,
    .get_name       = xbox_achievements_text_src_get_name,
    .create         = xbox_achievements_text_src_create,
    .destroy        = xbox_achievements_text_src_destroy,
    .update         = xbox_achievements_text_src_update,
    .get_properties = xbox_achievements_get_properties,
    .get_width      = xbox_achievements_text_src_get_width,
    .get_height     = xbox_achievements_text_src_get_height,
    .video_tick     = NULL,
    .video_render   = xbox_achievements_text_src_video_render,
};

//	Public methods

/**
 * Returns the configuration of the plugin
 *
 * @return
 */
const struct obs_source_info *xbox_achievements_text_source_get(void) {
    return &xbox_achievements_text_src_info;
}

void xbox_achievements_text_source_register(void) {
    obs_register_source(xbox_achievements_text_source_get());
}
