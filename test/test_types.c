#include "unity.h"

#include "common/types.h"

#include "test/stubs/time/time_stub.h"

void setUp(void) {}
void tearDown(void) {}

//  Tests game.c

static void free_game__game_is_null__null_game_returned(void) {
    //  Arrange.
    game_t *game = NULL;

    //  Act.
    free_game(&game);

    //  Assert.
    TEST_ASSERT_NULL(game);
}

static void free_game__game_is_not_null__null_game_returned(void) {
    //  Arrange.
    game_t *game = bzalloc(sizeof(game_t));
    game->id     = bstrdup("1234567890");
    game->title  = bstrdup("Test Game");

    //  Act.
    free_game(&game);

    //  Assert.
    TEST_ASSERT_NULL(game);
}

static void free_game__game_id_not_null__null_game_returned(void) {
    //  Arrange.
    game_t *game = bzalloc(sizeof(game_t));
    game->id     = NULL;
    game->title  = bstrdup("Test Game");

    //  Act.
    free_game(&game);

    //  Assert.
    TEST_ASSERT_NULL(game);
}

static void free_game__game_title_not_null__null_game_returned(void) {
    //  Arrange.
    game_t *game = bzalloc(sizeof(game_t));
    game->id     = bstrdup("1234567890");
    game->title  = NULL;

    //  Act.
    free_game(&game);

    //  Assert.
    TEST_ASSERT_NULL(game);
}

static void copy_game__game_is_null__null_game_returned(void) {
    //  Arrange.
    game_t *game = NULL;

    //  Act.
    const game_t *copy = copy_game(game);

    //  Assert.
    TEST_ASSERT_NULL(copy);
}

static void copy_game__game_is_not_null__game_returned(void) {
    //  Arrange.
    game_t *game = bzalloc(sizeof(game_t));
    game->id     = bstrdup("1234567890");
    game->title  = bstrdup("Test Game");

    //  Act.
    const game_t *copy = copy_game(game);

    //  Assert.
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_EQUAL_STRING(copy->id, game->id);
    TEST_ASSERT_EQUAL_STRING(copy->title, game->title);
}

static void copy_game__game_id_not_null__game_returned(void) {
    //  Arrange.
    game_t *game = bzalloc(sizeof(game_t));
    game->id     = NULL;
    game->title  = bstrdup("Test Game");

    //  Act.
    const game_t *copy = copy_game(game);

    //  Assert.
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_EQUAL_STRING(copy->id, game->id);
    TEST_ASSERT_EQUAL_STRING(copy->title, game->title);
}

static void copy_game__game_title_not_null__game_returned(void) {
    //  Arrange.
    game_t *game = bzalloc(sizeof(game_t));
    game->id     = bstrdup("1234567890");
    game->title  = NULL;

    //  Act.
    const game_t *copy = copy_game(game);

    //  Assert.
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_EQUAL_STRING(copy->id, game->id);
    TEST_ASSERT_EQUAL_STRING(copy->title, game->title);
}

//  Tests token.c

static void free_token__token_is_null__null_token_returned(void) {
    //  Arrange.
    token_t *token = NULL;

    //  Act.
    free_token(&token);

    //  Assert.
    TEST_ASSERT_NULL(token);
}

static void free_token__token_is_not_null__null_token_returned(void) {
    //  Arrange.
    token_t *token = bzalloc(sizeof(token_t));
    token->value   = bstrdup("default-access-token");
    token->expires = 123;

    //  Act.
    free_token(&token);

    //  Assert.
    TEST_ASSERT_NULL(token);
}

static void free_token__token_value_is_null__null_token_returned(void) {
    //  Arrange.
    token_t *token = bzalloc(sizeof(token_t));
    token->value   = NULL;
    token->expires = 123;

    //  Act.
    free_token(&token);

    //  Assert.
    TEST_ASSERT_NULL(token);
}

static void copy_token__token_is_null__null_token_returned(void) {
    //  Arrange.
    token_t *token = NULL;

    //  Act.
    const token_t *copy = copy_token(token);

    //  Assert.
    TEST_ASSERT_NULL(copy);
}

