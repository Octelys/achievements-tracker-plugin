#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct media_asset {
    const char         *url;
    struct media_asset *next;
} media_asset_t;

typedef struct reward {
    const char    *value;
    struct reward *next;
} reward_t;

typedef struct achievement {
    const char          *id;
    const char          *service_config_id;
    const char          *name;
    const char          *progress_state;
    const media_asset_t *media_assets;
    bool                 is_secret;
    const char          *description;
    const char          *locked_description;
    const reward_t      *rewards;
    struct achievement  *next;
} achievement_t;

media_asset_t *copy_media_asset(const media_asset_t *media_asset);
void           free_media_asset(media_asset_t **media_asset);
reward_t      *copy_reward(const reward_t *reward);
void           free_reward(reward_t **reward);
achievement_t *copy_achievement(const achievement_t *achievement);
void           free_achievement(achievement_t **achievement);
int            count_achievements(const achievement_t *achievements);

#ifdef __cplusplus
}
#endif
