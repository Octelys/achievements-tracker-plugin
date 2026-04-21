#include "integrations/xbox/contracts/xbox_achievement.h"
#include "common/memory.h"
#include "diagnostics/log.h"

#include <inttypes.h>
#include <obs-module.h>
#include <stdio.h>
#include <stdlib.h>

xbox_media_asset_t *xbox_copy_media_asset(const xbox_media_asset_t *media_asset) {
    if (!media_asset) {
        return NULL;
    }

    xbox_media_asset_t *root_copy     = NULL;
    xbox_media_asset_t *previous_copy = NULL;

    const xbox_media_asset_t *current = media_asset;

    while (current) {
        const xbox_media_asset_t *next = current->next;

        xbox_media_asset_t *copy = bzalloc(sizeof(xbox_media_asset_t));
        copy->url                = bstrdup(current->url);

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

void xbox_free_media_asset(xbox_media_asset_t **media_asset) {

    if (!media_asset || !*media_asset) {
        return;
    }

    xbox_media_asset_t *current = *media_asset;

    while (current) {
        xbox_media_asset_t *next = current->next;

        free_memory((void **)&current->url);
        free_memory((void **)&current);

        current = next;
    }

    *media_asset = NULL;
}

xbox_reward_t *xbox_copy_reward(const xbox_reward_t *reward) {

    if (!reward) {
        return NULL;
    }

    xbox_reward_t *root_copy     = NULL;
    xbox_reward_t *previous_copy = NULL;

    const xbox_reward_t *current = reward;

    while (current) {
        const xbox_reward_t *next = current->next;

        xbox_reward_t *copy = bzalloc(sizeof(xbox_reward_t));
        copy->value         = bstrdup(current->value);

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

void xbox_free_reward(xbox_reward_t **reward) {

    if (!reward || !*reward) {
        return;
    }

    xbox_reward_t *current = *reward;

    while (current) {
        xbox_reward_t *next = current->next;

        free_memory((void **)&current->value);
        free_memory((void **)&current);

        current = next;
    }

    *reward = NULL;
}

xbox_achievement_t *xbox_copy_achievement(const xbox_achievement_t *achievement) {

    if (!achievement) {
        return NULL;
    }

    xbox_achievement_t *root_copy     = NULL;
    xbox_achievement_t *previous_copy = NULL;

    const xbox_achievement_t *current = achievement;

    while (current) {
        const xbox_achievement_t *next = current->next;

        xbox_achievement_t *copy = bzalloc(sizeof(xbox_achievement_t));

        copy->id                  = bstrdup(current->id);
        copy->description         = bstrdup(current->description);
        copy->locked_description  = bstrdup(current->locked_description);
        copy->name                = bstrdup(current->name);
        copy->progress_state      = bstrdup(current->progress_state);
        copy->service_config_id   = bstrdup(current->service_config_id);
        copy->icon_url            = bstrdup(current->icon_url);
        copy->media_assets        = xbox_copy_media_asset(current->media_assets);
        copy->rewards             = xbox_copy_reward(current->rewards);
        copy->is_secret           = current->is_secret;
        copy->unlocked_timestamp  = current->unlocked_timestamp;
        copy->progression_current = bstrdup(current->progression_current);
        copy->progression_target  = bstrdup(current->progression_target);

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

void xbox_free_achievement(xbox_achievement_t **achievement) {

    if (!achievement || !*achievement) {
        return;
    }

    xbox_achievement_t *current = *achievement;

    while (current) {
        xbox_achievement_t *next = current->next;

        free_memory((void **)&current->service_config_id);
        free_memory((void **)&current->id);
        free_memory((void **)&current->name);
        free_memory((void **)&current->description);
        free_memory((void **)&current->locked_description);
        free_memory((void **)&current->progress_state);
        free_memory((void **)&current->icon_url);
        free_memory((void **)&current->progression_current);
        free_memory((void **)&current->progression_target);
        xbox_free_media_asset(&current->media_assets);
        xbox_free_reward(&current->rewards);
        free_memory((void **)&current);

        current = next;
    }

    *achievement = NULL;
}

int xbox_count_achievements(const xbox_achievement_t *achievements) {
    int                       count   = 0;
    const xbox_achievement_t *current = achievements;

    while (current) {
        count++;
        current = current->next;
    }

    obs_log(LOG_DEBUG, "Found %d Xbox achievements", count);

    return count;
}

const xbox_achievement_t *xbox_find_latest_unlocked_achievement(const xbox_achievement_t *achievements) {
    const xbox_achievement_t *last_unlocked    = NULL;
    int64_t                   latest_timestamp = 0;

    for (const xbox_achievement_t *a = achievements; a != NULL; a = a->next) {
        if (a->unlocked_timestamp > latest_timestamp) {
            latest_timestamp = a->unlocked_timestamp;
            last_unlocked    = a;
        }
    }

    return last_unlocked;
}

int xbox_count_locked_achievements(const xbox_achievement_t *achievements) {
    int count = 0;

    for (const xbox_achievement_t *a = achievements; a != NULL; a = a->next) {
        if (a->unlocked_timestamp == 0) {
            count++;
        }
    }

    obs_log(LOG_DEBUG, "Found %d locked Xbox achievements", count);

    return count;
}

int xbox_count_unlocked_achievements(const xbox_achievement_t *achievements) {
    int count = 0;

    for (const xbox_achievement_t *a = achievements; a != NULL; a = a->next) {
        if (a->unlocked_timestamp > 0) {
            count++;
        }
    }

    obs_log(LOG_DEBUG, "Found %d unlocked Xbox achievements", count);

    return count;
}

const xbox_achievement_t *xbox_get_random_locked_achievement(const xbox_achievement_t *achievements) {
    const int locked_count = xbox_count_locked_achievements(achievements);

    if (locked_count == 0) {
        return NULL;
    }

    const int target_index  = rand() % locked_count;
    int       current_index = 0;

    for (const xbox_achievement_t *a = achievements; a != NULL; a = a->next) {
        if (a->unlocked_timestamp == 0) {
            if (current_index == target_index) {
                return a;
            }
            current_index++;
        }
    }

    return NULL;
}

void xbox_sort_achievements(xbox_achievement_t **achievements) {

    if (!achievements || !*achievements || !(*achievements)->next) {
        return;
    }

    xbox_achievement_t *sorted  = NULL;
    xbox_achievement_t *current = *achievements;

    /* Insertion sort: take each node from the original list and insert it in sorted order */
    while (current) {
        xbox_achievement_t *next = current->next;

        if (!sorted) {
            sorted       = current;
            sorted->next = NULL;
        } else {
            bool should_insert_before_head = false;

            if (sorted->unlocked_timestamp == 0 && current->unlocked_timestamp != 0) {
                should_insert_before_head = true;
            } else if (current->unlocked_timestamp != 0 && sorted->unlocked_timestamp != 0 &&
                       current->unlocked_timestamp > sorted->unlocked_timestamp) {
                should_insert_before_head = true;
            }

            if (should_insert_before_head) {
                current->next = sorted;
                sorted        = current;
            } else {
                xbox_achievement_t *search = sorted;
                while (search->next) {
                    bool should_insert_here = false;

                    if (search->next->unlocked_timestamp == 0 && current->unlocked_timestamp != 0) {
                        should_insert_here = true;
                    } else if (current->unlocked_timestamp != 0 && search->next->unlocked_timestamp != 0 &&
                               current->unlocked_timestamp > search->next->unlocked_timestamp) {
                        should_insert_here = true;
                    }

                    if (should_insert_here) {
                        break;
                    }
                    search = search->next;
                }

                current->next = search->next;
                search->next  = current;
            }
        }

        current = next;
    }

    *achievements = sorted;
}

achievement_t *xbox_to_achievements(const xbox_achievement_t *xbox) {

    achievement_t *root     = NULL;
    achievement_t *previous = NULL;

    for (const xbox_achievement_t *x = xbox; x != NULL; x = x->next) {
        achievement_t *a      = bzalloc(sizeof(achievement_t));
        a->id                 = bstrdup(x->id);
        a->name               = bstrdup(x->name);
        a->description        = bstrdup(x->description);
        a->icon_url           = bstrdup(x->icon_url);
        a->is_secret          = x->is_secret;
        a->value              = (x->rewards && x->rewards->value) ? atoi(x->rewards->value) : 0;
        a->unlocked_timestamp = x->unlocked_timestamp;
        a->source             = ACHIEVEMENT_SOURCE_XBOX;

        if (x->progression_current && x->progression_target && strcmp(x->progression_current, "0") != 0) {
            char measured[128];
            snprintf(measured, sizeof(measured), "%s/%s", x->progression_current, x->progression_target);
            a->measured_progress = bstrdup(measured);
        }

        if (previous) {
            previous->next = a;
        } else {
            root = a;
        }
        previous = a;
    }

    return root;
}
