#include "text.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include "common/memory.h"
#include "diagnostics/log.h"

#include <inttypes.h>
#include <obs-module.h>
#include <graphics/graphics.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void blit_glyph_rgba(uint8_t *dst, uint32_t dst_w, uint32_t dst_h, const FT_Bitmap *bmp, uint32_t x,
                            uint32_t y, uint32_t color) {

    if (!bmp || !bmp->buffer || bmp->width == 0 || bmp->rows == 0) {

        return;
    }

    const int pitch = bmp->pitch;

    for (uint32_t row = 0; row < (uint32_t)bmp->rows; row++) {

        uint32_t dy = y + row;

        if (dy >= dst_h) {
            break;
        }

        const uint8_t *src = NULL;

        if (pitch < 0) {
            // FreeType can store rows upside down when pitch is negative.
            src = bmp->buffer + ((bmp->rows - 1 - row) * (uint32_t)(-pitch));
        } else {
            src = bmp->buffer + row * (uint32_t)pitch;
        }

        for (uint32_t col = 0; col < (uint32_t)bmp->width; col++) {

            uint32_t dx = x + col;

            if (dx >= dst_w) {
                break;
            }

            uint8_t a = src[col];
            if (a == 0) {
                continue;
            }

            uint32_t idx = (dy * dst_w + dx) * 4;

            uint8_t r = (uint8_t)((color >> 24) & 0xFF);
            uint8_t g = (uint8_t)((color >> 16) & 0xFF);
            uint8_t b = (uint8_t)((color >> 8) & 0xFF);

            dst[idx + 0] = r; // R
            dst[idx + 1] = g; // G
            dst[idx + 2] = b; // B
            dst[idx + 3] = a; // A
        }
    }
}

