#pragma once

#include "common/achievement.h"
#include "time/time.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Linked-list node describing a media asset for an Xbox achievement.
 *
 * Notes on ownership:
 * - In objects created by the copy_* helpers, @c url points to an allocated
 *   NUL-terminated string that must be freed by @ref xbox_free_media_asset.
 * - The list is singly-linked via @c next.
 */
typedef struct xbox_media_asset {
    /** Media URL (typically UTF-8). */
    const char              *url;
    /** Next node in the list, or NULL. */
    struct xbox_media_asset *next;
} xbox_media_asset_t;

/**
 * @brief Linked-list node describing a reward associated with an Xbox achievement.
 *
 * Notes on ownership:
 * - In objects created by the copy_* helpers, @c value points to an allocated
 *   NUL-terminated string that must be freed by @ref xbox_free_reward.
 * - The list is singly-linked via @c next.
 */
typedef struct xbox_reward {
    /** Reward value (the format depends on upstream service). */
    const char         *value;
    /** Next node in the list, or NULL. */
    struct xbox_reward *next;
} xbox_reward_t;

/**
 * @brief Linked-list node describing an Xbox achievement and its metadata.
 *
 * This type is used as a singly linked list (@c next). Most fields are strings
 * coming from the Xbox Live service. When an @c xbox_achievement_t is produced
 * by @ref xbox_copy_achievement, all strings and nested lists are deep-copied.
 *
 * Ownership:
 * - Instances returned by @ref xbox_copy_achievement are owned by the caller
 *   and must be freed with @ref xbox_free_achievement.
 * - @c media_assets and @c rewards are nested linked lists and are freed by
 *   @ref xbox_free_achievement.
 */
typedef struct xbox_achievement {
    /** Achievement id. */
    char                    *id;
    /** Service configuration id. Used for monitoring. */
    char                    *service_config_id;
    /** Display name. */
    char                    *name;
    /** Progress state (service-provided string). */
    char                    *progress_state;
    /** Linked list of media assets associated with this achievement. */
    xbox_media_asset_t      *media_assets;
    /** Whether the achievement is secret. */
    bool                     is_secret;
    /** Description shown when not secret/unlocked. */
    char                    *description;
    /** Description shown when locked/secret. */
    char                    *locked_description;
    /** Linked list of rewards associated with this achievement. */
    xbox_reward_t           *rewards;
    /** Unix timestamp (seconds since epoch) when the achievement was unlocked, or 0 if locked. */
    int64_t                  unlocked_timestamp;
    /**
     * Current progression value for the first requirement, as a service-provided string (e.g. "42").
     * NULL if not available.
     */
    char                    *progression_current;
    /**
     * Target progression value for the first requirement, as a service-provided string (e.g. "100").
     * NULL if not available.
     */
    char                    *progression_target;
    /**
     * Small icon or tile image URL for the achievement.
     *
     * Typically points to a PNG/JPEG hosted by the service.
     */
    char                    *icon_url;
    /** Next achievement in the list, or NULL. */
    struct xbox_achievement *next;
} xbox_achievement_t;

/**
 * @brief Deep-copies a linked list of Xbox media assets.
 *
 * @param media_asset Head of the source list (may be NULL).
 *
 * @return Head of the newly allocated list, or NULL if @p media_asset is NULL.
 *         The caller owns the returned list and must free it with
 *         @ref xbox_free_media_asset.
 */
xbox_media_asset_t *xbox_copy_media_asset(const xbox_media_asset_t *media_asset);

/**
 * @brief Frees a linked list of Xbox media assets and sets the caller's pointer to NULL.
 *
 * Safe to call with NULL or with @c *media_asset == NULL.
 *
 * @param[in,out] media_asset Address of the head pointer to free.
 */
void xbox_free_media_asset(xbox_media_asset_t **media_asset);