static void copy_token__token_is_not_null__token_returned(void) {
    //  Arrange.
    token_t *token = bzalloc(sizeof(token_t));
    token->value   = bstrdup("default-access-token");
    token->expires = 123;

    //  Act.
    const token_t *copy = copy_token(token);

    //  Assert.
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_EQUAL_STRING(copy->value, token->value);
    TEST_ASSERT_EQUAL_INT(copy->expires, token->expires);
}

static void copy_token__token_value_is_not_null__token_returned(void) {
    //  Arrange.
    token_t *token = bzalloc(sizeof(token_t));
    token->value   = NULL;
    token->expires = 123;

    //  Act.
    const token_t *copy = copy_token(token);

    //  Assert.
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_EQUAL_STRING(copy->value, token->value);
    TEST_ASSERT_EQUAL_INT(copy->expires, token->expires);
}

static void token_is_expired__token_is_expired__true_returned(void) {
    //  Arrange.
    token_t *token = bzalloc(sizeof(token_t));
    token->expires = 123;

    time_t current_time = 200;
    mock_now(current_time);

    //  Act.
    bool is_expired = token_is_expired(token);

    //  Assert.
    TEST_ASSERT_TRUE(is_expired);
}

static void token_is_expired__token_just_expired__true_returned(void) {
    //  Arrange.
    token_t *token = bzalloc(sizeof(token_t));
    token->expires = 200;

    time_t current_time = 200;
    mock_now(current_time);

    //  Act.
    bool is_expired = token_is_expired(token);

    //  Assert.
    TEST_ASSERT_TRUE(is_expired);
}

static void token_is_expired__token_is_not_expired__false_returned(void) {
    //  Arrange.
    token_t *token = bzalloc(sizeof(token_t));
    token->expires = 250;

    time_t current_time = 200;
    mock_now(current_time);

    //  Act.
    bool is_expired = token_is_expired(token);

    //  Assert.
    TEST_ASSERT_FALSE(is_expired);
}

//  Tests gamerscore.c

static void free_gamerscore__gamerscore_is_null__null_gamerscore_returned(void) {
    //  Arrange.
    gamerscore_t *gamerscore = NULL;

    //  Act.
    free_gamerscore(&gamerscore);

    //  Assert.
    TEST_ASSERT_NULL(gamerscore);
}

static void free_gamerscore__gamerscore_is_not_null__null_gamerscore_returned(void) {
    //  Arrange.
    unlocked_achievement_t *unlocked_achievement_2 = bzalloc(sizeof(unlocked_achievement_t));
    unlocked_achievement_2->value                  = 200;
    unlocked_achievement_2->next                   = NULL;

    unlocked_achievement_t *unlocked_achievement_1 = bzalloc(sizeof(unlocked_achievement_t));
    unlocked_achievement_1->value                  = 100;
    unlocked_achievement_1->next                   = unlocked_achievement_2;

    gamerscore_t *gamerscore          = bzalloc(sizeof(gamerscore_t));
    gamerscore->base_value            = 1000;
    gamerscore->unlocked_achievements = unlocked_achievement_1;

    //  Act.
    free_gamerscore(&gamerscore);

    //  Assert.
    TEST_ASSERT_NULL(gamerscore);
}

static void free_gamerscore__gamerscore_unlocked_achievement_is_null__null_gamerscore_returned(void) {
    //  Arrange.
    gamerscore_t *gamerscore          = bzalloc(sizeof(gamerscore_t));
    gamerscore->base_value            = 1000;
    gamerscore->unlocked_achievements = NULL;

    //  Act.
    free_gamerscore(&gamerscore);

    //  Assert.
    TEST_ASSERT_NULL(gamerscore);
}

static void copy_gamerscore__gamerscore_is_null__null_copy_returned(void) {
    //  Arrange.
    gamerscore_t *gamerscore = NULL;

    //  Act.
    const gamerscore_t *copy = copy_gamerscore(gamerscore);

    //  Assert.
    TEST_ASSERT_NULL(copy);
}

