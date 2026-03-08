#include "net/browser/browser.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

bool open_url(const char *url) {
    if (!url || !*url)
        return false;

#ifdef _WIN32

    /* Support for Windows */
    HINSTANCE result = ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        obs_log(LOG_WARNING, "Failed to open browser (ShellExecute error=%ld)", (long)(INT_PTR)result);
        return false;
    }

    return true;

#else

    char cmd[2048];
    int  n = 0;

#ifdef __APPLE__

    /* Support for Apple */
    n = snprintf(cmd, sizeof(cmd), "open '%s'", url);

#else

    /* Support for Linux. */
    n = snprintf(cmd, sizeof(cmd), "xdg-open '%s'", url);

#endif

    if (n <= 0 || (size_t)n >= sizeof(cmd)) {
        obs_log(LOG_WARNING, "Failed to build browser launch command");
        return false;
    }

    int rc = system(cmd);

    if (rc != 0) {
        obs_log(LOG_WARNING, "Failed to open browser (system rc=%d)", rc);
        return false;
    }

    return true;

#endif
}
