#include "sources/common/text_source.h"

#include <string.h>
#include <graphics/graphics.h>
#include <graphics/matrix4.h>

#include "drawing/color.h"
#include "system/font.h"
#include "diagnostics/log.h"

/**
 * @file text_source.c
 * @brief Implementation of common functionality for text-based OBS sources.
 */

/** Default duration for each fade phase (in seconds). */
#define TEXT_TRANSITION_DEFAULT_DURATION 0.5f

text_source_base_t *text_source_create(obs_source_t *source) {
    text_source_base_t *base = bzalloc(sizeof(*base));

    if (!base) {
        return NULL;
    }

    base->source               = source;
    base->text_freetype_source = NULL;
    base->current_text         = NULL;
    base->pending_text         = NULL;

    // Initialize transition state
    base->transition.phase    = TEXT_TRANSITION_NONE;
    base->transition.opacity  = 1.0f;
    base->transition.duration = TEXT_TRANSITION_DEFAULT_DURATION;

    return base;
}

void text_source_destroy(text_source_base_t *base) {

    if (!base) {
        return;
    }

    if (base->text_freetype_source) {
        obs_source_release(base->text_freetype_source);
        base->text_freetype_source = NULL;
    }

    if (base->current_text) {
        bfree(base->current_text);
        base->current_text = NULL;
    }

    if (base->pending_text) {
        bfree(base->pending_text);
        base->pending_text = NULL;
    }

    bfree(base);
}

bool text_source_reload(text_source_base_t *base, bool *must_reload, const text_source_config_t *config,
                        const char *text) {

    if (!base || !must_reload || !config || !text) {
        return base && base->text_freetype_source != NULL;
    }

    // If the source already exists, and we're not forcing a reload, keep it
    if (!*must_reload && base->text_freetype_source) {
        return true;
    }

    // Release existing text freetype source if present
    if (base->text_freetype_source) {
        obs_source_release(base->text_freetype_source);
        base->text_freetype_source = NULL;
    }

    // Create OBS text source settings
    obs_data_t *settings = obs_data_create();

    if (!settings) {
        obs_log(LOG_ERROR, "[TextSource] Failed to create settings object");
        return false;
    }

    // Set text content
    obs_data_set_string(settings, "text", text);

    // Set font using the font object for FreeType sources
    if (config->font_face && strlen(config->font_face) > 0) {
        obs_data_t *font = obs_data_create();
        obs_data_set_string(font, "face", config->font_face);
        obs_data_set_int(font, "size", config->font_size);
        obs_data_set_string(font, "style", config->font_style);
        obs_data_set_int(font, "flags", 0);
        obs_data_set_obj(settings, "font", font);
        obs_data_release(font);
    }

    // Set color - convert from RGBA to ABGR for OBS
    uint32_t active_top_rgba  = config->active_top_color;
    uint8_t  active_top_r     = (active_top_rgba >> 24) & 0xFF;
    uint8_t  active_top_g     = (active_top_rgba >> 16) & 0xFF;
    uint8_t  active_top_b     = (active_top_rgba >> 8) & 0xFF;
    uint8_t  active_top_a     = active_top_rgba & 0xFF;
    uint32_t active_top_color = (active_top_a << 24) | (active_top_b << 16) | (active_top_g << 8) | active_top_r;

    uint32_t active_bottom_rgba  = config->active_bottom_color;
    uint8_t  active_bottom_r     = (active_bottom_rgba >> 24) & 0xFF;
    uint8_t  active_bottom_g     = (active_bottom_rgba >> 16) & 0xFF;
    uint8_t  active_bottom_b     = (active_bottom_rgba >> 8) & 0xFF;
    uint8_t  active_bottom_a     = active_bottom_rgba & 0xFF;
    uint32_t active_bottom_color = (active_bottom_a << 24) | (active_bottom_b << 16) | (active_bottom_g << 8) | active_bottom_r;

    obs_data_set_int(settings, "color1", active_top_color);
    obs_data_set_int(settings, "color2", active_bottom_color);
    obs_data_set_int(settings, "opacity", 100);

    // Enable outline, disable drop shadow
    obs_data_set_bool(settings, "outline", true);
    obs_data_set_bool(settings, "drop_shadow", false);

    // Try to create a text source - try FreeType v2 first (most common on macOS)
    base->text_freetype_source = obs_source_create_private("text_ft2_source_v2", "internal_text", settings);

    if (!base->text_freetype_source) {
        base->text_freetype_source = obs_source_create_private("text_ft2_source", "internal_text", settings);
    }

    if (!base->text_freetype_source) {
        base->text_freetype_source = obs_source_create_private("text_gdiplus_v2", "internal_text", settings);
    }

    if (!base->text_freetype_source) {
        base->text_freetype_source = obs_source_create_private("text_gdiplus", "internal_text", settings);
    }

    obs_data_release(settings);

    if (!base->text_freetype_source) {
        obs_log(LOG_ERROR, "[TextSource] Failed to create internal OBS text source - no text source type available");
        *must_reload = false;
        return false;
    }

    // Store current text
    if (base->current_text) {
        bfree(base->current_text);
    }

    base->current_text = bstrdup(text);

    // Start with fade-in transition
    base->transition.phase   = TEXT_TRANSITION_FADE_IN;
    base->transition.opacity = 0.0f;

    *must_reload = false;

    return true;
}

