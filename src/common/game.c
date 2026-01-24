#include "game.h"

#include "memory.h"
#include <obs-module.h>

/**
 * @brief Creates a deep copy of a game object.
 *
 * Allocates a new @c game_t and duplicates all string fields. The returned
 * instance is independent from the input object.
 *
 * @param game Source game to copy (may be NULL).
 *
 * @return Newly allocated copy of @p game, or NULL if @p game is NULL.
 *         The caller owns the returned object and must free it with
 *         @ref free_game.
 */
game_t *copy_game(const game_t *game) {

    if (!game) {
        return NULL;
    }

    game_t *copy = bzalloc(sizeof(game_t));
    copy->id     = bstrdup(game->id);
    copy->title  = bstrdup(game->title);

    return copy;
}

/**
 * @brief Frees a game object and sets the caller's pointer to NULL.
 *
 * Frees all internal allocations (currently @c id and @c title) and then frees
 * the struct itself.
 *
 * Safe to call with NULL or with @c *game == NULL.
 *
 * @param[in,out] game Address of the @c game_t pointer to free.
 */
void free_game(game_t **game) {

    if (!game || !*game) {
        return;
    }

    game_t *current = *game;

    free_memory((void **)&current->id);
    free_memory((void **)&current->title);

    bfree(current);
    *game = NULL;
}
