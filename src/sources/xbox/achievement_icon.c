#include "sources/xbox/achievement_icon.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include "common/achievement.h"
#include "oauth/xbox-live.h"
#include "sources/common/achievement_cycle.h"
#include "sources/common/image_source.h"

/**
 * @brief Global singleton achievement icon cache.
 *
 * This source is implemented as a singleton that stores the current achievement icon
 * in a global cache.
 */
static image_source_cache_t g_achievement_icon;

static bool g_is_achievement_unlocked = false;

/** Default duration for each fade phase (in seconds). */
#define ICON_TRANSITION_DEFAULT_DURATION 0.5f

/**
 * @brief Transition phase for icon fade animations.
 */
typedef enum icon_transition_phase {
    ICON_TRANSITION_NONE = 0,
    ICON_TRANSITION_FADE_OUT,
    ICON_TRANSITION_FADE_IN,
} icon_transition_phase_t;

/**
 * @brief Transition state for icon fade animations.
 */
typedef struct icon_transition_state {
    icon_transition_phase_t phase;
    float                   opacity;
    float                   duration;
    char                   *pending_url;
    bool                    pending_is_unlocked;
} icon_transition_state_t;

static icon_transition_state_t g_transition = {
    .phase               = ICON_TRANSITION_NONE,
    .opacity             = 1.0f,
    .duration            = ICON_TRANSITION_DEFAULT_DURATION,
    .pending_url         = NULL,
    .pending_is_unlocked = false,
};

/**
 * @brief Update the achievement icon display.
 *
 * @param achievement Achievement to display icon for. If NULL, clears the display.
 */
static void update_achievement_icon(const achievement_t *achievement) {
    if (achievement && achievement->icon_url) {
        bool new_is_unlocked = achievement->unlocked_timestamp != 0;

        // Check if the icon URL or unlock state changed
        bool url_changed = (g_achievement_icon.image_url[0] == '\0') ||
                           (strcmp(g_achievement_icon.image_url, achievement->icon_url) != 0);
        bool state_changed = (g_is_achievement_unlocked != new_is_unlocked);

        if ((url_changed || state_changed) && g_achievement_icon.image_texture) {
            // Start a fade-out transition
            if (g_transition.pending_url) {
                bfree(g_transition.pending_url);
            }
            g_transition.pending_url         = bstrdup(achievement->icon_url);
            g_transition.pending_is_unlocked = new_is_unlocked;
            g_transition.phase               = ICON_TRANSITION_FADE_OUT;
            g_transition.opacity             = 1.0f;
        } else {
            // No existing texture or first load - load immediately
            g_is_achievement_unlocked = new_is_unlocked;
            image_source_download_if_changed(&g_achievement_icon, achievement->icon_url);

            if (g_achievement_icon.image_texture) {
                // Start fade-in
                g_transition.phase   = ICON_TRANSITION_FADE_IN;
                g_transition.opacity = 0.0f;
            }
        }
    } else {
        image_source_clear(&g_achievement_icon);
        g_is_achievement_unlocked = false;
        g_transition.phase        = ICON_TRANSITION_NONE;
        g_transition.opacity      = 1.0f;
    }
}

//  --------------------------------------------------------------------------------------------------------------------
//	Event handlers
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Achievement cycle callback invoked when the displayed achievement changes.
 *
 * Updates the achievement icon display with the new achievement. This is called
 * by the shared achievement cycle module whenever the display should change.
 *
 * @param achievement Achievement to display. If NULL, clears the display.
 */
static void on_achievement_changed(const achievement_t *achievement) {

    update_achievement_icon(achievement);
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
 * Loads a new texture if required and draws it with opacity for fade animations.
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

    /* Get current opacity from transition state */
    float opacity = g_transition.opacity;

    if (g_is_achievement_unlocked) {
        /* Render the image with opacity if we have a texture */
        image_source_render_with_opacity(&g_achievement_icon, source->size, effect, opacity);
    } else {
        image_source_render_greyscale_with_opacity(&g_achievement_icon, source->size, effect, opacity);
    }
}

/**
 * @brief OBS callback for animation tick.
 *
 * Updates fade transition animations and delegates achievement display cycle
 * management to the shared achievement_cycle module.
 */
static void on_source_video_tick(void *data, float seconds) {

    UNUSED_PARAMETER(data);

    /* Update fade transition animations */
    float duration = g_transition.duration;
    if (duration <= 0.0f) {
        duration = ICON_TRANSITION_DEFAULT_DURATION;
    }

    switch (g_transition.phase) {
    case ICON_TRANSITION_FADE_OUT:
        g_transition.opacity -= seconds / duration;
        if (g_transition.opacity <= 0.0f) {
            g_transition.opacity = 0.0f;

            /* Fade-out complete, load the pending icon */
            if (g_transition.pending_url) {
                g_is_achievement_unlocked = g_transition.pending_is_unlocked;
                image_source_download_if_changed(&g_achievement_icon, g_transition.pending_url);
                bfree(g_transition.pending_url);
                g_transition.pending_url = NULL;
            }

            /* Start fade-in */
            g_transition.phase = ICON_TRANSITION_FADE_IN;
        }
        break;

    case ICON_TRANSITION_FADE_IN:
        g_transition.opacity += seconds / duration;
        if (g_transition.opacity >= 1.0f) {
            g_transition.opacity = 1.0f;
            g_transition.phase   = ICON_TRANSITION_NONE;
        }
        break;

    case ICON_TRANSITION_NONE:
    default:
        break;
    }

    /* Update the shared achievement display cycle */
    achievement_cycle_tick(seconds);
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
    .video_tick     = on_source_video_tick,
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

    achievement_cycle_subscribe(&on_achievement_changed);
}
