#include "achievement_progress.h"

#include "memory.h"
#include <obs-module.h>

/**
 * @brief Deep-copies a linked list of achievement progress entries.
 *
 * Allocates a new list where each node is duplicated and all strings are copied.
 * The returned list is independent from the input list.
 *
 * @param achievement_progress Head of the source @c achievement_progress_t linked
 *        list (may be NULL).
 *
 * @return Head of the newly allocated list, or NULL if @p achievement_progress is
 *         NULL. The caller owns the returned list and must free it with
 *         @ref free_achievement_progress.
 */
achievement_progress_t *copy_achievement_progress(const achievement_progress_t *achievement_progress) {

    if (!achievement_progress) {
        return NULL;
    }

    achievement_progress_t *root_copy     = NULL;
    achievement_progress_t *previous_copy = NULL;

    const achievement_progress_t *current = achievement_progress;

    while (current) {
        const achievement_progress_t *next = current->next;

        achievement_progress_t *copy = bzalloc(sizeof(achievement_progress_t));
        copy->id                     = bstrdup(current->id);
        copy->progress_state         = bstrdup(current->progress_state);
        copy->service_config_id      = bstrdup(current->service_config_id);

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

/**
 * @brief Frees a linked list of achievement progress entries and sets the caller's
 * pointer to NULL.
 *
 * Frees each node's internal strings and then the node itself.
 * Safe to call with NULL or with @c *achievement_progress == NULL.
 *
 * @param[in,out] achievement_progress Address of the head pointer to free.
 */
void free_achievement_progress(achievement_progress_t **achievement_progress) {

    if (!achievement_progress || !*achievement_progress) {
        return;
    }

    achievement_progress_t *current = *achievement_progress;

    while (current) {
        achievement_progress_t *next = current->next;

        free_memory((void **)&current->id);
        free_memory((void **)&current->progress_state);
        free_memory((void **)&current->service_config_id);
        free_memory((void **)&current);

        current = next;
    }

    *achievement_progress = NULL;
}
