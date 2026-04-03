#include "achievement.h"
#include "memory.h"
#include "diagnostics/log.h"

#include <inttypes.h>
#include <obs-module.h>
#include <stdlib.h>

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
        copy->name               = bstrdup(current->name);
        copy->description        = bstrdup(current->description);
        copy->icon_url           = bstrdup(current->icon_url);
        copy->is_secret          = current->is_secret;
        copy->value              = current->value;
        copy->unlocked_timestamp = current->unlocked_timestamp;
        copy->source             = current->source;

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

        free_memory((void **)&current->id);
        free_memory((void **)&current->name);
        free_memory((void **)&current->description);
        free_memory((void **)&current->icon_url);
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

    obs_log(LOG_DEBUG, "Found %d achievements", count);

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
        if (a->unlocked_timestamp == 0) {
            count++;
        }
    }

    obs_log(LOG_DEBUG, "Found %d locked achievements", count);

    return count;
}

int count_unlocked_achievements(const achievement_t *achievements) {
    int count = 0;

    for (const achievement_t *a = achievements; a != NULL; a = a->next) {
        if (a->unlocked_timestamp > 0) {
            count++;
        }
    }

    obs_log(LOG_DEBUG, "Found %d unlocked achievements", count);

    return count;
}

const achievement_t *get_random_locked_achievement(const achievement_t *achievements) {
    const int locked_count = count_locked_achievements(achievements);

    if (locked_count == 0) {
        return NULL;
    }

    const int target_index  = rand() % locked_count;
    int       current_index = 0;

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

    /* Insertion sort: unlocked first, then by timestamp descending */
    while (current) {
        achievement_t *next = current->next;

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
                achievement_t *search = sorted;
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
