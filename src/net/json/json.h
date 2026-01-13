#pragma once

#ifdef __cplusplus
extern "C" {
#endif

char *json_read_string(const char *json, const char *key);
char *json_read_string_from_path(const char *json, const char *path);
long *json_get_long_value(const char *json, const char *key);

#ifdef __cplusplus
}
#endif
