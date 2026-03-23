#include "io/cache.h"

#include <obs-module.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#define CACHE_MAX_PATH MAX_PATH
#else
#include <limits.h>
#define CACHE_MAX_PATH PATH_MAX
#endif

#include <diagnostics/log.h>
#include <net/http/http.h>

#include "common/memory.h"

static const char *get_temp_dir(char *buf, size_t buf_size) {
    // TMPDIR  — macOS and most Linux distros
    const char *dir = getenv("TMPDIR");
    if (dir && dir[0] != '\0')
        return dir;

    // TEMP / TMP — Windows (and some Linux environments)
    dir = getenv("TEMP");
    if (dir && dir[0] != '\0')
        return dir;

    dir = getenv("TMP");
    if (dir && dir[0] != '\0')
        return dir;

#ifdef _WIN32
    // Last resort on Windows: ask the OS directly
    DWORD len = GetTempPathA((DWORD)buf_size, buf);
    if (len > 0 && len < buf_size)
        return buf;
#endif
    UNUSED_PARAMETER(buf);
    UNUSED_PARAMETER(buf_size);
    // Last resort on POSIX
    return "/tmp/";
}

void cache_build_path(const char *type, const char *id, char *out_path, size_t path_size) {

    char        tmpbuf[CACHE_MAX_PATH] = {0};
    const char *tmpdir                 = get_temp_dir(tmpbuf, sizeof(tmpbuf));

    // Ensure the temp dir ends with a separator
    size_t dirlen = strlen(tmpdir);
    char   sep    = (dirlen > 0 && (tmpdir[dirlen - 1] == '/' || tmpdir[dirlen - 1] == '\\')) ? '\0' : '/';

    if (sep)
        snprintf(out_path, path_size, "%s%cobs_achievement_tracker_%s_%s.png", tmpdir, sep, type, id);
    else
        snprintf(out_path, path_size, "%sobs_achievement_tracker_%s_%s.png", tmpdir, type, id);
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
        obs_log(LOG_DEBUG, "[Cache] Hit: %s", path_buf);
        return false;
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
