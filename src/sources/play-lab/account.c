#include "sources/play-lab/account.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include "io/state.h"
#include "oauth/play-lab.h"

typedef struct play_lab_account_source {
    obs_source_t *source;
    uint32_t      width;
    uint32_t      height;
} play_lab_account_source_t;

/**
 * @brief Starts the monitoring if the user is logged in.
 */
static void start_monitoring_if_needed(void) {
    if (!play_lab_is_signed_in()) {
        obs_log(LOG_INFO, "[play-lab] Monitoring will not be started: no identity signed-in");
        return;
    }

    obs_log(LOG_INFO, "[play-lab] User is signed in – monitoring can start here");
}

/**
 * @brief Refreshes the OBS properties UI for a source (runs on the UI thread).
 *
 * @param data The @c obs_source_t* whose properties should be refreshed.
 */
static void refresh_properties_on_main(void *data) {
    obs_source_t *source = data;

    if (!source)
        return;

    // Update the play_lab_status field in the source settings so the read-only
    // text field reflects the current sign-in state after the UI rebuilds.
    char       *display_name = play_lab_get_display_name();
    obs_data_t *settings     = obs_source_get_settings(source);

    if (settings) {
        if (display_name) {
            char status[1024];
            snprintf(status, sizeof(status), "Signed in as %s", display_name);
            obs_data_set_string(settings, "play_lab_status", status);
        } else {
            obs_data_set_string(settings, "play_lab_status", "Not connected.");
        }
        obs_data_release(settings);
    }

    bfree(display_name);

    obs_source_update_properties(source);
}

/**
 * @brief Schedules a refresh of the source properties UI.
 *
 * This queues a UI task so OBS will call @c source_get_properties() again.
 * Safe to call from worker threads.
 *
 * @param data Pointer to the @c play_lab_account_source_t instance.
 */
static void schedule_refresh_properties(void *data) {
    play_lab_account_source_t *s = data;

    if (!s || !s->source)
        return;

    // Force OBS to call source_get_properties() again, rebuilding the info texts.
    obs_queue_task(OBS_TASK_UI, refresh_properties_on_main, s->source, false);
}

/**
 * @brief OBS properties callback for the "Sign out" button.
 *
 * Clears cached state (tokens, identity, etc.) and refreshes the UI.
 *
 * @return Always true to indicate the button click was handled.
 */
static bool on_sign_out_clicked(obs_properties_t *props, obs_property_t *property, void *data) {

    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    play_lab_sign_out();

    schedule_refresh_properties(data);

    return true;
}

/**
 * @brief Completion callback invoked after Play Lab authentication.
 *
 * Refreshes the properties UI so the signed-in state is reflected.
 */
static void on_play_lab_signed_in(void *data) {

    start_monitoring_if_needed();

    schedule_refresh_properties(data);
}

/**
 * @brief OBS properties callback for the "Sign in" button.
 *
 * Starts the Play Lab device-code authentication flow.
 *
 * @return True if the authentication flow was successfully started.
 */
static bool on_sign_in_play_lab_clicked(obs_properties_t *props, obs_property_t *property, void *data) {
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    if (!play_lab_authenticate(data, &on_play_lab_signed_in)) {
        obs_log(LOG_WARNING, "[play-lab] Sign-in failed to start");
        return false;
    }

    return true;
}

//  --------------------------------------------------------------------------------------------------------------------
//	Source callbacks
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief OBS source create callback.
 *
 * Allocates and initializes the per-source context.
 *
 * @return Newly allocated @c play_lab_account_source_t instance.
 */
static void *on_source_create(obs_data_t *settings, obs_source_t *source) {

    UNUSED_PARAMETER(settings);

    play_lab_account_source_t *s = bzalloc(sizeof(*s));
    s->source                    = source;
    s->width                     = 10;
    s->height                    = 10;

    return s;
}

/**
 * @brief OBS source destroy callback.
 *
 * Frees the per-source context.
 */
