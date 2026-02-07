#include "time/time.h"

#include <stddef.h>

/**
 * @brief Returns the current time as seconds since the Unix epoch.
 *
 * @return Current Unix timestamp in seconds.
 */
time_t now() {
    return time(NULL);
}
