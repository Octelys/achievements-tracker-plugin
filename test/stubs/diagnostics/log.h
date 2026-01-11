#pragma once

#include <stdarg.h>

/* Log level constants (matching OBS) */
#define LOG_ERROR   100
#define LOG_WARNING 200
#define LOG_INFO    300
#define LOG_DEBUG   400

/* Stub for obs_log - does nothing in unit tests */
static inline void obs_log(int log_level, const char *format, ...) {
    (void)log_level;
    (void)format;
}

static inline void blogva(int log_level, const char *format, va_list args) {
    (void)log_level;
    (void)format;
    (void)args;
}
