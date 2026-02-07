#include "color.h"

/**
 * @file color.c
 * @brief Implementation of packed color conversion helpers.
 */

uint32_t color_argb_to_rgba(uint32_t argb) {

    const uint8_t a = (uint8_t)(argb >> 24 & 0xFF);
    const uint8_t r = (uint8_t)(argb >> 16 & 0xFF);
    const uint8_t g = (uint8_t)(argb >> 8 & 0xFF);
    const uint8_t b = (uint8_t)(argb & 0xFF);

    return (uint32_t)b << 24 | (uint32_t)g << 16 | (uint32_t)r << 8 | a;
}
