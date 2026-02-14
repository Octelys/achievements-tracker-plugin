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
#define TEXT_TRANSITION_DEFAULT_DURATION 1.0f

static void complete_transition(text_source_t *text_source) {

    obs_log(LOG_INFO, "[%s] Transition completed to show text '%s'", text_source->name, text_source->current_text);

    text_source->transition.phase   = TEXT_TRANSITION_NONE;
    text_source->transition.opacity = 1.0f;
}

static void initiate_fade_in_transition(text_source_t *text_source, const char *text) {

    obs_log(LOG_INFO, "[%s] Initiating fade-in transition to show '%s'", text_source->name, text);

    if (text_source->current_text) {
        bfree(text_source->current_text);
    }

    //  Sets the text to show once the fade out is complete.
    text_source->current_text = bstrdup(text);

    //  Initiates the fade-out transition
    text_source->transition.phase   = TEXT_TRANSITION_FADE_IN;
    text_source->transition.opacity = 0.0f;
}

static void initiate_fade_out_transition(text_source_t *text_source, const char *text) {

    obs_log(LOG_INFO,
            "[%s] Initiating fade-out transition from text '%s' to '%s'",
            text_source->name,
            text_source->current_text,
            text);

    //  Sets the text to show once the fade out is complete.
    bfree(text_source->pending_text);
    text_source->pending_text = bstrdup(text);

    //  Initiates the fade-out transition
    text_source->transition.phase   = TEXT_TRANSITION_FADE_OUT;
    text_source->transition.opacity = 1.0f;
}

static void set_font(text_source_t *text_source, obs_data_t *settings, const text_source_config_t *config) {
    obs_data_t *font = obs_data_create();
    obs_data_set_string(font, "face", config->font_face);
    obs_data_set_int(font, "size", config->font_size);
    obs_data_set_string(font, "style", config->font_style);
    obs_data_set_int(font, "flags", 0);
    obs_data_set_obj(settings, "font", font);
    obs_data_release(font);

    obs_log(LOG_INFO,
            "[%s] Private OBS text source settings is using font '%s' ('%s')",
            text_source->name,
            config->font_face,
            config->font_style);
}

static void set_color(text_source_t *text_source, obs_data_t *settings, const text_source_config_t *config) {
    // Set color - convert from RGBA to ABGR for OBS
    uint32_t top_rgba  = config->active_top_color;
    uint8_t  top_r     = (top_rgba >> 24) & 0xFF;
    uint8_t  top_g     = (top_rgba >> 16) & 0xFF;
    uint8_t  top_b     = (top_rgba >> 8) & 0xFF;
    uint8_t  top_a     = /*top_rgba & 0xFF;*/ text_source->transition.opacity * 0xFF;
    uint32_t top_color = (top_a << 24) | (top_b << 16) | (top_g << 8) | top_r;

    uint32_t bottom_rgba  = config->active_bottom_color;
    uint8_t  bottom_r     = (bottom_rgba >> 24) & 0xFF;
    uint8_t  bottom_g     = (bottom_rgba >> 16) & 0xFF;
    uint8_t  bottom_b     = (bottom_rgba >> 8) & 0xFF;
    uint8_t  bottom_a     = /*bottom_rgba & 0xFF;*/ text_source->transition.opacity * 0xFF;
    uint32_t bottom_color = (bottom_a << 24) | (bottom_b << 16) | (bottom_g << 8) | bottom_r;

    obs_data_set_int(settings, "color1", top_color);
    obs_data_set_int(settings, "color2", bottom_color);

    obs_log(LOG_INFO,
            "[%s] Private OBS text source settings is using colors '%X' and %X'",
            text_source->name,
            top_color,
            bottom_color);
}

static void set_text(text_source_t *text_source, obs_data_t *settings) {
    obs_data_set_string(settings, "text", text_source->current_text);
    obs_log(LOG_INFO,
            "[%s] Private OBS text source settings is using text '%s'",
            text_source->name,
            text_source->current_text);
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

    obs_log(LOG_INFO, "[%s] Creating a private OBS text source settings", text_source->name);

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
        obs_log(LOG_INFO, "[%s] Private OBS text source has been created", text_source->name);
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

    if (text_source->current_text) {
        bfree(text_source->current_text);
        text_source->current_text = NULL;
    }

    if (text_source->pending_text) {
        bfree(text_source->pending_text);
        text_source->pending_text = NULL;
    }

    bfree(text_source);
}

