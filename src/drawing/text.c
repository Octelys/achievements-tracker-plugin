#include "text.h"

#include <stdbool.h>

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

/**
 * @file text.c
 * @brief FreeType-based text rasterizer that draws glyph bitmaps into an RGBA buffer and uploads it as an OBS texture.
 */

/**
 * @brief Decode a UTF-8 character from a string.
 *
 * Reads a single Unicode code point from a UTF-8 encoded string and advances
 * the pointer past the decoded character.
 *
 * @param[in,out] str Pointer to the current position in the UTF-8 string.
 *                    Will be advanced past the decoded character.
 * @return The decoded Unicode code point, or 0xFFFD (replacement character) on error.
 */
static uint32_t utf8_decode(const char **str) {
    const unsigned char *s = (const unsigned char *)*str;
    uint32_t             codepoint;
    int                  bytes;

    if (s[0] == 0) {
        return 0;
    }

    if ((s[0] & 0x80) == 0) {
        /* 1-byte sequence (ASCII): 0xxxxxxx */
        codepoint = s[0];
        bytes     = 1;
    } else if ((s[0] & 0xE0) == 0xC0) {
        /* 2-byte sequence: 110xxxxx 10xxxxxx */
        if ((s[1] & 0xC0) != 0x80) {
            *str += 1;
            return 0xFFFD; /* Invalid continuation byte */
        }
        codepoint = ((uint32_t)(s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        bytes     = 2;
    } else if ((s[0] & 0xF0) == 0xE0) {
        /* 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx */
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) {
            *str += 1;
            return 0xFFFD;
        }
        codepoint = ((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        bytes     = 3;
    } else if ((s[0] & 0xF8) == 0xF0) {
        /* 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) {
            *str += 1;
            return 0xFFFD;
        }
        codepoint = ((uint32_t)(s[0] & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) | ((uint32_t)(s[2] & 0x3F) << 6) |
                    (s[3] & 0x3F);
        bytes = 4;
    } else {
        /* Invalid UTF-8 lead byte */
        *str += 1;
        return 0xFFFD;
    }

    *str += bytes;
    return codepoint;
}

/**
 * @brief Blit a FreeType glyph bitmap into an RGBA buffer.
 *
 * The glyph bitmap is expected to be an 8-bit grayscale coverage mask. The
 * coverage value is written into the destination alpha channel; RGB is taken
 * from @p color.
 *
 * @param dst Destination RGBA buffer (row-major).
 * @param dst_w Destination width in pixels.
 * @param dst_h Destination height in pixels.
 * @param bmp FreeType bitmap (typically FT_PIXEL_MODE_GRAY).
 * @param x Destination x offset.
 * @param y Destination y offset.
 * @param color Packed RGBA in 0xRRGGBBAA.
 */
static void blit_glyph_rgba(uint8_t *dst, uint32_t dst_w, uint32_t dst_h, const FT_Bitmap *bmp, uint32_t x, uint32_t y,
                            uint32_t color) {

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

            uint8_t r = (uint8_t)(color >> 24 & 0xFF);
            uint8_t g = (uint8_t)(color >> 16 & 0xFF);
            uint8_t b = (uint8_t)(color >> 8 & 0xFF);

            dst[idx + 0] = r; // R
            dst[idx + 1] = g; // G
            dst[idx + 2] = b; // B
            dst[idx + 3] = a; // A
        }
    }
}

text_context_t *text_context_create(const text_source_config_t *config, source_size_t size, const char *text) {

    text_context_t *out = NULL;

    if (!config || !config->font_face || !text) {
        obs_log(LOG_WARNING, "Unable to create the text context: invalid text parameters");
        return out;
    }

    const char        *ttf_path = config->font_face;
    const uint32_t     px_size  = config->font_size;
    const uint32_t     color    = config->active_top_color;
    const text_align_t align    = config->align;
    const uint32_t     width    = size.width;
    const uint32_t     height   = size.height;

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

    uint32_t w        = 16;
    uint32_t h        = 16;
    int32_t  offset_x = 0;
    int32_t  offset_y = 0;

    if (use_bounds) {
        // First pass: measure glyph bounds.
        int32_t min_x = INT32_MAX;
        int32_t min_y = INT32_MAX;
        int32_t max_x = INT32_MIN;
        int32_t max_y = INT32_MIN;

        int32_t pen_x    = (int32_t)padding;
        int32_t baseline = (int32_t)padding + (int32_t)px_size;

        const char *p = text;
        while (*p != '\0') {
            uint32_t codepoint = utf8_decode(&p);
            if (codepoint == 0)
                break;

            if (FT_Load_Char(face, codepoint, FT_LOAD_RENDER) != 0) {
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
                if (glyph_x < min_x)
                    min_x = glyph_x;
                if (glyph_y < min_y)
                    min_y = glyph_y;
                if (glyph_x + glyph_w > max_x)
                    max_x = glyph_x + glyph_w;
                if (glyph_y + glyph_h > max_y)
                    max_y = glyph_y + glyph_h;
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

            if (w < 16)
                w = 16;
            if (h < 16)
                h = 16;
        }
    }

    // If a canvas size is provided, render into that size.
    if (width > 0) {
        w = width < 16 ? 16 : width;
    }
    if (height > 0) {
        h = height < 16 ? 16 : height;
    }

    size_t   buffer_size = (size_t)w * (size_t)h * 4;
    uint8_t *rgba        = bzalloc(buffer_size);

    if (!rgba) {
        obs_log(LOG_WARNING,
                "Unable to create the text context: unable to allocate a new rgba array of size %" PRIu64,
                (uint64_t)buffer_size);
        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        return out;
    }

    int32_t pen_x;

    if (width > 0) {
        if (align == TEXT_ALIGN_RIGHT) {
            // Measure text width for the right alignment.
            int32_t     total_advance = 0;
            const char *p             = text;
            while (*p != '\0') {
                uint32_t codepoint = utf8_decode(&p);
                if (codepoint == 0)
                    break;
                if (FT_Load_Char(face, codepoint, FT_LOAD_NO_BITMAP) == 0) {
                    total_advance += (int32_t)(face->glyph->advance.x >> 6);
                }
            }
            pen_x = (int32_t)w - (int32_t)padding - total_advance;
        } else {
            // Left alignment: start from the left edge with padding.
            pen_x = (int32_t)padding;
        }
    } else {
        pen_x = (int32_t)padding + offset_x;
    }

    int32_t baseline = (int32_t)padding + offset_y + (int32_t)px_size;

    const char *p = text;
    while (*p != '\0') {
        uint32_t codepoint = utf8_decode(&p);
        if (codepoint == 0)
            break;

        int load_result = FT_Load_Char(face, codepoint, FT_LOAD_RENDER);

        if (load_result != 0) {
            obs_log(LOG_WARNING, "Unable to load glyph for codepoint U+%04X: %d", codepoint, load_result);
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

/**
 * @brief Destroy a text rendering context and free all associated resources.
 *
 * This function safely destroys the OBS texture (if it exists) by entering the
 * graphics context, calling gs_texture_destroy, and leaving the graphics context.
 * It then frees the text_context_t structure itself.
 *
 * Safe to call with NULL. After this call, the @p text_context pointer should not
 * be used.
 *
 * @param text_context Text context to destroy, or NULL.
 */
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

/**
 * @brief Draw a text context's texture to the current OBS render target.
 *
 * This function renders the pre-rasterized text texture using OBS's graphics API.
 * It handles two scenarios:
 *
 * 1. If @p effect is non-NULL (already active): assumes we're inside a video_render
 *    callback where an effect is already looping. In this case, the function sets
 *    the texture parameter and draws the sprite directly without calling
 *    gs_effect_loop (which would cause a nested effect error).
 *
 * 2. If @p effect is NULL: uses OBS's default effect (OBS_EFFECT_DEFAULT) and
 *    calls gs_effect_loop to properly render the texture.
 *
 * The texture is drawn as a sprite with its original dimensions (no scaling).
 *
 * @param text_context Text context to draw. If NULL or has no texture, does nothing.
 * @param effect Optional active effect. Pass NULL to use the default effect with looping.
 */
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
