#include "net/browser/browser.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include <stdlib.h>
#include <stdio.h>

bool open_url(const char *url) {
	if (!url || !*url)
		return false;

#ifdef __APPLE__

	char cmd[2048];
	int n = snprintf(cmd, sizeof(cmd), "open '%s'", url);
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

#else

	obs_log(LOG_WARNING, "Open-browser not implemented for this OS yet. Please open: %s", url);
	return false;

#endif
}
