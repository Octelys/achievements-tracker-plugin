#include "sources/common/text_source.h"

#include <graphics/graphics.h>
#include <graphics/matrix4.h>

#include "drawing/color.h"
#include "system/font.h"

/**
 * @file text_source.c
 * @brief Implementation of common functionality for text-based OBS sources.
 */

/** Default duration for each fade phase (in seconds). */
#define TEXT_TRANSITION_DEFAULT_DURATION 0.5f

text_source_base_t *text_source_create(obs_source_t *source, source_size_t size) {

    text_source_base_t *base = bzalloc(sizeof(*base));
    if (!base) {
        return NULL;
    }

    base->source = source;
    base->size   = size;

    // Initialize transition state
    base->transition.phase    = TEXT_TRANSITION_NONE;
    base->transition.opacity  = 1.0f;
    base->transition.duration = TEXT_TRANSITION_DEFAULT_DURATION;
    base->pending_text        = NULL;

    return base;
}

bool text_source_reload(text_context_t **ctx, bool *must_reload, const text_source_config_t *config,
                        text_source_base_t *base, const char *text) {

    if (!must_reload || !ctx || !config || !base) {
        return ctx != NULL && *ctx != NULL;
    }

    if (!*must_reload && *ctx) {
        return true;
    }

    // If we already have a context and need to reload, start a fade-out transition
    if (*ctx && base->transition.phase == TEXT_TRANSITION_NONE) {
        // Store the new text for later and start fading out
        if (base->pending_text) {
            bfree(base->pending_text);
        }
        base->pending_text       = bstrdup(text);
        base->transition.phase   = TEXT_TRANSITION_FADE_OUT;
        base->transition.opacity = 1.0f;
        *must_reload             = false;
        return true;
    }

    // If we're in a transition, don't reload yet
    if (base->transition.phase != TEXT_TRANSITION_NONE) {
        *must_reload = false;
        return *ctx != NULL;
    }

    // No existing context or transition complete - create new context
    if (*ctx) {
        text_context_destroy(*ctx);
        *ctx = NULL;
    }

    *ctx = text_context_create(config, base->size, text);

    // First time display: show immediately at full opacity (no fade-in)
    // The fade-in is only used after a fade-out completes (handled in tick)
    if (*ctx) {
        base->transition.phase   = TEXT_TRANSITION_FADE_IN;
        base->transition.opacity = 0.0f;
    }

    *must_reload = false;

    return *ctx != NULL;
}

void text_source_render(text_context_t *ctx, text_source_base_t *base, gs_effect_t *effect) {

    if (!ctx || !base || !ctx->texture) {
        return;
    }

    // Get the current transformation matrix to extract translation
    struct matrix4 current_matrix;
    gs_matrix_get(&current_matrix);

    // Extract translation from the matrix
    float trans_x = current_matrix.t.x;
    float trans_y = current_matrix.t.y;

    // Build a new matrix: translation only (no scaling)
    gs_matrix_push();
    gs_matrix_identity();
    gs_matrix_translate3f(trans_x, trans_y, 0.0f);

    // Apply transition opacity
    float opacity = base->transition.opacity;

    // Create an inline effect with opacity support on first use
    static gs_effect_t *opacity_effect        = NULL;
    static bool         effect_load_attempted = false;

    if (!opacity_effect && !effect_load_attempted) {
        effect_load_attempted = true;

        const char *effect_code = "uniform float4x4 ViewProj;\n"
                                  "uniform texture2d image;\n"
                                  "uniform float4 color;\n"
                                  "\n"
                                  "sampler_state def_sampler {\n"
                                  "    Filter   = Linear;\n"
                                  "    AddressU = Clamp;\n"
                                  "    AddressV = Clamp;\n"
                                  "};\n"
                                  "\n"
                                  "struct VertInOut {\n"
                                  "    float4 pos : POSITION;\n"
                                  "    float2 uv  : TEXCOORD0;\n"
                                  "};\n"
                                  "\n"
                                  "VertInOut VSDefault(VertInOut vert_in)\n"
                                  "{\n"
                                  "    VertInOut vert_out;\n"
                                  "    vert_out.pos = mul(float4(vert_in.pos.xyz, 1.0), ViewProj);\n"
                                  "    vert_out.uv  = vert_in.uv;\n"
                                  "    return vert_out;\n"
                                  "}\n"
                                  "\n"
                                  "float4 PSDrawOpacity(VertInOut vert_in) : TARGET\n"
                                  "{\n"
                                  "    float4 rgba = image.Sample(def_sampler, vert_in.uv);\n"
                                  "    return rgba * color;\n"
                                  "}\n"
                                  "\n"
                                  "technique Draw\n"
                                  "{\n"
                                  "    pass\n"
                                  "    {\n"
                                  "        vertex_shader = VSDefault(vert_in);\n"
                                  "        pixel_shader  = PSDrawOpacity(vert_in);\n"
                                  "    }\n"
                                  "}\n";

        char *error_string = NULL;
        opacity_effect     = gs_effect_create(effect_code, "text_opacity_effect", &error_string);

        if (error_string) {
            blog(LOG_ERROR, "[TextSource] Opacity effect compile error: %s", error_string);
            bfree(error_string);
        }
    }

    UNUSED_PARAMETER(effect);

    // Use our opacity-supporting effect
    if (opacity_effect) {
        gs_eparam_t *image_param = gs_effect_get_param_by_name(opacity_effect, "image");
        if (image_param) {
            gs_effect_set_texture(image_param, ctx->texture);
        }

        gs_eparam_t *color_param = gs_effect_get_param_by_name(opacity_effect, "color");
        if (color_param) {
            struct vec4 color;
            vec4_set(&color, 1.0f, 1.0f, 1.0f, opacity);
            gs_effect_set_vec4(color_param, &color);
        }

        gs_technique_t *tech = gs_effect_get_technique(opacity_effect, "Draw");
        if (tech) {
            gs_technique_begin(tech);
            gs_technique_begin_pass(tech, 0);
            gs_draw_sprite(ctx->texture, 0, ctx->width, ctx->height);
            gs_technique_end_pass(tech);
            gs_technique_end(tech);
        }
    } else {
        // Fallback: draw without opacity
        gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
        if (default_effect) {
            gs_eparam_t *image_param = gs_effect_get_param_by_name(default_effect, "image");
            if (image_param) {
                gs_effect_set_texture(image_param, ctx->texture);
            }

            while (gs_effect_loop(default_effect, "Draw")) {
                gs_draw_sprite(ctx->texture, 0, ctx->width, ctx->height);
            }
        }
    }

    gs_matrix_pop();
}

