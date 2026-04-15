#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Linked-list node describing an Xbox achievement progress entry.
 *
 * This is a lightweight representation used to track an Xbox achievement's
 * progress state. It is used as a singly-linked list via @c next.
 *
 * Ownership:
 * - Instances returned by @ref xbox_copy_achievement_progress are owned by the
 *   caller and must be freed with @ref xbox_free_achievement_progress.
 * - All string fields are deep-copied by the copy helper and freed by the free
 *   helper.
 */
typedef struct xbox_achievement_progress {
    /** Service configuration id. */
    const char                       *service_config_id;
    /** Achievement id. */
    const char                       *id;
    /** Progress state. */
    const char                       *progress_state;
    /** Unix timestamp (seconds since epoch) when the achievement was unlocked, or 0 if locked. */
    int64_t                           unlocked_timestamp;
    /**
     * Current progression value for the first requirement, as a service-provided string (e.g. "42").
     * NULL if not available.
     */
    const char                       *current;
    /**
     * Target progression value for the first requirement, as a service-provided string (e.g. "100").
     * NULL if not available.
     */
    const char                       *target;
    /** Next progress entry in the list, or NULL. */
    struct xbox_achievement_progress *next;
} xbox_achievement_progress_t;

/**
 * @brief Deep-copies a linked list of Xbox achievement progress entries.
 *
 * @param progress Head of the source list (may be NULL).
 *
 * @return Head of the newly allocated list, or NULL if @p progress is NULL.
 *         The caller owns the returned list and must free it with
 *         @ref xbox_free_achievement_progress.
 */
xbox_achievement_progress_t *xbox_copy_achievement_progress(const xbox_achievement_progress_t *progress);

/**
 * @brief Frees a linked list of Xbox achievement progress entries and sets the caller's pointer to NULL.
 *
 * Safe to call with NULL or with @c *progress == NULL.
 *
 * @param[in,out] progress Address of the head pointer to free.
 */
void xbox_free_achievement_progress(xbox_achievement_progress_t **progress);

#ifdef __cplusplus
}
#endif
