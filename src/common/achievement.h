#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Linked-list node describing a media asset for an achievement.
 *
 * Notes on ownership:
 * - In objects created by the copy_* helpers, @c url points to an allocated
 *   NUL-terminated string that must be freed by @ref free_media_asset.
 * - The list is singly-linked via @c next.
 */
typedef struct media_asset {
    /** Media URL (typically UTF-8). */
    const char         *url;
    /** Next node in the list, or NULL. */
    struct media_asset *next;
} media_asset_t;

/**
 * @brief Linked-list node describing a reward associated with an achievement.
 *
 * Notes on ownership:
 * - In objects created by the copy_* helpers, @c value points to an allocated
 *   NUL-terminated string that must be freed by @ref free_reward.
 * - The list is singly-linked via @c next.
 */
typedef struct reward {
    /** Reward value (format depends on upstream service). */
    const char    *value;
    /** Next node in the list, or NULL. */
    struct reward *next;
} reward_t;

/**
 * @brief Linked-list node describing an achievement and its metadata.
 *
 * This type is used as a singly-linked list (@c next). Most fields are strings
 * coming from the service. When an @c achievement_t is produced by
 * @ref copy_achievement, all strings and nested lists are deep-copied.
 *
 * Ownership:
 * - Instances returned by @ref copy_achievement are owned by the caller and must
 *   be freed with @ref free_achievement.
 * - @c media_assets and @c rewards are nested linked lists and are freed by
 *   @ref free_achievement.
 */
typedef struct achievement {
    /** Achievement id. */
    const char          *id;
    /** Service configuration id. Used for monitoring. */
    const char          *service_config_id;
    /** Display name. */
    const char          *name;
    /** Progress state (service-provided string). */
    const char          *progress_state;
    /** Linked list of media assets associated with this achievement. */
    const media_asset_t *media_assets;
    /** Whether the achievement is secret. */
    bool                 is_secret;
    /** Description shown when not secret/unlocked. */
    const char          *description;
    /** Description shown when locked/secret. */
    const char          *locked_description;
    /** Linked list of rewards associated with this achievement. */
    const reward_t      *rewards;
    /** Next achievement in the list, or NULL. */
    struct achievement  *next;
} achievement_t;

/**
 * @brief Deep-copies a linked list of media assets.
 *
 * @param media_asset Head of the source list (may be NULL).
 *
 * @return Head of the newly allocated list, or NULL if @p media_asset is NULL.
 *         The caller owns the returned list and must free it with
 *         @ref free_media_asset.
 */
media_asset_t *copy_media_asset(const media_asset_t *media_asset);

/**
 * @brief Frees a linked list of media assets and sets the caller's pointer to NULL.
 *
 * Safe to call with NULL or with @c *media_asset == NULL.
 *
 * @param[in,out] media_asset Address of the head pointer to free.
 */
void free_media_asset(media_asset_t **media_asset);

/**
 * @brief Deep-copies a linked list of rewards.
 *
 * @param reward Head of the source list (may be NULL).
 *
 * @return Head of the newly allocated list, or NULL if @p reward is NULL.
 *         The caller owns the returned list and must free it with
 *         @ref free_reward.
 */
reward_t *copy_reward(const reward_t *reward);

/**
 * @brief Frees a linked list of rewards and sets the caller's pointer to NULL.
 *
 * Safe to call with NULL or with @c *reward == NULL.
 *
 * @param[in,out] reward Address of the head pointer to free.
 */
void free_reward(reward_t **reward);

/**
 * @brief Deep-copies a linked list of achievements.
 *
 * Performs a deep copy of the list, including all strings and nested
 * @c media_assets and @c rewards lists.
 *
 * @param achievement Head of the source list (may be NULL).
 *
 * @return Head of the newly allocated list, or NULL if @p achievement is NULL.
 *         The caller owns the returned list and must free it with
 *         @ref free_achievement.
 */
achievement_t *copy_achievement(const achievement_t *achievement);

/**
 * @brief Frees a linked list of achievements and sets the caller's pointer to NULL.
 *
 * Frees all strings and nested lists, then frees the list nodes.
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
 * @return Number of nodes in the list. Returns 0 if @p achievements is NULL.
 */
int count_achievements(const achievement_t *achievements);

#ifdef __cplusplus
}
#endif
