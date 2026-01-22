#include "gamerscore.h"

#include <obs-module.h>
#include <stdlib.h>

gamerscore_t *copy_gamerscore(gamerscore_t *gamerscore) {

    gamerscore_t *copy          = bzalloc(sizeof(gamerscore_t));
    copy->base_value            = gamerscore->base_value;
    copy->unlocked_achievements = copy_unlocked_achievement(gamerscore->unlocked_achievements);
    return copy;
}

void free_gamerscore(gamerscore_t **gamerscore) {

    if (!gamerscore || !*gamerscore) {
        return;
    }

    gamerscore_t *current = *gamerscore;
    free_unlocked_achievement(&current->unlocked_achievements);

    free(current);
    *gamerscore = NULL;
}
