#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <common/types.h>

#ifdef __cplusplus
extern "C" {
#endif

bool xbox_fetch_gamerscore(int64_t *out_gamerscore);

game_t *xbox_get_current_game(void);

achievement_t *xbox_get_game_achievements(const game_t *game);

char *xbox_get_game_cover(const game_t *game);

#ifdef __cplusplus
}
#endif
