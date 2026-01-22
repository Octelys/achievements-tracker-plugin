#include "xbox_session.h"
#include "test/stubs/util/bmem.h"

xbox_session_t *copy_xbox_session(xbox_session_t *session) {

    if (!session) {
        return NULL;
    }

    xbox_session_t *copy = bzalloc(sizeof(xbox_session_t));
    copy->game           = copy_game(session->game);
    copy->gamerscore     = copy_gamerscore(session->gamerscore);
    copy->achievements   = copy_achievement(session->achievements);

    return copy;
}

void free_xbox_session(xbox_session_t **session) {

    if (!session || !*session) {
        return;
    }

    xbox_session_t *current = *session;

    free_game(&current->game);
    free_gamerscore(&current->gamerscore);
    free_achievement(&current->achievements);

    free(current);
    *session = NULL;
}