static void on_source_destroy(void *data) {

    play_lab_account_source_t *source = data;

    if (!source) {
        return;
    }

    bfree(source);
}

/**
 * @brief OBS source callback returning the current width.
 */
static uint32_t source_get_width(void *data) {
    const play_lab_account_source_t *s = data;
    return s->width;
}

/**
 * @brief OBS source callback returning the current height.
 */
static uint32_t source_get_height(void *data) {
    const play_lab_account_source_t *s = data;
    return s->height;
}

/**
 * @brief OBS source update callback.
 */
static void on_source_update(void *data, obs_data_t *settings) {
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(settings);
}

/**
 * @brief OBS source video render callback.
 */
static void on_source_video_render(void *data, gs_effect_t *effect) {
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(effect);
}

/**
 * @brief OBS source callback providing the properties UI.
 *
 * Displays sign-in status and current game info (if available).
 * Provides sign-in / sign-out buttons.
 */
static obs_properties_t *source_get_properties(void *data) {

    play_lab_account_source_t *s = data;
    obs_properties_t          *p = obs_properties_create();

    // Push the live sign-in state into obs_data so the read-only field
    // always shows the real value, not the stale default.
    if (s && s->source) {
        obs_data_t *settings = obs_source_get_settings(s->source);
        if (settings) {
            char *display_name = play_lab_get_display_name();
            if (display_name) {
                char status[1024];
                snprintf(status, sizeof(status), "Signed in as %s", display_name);
                obs_data_set_string(settings, "play_lab_status", status);
                bfree(display_name);
            } else {
                obs_data_set_string(settings, "play_lab_status", "Not connected.");
            }
            obs_data_release(settings);
        }
    }

    obs_property_t *status_prop = obs_properties_add_text(p, "play_lab_status", "Play Lab Account", OBS_TEXT_DEFAULT);
    obs_property_set_enabled(status_prop, false);

    obs_property_t *version_prop = obs_properties_add_text(p, "plugin_version", "Plugin version", OBS_TEXT_DEFAULT);
    obs_property_set_enabled(version_prop, false);

    if (play_lab_is_signed_in()) {
        obs_properties_add_button(p, "sign_out_play_lab", "Sign out from Play Lab", &on_sign_out_clicked);
    } else {
        obs_properties_add_button(p, "sign_in_play_lab", "Sign in with Play Lab", &on_sign_in_play_lab_clicked);
    }

    return p;
}

/**
 * @brief OBS source defaults callback.
 *
 * Pre-fills the read-only "Plugin version" field with the current version string
 * so OBS displays it in the bordered text field without requiring user input.
 */
static void source_get_defaults(obs_data_t *settings) {
    obs_data_set_default_string(settings, "play_lab_status", "Not connected.");
    obs_data_set_default_string(settings, "plugin_version", PLUGIN_VERSION);
}

/**
 * @brief OBS source display name.
 */
static const char *source_get_name(void *unused) {
    UNUSED_PARAMETER(unused);

    return "Play Lab Account";
}

static struct obs_source_info play_lab_account_source = {
    .id             = "play_lab_account_source",
    .type           = OBS_SOURCE_TYPE_INPUT,
    .output_flags   = OBS_SOURCE_VIDEO,
    .get_name       = source_get_name,
    .create         = on_source_create,
    .destroy        = on_source_destroy,
    .get_defaults   = source_get_defaults,
    .update         = on_source_update,
    .get_properties = source_get_properties,
    .get_width      = source_get_width,
    .get_height     = source_get_height,
    .video_tick     = NULL,
    .video_render   = on_source_video_render,
};

static const struct obs_source_info *play_lab_source_get(void) {
    return &play_lab_account_source;
}

//  --------------------------------------------------------------------------------------------------------------------
//      Public functions
//  --------------------------------------------------------------------------------------------------------------------

void playlab_account_source_register(void) {

    obs_register_source(play_lab_source_get());

    start_monitoring_if_needed();
}
