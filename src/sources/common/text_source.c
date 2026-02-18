#include "sources/common/text_source.h"

#include <math.h>
#include <string.h>
#include <graphics/graphics.h>
#include <graphics/matrix4.h>

#include "drawing/color.h"
#include "diagnostics/log.h"

/**
 * @file text_source.c
 * @brief Implementation of common functionality for text-based OBS sources.
 */

/** Default duration for each fade phase (in seconds). */
#define TEXT_TRANSITION_DEFAULT_DURATION 0.35f

/**
 * @brief Set text colors with a specified opacity for fade transitions.
 *
 * Converts colors from RGBA format to OBS's ABGR format and applies the given
 * opacity multiplier to the alpha channel. This is the core color-setting function
 * used for both static display and fade animations.
 *
 * @param text_source Text source instance containing the active/inactive color selection state.
 * @param settings    OBS data object to update with color1 and color2 values.
 * @param config      Text source configuration containing active and inactive color definitions.
 * @param opacity     Opacity multiplier in range [0.0, 1.0] applied to the alpha channel.
 *                    Values outside this range are clamped automatically.
 */
static void set_color_with_opacity(text_source_t *text_source, obs_data_t *settings, const text_source_config_t *config,
                                   float opacity) {

    uint32_t top_rgba    = text_source->use_active_color ? config->active_top_color : config->inactive_top_color;
    uint32_t bottom_rgba = text_source->use_active_color ? config->active_bottom_color : config->inactive_bottom_color;

    float capped_opacity = fmaxf(0.0f, fminf(1.0f, opacity));

    // Set color - convert from RGBA to ABGR for OBS
    uint8_t  top_r          = (top_rgba >> 24) & 0xFF;
    uint8_t  top_g          = (top_rgba >> 16) & 0xFF;
    uint8_t  top_b          = (top_rgba >> 8) & 0xFF;
    uint8_t  original_top_a = top_rgba & 0xFF;
    uint8_t  top_a          = (uint8_t)(original_top_a * capped_opacity);
    uint32_t top_color      = (top_a << 24) | (top_b << 16) | (top_g << 8) | top_r;

    uint8_t  bottom_r          = (bottom_rgba >> 24) & 0xFF;
    uint8_t  bottom_g          = (bottom_rgba >> 16) & 0xFF;
    uint8_t  bottom_b          = (bottom_rgba >> 8) & 0xFF;
    uint8_t  original_bottom_a = bottom_rgba & 0xFF;
    uint8_t  bottom_a          = (uint8_t)(original_bottom_a * capped_opacity);
    uint32_t bottom_color      = (bottom_a << 24) | (bottom_b << 16) | (bottom_g << 8) | bottom_r;

    obs_data_set_int(settings, "color1", top_color);
    obs_data_set_int(settings, "color2", bottom_color);

    bool use_outline = capped_opacity == 1.0f ? true : false;

    // Enable outline, disable drop shadow
    obs_data_set_bool(settings, "outline", use_outline);
    obs_data_set_bool(settings, "drop_shadow", use_outline);
}

/**
 * @brief Set text colors at full opacity.
 *
 * Convenience wrapper around set_color_with_opacity() that applies colors at
 * 100% opacity. Used when displaying text in its normal, non-transitioning state.
 *
 * @param text_source Text source instance containing the active/inactive color selection state.
 * @param settings    OBS data object to update with color1 and color2 values.
 * @param config      Text source configuration containing active and inactive color definitions.
 */
static void set_color(text_source_t *text_source, obs_data_t *settings, const text_source_config_t *config) {
    set_color_with_opacity(text_source, settings, config, 1.0f);
}

static void set_font(text_source_t *text_source, obs_data_t *settings, const text_source_config_t *config) {
    obs_data_t *font = obs_data_create();
    obs_data_set_string(font, "face", config->font_face);
    obs_data_set_int(font, "size", config->font_size);
    obs_data_set_string(font, "style", config->font_style);
    obs_data_set_int(font, "flags", 0);
    obs_data_set_obj(settings, "font", font);
    obs_data_release(font);

    obs_log(LOG_DEBUG,
            "[%s] Private OBS text source settings is using font '%s' ('%s')",
            text_source->name,
            config->font_face,
            config->font_style);
}

