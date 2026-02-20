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
 * This module provides a reusable framework for OBS sources that display downloaded
 * images, such as gamerpics, game covers, and achievement icons. It centralizes:
 *
 * - **URL-based image downloading** to temporary cache files
 * - **Deferred texture loading** on the graphics thread
 * - **Change detection** to avoid redundant downloads
 * - **Multiple rendering modes** (normal, opacity, greyscale)
 * - **Resource cleanup** and lifecycle management
 *
 * By consolidating this functionality, we eliminate duplication across multiple
 * image source implementations (gamerpic, game_cover, achievement_icon).
 *
 * @see image_t for the per-image runtime cache structure
 * @see image_source_t for the base source structure
 */

/**
 * @brief Common data structure for image-based OBS sources.
 *
 * Contains the minimal fields required by all image-based sources. Stores the
 * OBS source reference and display dimensions. Individual source types may embed
 * this struct or use it as a base for their specific needs.
 *
 * Used by: gamerpic, game_cover, achievement_icon sources.
 */
typedef struct image_source {

    /** OBS source instance for this image source. */
    obs_source_t *source;

    /** Display dimensions in pixels (width and height). */
    source_size_t size;

} image_source_t;

/**
 * @brief Runtime cache for a downloaded image and its associated texture.
 *
 * Manages the complete lifecycle of a downloaded image: URL tracking, file caching,
 * texture loading, and change detection. Each image source instance maintains one
 * of these caches to track the current image state.
 *
 * **Lifecycle:**
 * 1. Download image from URL to temporary file (download thread)
 * 2. Load texture from file (graphics thread via must_reload flag)
 * 3. Render texture each frame
 * 4. Cleanup when URL changes or source is destroyed
 *
 * **Thread Safety:**
 * - Download operations: background thread
 * - Texture operations: graphics thread only (enforced by OBS)
 * - Coordination via must_reload flag
 */
typedef struct image {

    /** Descriptive name for logging (e.g., "Gamerpic", "Game Cover", "Achievement Icon"). */
    char display_name[128];

    /** Currently cached image URL. Used for change detection to avoid redundant downloads. */
    char url[1024];

    /** Path to the cache file where the downloaded image is stored. Used by gs_texture_create_from_file(). */
    char cache_path[1024];

    /** Unique identifier for this image (e.g., gamertag hash, title ID, achievement ID). */
    char id[128];

    /** GPU texture created from the downloaded image. NULL if no image loaded. Owned by this cache. */
    gs_texture_t *texture;

    /** If true, texture will be reloaded from image_path on the next render tick. Set by download functions. */
    bool must_reload;

    /** Unique suffix for cache file naming (e.g., "gamerpic", "game_cover", "achievement_icon"). */
    char type[128];

} image_t;

/**
 * @brief Download an image from its URL to the local cache.
 *
 * Initiates a download of the image specified by the `url` field to a temporary
 * cache file. The download runs on a background thread to avoid blocking the
 * graphics thread. Upon completion, sets `must_reload` to true so the texture
 * will be created on the next render tick.
 *
 * **Change Detection:** If the URL matches the previously downloaded URL, no
 * download is performed (early return).
 *
 * **Thread Safety:** Safe to call from any thread. The actual texture creation
 * happens later on the graphics thread via image_source_reload_if_needed().
 *
 * @param image Image cache containing the URL to download. Must not be NULL.
 *              The `url` field must be set before calling.
 *
 * @post On success, `cache_path` contains the path to the downloaded file and
 *       `must_reload` is set to true.
 *
 * @see image_source_reload_if_needed() for texture creation from the cached file
 */
void image_source_download(image_t *image);

/**
 * @brief Clear the image cache and schedule texture unload.
 *
 * Clears the cached URL, path, and ID fields, and sets `must_reload` to true so
 * the texture will be freed on the next render. Does not immediately destroy the
 * texture; that happens in image_source_reload_if_needed() on the graphics thread.
 *
 * **Thread Safety:** Safe to call from any thread.
 *
 * @param image Image cache to clear. Must not be NULL.
 *
 * @post `url`, `cache_path`, and `id` fields are cleared (empty strings).
 * @post `must_reload` is set to true.
 *
 * @see image_source_reload_if_needed() for the actual texture destruction
 */
void image_source_clear(image_t *image);

