#pragma once

#include <stdbool.h>

#include "common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*on_xbox_live_authenticated_t)(void *);

bool xbox_live_authenticate(const device_t *device, void *data, on_xbox_live_authenticated_t callback);

#ifdef __cplusplus
}
#endif
