#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Include OBS base.h if available for blogva declaration */
#if __has_include(<util/base.h>)
#include <util/base.h>
#define ACHIEVEMENTS_TRACKER_HAVE_OBS_BASE_H 1
#else
#define ACHIEVEMENTS_TRACKER_HAVE_OBS_BASE_H 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern const char *PLUGIN_NAME;
extern const char *PLUGIN_VERSION;

/* blogva is provided by OBS. When building outside of OBS (unit tests / standalone helper
 * libs like diagnostics-log), declare a minimal prototype so C99 doesn't error. */
#if !ACHIEVEMENTS_TRACKER_HAVE_OBS_BASE_H
void blogva(int log_level, const char *format, va_list args);
#endif

void obs_log(int log_level, const char *format, ...);

#ifdef __cplusplus
}
#endif
