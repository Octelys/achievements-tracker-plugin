#pragma once

#include "common/types.h"
#include "graphics/graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file text.h
 * @brief Lightweight FreeType-backed text renderer that uploads glyphs to an OBS texture.
 */

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
 * @brief Create a text context by rasterizing @p text using the provided configuration.
 *
 * Renders text into a canvas of the specified size. Text is baseline-aligned and
 * positioned according to the alignment setting in the config.
 *
 * @param config Text rendering configuration (font path, font size, color, alignment).
 * @param size   Canvas dimensions (width and height in pixels).
 * @param text   NUL-terminated UTF-8 string to render.
 * @return Newly allocated text context, or NULL on error.
 */
text_context_t *text_context_create(const text_source_config_t *config, source_size_t size, const char *text);

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
