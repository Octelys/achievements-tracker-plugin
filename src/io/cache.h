#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file cache.h
 * @brief Local file cache for downloaded images.
 *
 * Provides a single, shared implementation for building cache file paths and
 * downloading remote resources to the local file system. Both the OBS image
 * source pipeline and the achievement icon prefetch thread use this module so
 * that the path convention is defined in exactly one place.
 *
 * Cache path format:
 *   `<TMPDIR>/obs_achievement_tracker_<type>_<id>.png`
 *
 * Thread safety:
 *   All functions are safe to call from any thread. Two concurrent downloads
 *   targeting the same cache path are benign (last writer wins, file is
 *   atomically replaced).
 */

/**
 * @brief Build the canonical cache file path for a given type and id.
 *
 * Writes the path into @p out_path using the naming convention:
 * `<TMPDIR>/obs_achievement_tracker_<type>_<id>.png`
 *
 * @param type       Category suffix (e.g. "achievement_icon", "gamerpic", "game_cover").
 * @param id         Unique identifier for this resource.
 * @param out_path   Destination buffer for the resulting path.
 * @param path_size  Size of @p out_path in bytes.
 */
void cache_build_path(const char *type, const char *id, char *out_path, size_t path_size);

/**
 * @brief Download a remote resource to the local file cache (if not already cached).
 *
 * Builds the cache path from @p type and @p id, checks whether the file already
 * exists on disk, and downloads it from @p url only when necessary.
 *
 * On success the resulting file path is written into @p out_path (if non-NULL)
 * so that the caller can use it immediately (e.g. for texture creation).
 *
 * @param url        Remote URL to download from.
 * @param type       Category suffix used for the cache path.
 * @param id         Unique identifier used for the cache path.
 * @param out_path   Optional destination buffer that receives the cache file path.
 *                   May be NULL if the caller does not need the path.
 * @param path_size  Size of @p out_path in bytes (ignored when @p out_path is NULL).
 *
 * @return true if the file is present in the cache after this call (either
 *         already existed or was successfully downloaded); false on failure.
 */
bool cache_download(const char *url, const char *type, const char *id, char *out_path, size_t path_size);

#ifdef __cplusplus
}
#endif
