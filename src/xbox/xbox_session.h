#pragma once

#include "common/types.h"
#include "util/bmem.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool xbox_session_is_game_played(xbox_session_t *session, const game_t *game);

void xbox_session_change_game(xbox_session_t *session, game_t *game);

int xbox_session_compute_gamerscore(const xbox_session_t *session);

void xbox_session_unlock_achievement(const xbox_session_t *session, const achievements_progress_t *progress);

#ifdef __cplusplus
}
#endif
