#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Linked-list node describing an achievement progress entry.
 *
 * This is a lightweight representation used to track an achievement's progress
 * state. It is used as a singly-linked list via @c next.
 *
 * Ownership:
 * - Instances returned by @ref copy_achievement_progress are owned by the caller
 *   and must be freed with @ref free_achievement_progress.
 * - All string fields are deep-copied by the copy helper and freed by the free
 *   helper.
 */
typedef struct achievement_progress {
    /** Service configuration id. */
    const char                  *service_config_id;
    /** Achievement id. */
    const char                  *id;
    /** Progress state. */
    const char                  *progress_state;
    /** Next progress entry in the list, or NULL. */
    struct achievement_progress *next;
} achievement_progress_t;

/**
 * @brief Deep-copies a linked list of achievement progress entries.
 *
 * @param progress Head of the source list (may be NULL).
 *
 * @return Head of the newly allocated list, or NULL if @p progress is NULL.
 *         The caller owns the returned list and must free it with
 *         @ref free_achievement_progress.
 */
achievement_progress_t *copy_achievement_progress(const achievement_progress_t *progress);

/**
 * @brief Frees a linked list of achievement progress entries and sets the caller's pointer to NULL.
 *
 * Safe to call with NULL or with @c *progress == NULL.
 *
 * @param[in,out] progress Address of the head pointer to free.
 */
void free_achievement_progress(achievement_progress_t **progress);

#ifdef __cplusplus
}
#endif