static void copy_gamerscore__gamerscore_is_not_null__copy_returned(void) {
    //  Arrange.
    unlocked_achievement_t *unlocked_achievement_2 = bzalloc(sizeof(unlocked_achievement_t));
    unlocked_achievement_2->value                  = 200;
    unlocked_achievement_2->next                   = NULL;

    unlocked_achievement_t *unlocked_achievement_1 = bzalloc(sizeof(unlocked_achievement_t));
    unlocked_achievement_1->value                  = 100;
    unlocked_achievement_1->next                   = unlocked_achievement_2;

    gamerscore_t *gamerscore          = bzalloc(sizeof(gamerscore_t));
    gamerscore->base_value            = 1000;
    gamerscore->unlocked_achievements = unlocked_achievement_1;

    //  Act.
    const gamerscore_t *copy = copy_gamerscore(gamerscore);

    //  Assert.
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_EQUAL_INT(copy->base_value, gamerscore->base_value);

    //  Validates the 1st achievement.
    TEST_ASSERT_EQUAL_INT(copy->unlocked_achievements->value, gamerscore->unlocked_achievements->value);
    TEST_ASSERT_NOT_NULL(copy->unlocked_achievements->next);

    //  Validates the 2nd achievement.
    TEST_ASSERT_EQUAL_INT(copy->unlocked_achievements->next->value, gamerscore->unlocked_achievements->next->value);
    TEST_ASSERT_NULL(copy->unlocked_achievements->next->next);
}

static void copy_gamerscore__gamerscore_is_null__zero_returned(void) {
    //  Arrange.
    gamerscore_t *gamerscore = NULL;

    //  Act.
    int result = gamerscore_compute(gamerscore);

    //  Assert.
    TEST_ASSERT_EQUAL_INT(result, 0);
}

static void copy_gamerscore__no_unlocked_achievements__base_value_returned(void) {
    //  Arrange.
    gamerscore_t *gamerscore = bzalloc(sizeof(gamerscore_t));
    gamerscore->base_value   = 400;

    //  Act.
    int result = gamerscore_compute(gamerscore);

    //  Assert.
    TEST_ASSERT_EQUAL_INT(result, 400);
}

static void copy_gamerscore__one_unlocked_achievement__total_returned(void) {
    //  Arrange.
    unlocked_achievement_t *unlocked_achievement = bzalloc(sizeof(unlocked_achievement_t));
    unlocked_achievement->id                     = bstrdup("achievement-id");
    unlocked_achievement->value                  = 200;
    unlocked_achievement->next                   = NULL;

    gamerscore_t *gamerscore          = bzalloc(sizeof(gamerscore_t));
    gamerscore->base_value            = 400;
    gamerscore->unlocked_achievements = unlocked_achievement;

    //  Act.
    int result = gamerscore_compute(gamerscore);

    //  Assert.
    TEST_ASSERT_EQUAL_INT(result, 600);
}

static void copy_gamerscore__two_unlocked_achievements__total_returned(void) {
    //  Arrange.
    unlocked_achievement_t *unlocked_achievement_2 = bzalloc(sizeof(unlocked_achievement_t));
    unlocked_achievement_2->id                     = bstrdup("achievement-id-2");
    unlocked_achievement_2->value                  = 200;
    unlocked_achievement_2->next                   = NULL;

    unlocked_achievement_t *unlocked_achievement_1 = bzalloc(sizeof(unlocked_achievement_t));
    unlocked_achievement_1->id                     = bstrdup("achievement-id-1");
    unlocked_achievement_1->value                  = 100;
    unlocked_achievement_1->next                   = unlocked_achievement_2;

    gamerscore_t *gamerscore          = bzalloc(sizeof(gamerscore_t));
    gamerscore->base_value            = 400;
    gamerscore->unlocked_achievements = unlocked_achievement_1;

    //  Act.
    int result = gamerscore_compute(gamerscore);

    //  Assert.
    TEST_ASSERT_EQUAL_INT(result, 700);
}

//  Tests unlocked_achievement.c

