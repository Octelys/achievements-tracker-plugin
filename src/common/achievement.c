#include "achievement.h"
#include "memory.h"
#include "test/stubs/util/bmem.h"

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
        copy->next          = previous_copy;

        previous_copy = copy;
        current       = next;

        if (!root_copy) {
            root_copy = copy;
        }
    }

    return root_copy;
}

void free_media_asset(media_asset_t **media_asset) {

    if (!media_asset || !*media_asset) {
        return;
    }

    media_asset_t *current = *media_asset;

    while (current) {
        media_asset_t *next = current->next;

        free_memory((void *)&current->url);
        current->next = NULL;
        free_memory((void *)media_asset);

        current = next;
    }
}

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
        copy->value    = bstrdup(current->value);
        copy->next     = previous_copy;

        previous_copy = copy;
        current       = next;

        if (!root_copy) {
            root_copy = copy;
        }
    }

    return root_copy;
}

void free_reward(reward_t **reward) {

    if (!reward || !*reward) {
        return;
    }

    reward_t *current = *reward;

    while (current) {
        reward_t *next = current->next;

        free_memory((void *)&current->value);
        current->next = NULL;
        free_memory((void *)reward);

        current = next;
    }
}

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

        copy->next = previous_copy;

        previous_copy = copy;
        current       = next;

        if (!root_copy) {
            root_copy = copy;
        }
    }

    return root_copy;
}

void free_achievement(achievement_t **achievement) {

    if (!achievement || !*achievement) {
        return;
    }

    achievement_t *current = *achievement;

    while (current) {
        achievement_t *next = current->next;

        free_memory((void *)&current->service_config_id);
        free_memory((void *)&current->id);
        free_memory((void *)&current->name);
        free_memory((void *)&current->description);
        free_memory((void *)&current->locked_description);
        free_memory((void *)&current->progress_state);
        free_media_asset((void *)&current->media_assets);
        free_reward((void *)&current->rewards);
        free_memory((void **)&current);

        current = next;
    }

    *achievement = NULL;
}

int count_achievements(const achievement_t *achievements) {
    int                  count   = 0;
    const achievement_t *current = achievements;

    while (current) {
        count++;
        current = current->next;
    }

    return count;
}