text_context_t *text_context_create(const char *ttf_path,
                                    uint32_t width,
                                    uint32_t height,
                                    const char *text,
                                    uint32_t px_size,
                                    uint32_t color) {

    text_context_t *out = NULL;

    if (!ttf_path || !text) {
        obs_log(LOG_WARNING, "Unable to create the text context: invalid text parameters");
        return out;
    }

    obs_log(LOG_INFO, "Loading font %s into a texture", ttf_path);

    FT_Library ft   = NULL;
    FT_Face    face = NULL;

    if (FT_Init_FreeType(&ft) != 0) {
        obs_log(LOG_WARNING, "Unable to create the text context: unable to initialize the font library");
        return out;
    }

    if (FT_New_Face(ft, ttf_path, 0, &face) != 0) {
        obs_log(LOG_WARNING, "Unable to create the text context: unable to create a new font '%s'", ttf_path);
        FT_Done_FreeType(ft);
        return out;
    }

    if (FT_Select_Charmap(face, FT_ENCODING_UNICODE) != 0) {
        obs_log(LOG_WARNING, "Unable to create the text context: font has no Unicode charmap");
        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        return out;
    }

    (void)FT_Set_Pixel_Sizes(face, 0, px_size);

    const uint32_t padding = 4;

    const bool use_bounds = (width == 0 || height == 0);

    uint32_t w = 16;
    uint32_t h = 16;
    int32_t offset_x = 0;
    int32_t offset_y = 0;

    if (use_bounds) {
        // First pass: measure glyph bounds.
        int32_t min_x = INT32_MAX;
        int32_t min_y = INT32_MAX;
        int32_t max_x = INT32_MIN;
        int32_t max_y = INT32_MIN;

        int32_t pen_x   = (int32_t)padding;
        int32_t baseline = (int32_t)padding + (int32_t)px_size;

        for (size_t i = 0; text[i] != '\0'; i++) {

            if (FT_Load_Char(face, (unsigned char)text[i], FT_LOAD_RENDER) != 0) {
                continue;
            }

            const FT_GlyphSlot g = face->glyph;

            if (g->bitmap.width == 0 || g->bitmap.rows == 0) {
                pen_x += (int32_t)(g->advance.x >> 6);
                continue;
            }

            int32_t glyph_x = pen_x + g->bitmap_left;
            int32_t glyph_y = baseline - g->bitmap_top;
            int32_t glyph_w = (int32_t)g->bitmap.width;
            int32_t glyph_h = (int32_t)g->bitmap.rows;

            if (glyph_w > 0 && glyph_h > 0) {
                if (glyph_x < min_x) min_x = glyph_x;
                if (glyph_y < min_y) min_y = glyph_y;
                if (glyph_x + glyph_w > max_x) max_x = glyph_x + glyph_w;
                if (glyph_y + glyph_h > max_y) max_y = glyph_y + glyph_h;
            }

            pen_x += (int32_t)(g->advance.x >> 6);
        }

        if (min_x != INT32_MAX) {
            if (min_x < (int32_t)padding) {
                offset_x = (int32_t)padding - min_x;
            }
            if (min_y < (int32_t)padding) {
                offset_y = (int32_t)padding - min_y;
            }

            w = (uint32_t)(max_x + offset_x + (int32_t)padding);
            h = (uint32_t)(max_y + offset_y + (int32_t)padding);

            if (w < 16) w = 16;
            if (h < 16) h = 16;
        }
    }

    // If a canvas size is provided, render into that size.
    if (width > 0) {
        w = width < 16 ? 16 : width;
    }
    if (height > 0) {
        h = height < 16 ? 16 : height;
    }

    size_t size = (size_t)w * (size_t)h * 4;
    uint8_t *rgba = bzalloc(size);

    if (!rgba) {
        obs_log(LOG_WARNING, "Unable to create the text context: unable to allocate a new rgba array of size %" PRIu64, (uint64_t)size);
        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        return out;
    }

    int32_t pen_x   = (int32_t)padding + offset_x;
    int32_t baseline = (int32_t)padding + offset_y + (int32_t)px_size;

    for (size_t i = 0; text[i] != '\0'; i++) {

        int load_result = FT_Load_Char(face, (unsigned char)text[i], FT_LOAD_RENDER);

        if (load_result != 0) {
            obs_log(LOG_WARNING, "Unable to load glyph for '%c': %d", text[i], load_result);
            continue;
        }

        const FT_GlyphSlot g = face->glyph;

        if (g->bitmap.width == 0 || g->bitmap.rows == 0) {
            pen_x += (int32_t)(g->advance.x >> 6);
            continue;
        }

        uint32_t gx = (uint32_t)(pen_x + g->bitmap_left);
        uint32_t gy = (uint32_t)(baseline - g->bitmap_top);

        blit_glyph_rgba(rgba, w, h, &g->bitmap, gx, gy, color);

        // Advance pen position (26.6 fixed point -> pixels).
        pen_x += (int32_t)(g->advance.x >> 6);
    }

    // Upload to OBS texture.
    out = bzalloc(sizeof(text_context_t));

    obs_enter_graphics();
    out->texture = gs_texture_create(w, h, GS_RGBA, 1, (const uint8_t **)&rgba, GS_DYNAMIC);
    obs_leave_graphics();

    if (!out->texture) {
        obs_log(LOG_WARNING, "Unable to create the text context: failed to create the texture");
        bfree(out);
        out = NULL;
    } else {
        out->width  = w;
        out->height = h;
    }

    free_memory((void **)&rgba);
    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    if (out) {
        obs_log(LOG_INFO, "Font '%s' has been loaded into a texture", ttf_path);
    }

    return out;
}

void text_context_destroy(text_context_t *text_context) {

    if (!text_context) {
        return;
    }

    if (text_context->texture) {
        obs_enter_graphics();
        gs_texture_destroy(text_context->texture);
        obs_leave_graphics();
        text_context->texture = NULL;
    }

    bfree(text_context);
}

void text_context_draw(const text_context_t *text_context, gs_effect_t *effect) {

    if (!text_context || !text_context->texture)
        return;

    // If an effect is already active (video_render callback), do not call gs_effect_loop again.
    if (effect) {
        gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
        gs_effect_set_texture(image, text_context->texture);
        gs_draw_sprite(text_context->texture, 0, text_context->width, text_context->height);
        return;
    }

    gs_effect_t *base_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    if (!base_effect) {
        return;
    }

    gs_eparam_t *image = gs_effect_get_param_by_name(base_effect, "image");
    gs_effect_set_texture(image, text_context->texture);

    while (gs_effect_loop(base_effect, "Draw")) {
        gs_draw_sprite(text_context->texture, 0, text_context->width, text_context->height);
    }
}