static void set_text(text_source_t *text_source, obs_data_t *settings) {
    obs_data_set_string(settings, "text", text_source->current_text);
    obs_log(LOG_DEBUG,
            "[%s] Private OBS text source settings is using text '%s'",
            text_source->name,
            text_source->current_text);
}

static void complete_transition(text_source_t *text_source, const text_source_config_t *config) {

    obs_log(LOG_DEBUG, "[%s] Transition completed to show text '%s'", text_source->name, text_source->current_text);

    text_source->transition.phase   = TEXT_TRANSITION_NONE;
    text_source->transition.opacity = 1.0f;

    obs_data_t *settings = obs_source_get_settings(text_source->private_obs_source);
    set_color_with_opacity(text_source, settings, config, text_source->transition.opacity);
    obs_source_update(text_source->private_obs_source, settings);
    obs_data_release(settings);
}

static void initiate_fade_in_transition(text_source_t *text_source, const char *text, bool use_active_color) {

    obs_log(LOG_DEBUG, "[%s] Initiating fade-in transition to show '%s'", text_source->name, text);

    if (text_source->current_text) {
        bfree(text_source->current_text);
    }

    //  Sets the text to show now that the fade out is complete.
    text_source->current_text     = bstrdup(text);
    text_source->use_active_color = use_active_color;

    //  Initiates the fade-out transition
    text_source->transition.phase   = TEXT_TRANSITION_FADE_IN;
    text_source->transition.opacity = 0.0f;

    obs_data_t *settings = obs_source_get_settings(text_source->private_obs_source);
    set_text(text_source, settings);
    obs_source_update(text_source->private_obs_source, settings);
    obs_data_release(settings);
}

static void initiate_fade_out_transition(text_source_t *text_source, const char *text, bool use_active_color) {

    obs_log(LOG_DEBUG,
            "[%s] Initiating fade-out transition from text '%s' to '%s'",
            text_source->name,
            text_source->current_text,
            text);

    //  Sets the text to show once the fade out is complete.
    bfree(text_source->pending_text);
    text_source->pending_text             = bstrdup(text);
    text_source->pending_use_active_color = use_active_color;

    //  Initiates the fade-out transition
    text_source->transition.phase   = TEXT_TRANSITION_FADE_OUT;
    text_source->transition.opacity = 1.0f;
}

static obs_data_t *create_private_obs_source_settings(text_source_t *text_source, const text_source_config_t *config) {

    obs_data_t *settings = obs_data_create();

    if (!settings) {
        obs_log(LOG_ERROR, "[%s] Failed to create private OBS text source settings", text_source->name);
        return NULL;
    }

    // Set text content
    obs_data_set_string(settings, "text", text_source->current_text);

    // Set font using the font object for FreeType sources
    if (config->font_face && strlen(config->font_face) > 0) {
        set_font(text_source, settings, config);
    }

    set_color(text_source, settings, config);

    // Enable outline, disable drop shadow
    obs_data_set_bool(settings, "outline", false);
    obs_data_set_bool(settings, "drop_shadow", false);

    return settings;
}

static bool ensure_private_obs_source(text_source_t *text_source, const text_source_config_t *config) {

    if (text_source->private_obs_source) {
        return true;
    }

    obs_log(LOG_DEBUG, "[%s] Creating a private OBS text source settings", text_source->name);

    text_source->private_obs_source_settings = create_private_obs_source_settings(text_source, config);

    if (!text_source->private_obs_source_settings) {
        return false;
    }

    // Try to create a text source - try FreeType v2 first (most common on macOS)
    text_source->private_obs_source =
        obs_source_create_private("text_ft2_source_v2", "internal_text", text_source->private_obs_source_settings);

    if (!text_source->private_obs_source) {
        text_source->private_obs_source =
            obs_source_create_private("text_ft2_source", "internal_text", text_source->private_obs_source_settings);
    }

    if (!text_source->private_obs_source) {
        text_source->private_obs_source =
            obs_source_create_private("text_gdiplus_v2", "internal_text", text_source->private_obs_source_settings);
    }

    if (!text_source->private_obs_source) {
        text_source->private_obs_source =
            obs_source_create_private("text_gdiplus", "internal_text", text_source->private_obs_source_settings);
    }

    if (text_source->private_obs_source) {
        obs_log(LOG_DEBUG, "[%s] Private OBS text source has been created", text_source->name);
    }

    return text_source->private_obs_source != NULL;
}