static void free_unlocked_achievement__unlocked_achievement_is_null__null_unlocked_achievement_returned(void) {
    //  Arrange.
    unlocked_achievement_t *unlocked_achievement = NULL;

    //  Act.
    free_unlocked_achievement(&unlocked_achievement);

    //  Assert.
    TEST_ASSERT_NULL(unlocked_achievement);
}

static void free_unlocked_achievement__unlocked_achievement_is_not_null__null_unlocked_achievement_returned(void) {
    //  Arrange.
    unlocked_achievement_t *unlocked_achievement_2 = bzalloc(sizeof(unlocked_achievement_t));
    unlocked_achievement_2->id                     = bstrdup("achievement-id-2");
    unlocked_achievement_2->value                  = 200;
    unlocked_achievement_2->next                   = NULL;

    unlocked_achievement_t *unlocked_achievement_1 = bzalloc(sizeof(unlocked_achievement_t));
    unlocked_achievement_1->id                     = bstrdup("achievement-id-1");
    unlocked_achievement_1->value                  = 100;
    unlocked_achievement_1->next                   = unlocked_achievement_2;

    //  Act.
    free_unlocked_achievement(&unlocked_achievement_1);

    //  Assert.
    TEST_ASSERT_NULL(unlocked_achievement_1);

    /* The `free_unlocked_achievement` cannot set other references to `NULL` so
     * the C codebase should never hold-on to these achievements.
     */
    TEST_ASSERT_NOT_NULL(unlocked_achievement_2);
}

static void copy_unlocked_achievement__unlocked_achievement_is_null__null_copy_returned(void) {
    //  Arrange.
    unlocked_achievement_t *unlocked_achievement = NULL;

    //  Act.
    const unlocked_achievement_t *copy = copy_unlocked_achievement(unlocked_achievement);

    //  Assert.
    TEST_ASSERT_NULL(copy);
}

static void copy_unlocked_achievement__unlocked_achievement_is_not_null__copy_returned(void) {
    //  Arrange.
    unlocked_achievement_t *unlocked_achievement_2 = bzalloc(sizeof(unlocked_achievement_t));
    unlocked_achievement_2->id                     = bstrdup("achievement-id-2");
    unlocked_achievement_2->value                  = 200;
    unlocked_achievement_2->next                   = NULL;

    unlocked_achievement_t *unlocked_achievement_1 = bzalloc(sizeof(unlocked_achievement_t));
    unlocked_achievement_1->id                     = bstrdup("achievement-id-1");
    unlocked_achievement_1->value                  = 100;
    unlocked_achievement_1->next                   = unlocked_achievement_2;

    //  Act.
    const unlocked_achievement_t *copy = copy_unlocked_achievement(unlocked_achievement_1);

    //  Assert.
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_EQUAL_STRING(copy->id, unlocked_achievement_1->id);
    TEST_ASSERT_EQUAL_INT(copy->value, unlocked_achievement_1->value);
    TEST_ASSERT_NOT_NULL(copy->next);
    TEST_ASSERT_EQUAL_STRING(copy->next->id, unlocked_achievement_2->id);
    TEST_ASSERT_EQUAL_INT(copy->next->value, unlocked_achievement_2->value);
    TEST_ASSERT_NULL(copy->next->next);
}

//  Tests achievement.c

static void free_reward__reward_is_null__null_reward_returned(void) {
    //  Arrange.
    reward_t *reward = NULL;

    //  Act.
    free_reward(&reward);

    //  Assert.
    TEST_ASSERT_NULL(reward);
}

static void free_reward__one_reward___null_reward_returned(void) {
    //  Arrange.
    reward_t *reward = bzalloc(sizeof(reward_t));
    reward->value    = bstrdup("1000");
    reward->next     = NULL;

    //  Act.
    free_reward(&reward);

    //  Assert.
    TEST_ASSERT_NULL(reward);
}

static void free_reward__two_rewards___null_reward_returned(void) {
    //  Arrange.
    reward_t *reward2 = bzalloc(sizeof(reward_t));
    reward2->value    = bstrdup("1000");
    reward2->next     = NULL;

    reward_t *reward1 = bzalloc(sizeof(reward_t));
    reward1->value    = bstrdup("1000");
    reward1->next     = reward2;

    //  Act.
    free_reward(&reward1);

    //  Assert.
    TEST_ASSERT_NULL(reward1);
}

