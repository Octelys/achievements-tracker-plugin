#pragma once
#include <stdbool.h>
#include <common/types.h>

#ifdef __cplusplus
extern "C" {
#endif

bool is_presence_message(const char *json_string);

bool is_achievement_message(const char *json_string);

game_t *parse_game(const char *json_string);

achievement_progress_t *parse_achievement_progress(const char *json_string);

achievement_t *parse_achievements(const char *json_string);

#ifdef __cplusplus
}
#endif
