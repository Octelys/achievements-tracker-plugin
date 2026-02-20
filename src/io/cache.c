#include "io/cache.h"

#include <obs-module.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <diagnostics/log.h>
#include <net/http/http.h>

#include "common/memory.h"

void cache_build_path(const char *type, const char *id, char *out_path, size_t path_size) {

    const char *tmpdir = getenv("TMPDIR");
    snprintf(out_path, path_size, "%sobs_achievement_tracker_%s_%s.png", tmpdir ? tmpdir : "/tmp/", type, id);
}

bool cache_download(const char *url, const char *type, const char *id, char *out_path, size_t path_size) {

    if (!url || url[0] == '\0') {
        return false;
    }

    char path_buf[1024];
    cache_build_path(type, id, path_buf, sizeof(path_buf));

    /* Copy the resolved path to the caller's buffer when provided */
    if (out_path) {
        snprintf(out_path, path_size, "%s", path_buf);
    }

    /* Already cached — nothing to do */
    struct stat st;
    if (stat(path_buf, &st) == 0) {
        obs_log(LOG_INFO, "[Cache] Hit: %s", path_buf);
        return false;
    }

    /* Download into memory */
    uint8_t *data = NULL;
    size_t   size = 0;

    obs_log(LOG_INFO, "[Cache] Downloading '%s'", url);

    if (!http_download(url, &data, &size)) {
        obs_log(LOG_WARNING, "[Cache] Failed to download '%s'", url);
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