bool text_source_reload(text_source_t *text_source, bool *must_reload, const text_source_config_t *config,
                        const char *text) {

    if (!text_source || !must_reload || !config || !text) {
        return text_source && text_source->private_obs_source != NULL;
    }

    // If the source already exists, and we're not forcing a reload, keep it
    if (!*must_reload && text_source->private_obs_source) {
        return true;
    }

    if (!ensure_private_obs_source(text_source, config)) {
        obs_log(LOG_ERROR, "[%s] Failed to create internal OBS text source", text_source->name);
        return false;
    }

    if (!text_source->current_text) {
        initiate_fade_in_transition(text_source, text);
    } else if (text_source->current_text && strcmp(text_source->current_text, text) != 0) {
        initiate_fade_out_transition(text_source, text);
        goto completed;
    }

    //  Update the private OBS source settings.
    obs_data_t *settings = obs_source_get_settings(text_source->private_obs_source);
    set_font(text_source, settings, config);
    set_color(text_source, settings, config);
    set_text(text_source, settings);
    obs_source_update(text_source->private_obs_source, settings);

    obs_log(LOG_INFO, "[%s] Private OBS text source settings have been updated", text_source->name);

completed:
    *must_reload = false;
    return true;
}

void text_source_render(text_source_t *text_source, const text_source_config_t *config, gs_effect_t *effect) {

    if (!text_source || !text_source->private_obs_source) {
        return;
    }

    UNUSED_PARAMETER(effect);

    if (text_source->transition.phase != TEXT_TRANSITION_NONE) {
        obs_data_t *settings = obs_source_get_settings(text_source->private_obs_source);
        set_color(text_source, settings, config);
        obs_source_update(text_source->private_obs_source, settings);
    }

    obs_source_video_render(text_source->private_obs_source);
}

void text_source_tick(text_source_t *text_source, const text_source_config_t *config, float seconds) {

    if (!text_source || !config) {
        return;
    }

    float duration = text_source->transition.duration;

    if (duration <= 0.0f) {
        duration = TEXT_TRANSITION_DEFAULT_DURATION;
    }

    float old_opacity = text_source->transition.opacity;

    switch (text_source->transition.phase) {
    case TEXT_TRANSITION_FADE_IN:
        text_source->transition.opacity += seconds / duration;
        if (text_source->transition.opacity >= 1.0f) {
            complete_transition(text_source);
        }
        break;

    case TEXT_TRANSITION_FADE_OUT:
        text_source->transition.opacity -= seconds / duration;
        if (text_source->transition.opacity <= 0.0f) {
            initiate_fade_in_transition(text_source, text_source->pending_text);

            bool must_reload = true;
            text_source_reload(text_source, &must_reload, config, text_source->current_text);
        }
        break;
    case TEXT_TRANSITION_NONE:
    default:
        break;
    }

    // Update opacity in the private source if it changed
    if (text_source->private_obs_source && fabsf(old_opacity - text_source->transition.opacity) > 0.001f) {
        obs_data_t *settings = obs_source_get_settings(text_source->private_obs_source);
        if (settings) {
            int opacity_percent = (int)(text_source->transition.opacity * 100.0f);
            obs_data_set_int(settings, "opacity", opacity_percent);
            obs_source_update(text_source->private_obs_source, settings);
            obs_data_release(settings);
        }
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

    /*
    // Alignment dropdown
    obs_property_t *align_list =
        obs_properties_add_list(props, "text_align", "Text alignment", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(align_list, "Left", TEXT_ALIGN_LEFT);
    obs_property_list_add_int(align_list, "Right", TEXT_ALIGN_RIGHT);
    */
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
            obs_log(LOG_INFO,
                    "[TextSource] Using font '%s' (%d) with style '%s'",
                    config->font_face,
                    config->font_size,
                    config->font_style);
            obs_data_release(font_obj);
            *must_reload = true;
        }
    }

    if (obs_data_has_user_value(settings, "text_align")) {
        config->align = (text_align_t)obs_data_get_int(settings, "text_align");
        *must_reload  = true;
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
