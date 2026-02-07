#pragma once

#include "graphics/graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct text_context {
    gs_texture_t *texture;
    uint32_t      width;
    uint32_t      height;
} text_context_t;

text_context_t *text_context_create(const char *ttf_path,
                                    uint32_t width,
                                    uint32_t height,
                                    const char *text,
                                    uint32_t px_size,
                                    uint32_t color);
void text_context_destroy(text_context_t *text_context);
void text_context_draw(const text_context_t *text_context, gs_effect_t *effect);

#ifdef __cplusplus
}
#endif
