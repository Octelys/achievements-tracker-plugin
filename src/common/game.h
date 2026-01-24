#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Lightweight game descriptor.
 *
 * Represents a game with an identifier and a display title.
 *
 * Ownership:
 * - Instances returned by @ref copy_game are owned by the caller and must be
 *   freed with @ref free_game.
 * - String fields are deep-copied by @ref copy_game and freed by @ref free_game.
 */
typedef struct game {
    /** Game identifier (service-provided). */
    const char *id;
    /** Human-readable title. */
    const char *title;
} game_t;

/**
 * @brief Creates a deep copy of a game object.
 *
 * @param game Source game to copy (may be NULL).
 *
 * @return Newly allocated copy of @p game, or NULL if @p game is NULL.
 *         The caller owns the returned object and must free it with
 *         @ref free_game.
 */
game_t *copy_game(const game_t *game);

/**
 * @brief Frees a game object and sets the caller's pointer to NULL.
 *
 * Safe to call with NULL or with @c *game == NULL.
 *
 * @param[in,out] game Address of the @c game_t pointer to free.
 */
void free_game(game_t **game);

#ifdef __cplusplus
}
#endif
