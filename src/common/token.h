#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Authentication token with an expiration time.
 *
 * Ownership:
 * - Instances returned by @ref copy_token are owned by the caller and must be
 *   freed with @ref free_token.
 * - @c value is deep-copied by @ref copy_token and freed by @ref free_token.
 */
typedef struct token {
    /** Token value (typically a bearer/access token). */
    char *value;
    /** Expiration time as a Unix timestamp (seconds since epoch). */
    int64_t     expires;
} token_t;

/**
 * @brief Creates a deep copy of a token.
 *
 * @param token Source token to copy (may be NULL).
 *
 * @return Newly allocated copy of @p token, or NULL if @p token is NULL.
 *         The caller owns the returned object and must free it with
 *         @ref free_token.
 */
token_t *copy_token(const token_t *token);

/**
 * @brief Frees a token and sets the caller's pointer to NULL.
 *
 * Safe to call with NULL or with @c *token == NULL.
 *
 * @param[in,out] token Address of the @c token_t pointer to free.
 */
void free_token(token_t **token);

/**
 * @brief Checks whether a token is expired.
 *
 * @param token Token to check (may be NULL).
 *
 * @return True if the token has expired (or @p token is NULL), false otherwise.
 */
bool token_is_expired(const token_t *token);

#ifdef __cplusplus
}
#endif