void text_source_tick(text_source_base_t *base, text_context_t **ctx, const text_source_config_t *config,
                      float seconds) {

    if (!base || !ctx || !config) {
        return;
    }

    float duration = base->transition.duration;
    if (duration <= 0.0f) {
        duration = TEXT_TRANSITION_DEFAULT_DURATION;
    }

    switch (base->transition.phase) {
    case TEXT_TRANSITION_FADE_OUT:
        base->transition.opacity -= seconds / duration;
        if (base->transition.opacity <= 0.0f) {
            base->transition.opacity = 0.0f;
            // Fade-out complete, switch to the pending text
            if (*ctx) {
                text_context_destroy(*ctx);
                *ctx = NULL;
            }
            if (base->pending_text) {
                *ctx = text_context_create(config, base->size, base->pending_text);
                bfree(base->pending_text);
                base->pending_text = NULL;
            }
            // Start fade-in
            base->transition.phase = TEXT_TRANSITION_FADE_IN;
        }
        break;

    case TEXT_TRANSITION_FADE_IN:
        base->transition.opacity += seconds / duration;
        if (base->transition.opacity >= 1.0f) {
            base->transition.opacity = 1.0f;
            base->transition.phase   = TEXT_TRANSITION_NONE;
        }
        break;

    case TEXT_TRANSITION_NONE:
    default:
        break;
    }
}

void text_source_add_properties(obs_properties_t *props) {

    if (!props) {
        return;
    }

    // Font dropdown
    obs_property_t *font_list =
        obs_properties_add_list(props, "text_font", "Font", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

    size_t  font_count = 0;
    font_t *fonts      = font_list_available(&font_count);

    if (fonts) {
        for (size_t i = 0; i < font_count; i++) {
            if (fonts[i].name && fonts[i].path) {
                obs_property_list_add_string(font_list, fonts[i].name, fonts[i].path);
            }
        }
        font_list_free(fonts, font_count);
    }

    // Color picker
    obs_properties_add_color(props, "text_color", "Text color");

    // Size slider
    obs_properties_add_int(props, "text_size", "Text size", 10, 164, 1);

    // Alignment dropdown
    obs_property_t *align_list =
        obs_properties_add_list(props, "text_align", "Text alignment", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(align_list, "Left", TEXT_ALIGN_LEFT);
    obs_property_list_add_int(align_list, "Right", TEXT_ALIGN_RIGHT);
}

void text_source_add_alternate_color_property(obs_properties_t *props) {

    if (!props) {
        return;
    }

    obs_properties_add_color(props, "text_alternate_color", "Locked achievement color");
}

void text_source_update_properties(obs_data_t *settings, text_source_config_t *config, bool *must_reload) {

    if (!settings || !must_reload || !config) {
        return;
    }

    if (obs_data_has_user_value(settings, "text_color")) {
        const uint32_t argb = (uint32_t)obs_data_get_int(settings, "text_color");
        config->color       = color_argb_to_rgba(argb);
        *must_reload        = true;
    }

    if (obs_data_has_user_value(settings, "text_alternate_color")) {
        const uint32_t argb     = (uint32_t)obs_data_get_int(settings, "text_alternate_color");
        config->alternate_color = color_argb_to_rgba(argb);
        *must_reload            = true;
    }

    if (obs_data_has_user_value(settings, "text_size")) {
        config->font_size = (uint32_t)obs_data_get_int(settings, "text_size");
        *must_reload      = true;
    }

    if (obs_data_has_user_value(settings, "text_font")) {
        config->font_path = obs_data_get_string(settings, "text_font");
        *must_reload      = true;
    }

    if (obs_data_has_user_value(settings, "text_align")) {
        config->align = (text_align_t)obs_data_get_int(settings, "text_align");
        *must_reload  = true;
    }
}
