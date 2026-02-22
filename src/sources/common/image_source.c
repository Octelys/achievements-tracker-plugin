#include "sources/common/image_source.h"

#include <graphics/graphics.h>
#include <string.h>
#include <stdlib.h>

#include <diagnostics/log.h>

#include "drawing/image.h"
#include "io/cache.h"

void image_source_download(image_t *image) {

    if (!image || image->url[0] == '\0') {
        return;
    }

    cache_download(image->url, image->type, image->id, image->cache_path, sizeof(image->cache_path));

    /* Schedule reload on the next render */
    image->must_reload = true;
}

void image_source_clear(image_t *image) {

    if (!image) {
        return;
    }

    image->url[0]        = '\0';
    image->cache_path[0] = '\0';
    image->must_reload   = true;
}

void image_source_reload_if_needed(image_t *image) {

    if (!image || !image->must_reload) {
        return;
    }

    /* Load the image from the temporary file using OBS graphics */
    obs_enter_graphics();

    /* Free existing texture */
    if (image->texture) {
        gs_texture_destroy(image->texture);
        image->texture = NULL;
    }

    /* Create new texture if we have a path */
    if (image->cache_path[0] != '\0') {
        image->texture = gs_texture_create_from_file(image->cache_path);
    }

    obs_leave_graphics();

    image->must_reload = false;

    if (image->texture) {
        obs_log(LOG_DEBUG,
                "[%s] New texture has been successfully loaded from cache file '%s'",
                image->display_name,
                image->cache_path);
    } else if (image->cache_path[0] != '\0') {
        obs_log(LOG_WARNING,
                "[%s] Failed to create texture from the cache file '%s'",
                image->display_name,
                image->cache_path);
    }
}

void image_source_render_active(image_t *image, source_size_t size, gs_effect_t *effect) {

    if (!image || !image->texture) {
        return;
    }

    draw_texture(image->texture, size.width, size.height, effect);
}

void image_source_render_inactive(image_t *image, source_size_t size, gs_effect_t *effect) {

    if (!image || !image->texture) {
        return;
    }

    draw_texture_greyscale(image->texture, size.width, size.height, effect);
}

void image_source_render_active_with_opacity(image_t *image, source_size_t size, gs_effect_t *effect, float opacity) {

    if (!image || !image->texture) {
        return;
    }

    draw_texture_with_opacity(image->texture, size.width, size.height, effect, opacity);
}

void image_source_render_inactive_with_opacity(image_t *image, source_size_t size, gs_effect_t *effect, float opacity) {

    if (!image || !image->texture) {
        return;
    }

    draw_texture_greyscale_with_opacity(image->texture, size.width, size.height, effect, opacity);
}

void image_source_destroy(image_t *image) {

    if (!image) {
        return;
    }

    if (image->texture) {
        obs_enter_graphics();
        gs_texture_destroy(image->texture);
        obs_leave_graphics();
        image->texture = NULL;
    }
}
