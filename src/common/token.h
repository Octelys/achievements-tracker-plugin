#pragma once

#include <stdlib.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct token {
    const char *value;
    /* unix timestamp */
    int64_t     expires;
} token_t;

token_t *copy_token(const token_t *token);
void     free_token(token_t **token);
bool     is_token_expired(const token_t *token);

#ifdef __cplusplus
}
#endif
