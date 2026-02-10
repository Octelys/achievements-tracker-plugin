#pragma once

#include <obs-module.h>
#include <stdbool.h>
#include <stdint.h>

#include "common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file image_source.h
 * @brief Common functionality for image-based OBS sources.
 *
 * This module provides shared downloading, texture management, and rendering
 * for sources that display images (gamerpic, game_cover, achievement_icon).
 * It eliminates code duplication by centralizing:
 * - Image downloading from URL to temporary file
 * - Deferred texture loading on the graphics thread
 * - Common rendering logic
 * - Texture cleanup
 */

/**
 * @brief Common data structure for image-based OBS sources.
 *
 * This structure provides the common fields needed by all image sources
 * (gamerpic, game_cover, achievement_icon). It stores the OBS source reference
 * and the display dimensions.
 */
typedef struct image_source_data {
    /** OBS source instance. */
    obs_source_t *source;

    /** Source display dimensions in pixels. */
    source_size_t size;
} image_source_data_t;

/**
 * @brief Runtime cache for a downloaded image.
 *
 * This structure holds the state needed to download an image from a URL,
 * store it in a temporary file, and load it as an OBS texture.
 */
typedef struct image_source_cache {
    /** Last fetched image URL (used to detect changes). */
    char image_url[1024];

    /** Temporary file path used as an intermediate for gs_texture_create_from_file(). */
    char image_path[512];

    /** GPU texture created from the downloaded image (owned by this cache). */
    gs_texture_t *image_texture;

    /** If true, the next render tick should reload the texture from image_path. */
    bool must_reload;

    /** Descriptive name for logging (e.g., "Gamerpic", "Game Cover"). */
    const char *display_name;

    /** Unique suffix for temp file naming (e.g., "gamerpic", "game_cover"). */
    const char *temp_file_suffix;
} image_source_cache_t;

/**
 * @brief Initialize an image source cache.
 *
 * Sets up the cache with the given display name and temp file suffix.
 * Must be called before using other image_source functions.
 *
 * @param cache           Cache structure to initialize.
 * @param display_name    Human-readable name for log messages (e.g., "Gamerpic").
 * @param temp_file_suffix Suffix for temp file naming (e.g., "gamerpic").
 */
void image_source_cache_init(image_source_cache_t *cache, const char *display_name, const char *temp_file_suffix);

/**
 * @brief Download an image from a URL if it has changed.
 *
 * Compares the new URL against the cached URL. If different, downloads the image
 * to a temporary file and sets must_reload to true. If the URL is NULL or empty,
 * clears the cache and schedules a texture unload.
 *
 * @param cache     Image cache to update.
 * @param image_url New image URL, or NULL to clear the image.
 * @return true if a new image was downloaded, false if URL unchanged or download failed.
 */
bool image_source_download_if_changed(image_source_cache_t *cache, const char *image_url);

/**
 * @brief Force download an image from a URL (ignores URL change detection).
 *
 * Downloads the image regardless of whether the URL has changed.
 * Useful for initial loads or forced refreshes.
 *
 * @param cache     Image cache to update.
 * @param image_url Image URL to download. If NULL or empty, clears the cache.
 */
void image_source_download(image_source_cache_t *cache, const char *image_url);

/**
 * @brief Clear the image cache and schedule texture unload.
 *
 * Clears the cached URL and path, and sets must_reload to true so the
 * texture will be freed on the next render.
 *
 * @param cache Image cache to clear.
 */
void image_source_clear(image_source_cache_t *cache);

/**
 * @brief Load the downloaded image into a texture if needed.
 *
 * If must_reload is true, enters the graphics context, destroys any existing
 * texture, creates a new texture from the temp file, and cleans up the temp file.
 *
 * This must be called from video_render or another context where
 * obs_enter_graphics/obs_leave_graphics is allowed.
 *
 * @param cache Image cache containing the texture to reload.
 */
void image_source_reload_if_needed(image_source_cache_t *cache);

/**
 * @brief Render the cached texture.
 *
 * Draws the texture at the specified dimensions using draw_texture().
 * Does nothing if no texture is loaded.
 *
 * @param cache  Image cache containing the texture to render.
 * @param size   Dimensions to render at in pixels.
 * @param effect Effect to use for rendering.
 */
void image_source_render(image_source_cache_t *cache, source_size_t size, gs_effect_t *effect);

/**
 * @brief Render the cached texture with opacity.
 *
 * Draws the texture at the specified dimensions with the given opacity.
 * Does nothing if no texture is loaded.
 *
 * @param cache   Image cache containing the texture to render.
 * @param size    Dimensions to render at in pixels.
 * @param effect  Effect to use for rendering.
 * @param opacity Opacity value (0.0 = fully transparent, 1.0 = fully opaque).
 */
void image_source_render_with_opacity(image_source_cache_t *cache, source_size_t size, gs_effect_t *effect,
                                      float opacity);

/**
 * @brief Render the cached texture in greyscale.
 *
 * Draws the texture at the specified dimensions using a greyscale effect.
 * Uses luminance coefficients for perceptually accurate conversion.
 * Does nothing if no texture is loaded.
 *
 * @param cache Image cache containing the texture to render.
 * @param size  Dimensions to render at in pixels.
 * @param effect Effect to use for rendering.
 */
void image_source_render_greyscale(image_source_cache_t *cache, source_size_t size, gs_effect_t *effect);

/**
 * @brief Render the cached texture in greyscale with opacity.
 *
 * Draws the texture at the specified dimensions using a greyscale effect
 * with the given opacity. Does nothing if no texture is loaded.
 *
 * @param cache   Image cache containing the texture to render.
 * @param size    Dimensions to render at in pixels.
 * @param effect  Effect to use for rendering.
 * @param opacity Opacity value (0.0 = fully transparent, 1.0 = fully opaque).
 */
void image_source_render_greyscale_with_opacity(image_source_cache_t *cache, source_size_t size, gs_effect_t *effect,
                                                float opacity);

/**
 * @brief Destroy the texture and free graphics resources.
 *
 * Should be called when the source is destroyed. Enters graphics context
 * to safely destroy the texture.
 *
 * @param cache Image cache to destroy.
 */
void image_source_destroy(image_source_cache_t *cache);

#ifdef __cplusplus
}
#endif
