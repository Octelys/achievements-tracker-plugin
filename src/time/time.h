#pragma once

#include <stdbool.h>
#include <stdint.h> // for int64_t/int32_t
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parses an ISO-8601 UTC timestamp into Unix time.
 *
 * This function parses a strict subset of ISO-8601 timestamps that are commonly
 * returned by Xbox/Microsoft APIs.
 *
 * Supported formats (UTC only):
 * - `YYYY-MM-DDTHH:MM:SSZ`
 * - `YYYY-MM-DDTHH:MM:SS.<fraction>Z`
 *
 * Where `<fraction>` contains 1..9 decimal digits and will be scaled to
 * nanoseconds.
 *
 * Strictness / validation rules:
 * - The timestamp must use the `T` separator and a trailing `Z` (UTC designator).
 * - No timezone offsets (`+01:00`, `-0800`, etc.) are supported.
 * - No extra characters (including whitespace) are permitted after the trailing
 *   `Z`.
 * - Calendar fields are validated (month/day ranges, leap years).
 * - Seconds are accepted in the range 0..60 to accommodate leap seconds.
 *
 * Outputs:
 * - @p out_unix_seconds receives whole seconds since the Unix epoch
 *   (`1970-01-01T00:00:00Z`).
 * - @p out_fraction_ns receives the fractional part in nanoseconds
 *   (0..999,999,999). If no fractional part is present, 0 is returned.
 *
 * @param iso8601 Input timestamp string (UTC).
 * @param[out] out_unix_seconds Output Unix timestamp in seconds.
 * @param[out] out_fraction_ns Output fractional part in nanoseconds.
 *
 * @return True on successful parse, false otherwise.
 */
bool time_iso8601_utc_to_unix(const char *iso8601, int64_t *out_unix_seconds, int32_t *out_fraction_ns);

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
