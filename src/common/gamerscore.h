#pragma once

#include "integrations/xbox/contracts/xbox_unlocked_achievement.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Gamerscore container.
 *
 * Holds a base gamerscore value and a linked list of unlocked achievements that
 * contribute additional points.
 *
 * When an achievement is unlocked, the gamerscore is not immediately updated on the server so retrieving it
 * via the API is not working. Instead, I chose to keep track of all the unlocked achievements locally.
 *
 * Ownership:
 * - Instances returned by @ref copy_gamerscore are owned by the caller and must
 *   be freed with @ref free_gamerscore.
 * - @c unlocked_achievements is a linked list. When produced by
 *   @ref copy_gamerscore, it is deep-copied and freed by @ref free_gamerscore.
 */
typedef struct gamerscore {
    /** Base gamerscore value. */
    int                          base_value;
    /** Linked list of unlocked achievements used to compute additional score. */
    xbox_unlocked_achievement_t *unlocked_achievements;
} gamerscore_t;

/**
 * @brief Creates a deep copy of a gamerscore object.
 *
 * @param gamerscore Source gamerscore to copy (may be NULL).
 *
 * @return Newly allocated copy of @p gamerscore, or NULL if @p gamerscore is
 *         NULL. The caller owns the returned object and must free it with
 *         @ref free_gamerscore.
 */
gamerscore_t *copy_gamerscore(const gamerscore_t *gamerscore);

/**
 * @brief Computes the total gamerscore.
 *
 * Returns @c base_value plus the sum of values from all unlocked achievements.
 *
 * @param gamerscore Gamerscore container (may be NULL).
 *
 * @return Total computed gamerscore. Returns 0 if @p gamerscore is NULL.
 */
int gamerscore_compute(const gamerscore_t *gamerscore);

/**
 * @brief Frees a gamerscore object and sets the caller's pointer to NULL.
 *
 * Safe to call with NULL or with @c *gamerscore == NULL.
 *
 * @param[in,out] gamerscore Address of the @c gamerscore_t pointer to free.
 */
void free_gamerscore(gamerscore_t **gamerscore);

#ifdef __cplusplus
}
#endif
