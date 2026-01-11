#pragma once

#ifdef _WIN32
#include <rpc.h>
#else
#include <uuid/uuid.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

void uuid_get_random(char random_uuid[37]);

#ifdef __cplusplus
}
#endif
