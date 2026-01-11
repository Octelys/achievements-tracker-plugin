#pragma once

#include <stdbool.h>

#include "common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

bool xbox_live_get_authenticate(const device_t *device, char **out_uhs, char **out_xid, char **out_xsts_token);

#ifdef __cplusplus
}
#endif
