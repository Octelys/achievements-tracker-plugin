#include "achievement.h"
#include "memory.h"
#include <obs-module.h>

/**
 * @brief Deep-copies a linked list of media assets.
 *
 * Allocates a new list where each node is duplicated and the @c url string is
 * copied. The returned list is independent from the input list.
 *
 * @param media_asset Head of the source @c media_asset_t linked list (may be NULL).
 *
 * @return Head of the newly allocated list, or NULL if @p media_asset is NULL.
 *         The caller owns the returned list and must free it with
 *         @ref free_media_asset.
 */
media_asset_t *copy_media_asset(const media_asset_t *media_asset) {
    if (!media_asset) {
        return NULL;
    }

    media_asset_t *root_copy     = NULL;
    media_asset_t *previous_copy = NULL;

    const media_asset_t *current = media_asset;

    while (current) {
        const media_asset_t *next = current->next;

        media_asset_t *copy = bzalloc(sizeof(media_asset_t));
        copy->url           = bstrdup(current->url);

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
 * @brief Frees a linked list of media assets and sets the caller's pointer to NULL.
 *
 * Frees each node's internal allocations (currently @c url) and then the node
 * itself. Safe to call with NULL or with @c *media_asset == NULL.
 *
 * @param[in,out] media_asset Address of the head pointer to free.
 */
void free_media_asset(media_asset_t **media_asset) {

    if (!media_asset || !*media_asset) {
        return;
    }

    media_asset_t *current = *media_asset;

    while (current) {
        media_asset_t *next = current->next;

        free_memory((void **)&current->url);
        free_memory((void **)&current);

        current = next;
    }

    *media_asset = NULL;
}

/**
 * @brief Deep-copies a linked list of rewards.
 *
 * Allocates a new list where each node and its @c value string are duplicated.
 * The returned list is independent from the input list.
 *
 * @param reward Head of the source @c reward_t linked list (may be NULL).
 *
 * @return Head of the newly allocated list, or NULL if @p reward is NULL.
 *         The caller owns the returned list and must free it with
 *         @ref free_reward.
 */
reward_t *copy_reward(const reward_t *reward) {

    if (!reward) {
        return NULL;
    }

    reward_t *root_copy     = NULL;
    reward_t *previous_copy = NULL;

    const reward_t *current = reward;

    while (current) {
        const reward_t *next = current->next;

        reward_t *copy = bzalloc(sizeof(reward_t));

        copy->value = bstrdup(current->value);

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
 * @brief Frees a linked list of rewards and sets the caller's pointer to NULL.
 *
 * Frees each node's internal allocations (currently @c value) and then the node
 * itself. Safe to call with NULL or with @c *reward == NULL.
 *
 * @param[in,out] reward Address of the head pointer to free.
 */
void free_reward(reward_t **reward) {

    if (!reward || !*reward) {
        return;
    }

    reward_t *current = *reward;

    while (current) {

        reward_t *next = current->next;

        free_memory((void **)&current->value);
        free_memory((void **)&current);

        current = next;
    }

    *reward = NULL;
}

/**
 * @brief Deep-copies a linked list of achievements.
 *
 * Allocates a new list where each achievement node is duplicated, including all
 * strings and nested linked lists (media assets and rewards).
 *
 * @param achievement Head of the source @c achievement_t linked list (may be NULL).
 *
 * @return Head of the newly allocated list, or NULL if @p achievement is NULL.
 *         The caller owns the returned list and must free it with
 *         @ref free_achievement.
 */
achievement_t *copy_achievement(const achievement_t *achievement) {

    if (!achievement) {
        return NULL;
    }

    achievement_t *root_copy     = NULL;
    achievement_t *previous_copy = NULL;

    const achievement_t *current = achievement;

    while (current) {
        const achievement_t *next = current->next;

        achievement_t *copy = bzalloc(sizeof(achievement_t));

        copy->id                 = bstrdup(current->id);
        copy->description        = bstrdup(current->description);
        copy->locked_description = bstrdup(current->locked_description);
        copy->name               = bstrdup(current->name);
        copy->progress_state     = bstrdup(current->progress_state);
        copy->service_config_id  = bstrdup(current->service_config_id);
        copy->media_assets       = copy_media_asset(current->media_assets);
        copy->rewards            = copy_reward(current->rewards);
        copy->is_secret          = current->is_secret;

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
 * @brief Frees a linked list of achievements and sets the caller's pointer to NULL.
 *
 * Frees each achievement's internal strings and nested lists (media assets and
 * rewards) and then frees the achievement nodes.
 *
 * Safe to call with NULL or with @c *achievement == NULL.
 *
 * @param[in,out] achievement Address of the head pointer to free.
 */
void free_achievement(achievement_t **achievement) {

    if (!achievement || !*achievement) {
        return;
    }

    achievement_t *current = *achievement;

    while (current) {
        achievement_t *next = current->next;

        free_memory((void **)&current->service_config_id);
        free_memory((void **)&current->id);
        free_memory((void **)&current->name);
        free_memory((void **)&current->description);
        free_memory((void **)&current->locked_description);
        free_memory((void **)&current->progress_state);
        free_media_asset((media_asset_t **)&current->media_assets);
        free_reward((reward_t **)&current->rewards);
        free_memory((void **)&current);

        current = next;
    }

    *achievement = NULL;
}

/**
 * @brief Counts the number of achievements in a linked list.
 *
 * @param achievements Head of the @c achievement_t linked list (may be NULL).
 *
 * @return Number of nodes in the list. Returns 0 if @p achievements is NULL.
 */
int count_achievements(const achievement_t *achievements) {
    int                  count   = 0;
    const achievement_t *current = achievements;

    while (current) {
        count++;
        current = current->next;
    }

    return count;
}
