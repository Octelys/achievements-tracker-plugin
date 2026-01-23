#pragma once

#include "common/unlocked_achievement.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gamerscore {
    int                     base_value;
    unlocked_achievement_t *unlocked_achievements;
} gamerscore_t;

gamerscore_t *copy_gamerscore(const gamerscore_t *gamerscore);
int           gamerscore_compute(const gamerscore_t *gamerscore);

void free_gamerscore(gamerscore_t **gamerscore);

#ifdef __cplusplus
}
#endif
