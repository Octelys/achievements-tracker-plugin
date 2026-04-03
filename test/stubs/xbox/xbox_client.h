#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <common/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void mock_xbox_client_set_achievements(xbox_achievement_t *achievements);
void mock_xbox_client_reset(void);

/* Stub for xbox_fetch_gamerscore - does nothing in unit tests */
static inline bool xbox_fetch_gamerscore(int64_t *out_gamerscore) {
    (void)out_gamerscore;
    return false;
}

/* Stub for xbox_get_current_game - returns NULL in unit tests */
static inline game_t *xbox_get_current_game(void) {
    return NULL;
}

xbox_achievement_t *xbox_get_game_achievements(const game_t *game);

/* Stub for xbox_get_game_cover - returns NULL in unit tests */
static inline char *xbox_get_game_cover(const game_t *game) {
    (void)game;
    return NULL;
}

#ifdef __cplusplus
}
#endif
