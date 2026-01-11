#pragma once

#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

void xbox_achievements_text_source_register(void);

const struct obs_source_info *xbox_achievements_text_source_get(void);

#ifdef __cplusplus
}
#endif
