#include "unity.h"

#include "common/types.h"

#include <string.h>

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
    ;

    //  Act.
    const token_t *copy = copy_token(token);

    //  Assert.
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_EQUAL_STRING(copy->value, token->value);
    TEST_ASSERT_EQUAL_STRING(copy->expires, token->expires);
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
    TEST_ASSERT_EQUAL_STRING(copy->expires, token->expires);
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

    return UNITY_END();
}