static void copy_reward__reward_is_null__null_copy_returned(void) {
    //  Arrange.
    reward_t *reward = NULL;

    //  Act.
    const reward_t *copy = copy_reward(reward);

    //  Assert.
    TEST_ASSERT_NULL(copy);
}

static void copy_reward__one_reward__copy_returned(void) {
    //  Arrange.
    reward_t *reward = bzalloc(sizeof(reward_t));
    reward->value    = bstrdup("1000");
    reward->next     = NULL;

    //  Act.
    const reward_t *copy = copy_reward(reward);

    //  Assert.
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_EQUAL_STRING(copy->value, reward->value);
    TEST_ASSERT_NULL(copy->next);
}

static void copy_reward__two_rewards__copy_returned(void) {
    //  Arrange.
    reward_t *reward2 = bzalloc(sizeof(reward_t));
    reward2->value    = bstrdup("1000");
    reward2->next     = NULL;

    reward_t *reward1 = bzalloc(sizeof(reward_t));
    reward1->value    = bstrdup("1000");
    reward1->next     = reward2;

    //  Act.
    const reward_t *copy = copy_reward(reward1);

    //  Assert.
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_EQUAL_STRING(copy->value, reward1->value);
    TEST_ASSERT_NOT_NULL(copy->next);
    TEST_ASSERT_EQUAL_STRING(copy->next->value, reward2->value);
    TEST_ASSERT_NULL(copy->next->next);
}

static void free_media_asset__media_asset_is_null__null_media_asset_returned(void) {
    //  Arrange.
    media_asset_t *media_asset = NULL;

    //  Act.
    free_media_asset(&media_asset);

    //  Assert.
    TEST_ASSERT_NULL(media_asset);
}

static void free_media_asset__one_media_asset__null_media_asset_returned(void) {
    //  Arrange.
    media_asset_t *media_asset = bzalloc(sizeof(media_asset_t));
    media_asset->url           = bstrdup("https://www.example.com/image.png");

    //  Act.
    free_media_asset(&media_asset);

    //  Assert.
    TEST_ASSERT_NULL(media_asset);
}

static void free_media_asset__two_media_assets__null_media_asset_returned(void) {
    //  Arrange.
    media_asset_t *media_asset2 = bzalloc(sizeof(media_asset_t));
    media_asset2->url           = bstrdup("https://www.example.com/image-1.png");

    media_asset_t *media_asset1 = bzalloc(sizeof(media_asset_t));
    media_asset1->url           = bstrdup("https://www.example.com/image-2.png");
    media_asset1->next          = media_asset2;

    //  Act.
    free_media_asset(&media_asset1);

    //  Assert.
    TEST_ASSERT_NULL(media_asset1);
}

static void copy_media_asset__media_asset_is_null__null_copy_returned(void) {
    //  Arrange.
    media_asset_t *media_asset = NULL;

    //  Act.
    media_asset_t *copy = copy_media_asset(media_asset);

    //  Assert.
    TEST_ASSERT_NULL(copy);
}

static void copy_media_asset__one_media_asset__copy_returned(void) {
    //  Arrange.
    media_asset_t *media_asset = bzalloc(sizeof(media_asset_t));
    media_asset->url           = bstrdup("https://www.example.com/image.png");

    //  Act.
    const media_asset_t *copy = copy_media_asset(media_asset);

    //  Assert.
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_EQUAL_STRING(copy->url, media_asset->url);
    TEST_ASSERT_NULL(copy->next);
}

static void copy_media_asset__two_media_assets__copy_returned(void) {
    //  Arrange.
    media_asset_t *media_asset2 = bzalloc(sizeof(media_asset_t));
    media_asset2->url           = bstrdup("https://www.example.com/image-1.png");

    media_asset_t *media_asset1 = bzalloc(sizeof(media_asset_t));
    media_asset1->url           = bstrdup("https://www.example.com/image-2.png");
    media_asset1->next          = media_asset2;

    //  Act.
    const media_asset_t *copy = copy_media_asset(media_asset1);

    //  Assert.
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_EQUAL_STRING(copy->url, media_asset1->url);
    TEST_ASSERT_NOT_NULL(copy->next);
    TEST_ASSERT_EQUAL_STRING(copy->next->url, media_asset2->url);
    TEST_ASSERT_NULL(copy->next->next);
}

