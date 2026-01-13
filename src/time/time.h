#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool time_iso8601_utc_to_unix(const char *iso8601, int64_t *out_unix_seconds, int32_t *out_fraction_ns);

#ifdef __cplusplus
}
#endif
