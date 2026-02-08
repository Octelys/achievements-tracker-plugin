#include "gamerscore.h"

#include <obs-module.h>

gamerscore_t *copy_gamerscore(const gamerscore_t *gamerscore) {

    if (!gamerscore) {
        return NULL;
    }

    gamerscore_t *copy = bzalloc(sizeof(gamerscore_t));

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

    bfree(current);
    *gamerscore = NULL;
}

int gamerscore_compute(const gamerscore_t *gamerscore) {

    if (!gamerscore) {
        return 0;
    }

    int total_value = gamerscore->base_value;

    const unlocked_achievement_t *current = gamerscore->unlocked_achievements;

    while (current) {
        total_value += current->value;
        current = current->next;
    }

    return total_value;
}