/**
 * @brief Load the downloaded image into a GPU texture if needed.
 *
 * Checks the `must_reload` flag. If true, enters the graphics context, destroys
 * any existing texture, creates a new texture from `cache_path`, and cleans up
 * the temporary file. The `must_reload` flag is cleared after processing.
 *
 * If `cache_path` is empty (image was cleared via image_source_clear()), only
 * destroys the existing texture without creating a new one.
 *
 * @pre Must be called from the graphics thread (e.g., video_render callback).
 *
 * @param image Image cache containing the texture to reload. Must not be NULL.
 *
 * @post `must_reload` is set to false.
 * @post If `cache_path` was non-empty, `texture` points to the newly created texture.
 * @post The temporary cache file is deleted after successful texture creation.
 *
 * @see image_source_download() for the function that sets `must_reload`
 */
void image_source_reload_if_needed(image_t *image);

/**
 * @brief Render the cached texture at full opacity.
 *
 * Draws the texture at the specified dimensions using the provided effect.
 * Does nothing if no texture is loaded (`texture` is NULL).
 *
 * @pre Must be called from the graphics thread (e.g., video_render callback).
 *
 * @param image  Image cache containing the texture to render. Must not be NULL.
 * @param size   Dimensions to render at in pixels (width and height).
 * @param effect Effect to use for rendering. If NULL, uses OBS default effect.
 *
 * @see image_source_render_active_with_opacity() for rendering with custom opacity
 */
void image_source_render_active(image_t *image, source_size_t size, gs_effect_t *effect);

/**
 * @brief Render the cached texture with adjustable opacity.
 *
 * Draws the texture at the specified dimensions with the given opacity level.
 * Does nothing if no texture is loaded (`texture` is NULL).
 *
 * @pre Must be called from the graphics thread (e.g., video_render callback).
 *
 * @param image   Image cache containing the texture to render. Must not be NULL.
 * @param size    Dimensions to render at in pixels (width and height).
 * @param effect  Effect to use for rendering. If NULL, uses OBS default effect.
 * @param opacity Opacity level in range [0.0, 1.0] (0.0 = fully transparent, 1.0 = fully opaque).
 */
void image_source_render_active_with_opacity(image_t *image, source_size_t size, gs_effect_t *effect, float opacity);

/**
 * @brief Render the cached texture in greyscale.
 *
 * Draws the texture at the specified dimensions using a greyscale shader effect.
 * Uses perceptually accurate luminance coefficients (Rec. 709) for color-to-grey conversion.
 * Does nothing if no texture is loaded (`texture` is NULL).
 *
 * @pre Must be called from the graphics thread (e.g., video_render callback).
 *
 * @param image  Image cache containing the texture to render. Must not be NULL.
 * @param size   Dimensions to render at in pixels (width and height).
 * @param effect Effect to use for rendering. If NULL, uses OBS default effect.
 */
void image_source_render_inactive(image_t *image, source_size_t size, gs_effect_t *effect);

/**
 * @brief Render the cached texture in greyscale with adjustable opacity.
 *
 * Draws the texture at the specified dimensions using a greyscale shader effect
 * with the given opacity level. Uses perceptually accurate luminance coefficients
 * (Rec. 709) for color-to-grey conversion. Does nothing if no texture is loaded
 * (`texture` is NULL).
 *
 * @pre Must be called from the graphics thread (e.g., video_render callback).
 *
 * @param image   Image cache containing the texture to render. Must not be NULL.
 * @param size    Dimensions to render at in pixels (width and height).
 * @param effect  Effect to use for rendering. If NULL, uses OBS default effect.
 * @param opacity Opacity level in range [0.0, 1.0] (0.0 = fully transparent, 1.0 = fully opaque).
 */
void image_source_render_inactive_with_opacity(image_t *image, source_size_t size, gs_effect_t *effect, float opacity);

/**
 * @brief Destroy the texture and free graphics resources.
 *
 * Safely destroys the GPU texture by entering the graphics context. Should be
 * called when the source is destroyed to prevent memory leaks. Safe to call
 * even if no texture is loaded.
 *
 * **Thread Safety:** Handles graphics context internally, safe to call from any thread.
 *
 * @param image Image cache to destroy. Must not be NULL.
 *
 * @post `texture` is set to NULL.
 */
void image_source_destroy(image_t *image);

#ifdef __cplusplus
}
#endif