int main(void) {
    UNITY_BEGIN();

    //  Tests achievement.c
    RUN_TEST(free_reward__reward_is_null__null_reward_returned);
    RUN_TEST(free_reward__one_reward___null_reward_returned);
    RUN_TEST(free_reward__two_rewards___null_reward_returned);

    RUN_TEST(copy_reward__reward_is_null__null_copy_returned);
    RUN_TEST(copy_reward__one_reward__copy_returned);
    RUN_TEST(copy_reward__two_rewards__copy_returned);

    RUN_TEST(free_media_asset__media_asset_is_null__null_media_asset_returned);
    RUN_TEST(free_media_asset__one_media_asset__null_media_asset_returned);
    RUN_TEST(free_media_asset__two_media_assets__null_media_asset_returned);

    RUN_TEST(copy_media_asset__media_asset_is_null__null_copy_returned);
    RUN_TEST(copy_media_asset__one_media_asset__copy_returned);
    RUN_TEST(copy_media_asset__two_media_assets__copy_returned);

    //  Tests game.c
    RUN_TEST(free_game__game_is_null__null_game_returned);
    RUN_TEST(free_game__game_id_not_null__null_game_returned);
    RUN_TEST(free_game__game_title_not_null__null_game_returned);
    RUN_TEST(free_game__game_is_not_null__null_game_returned);

    RUN_TEST(copy_game__game_is_null__null_game_returned);
    RUN_TEST(copy_game__game_id_not_null__game_returned);
    RUN_TEST(copy_game__game_title_not_null__game_returned);
    RUN_TEST(copy_game__game_is_not_null__game_returned);

    //  Tests token.c
    RUN_TEST(free_token__token_is_null__null_token_returned);
    RUN_TEST(free_token__token_is_not_null__null_token_returned);
    RUN_TEST(free_token__token_value_is_null__null_token_returned);

    RUN_TEST(copy_token__token_is_null__null_token_returned);
    RUN_TEST(copy_token__token_is_not_null__token_returned);
    RUN_TEST(copy_token__token_value_is_not_null__token_returned);

    RUN_TEST(token_is_expired__token_is_expired__true_returned);
    RUN_TEST(token_is_expired__token_just_expired__true_returned);
    RUN_TEST(token_is_expired__token_is_not_expired__false_returned);

    //  Tests gamerscore.c
    RUN_TEST(free_gamerscore__gamerscore_is_null__null_gamerscore_returned);
    RUN_TEST(free_gamerscore__gamerscore_is_not_null__null_gamerscore_returned);
    RUN_TEST(free_gamerscore__gamerscore_unlocked_achievement_is_null__null_gamerscore_returned);

    RUN_TEST(copy_gamerscore__gamerscore_is_null__null_copy_returned);
    RUN_TEST(copy_gamerscore__gamerscore_is_not_null__copy_returned);

    RUN_TEST(copy_gamerscore__gamerscore_is_null__zero_returned);
    RUN_TEST(copy_gamerscore__no_unlocked_achievements__base_value_returned);
    RUN_TEST(copy_gamerscore__one_unlocked_achievement__total_returned);
    RUN_TEST(copy_gamerscore__two_unlocked_achievements__total_returned);

    //  Tests unlocked_achievement.c
    RUN_TEST(free_unlocked_achievement__unlocked_achievement_is_null__null_unlocked_achievement_returned);
    RUN_TEST(free_unlocked_achievement__unlocked_achievement_is_not_null__null_unlocked_achievement_returned);

    RUN_TEST(copy_unlocked_achievement__unlocked_achievement_is_null__null_copy_returned);
    RUN_TEST(copy_unlocked_achievement__unlocked_achievement_is_not_null__copy_returned);

    return UNITY_END();
}
