#include "sources/common/text_source.h"

#include <graphics/graphics.h>
#include <graphics/matrix4.h>

#include "drawing/color.h"
#include "system/font.h"

/**
 * @file text_source.c
 * @brief Implementation of common functionality for text-based OBS sources.
 */

text_source_base_t *text_source_create(obs_source_t *source, source_size_t size) {

    text_source_base_t *base = bzalloc(sizeof(*base));
    if (!base) {
        return NULL;
    }

    base->source = source;
    base->size   = size;

    return base;
}

bool text_source_reload_if_needed(text_context_t **ctx, bool *must_reload, const text_source_config_t *config,
                                  const text_source_base_t *base, const char *text) {

    if (!must_reload || !ctx || !config || !base) {
        return ctx != NULL && *ctx != NULL;
    }

    if (!*must_reload && *ctx) {
        return true;
    }

    if (*ctx) {
        text_context_destroy(*ctx);
        *ctx = NULL;
    }

    *ctx = text_context_create(config, base->size, text);

    *must_reload = false;

    return *ctx != NULL;
}

void text_source_render_unscaled(text_context_t *ctx, gs_effect_t *effect) {

    if (!ctx) {
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

    text_context_draw(ctx, effect);

    gs_matrix_pop();
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

void text_source_update_properties(obs_data_t *settings, text_source_config_t *config, bool *must_reload) {

    if (!settings || !must_reload || !config) {
        return;
    }

    if (obs_data_has_user_value(settings, "text_color")) {
        const uint32_t argb = (uint32_t)obs_data_get_int(settings, "text_color");
        config->color       = color_argb_to_rgba(argb);
        *must_reload        = true;
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
