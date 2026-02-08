#include "image.h"

#include <obs-module.h>
#include <graphics/graphics.h>

void draw_texture(gs_texture_t *texture, const uint32_t width, const uint32_t height, gs_effect_t *effect) {

    if (!texture) {
        return;
    }

    /* Use the passed effect or get the default if NULL */
    gs_effect_t *used_effect = effect ? effect : obs_get_base_effect(OBS_EFFECT_DEFAULT);

    /* If we didn't get an effect and OBS default effect isn't available, bail. */
    if (!used_effect) {
        return;
    }

    /* Only start our own effect loop if no effect is currently active.
     * If an effect was passed in, it's already active - just set texture and draw. */
    if (effect) {
        /* Effect already active from caller - just set texture and draw */
        gs_eparam_t *image_param = gs_effect_get_param_by_name(effect, "image");
        if (image_param)
            gs_effect_set_texture(image_param, texture);
        gs_draw_sprite(texture, 0, width, height);
    } else {
        /* No effect passed - start our own loop */
        gs_eparam_t *image_param = gs_effect_get_param_by_name(used_effect, "image");

        if (image_param) {
            gs_effect_set_texture(image_param, texture);
        }

        while (gs_effect_loop(used_effect, "Draw")) {
            gs_draw_sprite(texture, 0, width, height);
        }
    }
}
