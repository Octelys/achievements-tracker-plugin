#include "test/stubs/xbox/xbox_client.h"

static xbox_achievement_t *mock_achievements = NULL;

void mock_xbox_client_set_achievements(xbox_achievement_t *achievements) {
    mock_achievements = achievements;
}

void mock_xbox_client_reset(void) {
    mock_achievements = NULL;
}

xbox_achievement_t *xbox_get_game_achievements(const game_t *game) {
    (void)game;
    return mock_achievements;
}
