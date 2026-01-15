#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <common/types.h>

#ifdef __cplusplus
extern "C" {
#endif

bool xbox_fetch_gamerscore(int64_t *out_gamerscore);

game_t *xbox_get_current_game(void);

/*
 * Fetch achievements for a given titleId using the persisted XSTS token + XUID.
 * Returns a newly allocated JSON string (bfree() it) or NULL on error.
 */
char *xbox_fetch_achievements_json(long *out_http_code);

/* Fetch user presence for the stored XUID. Returns JSON string (bfree) or NULL.
 */
char *xbox_fetch_presence_json(long *out_http_code);

char *xbox_get_game_cover(const game_t *game);

#ifdef __cplusplus
}
#endif