void text_source_render(text_source_base_t *base, gs_effect_t *effect) {

    if (!base || !base->text_freetype_source) {
        return;
    }

    UNUSED_PARAMETER(effect);

    // Apply transition opacity
    const float opacity = base->transition.opacity;

    gs_blend_state_push();
    gs_enable_blending(true);
    gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);

    // Set color modulation for opacity (white with alpha)
    struct vec4 color_vec;
    vec4_set(&color_vec, 1.0f, 1.0f, 1.0f, opacity);

    gs_effect_t *current_effect = gs_get_effect();

    if (current_effect) {
        gs_eparam_t *color_param = gs_effect_get_param_by_name(current_effect, "color");
        if (color_param) {
            gs_effect_set_vec4(color_param, &color_vec);
        }
    }

    obs_source_video_render(base->text_freetype_source);

    gs_blend_state_pop();
}

void text_source_tick(text_source_base_t *base, const text_source_config_t *config, float seconds) {

    if (!base || !config) {
        return;
    }

    float duration = base->transition.duration;
    if (duration <= 0.0f) {
        duration = TEXT_TRANSITION_DEFAULT_DURATION;
    }

    switch (base->transition.phase) {
    case TEXT_TRANSITION_FADE_IN:
        base->transition.opacity += seconds / duration;
        if (base->transition.opacity >= 1.0f) {
            base->transition.opacity = 1.0f;
            base->transition.phase   = TEXT_TRANSITION_NONE;
        }
        break;

    case TEXT_TRANSITION_FADE_OUT:
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

    // Alignment dropdown
    obs_property_t *align_list =
        obs_properties_add_list(props, "text_align", "Text alignment", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(align_list, "Left", TEXT_ALIGN_LEFT);
    obs_property_list_add_int(align_list, "Right", TEXT_ALIGN_RIGHT);
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

    if (obs_data_has_user_value(settings, "text_alternate_color")) {
        const uint32_t argb        = (uint32_t)obs_data_get_int(settings, "text_alternate_color");
        config->inactive_top_color = color_argb_to_rgba(argb);
        *must_reload               = true;
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

uint32_t text_source_get_width(text_source_base_t *base) {
    if (!base || !base->text_freetype_source) {
        return 0;
    }
    return obs_source_get_width(base->text_freetype_source);
}

uint32_t text_source_get_height(text_source_base_t *base) {
    if (!base || !base->text_freetype_source) {
        return 0;
    }
    return obs_source_get_height(base->text_freetype_source);
}
