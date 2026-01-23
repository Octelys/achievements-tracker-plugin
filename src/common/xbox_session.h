#pragma once

#include <stdlib.h>

#include "common/achievement.h"
#include "common/game.h"
#include "common/gamerscore.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xbox_session {
    game_t        *game;
    gamerscore_t  *gamerscore;
    achievement_t *achievements;
} xbox_session_t;

xbox_session_t *copy_xbox_session(const xbox_session_t *session);
int             xbox_session_compute_gamerscore(const xbox_session_t *session);

void free_xbox_session(xbox_session_t **session);

#ifdef __cplusplus
}
#endif
