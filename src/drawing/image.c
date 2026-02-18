#include "image.h"

#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/matrix4.h>

/* Static effects cached for the lifetime of the plugin */
static gs_effect_t *greyscale_effect                 = NULL;
static gs_effect_t *opacity_effect                   = NULL;
static gs_effect_t *greyscale_opacity_effect         = NULL;
static bool         greyscale_load_attempted         = false;
static bool         opacity_load_attempted           = false;
static bool         greyscale_opacity_load_attempted = false;

void draw_texture(gs_texture_t *texture, const uint32_t width, const uint32_t height, gs_effect_t *effect) {

    if (!texture) {
        return;
    }

    /* Use the passed effect or get the default if NULL */
    gs_effect_t *used_effect = effect ? effect : obs_get_base_effect(OBS_EFFECT_DEFAULT);

    /* If we didn't get an effect and OBS default effect isn't available, bail. */
    if (!used_effect) {
        return;
    }

    /* Only start our own effect loop if no effect is currently active.
     * If an effect was passed in, it's already active - just set texture and draw. */
    if (effect) {
        /* Effect already active from caller - just set texture and draw */
        gs_eparam_t *image_param = gs_effect_get_param_by_name(effect, "image");
        if (image_param)
            gs_effect_set_texture(image_param, texture);
        gs_draw_sprite(texture, 0, width, height);
    } else {
        /* No effect passed - start our own loop */
        gs_eparam_t *image_param = gs_effect_get_param_by_name(used_effect, "image");

        if (image_param) {
            gs_effect_set_texture(image_param, texture);
        }

        while (gs_effect_loop(used_effect, "Draw")) {
            gs_draw_sprite(texture, 0, width, height);
        }
    }
}

void draw_texture_greyscale(gs_texture_t *texture, const uint32_t width, const uint32_t height, gs_effect_t *effect) {

    UNUSED_PARAMETER(effect);

    if (!texture) {
        return;
    }

    /*
     * We ignore the passed effect and use our own greyscale effect.
     * This is necessary because OBS's default effect doesn't support color manipulation.
     */

    if (!greyscale_effect && !greyscale_load_attempted) {
        greyscale_load_attempted = true;

        const char *effect_code = "uniform float4x4 ViewProj;\n"
                                  "uniform texture2d image;\n"
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
                                  "float4 PSGreyscale(VertInOut vert_in) : TARGET\n"
                                  "{\n"
                                  "    float4 rgba = image.Sample(def_sampler, vert_in.uv);\n"
                                  "    float luma = rgba.r * 0.42 + rgba.g * 0.45 + rgba.b * 0.13;\n"
                                  "    return float4(luma, luma, luma, rgba.a);\n"
                                  "}\n"
                                  "\n"
                                  "technique Draw\n"
                                  "{\n"
                                  "    pass\n"
                                  "    {\n"
                                  "        vertex_shader = VSDefault(vert_in);\n"
                                  "        pixel_shader  = PSGreyscale(vert_in);\n"
                                  "    }\n"
                                  "}\n";

        char *error_string = NULL;
        greyscale_effect   = gs_effect_create(effect_code, "greyscale_inline", &error_string);

        if (error_string) {
            blog(LOG_ERROR, "[Greyscale] Effect compile error: %s", error_string);
            bfree(error_string);
        } else if (greyscale_effect) {
            blog(LOG_DEBUG, "[Greyscale] Custom effect created successfully");
        } else {
            blog(LOG_WARNING, "[Greyscale] Failed to create custom effect (no error string)");
        }
    }

    /* Fallback to normal draw if the greyscale effect failed */
    if (!greyscale_effect) {
        draw_texture(texture, width, height, effect);
        return;
    }

    /*
     * We can't use gs_effect_loop because OBS already has an effect active.
     * Instead, we use gs_technique_begin/end directly.
     */
    gs_effect_set_texture(gs_effect_get_param_by_name(greyscale_effect, "image"), texture);

    gs_technique_t *tech = gs_effect_get_technique(greyscale_effect, "Draw");
    if (tech) {
        gs_technique_begin(tech);
        gs_technique_begin_pass(tech, 0);
        gs_draw_sprite(texture, 0, width, height);
        gs_technique_end_pass(tech);
        gs_technique_end(tech);
    } else {
        /* Fallback if technique not found */
        draw_texture(texture, width, height, effect);
    }
}

