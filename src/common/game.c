#include "game.h"
#include "memory.h"
#include "test/stubs/util/bmem.h"

game_t *copy_game(const game_t *game) {
    if (!game) {
        return NULL;
    }

    game_t *copy = bzalloc(sizeof(game_t));
    copy->id     = bstrdup(game->id);
    copy->title  = bstrdup(game->title);

    return copy;
}

void free_game(game_t **game) {

    if (!game || !*game) {
        return;
    }

    game_t *current = *game;

    free_memory((void *)&current->id);
    free_memory((void *)&current->title);

    free(current);
    *game = NULL;
}
