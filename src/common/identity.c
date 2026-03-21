#include "common/identity.h"

#include "common/gamerscore.h"
#include "common/memory.h"

#include <obs-module.h>

/* --------------------------------------------------------------------------
 * Internal helper
 * ----------------------------------------------------------------------- */

/**
 * Allocates and zero-initialises a new identity_t.
 */
static identity_t *alloc_identity(void) {
    return bzalloc(sizeof(identity_t));
}

/* --------------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

identity_t *copy_identity(const identity_t *identity) {
    if (!identity) {
        return NULL;
    }

    identity_t *copy = alloc_identity();
    copy->name       = identity->name ? bstrdup(identity->name) : NULL;
    copy->avatar_url = identity->avatar_url ? bstrdup(identity->avatar_url) : NULL;
    copy->score      = identity->score;

    return copy;
}

identity_t *identity_from_xbox(const xbox_identity_t *xbox_identity, const gamerscore_t *gamerscore) {
    if (!xbox_identity) {
        return NULL;
    }

    identity_t *identity = alloc_identity();

    identity->name       = xbox_identity->gamertag ? bstrdup(xbox_identity->gamertag) : NULL;
    identity->avatar_url = NULL; /* Xbox identity does not carry an avatar URL. */

    int computed    = gamerscore_compute(gamerscore);
    identity->score = (computed > 0) ? (uint32_t)computed : 0;

    return identity;
}

identity_t *identity_from_retro(const retro_user_t *user) {
    if (!user) {
        return NULL;
    }

    identity_t *identity = alloc_identity();

    /* Prefer display_name; fall back to username when it is empty. */
    const char *name = (user->display_name[0] != '\0') ? user->display_name : user->username;
    identity->name   = bstrdup(name);

    identity->avatar_url = (user->avatar_url[0] != '\0') ? bstrdup(user->avatar_url) : NULL;

    /* Pick the higher of hardcore and softcore scores. */
    identity->score = (user->score >= user->score_softcore) ? user->score : user->score_softcore;

    return identity;
}

void free_identity_t(identity_t **identity) {
    if (!identity || !*identity) {
        return;
    }

    identity_t *current = *identity;

    free_memory((void **)&current->name);
    free_memory((void **)&current->avatar_url);

    bfree(current);
    *identity = NULL;
}
