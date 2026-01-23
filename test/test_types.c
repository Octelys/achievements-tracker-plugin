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

int main(void) {
    UNITY_BEGIN();

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

    //  Tests unlocked_achievement.c
    RUN_TEST(free_unlocked_achievement__unlocked_achievement_is_null__null_unlocked_achievement_returned);
    RUN_TEST(free_unlocked_achievement__unlocked_achievement_is_not_null__null_unlocked_achievement_returned);

    RUN_TEST(copy_unlocked_achievement__unlocked_achievement_is_null__null_copy_returned);
    RUN_TEST(copy_unlocked_achievement__unlocked_achievement_is_not_null__copy_returned);

    return UNITY_END();
}
