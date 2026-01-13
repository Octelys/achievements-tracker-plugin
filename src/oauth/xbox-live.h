#pragma once

#include <stdbool.h>

#include "common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*on_xbox_live_authenticate_completed_t)();

bool xbox_live_get_authenticate(const device_t *device, on_xbox_live_authenticate_completed_t callback);

#ifdef __cplusplus
}
#endif
