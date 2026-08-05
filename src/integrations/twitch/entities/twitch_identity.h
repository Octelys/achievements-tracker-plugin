#pragma once

#include "common/token.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Twitch identity information for the authenticated user.
 *
 * Provides profile identifiers (login, display name, user id) as well as the
 * OAuth tokens used for subsequent Twitch Helix calls.
 *
 * Ownership:
 * - Instances returned by @ref copy_twitch_identity are owned by the caller
 *   and must be freed with @ref free_twitch_identity.
 * - All string fields are deep-copied by @ref copy_twitch_identity and freed
 *   by @ref free_twitch_identity.
 * - @c token is deep-copied by @ref copy_twitch_identity (see @ref token_t)
 *   and freed by @ref free_twitch_identity.
 */
typedef struct twitch_identity {
    /** Twitch login name (lowercase). */
    char    *login;
    /** User-facing display name. */
    char    *display_name;
    /** Twitch user id (numeric, as a string); used as both broadcaster_id and sender_id. */
    char    *user_id;
    /** Access token. */
    token_t *token;
    /**
     * Refresh token. Twitch rotates this value on every refresh — the newest
     * value returned by a refresh call must always replace the persisted one.
     */
    char    *refresh_token;
} twitch_identity_t;

/**
 * @brief Creates a deep copy of a Twitch identity.
 *
 * @param identity Source identity to copy (may be NULL).
 *
 * @return Newly allocated copy of @p identity, or NULL if @p identity is NULL.
 *         The caller owns the returned object and must free it with
 *         @ref free_twitch_identity.
 */
twitch_identity_t *copy_twitch_identity(const twitch_identity_t *identity);

/**
 * @brief Frees a Twitch identity and sets the caller's pointer to NULL.
 *
 * Safe to call with NULL or with @c *identity == NULL.
 *
 * @param[in,out] identity Address of the @c twitch_identity_t pointer to free.
 */
void free_twitch_identity(twitch_identity_t **identity);

#ifdef __cplusplus
}
#endif
