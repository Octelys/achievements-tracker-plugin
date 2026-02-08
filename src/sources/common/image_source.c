#include "sources/common/image_source.h"

#include <graphics/graphics.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <diagnostics/log.h>
#include <net/http/http.h>

#include "drawing/image.h"

/**
 * @file image_source.c
 * @brief Implementation of common functionality for image-based OBS sources.
 */

void image_source_cache_init(image_source_cache_t *cache, const char *display_name, const char *temp_file_suffix) {

    if (!cache) {
        return;
    }

    memset(cache, 0, sizeof(*cache));
    cache->display_name     = display_name;
    cache->temp_file_suffix = temp_file_suffix;
}

/**
 * @brief Internal helper to download an image from URL to temp file.
 *
 * @param cache     Image cache to update.
 * @param image_url URL to download from.
 * @return true on success, false on failure.
 */
static bool download_image_to_temp_file(image_source_cache_t *cache, const char *image_url) {

    if (!cache || !image_url || image_url[0] == '\0') {
        return false;
    }

    obs_log(LOG_INFO, "Downloading %s image from URL: %s", cache->display_name, image_url);

    /* Download the image in memory */
    uint8_t *data = NULL;
    size_t   size = 0;

    if (!http_download(image_url, &data, &size)) {
        obs_log(LOG_WARNING, "Unable to download %s image from URL: %s", cache->display_name, image_url);
        return false;
    }

    obs_log(LOG_INFO, "Downloaded %zu bytes for %s image", size, cache->display_name);

    /* Write the bytes to a temp file */
    const char *tmpdir = getenv("TMPDIR");
    snprintf(cache->image_path,
             sizeof(cache->image_path),
             "%s/obs_plugin_temp_%s.png",
             tmpdir ? tmpdir : "/tmp",
             cache->temp_file_suffix);

    FILE *temp_file = fopen(cache->image_path, "wb");

    if (!temp_file) {
        obs_log(LOG_ERROR, "Failed to create temp file for %s image", cache->display_name);
        bfree(data);
        return false;
    }

    fwrite(data, 1, size, temp_file);
    fclose(temp_file);
    bfree(data);

    /* Schedule reload on next render */
    cache->must_reload = true;

    return true;
}

bool image_source_download_if_changed(image_source_cache_t *cache, const char *image_url) {

    if (!cache) {
        return false;
    }

    /* If URL is NULL or empty, clear the cache */
    if (!image_url || image_url[0] == '\0') {
        image_source_clear(cache);
        return false;
    }

    /* Check if URL has changed */
    if (strcasecmp(image_url, cache->image_url) == 0) {
        /* URL hasn't changed, no need to download */
        return false;
    }

    /* Store the new URL */
    snprintf(cache->image_url, sizeof(cache->image_url), "%s", image_url);

    return download_image_to_temp_file(cache, image_url);
}

void image_source_download(image_source_cache_t *cache, const char *image_url) {

    if (!cache) {
        return;
    }

    if (!image_url || image_url[0] == '\0') {
        image_source_clear(cache);
        return;
    }

    /* Store the URL and download */
    snprintf(cache->image_url, sizeof(cache->image_url), "%s", image_url);
    download_image_to_temp_file(cache, image_url);
}

void image_source_clear(image_source_cache_t *cache) {

    if (!cache) {
        return;
    }

    cache->image_url[0]  = '\0';
    cache->image_path[0] = '\0';
    cache->must_reload   = true;
}

void image_source_reload_if_needed(image_source_cache_t *cache) {

    if (!cache || !cache->must_reload) {
        return;
    }

    /* Load the image from the temporary file using OBS graphics */
    obs_enter_graphics();

    /* Free existing texture */
    if (cache->image_texture) {
        gs_texture_destroy(cache->image_texture);
        cache->image_texture = NULL;
    }

    /* Create new texture if we have a path */
    if (cache->image_path[0] != '\0') {
        cache->image_texture = gs_texture_create_from_file(cache->image_path);
    }

    obs_leave_graphics();

    cache->must_reload = false;

    /* Clean up temp file */
    if (cache->image_path[0] != '\0') {
        remove(cache->image_path);
    }

    if (cache->image_texture) {
        obs_log(LOG_INFO, "New %s texture has been successfully loaded", cache->display_name);
    } else if (cache->image_path[0] != '\0') {
        obs_log(LOG_WARNING, "Failed to create %s texture from the downloaded file", cache->display_name);
    }
}

void image_source_render(image_source_cache_t *cache, source_size_t size, gs_effect_t *effect) {

    if (!cache || !cache->image_texture) {
        return;
    }

    draw_texture(cache->image_texture, size.width, size.height, effect);
}

void image_source_destroy(image_source_cache_t *cache) {

    if (!cache) {
        return;
    }

    if (cache->image_texture) {
        obs_enter_graphics();
        gs_texture_destroy(cache->image_texture);
        obs_leave_graphics();
        cache->image_texture = NULL;
    }
}
