#include "unity.h"
#include "stubs/xbox/xbox_client.h"
#include "util/bmem.h"
#include "xbox/xbox_session.h"

#define OUTER_WORLD_2_ID "outer_worlds_2_id"
#define FALLOUT_4_ID "fallout_4_id"

static game_t *game_outer_worlds_2;
static game_t *game_fallout_4;

static achievement_t *achievement_1;
static achievement_t *achievement_2;

void setUp(void) {
    game_outer_worlds_2     = bzalloc(sizeof(game_t));
    game_outer_worlds_2->id = bstrdup(OUTER_WORLD_2_ID);

    game_fallout_4     = bzalloc(sizeof(game_t));
    game_fallout_4->id = bstrdup(FALLOUT_4_ID);

    achievement_2                     = bzalloc(sizeof(achievement_t));
    achievement_2->id                 = bstrdup("achievement-2");
    achievement_2->service_config_id  = NULL;
    achievement_2->name               = NULL;
    achievement_2->description        = NULL;
    achievement_2->locked_description = NULL;
    achievement_2->progress_state     = NULL;
    achievement_2->description        = NULL;
    achievement_2->rewards            = NULL;
    achievement_2->media_assets       = NULL;
    achievement_2->next               = NULL;

    achievement_1                     = bzalloc(sizeof(achievement_t));
    achievement_1->id                 = bstrdup("achievement-1");
    achievement_1->service_config_id  = NULL;
    achievement_1->name               = NULL;
    achievement_1->description        = NULL;
    achievement_1->locked_description = NULL;
    achievement_1->progress_state     = NULL;
    achievement_1->description        = NULL;
    achievement_1->rewards            = NULL;
    achievement_1->media_assets       = NULL;
    achievement_1->next               = NULL;
}

void tearDown(void) {
    mock_xbox_client_reset();
}

//  Test xbox_session_is_game_played

static void xbox_session_is_game_played__session_is_null_and_game_is_null__false_returned(void) {
    //  Arrange.
    xbox_session_t *session = NULL;
    const game_t   *game    = NULL;

    //  Act.
    bool actualResult = xbox_session_is_game_played(session, game);

    //  Assert.
    TEST_ASSERT_FALSE(actualResult);
}

static void xbox_session_is_game_played__session_has_no_game_and_game_is_null__false_returned(void) {
    //  Arrange.
    xbox_session_t session = {
        .game         = NULL,
        .achievements = NULL,
    };

    const game_t *game = NULL;

    //  Act.
    bool actualResult = xbox_session_is_game_played(&session, game);

    //  Assert.
    TEST_ASSERT_FALSE(actualResult);
}

static void xbox_session_is_game_played__session_has_no_game_and_game_is_not_null__false_returned(void) {
    //  Arrange.
    xbox_session_t session = {
        .game         = NULL,
        .achievements = NULL,
    };

    const game_t *game = game_fallout_4;

    //  Act.
    bool actualResult = xbox_session_is_game_played(&session, game);

    //  Assert.
    TEST_ASSERT_FALSE(actualResult);
}

static void xbox_session_is_game_played__session_has_game_and_game_is_different__false_returned(void) {
    //  Arrange.
    xbox_session_t session = {
        .game         = game_outer_worlds_2,
        .achievements = NULL,
    };

    const game_t *game = game_fallout_4;

    //  Act.
    bool actualResult = xbox_session_is_game_played(&session, game);

    //  Assert.
    TEST_ASSERT_FALSE(actualResult);
}

static void xbox_session_is_game_played__session_has_game_and_game_is_the_same__true_returned(void) {
    //  Arrange.
    xbox_session_t session = {
        .game         = game_outer_worlds_2,
        .achievements = NULL,
    };

    const game_t *game = game_outer_worlds_2;

    //  Act.
    bool actualResult = xbox_session_is_game_played(&session, game);

    //  Assert.
    TEST_ASSERT_TRUE(actualResult);
}

//  Test xbox_session_change_game

static void xbox_session_change_game__session_has_no_game_and_game_is_null__no_game_selected(void) {
    //  Arrange.
    xbox_session_t session = {
        .game         = NULL,
        .achievements = NULL,
    };

    game_t *game = NULL;

    //  Act.
    xbox_session_change_game(&session, game);

    //  Assert.
    TEST_ASSERT_NULL(session.game);
    TEST_ASSERT_NULL(session.achievements);
}

static void xbox_session_change_game__session_has_game_and_game_is_null__no_game_selected(void) {
    //  Arrange.
    xbox_session_t session = {.game = game_outer_worlds_2, .achievements = achievement_1};

    game_t *game = NULL;

    //  Act.
    xbox_session_change_game(&session, game);

    //  Assert.
    TEST_ASSERT_NULL(session.game);
    TEST_ASSERT_NULL(session.achievements);
}

static void xbox_session_change_game__session_has_no_game_and_game_is_not_null__game_selected(void) {
    //  Arrange.
    mock_xbox_client_set_achievements(achievement_1);

    xbox_session_t session = {.game = NULL, .achievements = NULL};

    game_t *game = game_fallout_4;

    //  Act.
    xbox_session_change_game(&session, game);

    //  Assert.
    TEST_ASSERT_NOT_NULL(session.game);
    TEST_ASSERT_EQUAL(session.game->id, game_fallout_4->id);

    TEST_ASSERT_NOT_NULL(session.achievements);
    TEST_ASSERT_EQUAL(session.achievements->id, achievement_1->id);
}

