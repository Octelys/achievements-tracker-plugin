#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct unlocked_achievement {
    const char                  *id;
    int                          value;
    struct unlocked_achievement *next;
} unlocked_achievement_t;

unlocked_achievement_t *copy_unlocked_achievement(const unlocked_achievement_t *unlocked_achievement);
void                    free_unlocked_achievement(unlocked_achievement_t **unlocked_achievement);

#ifdef __cplusplus
}
#endif
