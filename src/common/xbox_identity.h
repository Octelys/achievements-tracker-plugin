#pragma once

#include "token.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Xbox identity information for the authenticated user.
 *
 * Provides profile identifiers (gamertag, XUID/XID, UHS) as well as an
 * authentication token used for subsequent Xbox Live calls.
 *
 * Ownership:
 * - Instances returned by @ref copy_xbox_identity are owned by the caller and
 *   must be freed with @ref free_identity.
 * - All string fields are deep-copied by @ref copy_xbox_identity and freed by
 *   @ref free_identity.
 * - @c token is deep-copied by @ref copy_xbox_identity (see @ref token_t) and
 *   freed by @ref free_identity.
 */
typedef struct xbox_identity {
    /** User-facing name (gamertag). */
    char    *gamertag;
    /** Xbox user identifier (XUID/XID). */
    char    *xid;
    /** User hash string. */
    char    *uhs;
    /** Authentication token. */
    token_t *token;
} xbox_identity_t;

/**
 * @brief Creates a deep copy of an Xbox identity.
 *
 * @param identity Source identity to copy (may be NULL).
 *
 * @return Newly allocated copy of @p identity, or NULL if @p identity is NULL.
 *         The caller owns the returned object and must free it with
 *         @ref free_identity.
 */
xbox_identity_t *copy_xbox_identity(const xbox_identity_t *identity);

/**
 * @brief Frees an Xbox identity and sets the caller's pointer to NULL.
 *
 * Safe to call with NULL or with @c *identity == NULL.
 *
 * @param[in,out] identity Address of the @c xbox_identity_t pointer to free.
 */
void free_identity(xbox_identity_t **identity);

#ifdef __cplusplus
}
#endif
