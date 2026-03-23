#include "game.h"

#include "memory.h"
#include <obs-module.h>

game_t *copy_game(const game_t *game) {

    if (!game) {
        return NULL;
    }

    game_t *copy       = bzalloc(sizeof(game_t));
    copy->id           = bstrdup(game->id);
    copy->title        = bstrdup(game->title);
    copy->console_name = bstrdup(game->console_name);
    copy->cover_url    = bstrdup(game->cover_url);

    return copy;
}

void free_game(game_t **game) {

    if (!game || !*game) {
        return;
    }

    game_t *current = *game;

    free_memory((void **)&current->id);
    free_memory((void **)&current->title);
    free_memory((void **)&current->console_name);
    free_memory((void **)&current->cover_url);

    bfree(current);
    *game = NULL;
}
