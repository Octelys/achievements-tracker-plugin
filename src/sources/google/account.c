#include "sources/google/account.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include "oauth/google.h"

typedef struct google_account_source {
    obs_source_t *source;
    uint32_t      width;
    uint32_t      height;
} google_account_source_t;

/* ------------------------------------------------------------------ helpers */

/**
 * @brief Refreshes the OBS properties UI for a source (runs on the UI thread).
 */
static void google_refresh_properties_on_main(void *data) {
    obs_source_t *source = data;

    if (!source)
        return;

    char       *display_name = google_get_display_name();
    obs_data_t *settings     = obs_source_get_settings(source);

    if (settings) {
        if (display_name) {
            char status[1024];
            snprintf(status, sizeof(status), "Signed in as %s", display_name);
            obs_data_set_string(settings, "google_status", status);
        } else {
            obs_data_set_string(settings, "google_status", "Not connected.");
        }
        obs_data_release(settings);
    }

    bfree(display_name);

    obs_source_update_properties(source);
}

/**
 * @brief Schedules a refresh of the source properties UI.
 *
 * Safe to call from worker threads.
 */
static void google_schedule_refresh_properties(void *data) {
    google_account_source_t *s = data;

    if (!s || !s->source)
        return;

    obs_queue_task(OBS_TASK_UI, google_refresh_properties_on_main, s->source, false);
}

/* ------------------------------------------------------------------ button callbacks */

/**
 * @brief OBS properties callback for the "Sign out" button.
 */
static bool on_google_sign_out_clicked(obs_properties_t *props, obs_property_t *property, void *data) {
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    google_sign_out();

    google_schedule_refresh_properties(data);

    return true;
}

/**
 * @brief Completion callback invoked after Google authentication.
 */
static void on_google_signed_in(void *data) {
    google_schedule_refresh_properties(data);
}

/**
 * @brief OBS properties callback for the "Sign in with Google" button.
 *
 * Opens the system browser at Google's authorization URL and waits for the
 * redirect with the authorization code.
 */
static bool on_sign_in_google_clicked(obs_properties_t *props, obs_property_t *property, void *data) {
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);

    if (!google_authenticate(data, &on_google_signed_in)) {
        obs_log(LOG_WARNING, "[google] Sign-in failed to start");
        return false;
    }

    return true;
}

/* ------------------------------------------------------------------ source callbacks */

static void *on_google_source_create(obs_data_t *settings, obs_source_t *source) {
    UNUSED_PARAMETER(settings);

    google_account_source_t *s = bzalloc(sizeof(*s));
    s->source                  = source;
    s->width                   = 10;
    s->height                  = 10;

    return s;
}

static void on_google_source_destroy(void *data) {
    google_account_source_t *source = data;

    if (!source)
        return;

    bfree(source);
}

static uint32_t google_source_get_width(void *data) {
    const google_account_source_t *s = data;
    return s->width;
}

static uint32_t google_source_get_height(void *data) {
    const google_account_source_t *s = data;
    return s->height;
}

static void on_google_source_update(void *data, obs_data_t *settings) {
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(settings);
}

static void on_google_source_video_render(void *data, gs_effect_t *effect) {
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(effect);
}

/**
 * @brief OBS source callback providing the properties UI.
 *
 * Displays sign-in status. Provides "Sign in with Google" / "Sign out" buttons.
 */
static obs_properties_t *google_source_get_properties(void *data) {
    google_account_source_t *s = data;
    obs_properties_t        *p = obs_properties_create();

    /* Push the live sign-in state into obs_data so the read-only field
     * always shows the real value, not the stale default. */
    if (s && s->source) {
        obs_data_t *settings = obs_source_get_settings(s->source);
        if (settings) {
            char *display_name = google_get_display_name();
            if (display_name) {
                char status[1024];
                snprintf(status, sizeof(status), "Signed in as %s", display_name);
                obs_data_set_string(settings, "google_status", status);
                bfree(display_name);
            } else {
                obs_data_set_string(settings, "google_status", "Not connected.");
            }
            obs_data_release(settings);
        }
    }

    obs_property_t *status_prop =
        obs_properties_add_text(p, "google_status", "Google Account", OBS_TEXT_DEFAULT);
    obs_property_set_enabled(status_prop, false);

    obs_property_t *version_prop =
        obs_properties_add_text(p, "plugin_version", "Plugin version", OBS_TEXT_DEFAULT);
    obs_property_set_enabled(version_prop, false);

    if (google_is_signed_in()) {
        obs_properties_add_button(p, "sign_out_google", "Sign out from Google",
                                  &on_google_sign_out_clicked);
    } else {
        obs_properties_add_button(p, "sign_in_google", "Sign in with Google",
                                  &on_sign_in_google_clicked);
    }

    return p;
}

static void google_source_get_defaults(obs_data_t *settings) {
    obs_data_set_default_string(settings, "google_status", "Not connected.");
    obs_data_set_default_string(settings, "plugin_version", PLUGIN_VERSION);
}

static const char *google_source_get_name(void *unused) {
    UNUSED_PARAMETER(unused);
    return "Google Account";
}

static struct obs_source_info google_account_source_info = {
    .id             = "google_account_source",
    .type           = OBS_SOURCE_TYPE_INPUT,
    .output_flags   = OBS_SOURCE_VIDEO,
    .get_name       = google_source_get_name,
    .create         = on_google_source_create,
    .destroy        = on_google_source_destroy,
    .get_defaults   = google_source_get_defaults,
    .update         = on_google_source_update,
    .get_properties = google_source_get_properties,
    .get_width      = google_source_get_width,
    .get_height     = google_source_get_height,
    .video_tick     = NULL,
    .video_render   = on_google_source_video_render,
};

/* ------------------------------------------------------------------ public */

void google_account_source_register(void) {
    obs_register_source(&google_account_source_info);
}

