#include "token.h"

#include "memory.h"

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

    time_t current_time = time(NULL);

    return difftime(current_time, token->expires) >= 0;
}
