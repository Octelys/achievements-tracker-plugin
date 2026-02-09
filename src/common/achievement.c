#include "achievement.h"
#include "memory.h"
#include "diagnostics/log.h"

#include <_inttypes.h>
#include <obs-module.h>
#include <stdlib.h>

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

int count_achievements(const achievement_t *achievements) {
    int                  count   = 0;
    const achievement_t *current = achievements;

    while (current) {
        count++;
        current = current->next;
    }

    obs_log(LOG_INFO, "Found %d achievements", count);

    return count;
}

const achievement_t *find_latest_unlocked_achievement(const achievement_t *achievements) {
    const achievement_t *last_unlocked    = NULL;
    int64_t              latest_timestamp = 0;

    for (const achievement_t *a = achievements; a != NULL; a = a->next) {
        if (a->unlocked_timestamp > latest_timestamp) {
            latest_timestamp = a->unlocked_timestamp;
            last_unlocked    = a;
        }
    }

    return last_unlocked;
}

int count_locked_achievements(const achievement_t *achievements) {
    int count = 0;

    for (const achievement_t *a = achievements; a != NULL; a = a->next) {

        obs_log(LOG_INFO, "Achievements #%s %s | %" PRId64, a->id, a->progress_state, a->unlocked_timestamp);

        if (a->unlocked_timestamp == 0) {
            count++;
        }
    }

    obs_log(LOG_INFO, "Found %d locked achievements", count);

    return count;
}

const achievement_t *get_random_locked_achievement(const achievement_t *achievements) {
    int locked_count = count_locked_achievements(achievements);

    if (locked_count == 0) {
        return NULL;
    }

    int target_index  = rand() % locked_count;
    int current_index = 0;

    for (const achievement_t *a = achievements; a != NULL; a = a->next) {
        if (a->unlocked_timestamp == 0) {
            if (current_index == target_index) {
                return a;
            }
            current_index++;
        }
    }

    return NULL;
}

void sort_achievements(achievement_t **achievements) {

    if (!achievements || !*achievements || !(*achievements)->next) {
        return;
    }

    achievement_t *sorted  = NULL;
    achievement_t *current = *achievements;

    /* Insertion sort: take each node from the original list and insert it in sorted order */
    while (current) {
        achievement_t *next = current->next;

        /* Insert current into the sorted list at the correct position */
        if (!sorted) {
            /* First node in sorted list */
            sorted       = current;
            sorted->next = NULL;
        } else {
            /* Determine if current should go before sorted head */
            bool should_insert_before_head = false;

            if (sorted->unlocked_timestamp == 0 && current->unlocked_timestamp != 0) {
                /* Current is unlocked, head is locked */
                should_insert_before_head = true;
            } else if (current->unlocked_timestamp != 0 && sorted->unlocked_timestamp != 0 &&
                       current->unlocked_timestamp > sorted->unlocked_timestamp) {
                /* Both unlocked, current has more recent timestamp */
                should_insert_before_head = true;
            }

            if (should_insert_before_head) {
                /* Insert at head */
                current->next = sorted;
                sorted        = current;
            } else {
                /* Find the correct position in the sorted list */
                achievement_t *search = sorted;
                while (search->next) {
                    bool should_insert_here = false;

                    if (search->next->unlocked_timestamp == 0 && current->unlocked_timestamp != 0) {
                        /* Current is unlocked, next is locked */
                        should_insert_here = true;
                    } else if (current->unlocked_timestamp != 0 && search->next->unlocked_timestamp != 0 &&
                               current->unlocked_timestamp > search->next->unlocked_timestamp) {
                        /* Both unlocked, current has more recent timestamp */
                        should_insert_here = true;
                    }

                    if (should_insert_here) {
                        break;
                    }
                    search = search->next;
                }

                /* Insert current after search */
                current->next = search->next;
                search->next  = current;
            }
        }

        current = next;
    }

    *achievements = sorted;
}
