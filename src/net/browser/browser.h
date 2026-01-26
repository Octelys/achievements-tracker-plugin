#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open a URL in the user's default web browser.
 *
 * On macOS this uses the system `open` command. On other platforms it currently
 * returns false and logs a warning.
 *
 * @param url NUL-terminated URL string to open. Must be non-NULL and non-empty.
 * @return true if the browser launch command was successfully invoked;
 *         false otherwise.
 */
bool open_url(const char *url);

#ifdef __cplusplus
}
#endif
