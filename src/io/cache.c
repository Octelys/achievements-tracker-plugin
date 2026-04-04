#include "io/cache.h"

#include <obs-module.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <util/platform.h>

#include <limits.h>

/* PATH_MAX is a POSIX constant absent from Windows / MSVC's limits.h.
 * MAX_PATH (260) is the traditional Win32 limit; use it as a fallback. */
#ifndef PATH_MAX
#ifdef MAX_PATH
#define PATH_MAX MAX_PATH
#else
#define PATH_MAX 4096
#endif
#endif

#define CACHE_MAX_PATH PATH_MAX

#include <diagnostics/log.h>
#include <net/http/http.h>

#include "common/memory.h"

#define CACHE_DIRECTORY "cache"

static uint32_t cache_hash_source(const char *source) {
    /* FNV-1a 32-bit: small, stable, and sufficient for cache keying. */
    const unsigned char *p = (const unsigned char *)(source ? source : "");
    uint32_t             h = 2166136261u;

    while (*p) {
        h ^= (uint32_t)*p++;
        h *= 16777619u;
    }

    return h;
}

static bool get_cache_dir(char *buf, size_t buf_size) {

    char *cache_dir = obs_module_config_path(CACHE_DIRECTORY);

    if (!cache_dir) {
        if (buf && buf_size > 0) {
            buf[0] = '\0';
        }

        return false;
    }

    os_mkdirs(cache_dir);
    snprintf(buf, buf_size, "%s", cache_dir);
    bfree(cache_dir);
    return true;
}

void cache_build_path(const char *type, const char *id, const char *source, char *out_path, size_t path_size) {

    char     cache_dir[CACHE_MAX_PATH] = {0};
    uint32_t source_hash               = cache_hash_source(source);

    if (!get_cache_dir(cache_dir, sizeof(cache_dir))) {
        if (out_path && path_size > 0) {
            out_path[0] = '\0';
        }

        return;
    }

    // Ensure the cache dir ends with a separator
    size_t dirlen = strlen(cache_dir);
    char   sep    = (dirlen > 0 && (cache_dir[dirlen - 1] == '/' || cache_dir[dirlen - 1] == '\\')) ? '\0' : '/';

    if (sep)
        snprintf(out_path,
                 path_size,
                 "%s%cobs_achievement_tracker_%s_%s_%08x.png",
                 cache_dir,
                 sep,
                 type,
                 id,
                 source_hash);
    else
        snprintf(out_path, path_size, "%sobs_achievement_tracker_%s_%s_%08x.png", cache_dir, type, id, source_hash);
}

bool cache_download(const char *url, const char *type, const char *id, char *out_path, size_t path_size) {

    if (!url || url[0] == '\0') {
        return false;
    }

    char path_buf[1024];
    cache_build_path(type, id, url, path_buf, sizeof(path_buf));

    if (path_buf[0] == '\0') {
        obs_log(LOG_ERROR, "[Cache] Failed to resolve the OBS module cache directory");
        return false;
    }

    /* Copy the resolved path to the caller's buffer when provided */
    if (out_path) {
        snprintf(out_path, path_size, "%s", path_buf);
    }

    /* Already cached — nothing to do */
    struct stat st;
    if (stat(path_buf, &st) == 0) {
        if (st.st_size == 0) {
            obs_log(LOG_WARNING, "[Cache] Discarding zero-byte cached file '%s'", path_buf);

            if (remove(path_buf) != 0) {
                obs_log(LOG_WARNING, "[Cache] Failed to delete zero-byte cached file '%s'", path_buf);
                return false;
            }
        } else {
            obs_log(LOG_DEBUG, "[Cache] Hit: %s", path_buf);
            return false;
        }
    }

    /* Download into memory */
    uint8_t *data = NULL;
    size_t   size = 0;

    /* Normalize the URL so that any unencoded characters in the path or query
     * (e.g. spaces, Unicode) are properly percent-encoded. */
    char       *encoded_url  = http_encode_url(url);
    const char *download_url = encoded_url ? encoded_url : url;

    obs_log(LOG_INFO, "[Cache] Downloading '%s'", download_url);

    if (!http_download(download_url, &data, &size)) {
        obs_log(LOG_WARNING, "[Cache] Failed to download '%s'", download_url);
        bfree(encoded_url);
        return false;
    }

    bfree(encoded_url);

    if (size == 0) {
        obs_log(LOG_WARNING, "[Cache] Downloaded zero bytes from '%s'", download_url);
        free_memory((void **)&data);
        return false;
    }

    /* Write to disk */
    FILE *file = fopen(path_buf, "wb");
    if (!file) {
        obs_log(LOG_ERROR, "[Cache] Failed to create file '%s'", path_buf);
        free_memory((void **)&data);
        return false;
    }

    size_t written = fwrite(data, sizeof(uint8_t), size, file);
    fflush(file);
    fclose(file);
    free_memory((void **)&data);

    obs_log(LOG_INFO, "[Cache] Saved '%s' (%zu bytes written)", path_buf, written);

    return true;
}
