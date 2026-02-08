#include "token.h"

#include "memory.h"
#include "diagnostics/log.h"
#include "time/time.h"

#include <obs-module.h>
#include <time.h>

token_t *copy_token(const token_t *token) {

    if (!token) {
        return NULL;
    }

    token_t *copy = bzalloc(sizeof(token_t));
    copy->value   = bstrdup(token->value);
    copy->expires = token->expires;

    return copy;
}

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

bool token_is_expired(const token_t *token) {

    if (!token) {
        return true;
    }

    /*
     * Safety margin: treat tokens as expired slightly before their reported
     * expiration time to avoid races/clock skew.
     */
    const int64_t expires_with_margin = token->expires - 15 * 60;

    time_t current_time = now();
    bool   will_expire  = (int64_t)current_time >= expires_with_margin;

    obs_log(LOG_INFO,
            "Now is %lld. Token expires at %lld (effective at %lld). Status: %s",
            (long long)current_time,
            (long long)token->expires,
            (long long)expires_with_margin,
            will_expire ? "token is expired" : "token is valid");

    return will_expire;
}
