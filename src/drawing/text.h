#pragma once

#include "graphics/graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file text.h
 * @brief Lightweight FreeType-backed text renderer that uploads glyphs to an OBS texture.
 */

/**
 * @brief Text alignment options.
 */
typedef enum text_align {
    /** Align text to the left edge of the canvas. */
    TEXT_ALIGN_LEFT = 0,
    /** Align text to the right edge of the canvas. */
    TEXT_ALIGN_RIGHT = 1,
} text_align_t;

/**
 * @brief Opaque-ish text rendering context.
 *
 * Holds a GPU texture containing the rendered text and its dimensions.
 * The texture is created on the OBS graphics context.
 */
typedef struct text_context {
    /** GPU texture storing the rendered text (RGBA). */
    gs_texture_t *texture;
    /** Texture width in pixels. */
    uint32_t      width;
    /** Texture height in pixels. */
    uint32_t      height;
} text_context_t;

/**
 * @brief Create a text context by rasterizing @p text using the font at @p ttf_path.
 *
 * If @p width/@p height are non-zero, the function renders into that fixed canvas size.
 * Otherwise, it computes a minimal texture size based on glyph bounds.
 *
 * Text layout behavior:
 * - Text is baseline-aligned.
 * - If @p width is non-zero and @p align is TEXT_ALIGN_RIGHT, text is right-aligned within the canvas.
 * - If @p width is non-zero and @p align is TEXT_ALIGN_LEFT, text is left-aligned within the canvas.
 * - If @p width is zero, alignment is ignored and the texture auto-sizes to fit the text.
 *
 * @param ttf_path Path to a .ttf/.otf font file.
 * @param width Canvas width in pixels. Set to 0 to auto-size.
 * @param height Canvas height in pixels. Set to 0 to auto-size.
 * @param text NUL-terminated UTF-8 string (currently treated as single-byte/ASCII for glyph indexing).
 * @param px_size Font size (pixel height) passed to FreeType.
 * @param color Packed RGBA color in 0xRRGGBBAA format.
 * @param align Text alignment (TEXT_ALIGN_LEFT or TEXT_ALIGN_RIGHT). Only applies when width > 0.
 * @return Newly allocated text context, or NULL on error.
 */
text_context_t *text_context_create(const char *ttf_path, uint32_t width, uint32_t height, const char *text,
                                    uint32_t px_size, uint32_t color, text_align_t align);

/**
 * @brief Destroy a text context and its underlying GPU texture.
 *
 * Safe to call with NULL.
 *
 * @param text_context Context to destroy.
 */
void text_context_destroy(text_context_t *text_context);

/**
 * @brief Draw a text context texture.
 *
 * If @p effect is non-NULL, it is assumed to already be active (e.g. inside a
 * video_render callback). In that case, this function will not call
 * @c gs_effect_loop() to avoid nested effects.
 *
 * If @p effect is NULL, this will use the OBS default effect and loop it.
 *
 * @param text_context Context to draw.
 * @param effect Optional active effect.
 */
void text_context_draw(const text_context_t *text_context, gs_effect_t *effect);

#ifdef __cplusplus
}
#endif
