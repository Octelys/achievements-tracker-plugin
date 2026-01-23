#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

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
bool     token_is_expired(const token_t *token);

#ifdef __cplusplus
}
#endif