/**
 * @brief Deep-copies a linked list of Xbox rewards.
 *
 * @param reward Head of the source list (may be NULL).
 *
 * @return Head of the newly allocated list, or NULL if @p reward is NULL.
 *         The caller owns the returned list and must free it with
 *         @ref xbox_free_reward.
 */
xbox_reward_t *xbox_copy_reward(const xbox_reward_t *reward);

/**
 * @brief Frees a linked list of Xbox rewards and sets the caller's pointer to NULL.
 *
 * Safe to call with NULL or with @c *reward == NULL.
 *
 * @param[in,out] reward Address of the head pointer to free.
 */
void xbox_free_reward(xbox_reward_t **reward);

/**
 * @brief Deep-copies a linked list of Xbox achievements.
 *
 * Performs a deep copy of the list, including all strings and nested
 * @c media_assets and @c rewards lists.
 *
 * @param achievement Head of the source list (may be NULL).
 *
 * @return Head of the newly allocated list, or NULL if @p achievement is NULL.
 *         The caller owns the returned list and must free it with
 *         @ref xbox_free_achievement.
 */
xbox_achievement_t *xbox_copy_achievement(const xbox_achievement_t *achievement);

/**
 * @brief Frees a linked list of Xbox achievements and sets the caller's pointer to NULL.
 *
 * Frees all strings and nested lists, then frees the list nodes.
 * Safe to call with NULL or with @c *achievement == NULL.
 *
 * @param[in,out] achievement Address of the head pointer to free.
 */
void xbox_free_achievement(xbox_achievement_t **achievement);

/**
 * @brief Counts the number of Xbox achievements in a linked list.
 *
 * @param achievements Head of the list (may be NULL).
 *
 * @return Number of nodes in the list. Returns 0 if @p achievements is NULL.
 */
int xbox_count_achievements(const xbox_achievement_t *achievements);

/**
 * @brief Find the most recently unlocked Xbox achievement.
 *
 * Iterates through the achievements list and returns the one with the highest
 * unlocked_timestamp (most recent unlock).
 *
 * @param achievements Head of the achievements linked list.
 * @return Pointer to the most recently unlocked achievement, or NULL if none are unlocked.
 */
const xbox_achievement_t *xbox_find_latest_unlocked_achievement(const xbox_achievement_t *achievements);

/**
 * @brief Count the number of locked Xbox achievements.
 *
 * @param achievements Head of the achievements linked list.
 * @return Number of locked achievements (unlocked_timestamp == 0).
 */
int xbox_count_locked_achievements(const xbox_achievement_t *achievements);

/**
 * @brief Count the number of unlocked Xbox achievements.
 *
 * @param achievements Head of the achievements linked list.
 * @return Number of unlocked achievements (unlocked_timestamp != 0).
 */
int xbox_count_unlocked_achievements(const xbox_achievement_t *achievements);

/**
 * @brief Get a random locked Xbox achievement.
 *
 * @param achievements Head of the achievements linked list.
 * @return Pointer to a random locked achievement, or NULL if none are locked.
 */
const xbox_achievement_t *xbox_get_random_locked_achievement(const xbox_achievement_t *achievements);

/**
 * @brief Sort Xbox achievements in place (unlocked first, then by timestamp descending).
 *
 * @param achievements Address of the head pointer to sort.
 */
void xbox_sort_achievements(xbox_achievement_t **achievements);

/**
 * @brief Convert a linked list of Xbox achievements to generic achievements.
 *
 * Maps the common fields from the Xbox contract type to the platform-agnostic
 * @ref achievement_t type. The caller owns the returned list and must free it
 * with @ref free_achievement.
 *
 * @param xbox Head of the Xbox achievements list (may be NULL).
 *
 * @return Head of the newly allocated generic list, or NULL if @p xbox is NULL.
 */
achievement_t *xbox_to_achievements(const xbox_achievement_t *xbox);

#ifdef __cplusplus
}
#endif
