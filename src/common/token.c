#include "token.h"

#include "memory.h"
#include "diagnostics/log.h"
#include "time/time.h"

#include <obs-module.h>
#include <time.h>

/**
 * @brief Creates a deep copy of a token.
 *
 * Allocates a new @c token_t, duplicates the token string, and copies the expiry
 * timestamp.
 *
 * @param token Source token to copy (may be NULL).
 *
 * @return Newly allocated copy of @p token, or NULL if @p token is NULL.
 *         The caller owns the returned object and must free it with
 *         @ref free_token.
 */
token_t *copy_token(const token_t *token) {

    if (!token) {
        return NULL;
    }

    token_t *copy = bzalloc(sizeof(token_t));
    copy->value   = bstrdup(token->value);
    copy->expires = token->expires;

    return copy;
}

/**
 * @brief Frees a token and sets the caller's pointer to NULL.
 *
 * Frees internal allocations (currently @c value) and then frees the token
 * struct itself.
 *
 * Safe to call with NULL or with @c *token == NULL.
 *
 * @param[in,out] token Address of the @c token_t pointer to free.
 */
void free_token(token_t **token) {

    if (!token || !*token) {
        return;
    }

    token_t *current = *token;

    free_memory((void **)&current->value);
    current->expires = 0;

    bfree(current);
    *token = NULL;
}

/**
 * @brief Checks whether a token is expired.
 *
 * Compares the token expiry timestamp to the current time.
 *
 * @param token Token to check (may be NULL).
 *
 * @return True if the token has expired (or @p token is NULL), false otherwise.
 */
bool token_is_expired(const token_t *token) {

    if (!token) {
        return true;
    }

    time_t current_time = now();
    bool   is_expired   = current_time >= token->expires;

    obs_log(LOG_INFO,
            "Now: %lld. Token expires=%lld. Status? %s",
            (long long)current_time,
            (long long)token->expires,
            is_expired ? "expired" : "valid");

    return is_expired;
}
