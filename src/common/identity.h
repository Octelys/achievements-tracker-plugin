#pragma once

#include "common/gamerscore.h"
#include "integrations/xbox/entities/xbox_identity.h"
#include "integrations/retro-achievements/retro_achievements_monitor.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Identifies which integration produced an @c identity_t.
 */
typedef enum {
    IDENTITY_SOURCE_XBOX  = 0, /**< Xbox Live. */
    IDENTITY_SOURCE_RETRO = 1, /**< RetroAchievements. */
} identity_source_t;

/**
 * @file identity.h
 * @brief Source-agnostic user identity record.
 *
 * @c identity_t is an intermediary type that normalises user information from
 * both Xbox Live and RetroAchievements into a single, uniform representation.
 * Consumers that display user information (name, avatar, score) should depend
 * on this type rather than on the source-specific types.
 *
 * Ownership:
 * - Instances returned by @ref copy_identity, @ref identity_from_xbox, or
 *   @ref identity_from_retro are owned by the caller and must be freed with
 *   @ref free_identity_t.
 * - All string fields are heap-allocated (via @c bstrdup / @c bzalloc) and
 *   freed by @ref free_identity_t.
 */
typedef struct identity {
    /**
     * Which integration produced this identity.
     */
    identity_source_t source;

    /**
     * Display name shown to the user.
     *
     * - Xbox:  the gamertag from @c xbox_identity_t.
     * - Retro: the @c display_name field from @c retro_user_t (falls back to
     *          @c username when @c display_name is empty).
     */
    char *name;

    /**
     * URL of the user's avatar/icon image, or NULL when unavailable.
     *
     * - Xbox:  not provided by @c xbox_identity_t; set to NULL.
     * - Retro: the @c avatar_url field from @c retro_user_t.
     */
    char *avatar_url;

    /**
     * Aggregate score / points.
     *
     * - Xbox:  total gamerscore computed via @ref gamerscore_compute from the
     *          @c gamerscore_t passed to @ref identity_from_xbox.
     *          Pass NULL for @p gamerscore to store 0.
     * - Retro: @c max(score, score_softcore) from @c retro_user_t, choosing
     *          the higher of the two values.
     */
    uint32_t score;
} identity_t;

/**
 * @brief Creates a deep copy of an identity.
 *
 * @param identity Source identity to copy (may be NULL).
 *
 * @return Newly allocated copy, or NULL if @p identity is NULL.
 *         The caller owns the returned object and must free it with
 *         @ref free_identity_t.
 */
identity_t *copy_identity(const identity_t *identity);

/**
 * @brief Builds an @c identity_t from an Xbox identity and its gamerscore.
 *
 * @param xbox_identity  Xbox identity supplying the display name.  Must not be
 *                       NULL.
 * @param gamerscore     Gamerscore used to populate @c score.  May be NULL, in
 *                       which case @c score is set to 0.
 *
 * @return Newly allocated @c identity_t.  The caller owns it and must free it
 *         with @ref free_identity_t.  Returns NULL if @p xbox_identity is NULL.
 */
identity_t *identity_from_xbox(const xbox_identity_t *xbox_identity, const gamerscore_t *gamerscore);

/**
 * @brief Builds an @c identity_t from a RetroAchievements user record.
 *
 * The score is set to @c max(user->score, user->score_softcore).
 *
 * @param user  RetroAchievements user record.  Must not be NULL.
 *
 * @return Newly allocated @c identity_t.  The caller owns it and must free it
 *         with @ref free_identity_t.  Returns NULL if @p user is NULL.
 */
identity_t *identity_from_retro(const retro_user_t *user);

/**
 * @brief Frees an identity and sets the caller's pointer to NULL.
 *
 * Safe to call with NULL or with @c *identity == NULL.
 *
 * @param[in,out] identity Address of the @c identity_t pointer to free.
 */
void free_identity_t(identity_t **identity);

#ifdef __cplusplus
}
#endif
