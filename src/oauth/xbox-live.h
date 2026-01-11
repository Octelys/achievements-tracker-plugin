#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool xbox_live_get_authenticate(const char *device_uuid, char **out_uhs, char **out_xid, char **out_xsts_token);

#ifdef __cplusplus
}
#endif
