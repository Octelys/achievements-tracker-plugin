#include "integrations/xbox/contracts/xbox_unlocked_achievement.h"
#include "common/memory.h"
#include <obs-module.h>

xbox_unlocked_achievement_t *xbox_copy_unlocked_achievement(const xbox_unlocked_achievement_t *unlocked_achievement) {

    if (!unlocked_achievement) {
        return NULL;
    }

    xbox_unlocked_achievement_t *root_copy     = NULL;
    xbox_unlocked_achievement_t *previous_copy = NULL;

    const xbox_unlocked_achievement_t *current = unlocked_achievement;

    while (current) {
        const xbox_unlocked_achievement_t *next = current->next;

        xbox_unlocked_achievement_t *copy = bzalloc(sizeof(xbox_unlocked_achievement_t));

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

void xbox_free_unlocked_achievement(xbox_unlocked_achievement_t **unlocked_achievement) {

    if (!unlocked_achievement || !*unlocked_achievement) {
        return;
    }

    xbox_unlocked_achievement_t *current = *unlocked_achievement;

    while (current) {
        xbox_unlocked_achievement_t *next = current->next;

        free_memory((void **)&current->id);
        free_memory((void **)&current);

        current = next;
    }

    *unlocked_achievement = NULL;
}
