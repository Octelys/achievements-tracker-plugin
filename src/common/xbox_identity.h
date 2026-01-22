#pragma once

#include "token.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xbox_identity {
    const char    *gamertag;
    const char    *xid;
    const char    *uhs;
    const token_t *token;
} xbox_identity_t;

xbox_identity_t *copy_xbox_identity(const xbox_identity_t *identity);
void             free_identity(xbox_identity_t **identity);

#ifdef __cplusplus
}
#endif