text_source_t *text_source_create(obs_source_t *source, const char *name) {

    if (!name || !source) {
        obs_log(LOG_ERROR, "[TextSource] Failed to create text source - invalid parameters");
        return NULL;
    }

    text_source_t *text_source = bzalloc(sizeof(*text_source));

    if (!text_source) {
        obs_log(LOG_ERROR, "[%s] Failed to create text source - invalid parameters", name);
        return NULL;
    }

    text_source->name               = bstrdup(name);
    text_source->obs_source         = source;
    text_source->private_obs_source = NULL;
    text_source->texrender          = NULL;
    text_source->current_text       = NULL;
    text_source->pending_text       = bstrdup("");

    /* Sets transition state */
    text_source->transition.phase    = TEXT_TRANSITION_NONE;
    text_source->transition.opacity  = 1.0f;
    text_source->transition.duration = TEXT_TRANSITION_DEFAULT_DURATION;

    return text_source;
}

void text_source_destroy(text_source_t *text_source) {

    if (!text_source) {
        return;
    }

    if (text_source->private_obs_source) {
        obs_source_release(text_source->private_obs_source);
        text_source->private_obs_source = NULL;
    }

    if (text_source->private_obs_source_settings) {
        obs_data_release(text_source->private_obs_source_settings);
        text_source->private_obs_source_settings = NULL;
    }

    if (text_source->texrender) {
        obs_enter_graphics();
        gs_texrender_destroy(text_source->texrender);
        obs_leave_graphics();
        text_source->texrender = NULL;
    }

    if (text_source->current_text) {
        bfree(text_source->current_text);
        text_source->current_text = NULL;
    }

    if (text_source->pending_text) {
        bfree(text_source->pending_text);
        text_source->pending_text = NULL;
    }

    if (text_source->name) {
        bfree(text_source->name);
        text_source->name = NULL;
    }

    bfree(text_source);
}

bool text_source_update_text(text_source_t *text_source, bool *force_reload, const text_source_config_t *config,
                             const char *text, bool use_active_color) {

    if (!text_source || !force_reload || !config || !text) {
        return text_source && text_source->private_obs_source != NULL;
    }

    // If the source already exists, and we're not forcing a reload, keep it
    if (!*force_reload && text_source->private_obs_source) {
        return true;
    }

    if (!ensure_private_obs_source(text_source, config)) {
        obs_log(LOG_ERROR, "[%s] Failed to create internal OBS text source", text_source->name);
        return false;
    }

    if (!text_source->current_text) {
        initiate_fade_in_transition(text_source, text, use_active_color);
    } else if (text_source->current_text && strcmp(text_source->current_text, text) != 0) {
        initiate_fade_out_transition(text_source, text, use_active_color);
        goto completed;
    }

    //  Update the private OBS source settings.
    obs_data_t *settings = obs_source_get_settings(text_source->private_obs_source);
    set_font(text_source, settings, config);
    set_color(text_source, settings, config);

    obs_source_update(text_source->private_obs_source, settings);

    obs_log(LOG_DEBUG, "[%s] Private OBS text source settings have been updated", text_source->name);

completed:
    *force_reload = false;
    return true;
}

void text_source_render(text_source_t *text_source, const text_source_config_t *config, gs_effect_t *effect) {

    UNUSED_PARAMETER(config);
    UNUSED_PARAMETER(effect);

    if (!text_source || !text_source->private_obs_source) {
        return;
    }

    if (text_source->transition.last_opacity != text_source->transition.opacity) {
        obs_data_t *settings = obs_source_get_settings(text_source->private_obs_source);
        set_color_with_opacity(text_source, settings, config, text_source->transition.opacity);
        obs_source_update(text_source->private_obs_source, settings);
        obs_data_release(settings);
    }

    // Opacity is handled by updating the text color's alpha channel in tick()
    obs_source_video_render(text_source->private_obs_source);

    text_source->transition.last_opacity = text_source->transition.opacity;
}

