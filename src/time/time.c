#include "time/time.h"

#include <stddef.h>

time_t now() {
    return time(NULL);
}
