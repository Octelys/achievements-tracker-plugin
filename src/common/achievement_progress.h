#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct achievement_progress {
    const char                  *service_config_id;
    const char                  *id;
    const char                  *progress_state;
    struct achievement_progress *next;
} achievement_progress_t;

achievement_progress_t *copy_achievement_progress(const achievement_progress_t *progress);
void                    free_achievement_progress(achievement_progress_t **progress);

#ifdef __cplusplus
}
#endif
