#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <common/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void mock_time(time_t current_time);

time_t time(time_t *);

#ifdef __cplusplus
}
#endif