void text_source_tick(text_source_t *text_source, const text_source_config_t *config, float seconds) {

    if (!text_source || !config || !text_source->private_obs_source) {
        return;
    }

    float duration = text_source->transition.duration;

    if (duration <= 0.0f) {
        duration = TEXT_TRANSITION_DEFAULT_DURATION;
    }

    switch (text_source->transition.phase) {
    case TEXT_TRANSITION_FADE_IN:
        text_source->transition.opacity = fminf(1.0f, text_source->transition.opacity + seconds / duration);

        if (text_source->transition.opacity >= 1.0f) {
            complete_transition(text_source, config);
        }
        break;

    case TEXT_TRANSITION_FADE_OUT:
        text_source->transition.opacity = fmaxf(0.0f, text_source->transition.opacity - seconds / duration);

        if (text_source->transition.opacity <= 0.0f) {
            initiate_fade_in_transition(text_source, text_source->pending_text, text_source->pending_use_active_color);
        }
        break;
    case TEXT_TRANSITION_NONE:
    default:
        break;
    }
}

void text_source_add_properties(obs_properties_t *props, bool supports_inactive_color) {

    if (!props) {
        return;
    }

    obs_properties_add_font(props, "text_font", "Font");

    // (Active) Color picker
    obs_properties_add_color(props, "text_active_top_color", "Active text color (Top)");
    obs_properties_add_color(props, "text_active_bottom_color", "Active text color (Bottom)");

    if (supports_inactive_color) {
        // (Inactive) Color picker
        obs_properties_add_color(props, "text_inactive_top_color", "Inactive text color (Top)");
        obs_properties_add_color(props, "text_inactive_bottom_color", "Inactive text color (Bottom)");
    }
}

void text_source_update_properties(obs_data_t *settings, text_source_config_t *config, bool *must_reload) {

    if (!settings || !must_reload || !config) {
        return;
    }

    if (obs_data_has_user_value(settings, "text_active_top_color")) {
        const uint32_t argb      = (uint32_t)obs_data_get_int(settings, "text_active_top_color");
        config->active_top_color = color_argb_to_rgba(argb);
        *must_reload             = true;
    }

    if (obs_data_has_user_value(settings, "text_active_bottom_color")) {
        const uint32_t argb         = (uint32_t)obs_data_get_int(settings, "text_active_bottom_color");
        config->active_bottom_color = color_argb_to_rgba(argb);
        *must_reload                = true;
    }

    if (obs_data_has_user_value(settings, "text_inactive_top_color")) {
        const uint32_t argb        = (uint32_t)obs_data_get_int(settings, "text_inactive_top_color");
        config->inactive_top_color = color_argb_to_rgba(argb);
        *must_reload               = true;
    }

    if (obs_data_has_user_value(settings, "text_inactive_bottom_color")) {
        const uint32_t argb           = (uint32_t)obs_data_get_int(settings, "text_inactive_bottom_color");
        config->inactive_bottom_color = color_argb_to_rgba(argb);
        *must_reload                  = true;
    }

    if (obs_data_has_user_value(settings, "text_size")) {
        config->font_size = (uint32_t)obs_data_get_int(settings, "text_size");
        *must_reload      = true;
    }

    if (obs_data_has_user_value(settings, "text_font")) {
        obs_data_t *font_obj = obs_data_get_obj(settings, "text_font");
        if (font_obj) {
            config->font_face  = obs_data_get_string(font_obj, "face");
            config->font_size  = (uint32_t)obs_data_get_int(font_obj, "size");
            config->font_style = obs_data_get_string(font_obj, "style");
            obs_log(LOG_DEBUG,
                    "[TextSource] Using font '%s' (%d) with style '%s'",
                    config->font_face,
                    config->font_size,
                    config->font_style);
            obs_data_release(font_obj);
            *must_reload = true;
        }
    }
}

uint32_t text_source_get_width(text_source_t *base) {
    if (!base || !base->private_obs_source) {
        return 0;
    }
    return obs_source_get_width(base->private_obs_source);
}

uint32_t text_source_get_height(text_source_t *base) {
    if (!base || !base->private_obs_source) {
        return 0;
    }
    return obs_source_get_height(base->private_obs_source);
}
