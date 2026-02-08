#include "xbox_identity.h"

#include "memory.h"

xbox_identity_t *copy_xbox_identity(const xbox_identity_t *identity) {

    if (!identity) {
        return NULL;
    }

    xbox_identity_t *copy = bzalloc(sizeof(xbox_identity_t));
    copy->gamertag        = bstrdup(identity->gamertag);
    copy->xid             = bstrdup(identity->xid);
    copy->uhs             = bstrdup(identity->uhs);
    copy->token           = copy_token(identity->token);

    return copy;
}

void free_identity(xbox_identity_t **identity) {

    if (!identity || !*identity) {
        return;
    }

    xbox_identity_t *current = *identity;

    free_memory((void **)&current->gamertag);
    free_memory((void **)&current->xid);
    free_memory((void **)&current->uhs);
    free_token((token_t **)&current->token);

    bfree(current);
    *identity = NULL;
}
