#pragma once

#ifdef __cplusplus
extern "C" {
#endif

char *json_get_string_value(const char *json, const char *key);

long *json_get_long_value(const char *json, const char *key);

#ifdef __cplusplus
}
#endif
