#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file color.h
 * @brief Color helpers for converting between packed pixel formats.
 */

/**
 * @brief Convert a packed 32-bit ARGB color to the plugin's packed RGBA format.
 *
 * OBS uses ARGB (0xAARRGGBB) for `obs_properties_add_color()` values.
 * The drawing code in this plugin uses RGBA packed as 0xRRGGBBAA.
 *
 * @param argb Packed ARGB color value (0xAARRGGBB).
 * @return Packed RGBA color value (0xRRGGBBAA).
 */
uint32_t color_argb_to_rgba(uint32_t argb);

#ifdef __cplusplus
}
#endif
