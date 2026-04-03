#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file achievement.h
 * @brief Generic achievement abstraction shared across all integrations.
 *
 * This header provides a platform-agnostic representation of an achievement
 * that can be populated from any integration (Xbox Live, RetroAchievements,
 * etc.). Platform-specific contract types are kept in their respective
 * integration folders (e.g. @c integrations/xbox/contracts/).
 */

/**
 * @brief Source platform for an achievement.
 */
typedef enum achievement_source {
    ACHIEVEMENT_SOURCE_UNKNOWN = 0, /**< Source not set / unknown.              */
    ACHIEVEMENT_SOURCE_XBOX    = 1, /**< Achievement originates from Xbox Live. */
    ACHIEVEMENT_SOURCE_RETRO   = 2, /**< Achievement originates from RetroAchievements. */
} achievement_source_t;

/**
 * @brief Generic, platform-agnostic achievement.
 *
 * Fields are the common denominator across Xbox Live and RetroAchievements.
 * All string fields are NUL-terminated and heap-allocated; use
 * @ref copy_achievement / @ref free_achievement to manage lifetime.
 *
 * This type forms a singly-linked list via @c next.
 *
 * Ownership:
 * - Instances returned by @ref copy_achievement are owned by the caller and
 *   must be freed with @ref free_achievement.
 */
typedef struct achievement {
    /** Platform-agnostic string identifier for the achievement. */
    char                *id;
    /** Human-readable display name. */
    char                *name;
    /** Description shown when the achievement is unlocked or not secret. */
    char                *description;
    /** Whether the achievement is secret / hidden. */
    bool                 is_secret;
    /** Point / score value (gamerscore, retro-points, …). */
    int                  value;
    /**
     * Icon URL (PNG/JPEG).
     *
     * Typically the unlocked-badge image.  May be NULL if unavailable.
     */
    char                *icon_url;
    /** Unix timestamp (seconds since epoch) when unlocked; 0 if still locked. */
    int64_t              unlocked_timestamp;
    /** Which integration produced this achievement. */
    achievement_source_t source;
    /** Next achievement in the list, or NULL. */
    struct achievement  *next;
} achievement_t;

/**
 * @brief Deep-copies a linked list of generic achievements.
 *
 * @param achievement Head of the source list (may be NULL).
 *
 * @return Head of the newly allocated list, or NULL if @p achievement is NULL.
 *         The caller owns the returned list and must free it with
 *         @ref free_achievement.
 */
achievement_t *copy_achievement(const achievement_t *achievement);

/**
 * @brief Frees a linked list of generic achievements and sets the caller's pointer to NULL.
 *
 * Frees all string fields and list nodes.
 * Safe to call with NULL or with @c *achievement == NULL.
 *
 * @param[in,out] achievement Address of the head pointer to free.
 */
void free_achievement(achievement_t **achievement);

/**
 * @brief Counts the number of achievements in a linked list.
 *
 * @param achievements Head of the list (may be NULL).
 *
 * @return Number of nodes. Returns 0 if @p achievements is NULL.
 */
int count_achievements(const achievement_t *achievements);

/**
 * @brief Find the most recently unlocked achievement.
 *
 * @param achievements Head of the achievements linked list.
 * @return Pointer to the achievement with the highest @c unlocked_timestamp,
 *         or NULL if none are unlocked.
 */
const achievement_t *find_latest_unlocked_achievement(const achievement_t *achievements);

/**
 * @brief Count the number of locked achievements.
 *
 * @param achievements Head of the achievements linked list.
 * @return Number of locked achievements (@c unlocked_timestamp == 0).
 */
int count_locked_achievements(const achievement_t *achievements);

/**
 * @brief Count the number of unlocked achievements.
 *
 * @param achievements Head of the achievements linked list.
 * @return Number of unlocked achievements (@c unlocked_timestamp != 0).
 */
int count_unlocked_achievements(const achievement_t *achievements);

/**
 * @brief Get a random locked achievement.
 *
 * @param achievements Head of the achievements linked list.
 * @return Pointer to a random locked achievement, or NULL if none are locked.
 */
const achievement_t *get_random_locked_achievement(const achievement_t *achievements);

/**
 * @brief Sort achievements in place (unlocked first, then by timestamp descending).
 *
 * @param achievements Address of the head pointer to sort.
 */
void sort_achievements(achievement_t **achievements);

#ifdef __cplusplus
}
#endif