void draw_texture_with_opacity(gs_texture_t *texture, const uint32_t width, const uint32_t height, gs_effect_t *effect,
                               float opacity) {

    UNUSED_PARAMETER(effect);

    if (!texture) {
        return;
    }

    // Create an inline effect with opacity support on first use (same as text opacity effect)
    if (!opacity_effect && !opacity_load_attempted) {
        opacity_load_attempted = true;

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
        opacity_effect     = gs_effect_create(effect_code, "image_opacity_effect", &error_string);

        if (error_string) {
            blog(LOG_ERROR, "[ImageOpacity] Effect compile error: %s", error_string);
            bfree(error_string);
        }
    }

    if (opacity_effect) {
        gs_effect_set_texture(gs_effect_get_param_by_name(opacity_effect, "image"), texture);

        struct vec4 color;
        vec4_set(&color, 1.0f, 1.0f, 1.0f, opacity);
        gs_effect_set_vec4(gs_effect_get_param_by_name(opacity_effect, "color"), &color);

        gs_technique_t *tech = gs_effect_get_technique(opacity_effect, "Draw");
        if (tech) {
            gs_technique_begin(tech);
            gs_technique_begin_pass(tech, 0);
            gs_draw_sprite(texture, 0, width, height);
            gs_technique_end_pass(tech);
            gs_technique_end(tech);
        }
    } else {
        // Fallback: draw without opacity
        draw_texture(texture, width, height, NULL);
    }
}

void draw_texture_greyscale_with_opacity(gs_texture_t *texture, const uint32_t width, const uint32_t height,
                                         gs_effect_t *effect, float opacity) {

    UNUSED_PARAMETER(effect);

    if (!texture) {
        return;
    }

    // Create an inline greyscale effect with opacity support
    if (!greyscale_opacity_effect && !greyscale_opacity_load_attempted) {
        greyscale_opacity_load_attempted = true;

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
                                  "float4 PSGreyscaleOpacity(VertInOut vert_in) : TARGET\n"
                                  "{\n"
                                  "    float4 rgba = image.Sample(def_sampler, vert_in.uv);\n"
                                  "    float luma = rgba.r * 0.299 + rgba.g * 0.587 + rgba.b * 0.114;\n"
                                  "    return float4(luma, luma, luma, rgba.a) * color;\n"
                                  "}\n"
                                  "\n"
                                  "technique Draw\n"
                                  "{\n"
                                  "    pass\n"
                                  "    {\n"
                                  "        vertex_shader = VSDefault(vert_in);\n"
                                  "        pixel_shader  = PSGreyscaleOpacity(vert_in);\n"
                                  "    }\n"
                                  "}\n";

        char *error_string       = NULL;
        greyscale_opacity_effect = gs_effect_create(effect_code, "greyscale_opacity_effect", &error_string);

        if (error_string) {
            blog(LOG_ERROR, "[GreyscaleOpacity] Effect compile error: %s", error_string);
            bfree(error_string);
        }
    }

    if (greyscale_opacity_effect) {
        gs_effect_set_texture(gs_effect_get_param_by_name(greyscale_opacity_effect, "image"), texture);

        struct vec4 color;
        vec4_set(&color, 1.0f, 1.0f, 1.0f, opacity);
        gs_effect_set_vec4(gs_effect_get_param_by_name(greyscale_opacity_effect, "color"), &color);

        gs_technique_t *tech = gs_effect_get_technique(greyscale_opacity_effect, "Draw");
        if (tech) {
            gs_technique_begin(tech);
            gs_technique_begin_pass(tech, 0);
            gs_draw_sprite(texture, 0, width, height);
            gs_technique_end_pass(tech);
            gs_technique_end(tech);
        }
    } else {
        // Fallback: draw greyscale without opacity
        draw_texture_greyscale(texture, width, height, NULL);
    }
}

void image_cleanup(void) {
    /* Clean up static effects created by this module.
     * These are created once and cached but need to be destroyed on plugin unload. */

    if (greyscale_effect) {
        gs_effect_destroy(greyscale_effect);
        greyscale_effect = NULL;
    }

    if (opacity_effect) {
        gs_effect_destroy(opacity_effect);
        opacity_effect = NULL;
    }

    if (greyscale_opacity_effect) {
        gs_effect_destroy(greyscale_opacity_effect);
        greyscale_opacity_effect = NULL;
    }
}
