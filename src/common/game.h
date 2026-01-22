#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct game {
    const char *id;
    const char *title;
} game_t;

game_t *copy_game(const game_t *game);
void    free_game(game_t **game);

#ifdef __cplusplus
}
#endif
