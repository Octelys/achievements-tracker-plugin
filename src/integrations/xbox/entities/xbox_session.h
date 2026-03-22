#pragma once

#include "integrations/xbox/contracts/xbox_achievement.h"
#include "common/game.h"
#include "common/gamerscore.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Container for a user's current Xbox session state.
 *
 * Groups together the currently selected game, the gamerscore data, and the list
 * of achievements.
 *
 * Ownership:
 * - Instances returned by @ref copy_xbox_session are owned by the caller and must
 *   be freed with @ref free_xbox_session.
 * - Nested pointers (@c game, @c gamerscore, @c achievements) are deep-copied by
 *   @ref copy_xbox_session and freed by @ref free_xbox_session.
 */
typedef struct xbox_session {
    /** Current game information. */
    game_t             *game;
    /** Gamerscore container (base value + unlocked achievements). */
    gamerscore_t       *gamerscore;
    /** Linked list of achievements for the game. */
    xbox_achievement_t *achievements;
} xbox_session_t;

/**
 * @brief Creates a deep copy of an Xbox session.
 *
 * @param session Source session to copy (may be NULL).
 *
 * @return Newly allocated copy of @p session, or NULL if @p session is NULL.
 *         The caller owns the returned object and must free it with
 *         @ref free_xbox_session.
 */
xbox_session_t *copy_xbox_session(const xbox_session_t *session);

/**
 * @brief Computes the total gamerscore for a session.
 *
 * Equivalent to calling @ref gamerscore_compute on @c session->gamerscore.
 *
 * @param session Session to compute gamerscore for (may be NULL).
 *
 * @return Total gamerscore for the session. Returns 0 if @p session is NULL.
 */
int xbox_session_compute_gamerscore(const xbox_session_t *session);

/**
 * @brief Frees an Xbox session and sets the caller's pointer to NULL.
 *
 * Safe to call with NULL or with @c *session == NULL.
 *
 * @param[in,out] session Address of the @c xbox_session_t pointer to free.
 */
void free_xbox_session(xbox_session_t **session);

#ifdef __cplusplus
}
#endif
