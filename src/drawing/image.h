#pragma once

#include <obs-module.h>
#include <graphics/graphics.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Draw a texture using an OBS graphics effect.
 *
 * Renders @p texture as a quad with the given @p width and @p height. The caller
 * is responsible for setting up any required graphics state (e.g., render target,
 * blend state) and for providing an effect configured with the expected texture
 * parameter(s).
 *
 * @param texture Texture to draw. Must be non-NULL.
 * @param width   Output width in pixels.
 * @param height  Output height in pixels.
 * @param effect  Effect used to draw the texture (e.g., a default OBS effect).
 *                Must be non-NULL.
 */
void draw_texture(gs_texture_t *texture, uint32_t width, uint32_t height, gs_effect_t *effect);

#ifdef __cplusplus
}
#endif