static void xbox_session_change_game__session_has_game_and_game_is_not_null__new_game_selected(void) {
    //  Arrange.
    mock_xbox_client_set_achievements(achievement_2);

    xbox_session_t session = {.game = game_outer_worlds_2, .achievements = achievement_1};

    game_t *game = game_fallout_4;

    //  Act.
    xbox_session_change_game(&session, game);

    //  Assert.
    TEST_ASSERT_NOT_NULL(session.game);
    TEST_ASSERT_EQUAL(session.game->id, game_fallout_4->id);

    TEST_ASSERT_NOT_NULL(session.achievements);
    TEST_ASSERT_EQUAL(session.achievements->id, achievement_2->id);
}

//  Test xbox_session_compute_gamerscore

static void xbox_session_compute_gamerscore__session_is_null__0_returned(void) {
    //  Assert.
    xbox_session_t *session = NULL;

    //  Act.
    int gamerscore = xbox_session_compute_gamerscore(session);

    //  Assert.
    TEST_ASSERT_EQUAL(gamerscore, 0);
}

static void xbox_session_compute_gamerscore__session_has_no_unlocked_achievement__base_value_returned(void) {
    //  Assert.
    xbox_session_t *session                    = bzalloc(sizeof(xbox_session_t));
    session->gamerscore                        = bzalloc(sizeof(gamerscore_t));
    session->gamerscore->base_value            = 1000;
    session->gamerscore->unlocked_achievements = NULL;

    //  Act.
    int gamerscore = xbox_session_compute_gamerscore(session);

    //  Assert.
    TEST_ASSERT_EQUAL(gamerscore, session->gamerscore->base_value);
}

static void xbox_session_compute_gamerscore__session_has_one_unlocked_achievement__total_value_returned(void) {
    //  Assert.
    xbox_session_t *session = bzalloc(sizeof(xbox_session_t));

    session->gamerscore             = bzalloc(sizeof(gamerscore_t));
    session->gamerscore->base_value = 1000;

    session->gamerscore->unlocked_achievements        = bzalloc(sizeof(unlocked_achievement_t));
    session->gamerscore->unlocked_achievements->value = 50;
    session->gamerscore->unlocked_achievements->next  = NULL;

    //  Act.
    int gamerscore = xbox_session_compute_gamerscore(session);

    //  Assert.
    TEST_ASSERT_EQUAL(gamerscore, 1000 + 50);
}

static void xbox_session_compute_gamerscore__session_has_two_unlocked_achievements__total_value_returned(void) {
    //  Assert.
    xbox_session_t *session = bzalloc(sizeof(xbox_session_t));

    session->gamerscore             = bzalloc(sizeof(gamerscore_t));
    session->gamerscore->base_value = 1000;

    session->gamerscore->unlocked_achievements        = bzalloc(sizeof(unlocked_achievement_t));
    session->gamerscore->unlocked_achievements->value = 50;

    session->gamerscore->unlocked_achievements->next        = bzalloc(sizeof(unlocked_achievement_t));
    session->gamerscore->unlocked_achievements->next->value = 80;
    session->gamerscore->unlocked_achievements->next->next  = NULL;

    //  Act.
    int gamerscore = xbox_session_compute_gamerscore(session);

    //  Assert.
    TEST_ASSERT_EQUAL(gamerscore, 1000 + 50 + 80);
}

int main(void) {
    UNITY_BEGIN();
    //  Test xbox_session_is_game_played
    RUN_TEST(xbox_session_is_game_played__session_is_null_and_game_is_null__false_returned);
    RUN_TEST(xbox_session_is_game_played__session_has_no_game_and_game_is_null__false_returned);
    RUN_TEST(xbox_session_is_game_played__session_has_no_game_and_game_is_not_null__false_returned);
    RUN_TEST(xbox_session_is_game_played__session_has_game_and_game_is_different__false_returned);
    RUN_TEST(xbox_session_is_game_played__session_has_game_and_game_is_the_same__true_returned);
    //  Test xbox_session_change_game
    RUN_TEST(xbox_session_change_game__session_has_no_game_and_game_is_null__no_game_selected);
    RUN_TEST(xbox_session_change_game__session_has_game_and_game_is_null__no_game_selected);
    RUN_TEST(xbox_session_change_game__session_has_no_game_and_game_is_not_null__game_selected);
    RUN_TEST(xbox_session_change_game__session_has_game_and_game_is_not_null__new_game_selected);
    //   Test xbox_session_compute_gamerscore
    RUN_TEST(xbox_session_compute_gamerscore__session_is_null__0_returned);
    RUN_TEST(xbox_session_compute_gamerscore__session_has_no_unlocked_achievement__base_value_returned);
    RUN_TEST(xbox_session_compute_gamerscore__session_has_one_unlocked_achievement__total_value_returned);
    RUN_TEST(xbox_session_compute_gamerscore__session_has_two_unlocked_achievements__total_value_returned);
    return UNITY_END();
}
