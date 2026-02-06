#include "text.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <obs-module.h>
#include <graphics/graphics.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct text_tex {
    gs_texture_t *tex;
    uint32_t w;
    uint32_t h;
};

static void blit_glyph_rgba(uint8_t *dst, uint32_t dst_w, uint32_t dst_h, const FT_Bitmap *bmp, uint32_t x, uint32_t y)
{
    for (uint32_t row = 0; row < (uint32_t)bmp->rows; row++) {

        uint32_t dy = y + row;

        if (dy >= dst_h) {
            break;
        }

        for (uint32_t col = 0; col < (uint32_t)bmp->width; col++) {

            uint32_t dx = x + col;

            if (dx >= dst_w) {
                break;
            }

            uint8_t a = bmp->buffer[row * bmp->pitch + col];
            uint32_t idx = (dy * dst_w + dx) * 4;

            // White text with alpha from glyph coverage.
            dst[idx + 0] = 255; // R
            dst[idx + 1] = 255; // G
            dst[idx + 2] = 255; // B
            dst[idx + 3] = a;   // A
        }
    }
}

static bool make_text_texture(struct text_tex *out, const char *ttf_path, const char *text, uint32_t px_size)
{
    if (!out || !ttf_path || !text) return false;
    memset(out, 0, sizeof(*out));

    FT_Library ft = NULL;
    FT_Face face = NULL;

    if (FT_Init_FreeType(&ft) != 0) {
        return false;
    }

    if (FT_New_Face(ft, ttf_path, 0, &face) != 0) {
        FT_Done_FreeType(ft);
        return false;
    }

    (void)FT_Set_Pixel_Sizes(face, 0, px_size);

    // Very simple layout: monospaced advance estimate.
    uint32_t w = 0;
    uint32_t h = px_size + 8;

    for (size_t i = 0; text[i] != '\0'; i++) {
        w += (px_size / 2) + 2;
    }

    if (w < 16) {
        w = 16;
    }

    uint8_t *rgba = (uint8_t *)calloc((size_t)w * (size_t)h * 4, 1);

    if (!rgba) {
        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        return false;
    }

    uint32_t pen_x = 4;
    uint32_t pen_y = 4;

    for (size_t i = 0; text[i] != '\0'; i++) {

        FT_UInt glyph_index = FT_Get_Char_Index(face, (FT_ULong)(unsigned char)text[i]);

        if (FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT) != 0) {
            continue;
        }

        if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
            continue;
        }

        const FT_GlyphSlot g = face->glyph;

        uint32_t gx = pen_x + (uint32_t)((g->bitmap_left > 0) ? g->bitmap_left : 0);
        uint32_t gy = pen_y + (uint32_t)((px_size > (uint32_t)g->bitmap_top) ? (px_size - (uint32_t)g->bitmap_top) : 0);

        blit_glyph_rgba(rgba, w, h, &g->bitmap, gx, gy);

        // Advance pen position (26.6 fixed point -> pixels).
        pen_x += (uint32_t)(g->advance.x >> 6);

        if (pen_x + px_size >= w) {
            break;
        }
    }

    // Upload to OBS texture.
    out->tex = gs_texture_create(w, h, GS_RGBA, 1, (const uint8_t **)&rgba, GS_DYNAMIC);
    out->w = w;
    out->h = h;

    free(rgba);
    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    return out->tex != NULL;
}

static void draw_text_texture(const struct text_tex *t, float x, float y)
{
    if (!t || !t->tex) return;

    gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);

    if (!effect) {
        return;
    }

    gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
    gs_effect_set_texture(image, t->tex);

    while (gs_effect_loop(effect, "Draw")) {
        gs_draw_sprite(t->tex, 0, (uint32_t)t->w, (uint32_t)t->h);
    }

    (void)x;
    (void)y;
    // For positioning, typically set a transform or use a vertex buffer / matrix;
    // this sample focuses on the text-to-texture part.
}
