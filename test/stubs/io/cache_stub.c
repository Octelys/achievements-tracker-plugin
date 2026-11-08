#include "io/cache.h"

bool cache_download(const char *url, const char *type, const char *id, char *out_path, size_t path_size) {
    (void)url;
    (void)type;
    (void)id;
    (void)out_path;
    (void)path_size;
    return true;
}

bool cache_download_with_max_age(const char *url, const char *type, const char *id, int64_t max_age_seconds,
                                 char *out_path, size_t path_size) {
    (void)max_age_seconds;
    return cache_download(url, type, id, out_path, path_size);
}
