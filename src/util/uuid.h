#pragma once

#include <uuid/uuid.h>

#ifdef __cplusplus
extern "C" {
#endif

void uuid_get_random(char random_uuid[37]);

#ifdef __cplusplus
}
#endif
