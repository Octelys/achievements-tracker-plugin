#include "unlocked_achievement.h"
#include "memory.h"
#include <obs-module.h>

unlocked_achievement_t *copy_unlocked_achievement(const unlocked_achievement_t *unlocked_achievement) {

    if (!unlocked_achievement) {
        return NULL;
    }

    unlocked_achievement_t *root_copy     = NULL;
    unlocked_achievement_t *previous_copy = NULL;

    const unlocked_achievement_t *current = unlocked_achievement;

    while (current) {
        const unlocked_achievement_t *next = current->next;

        unlocked_achievement_t *copy = bzalloc(sizeof(unlocked_achievement_t));

        copy->id    = bstrdup(current->id);
        copy->value = current->value;

        if (previous_copy) {
            previous_copy->next = copy;
        }

        previous_copy = copy;
        current       = next;

        if (!root_copy) {
            root_copy = copy;
        }
    }

    return root_copy;
}

void free_unlocked_achievement(unlocked_achievement_t **unlocked_achievement) {

    if (!unlocked_achievement || !*unlocked_achievement) {
        return;
    }

    unlocked_achievement_t *current = *unlocked_achievement;

    while (current) {

        unlocked_achievement_t *next = current->next;

        free_memory((void **)&current->id);
        free_memory((void **)&current);

        current = next;
    }

    *unlocked_achievement = NULL;
}
