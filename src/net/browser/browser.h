#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Open a URL in the user's default browser.
 *
 * Returns true if the launch command was executed successfully, false otherwise.
 *
 * Note: "success" here means the OS command was invoked; it doesn't guarantee
 * the user completed authentication.
 */
bool plugin_open_url_in_browser(const char *url);

#ifdef __cplusplus
}
#endif
