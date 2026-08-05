#include "integrations/twitch/entities/twitch_identity.h"

#include "common/memory.h"

twitch_identity_t *copy_twitch_identity(const twitch_identity_t *identity) {

    if (!identity) {
        return NULL;
    }

    twitch_identity_t *copy = bzalloc(sizeof(twitch_identity_t));
    copy->login             = bstrdup(identity->login);
    copy->display_name      = bstrdup(identity->display_name);
    copy->user_id           = bstrdup(identity->user_id);
    copy->token             = copy_token(identity->token);
    copy->refresh_token     = bstrdup(identity->refresh_token);

    return copy;
}

void free_twitch_identity(twitch_identity_t **identity) {

    if (!identity || !*identity) {
        return;
    }

    twitch_identity_t *current = *identity;

    free_memory((void **)&current->login);
    free_memory((void **)&current->display_name);
    free_memory((void **)&current->user_id);
    free_token(&current->token);
    free_memory((void **)&current->refresh_token);

    bfree(current);
    *identity = NULL;
}
