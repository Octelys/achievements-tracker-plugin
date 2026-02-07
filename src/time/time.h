#pragma once

#include <stdbool.h>
#include <stdint.h> // for int64_t/int32_t
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Returns the current time as seconds since the Unix epoch.
 *
 * This is a small wrapper around the platform time source used by the project.
 * Primarily used to improve testability (can be stubbed in unit tests).
 *
 * @return Current Unix timestamp in seconds.
 */
time_t now();

#ifdef __cplusplus
}
#endif
