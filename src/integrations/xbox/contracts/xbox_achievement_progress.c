#include "integrations/xbox/contracts/xbox_achievement_progress.h"
#include "common/memory.h"
#include <obs-module.h>

xbox_achievement_progress_t *xbox_copy_achievement_progress(const xbox_achievement_progress_t *achievement_progress) {

    if (!achievement_progress) {
        return NULL;
    }

    xbox_achievement_progress_t *root_copy     = NULL;
    xbox_achievement_progress_t *previous_copy = NULL;

    const xbox_achievement_progress_t *current = achievement_progress;

    while (current) {
        const xbox_achievement_progress_t *next = current->next;

        xbox_achievement_progress_t *copy = bzalloc(sizeof(xbox_achievement_progress_t));
        copy->id                          = bstrdup(current->id);
        copy->progress_state              = bstrdup(current->progress_state);
        copy->service_config_id           = bstrdup(current->service_config_id);
        copy->unlocked_timestamp          = current->unlocked_timestamp;

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

void xbox_free_achievement_progress(xbox_achievement_progress_t **achievement_progress) {

    if (!achievement_progress || !*achievement_progress) {
        return;
    }

    xbox_achievement_progress_t *current = *achievement_progress;

    while (current) {
        xbox_achievement_progress_t *next = current->next;

        free_memory((void **)&current->id);
        free_memory((void **)&current->progress_state);
        free_memory((void **)&current->service_config_id);
        free_memory((void **)&current);

        current = next;
    }

    *achievement_progress = NULL;
}
