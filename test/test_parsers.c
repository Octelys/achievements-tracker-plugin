#include "unity.h"

#include "text/parsers.h"

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

//  Test is_presence_message

static void is_presence_message__message_is_null_false_returned(void) {
    //  Arrange.
    const char *message = NULL;

    //  Act.
    bool actual = is_presence_message(message);

    //  Assert.
    TEST_ASSERT_FALSE(actual);
}

static void is_presence_message__message_is_empty_false_returned(void) {
    //  Arrange.
    const char *message = " ";

    //  Act.
    bool actual = is_presence_message(message);

    //  Assert.
    TEST_ASSERT_FALSE(actual);
}

static void is_presence_message__message_is_not_json_false_returned(void) {
    //  Arrange.
    const char *message = "this-is-not-a-json";

    //  Act.
    bool actual = is_presence_message(message);

    //  Assert.
    TEST_ASSERT_FALSE(actual);
}

static void is_presence_message__message_is_achievement_false_returned(void) {
    //  Arrange.
    const char *message =
        "{\"serviceConfigId\":\"00000000-0000-0000-0000-00007972ac43\",\"progression\":[{\"id\":\"1\",\"requirements\":[{\"id\":\"00000000-0000-0000-0000-000000000000\",\"current\":\"100\",\"target\":\"100\",\"operationType\":\"Sum\",\"valueType\":\"Integer\",\"ruleParticipationType\":\"Individual\"}],\"progressState\":\"Achieved\",\"timeUnlocked\":\"2026-01-18T02:48:21.707Z\"}],\"contractVersion\":1}";

    //  Act.
    bool actual = is_presence_message(message);

    //  Assert.
    TEST_ASSERT_FALSE(actual);
}

static void is_presence_message__message_is_presence_true_returned(void) {
    //  Arrange.
    const char *message =
        "{\"devicetype\":\"XboxOne\",\"titleid\":0,\"string1\":\"The Outer Worlds 2\",\"string2\":\"\",\"presenceState\":\"Online\",\"presenceText\":\"The Outer Worlds 2\",\"presenceDetails\":[{\"isBroadcasting\":false,\"device\":\"Scarlett\",\"presenceText\":\"Accueil\",\"state\":\"Active\",\"titleId\":\"750323071\",\"isGame\":false,\"isPrimary\":false,\"richPresenceText\":\"\"},{\"isBroadcasting\":false,\"device\":\"Scarlett\",\"presenceText\":\"The Outer Worlds 2\",\"state\":\"Active\",\"titleId\":\"1879711255\",\"isGame\":true,\"isPrimary\":true,\"richPresenceText\":\"\"}],\"xuid\":2533274953419891}";

    //  Act.
    bool actual = is_presence_message(message);

    //  Assert.
    TEST_ASSERT_TRUE(actual);
}

//  Test is_achievement_message

static void is_achievement_message__message_is_null_false_returned(void) {
    //  Arrange.
    const char *message = NULL;

    //  Act.
    bool actual = is_achievement_message(message);

    //  Assert.
    TEST_ASSERT_FALSE(actual);
}

static void is_achievement_message__message_is_empty_false_returned(void) {
    //  Arrange.
    const char *message = " ";

    //  Act.
    bool actual = is_achievement_message(message);

    //  Assert.
    TEST_ASSERT_FALSE(actual);
}

static void is_achievement_message__message_is_not_json_false_returned(void) {
    //  Arrange.
    const char *message = "this-is-not-a-json";

    //  Act.
    bool actual = is_achievement_message(message);

    //  Assert.
    TEST_ASSERT_FALSE(actual);
}

static void is_achievement_message__message_is_achievement_true_returned(void) {
    //  Arrange.
    const char *message =
        "{\"serviceConfigId\":\"00000000-0000-0000-0000-00007972ac43\",\"progression\":[{\"id\":\"1\",\"requirements\":[{\"id\":\"00000000-0000-0000-0000-000000000000\",\"current\":\"100\",\"target\":\"100\",\"operationType\":\"Sum\",\"valueType\":\"Integer\",\"ruleParticipationType\":\"Individual\"}],\"progressState\":\"Achieved\",\"timeUnlocked\":\"2026-01-18T02:48:21.707Z\"}],\"contractVersion\":1}";

    //  Act.
    bool actual = is_achievement_message(message);

    //  Assert.
    TEST_ASSERT_TRUE(actual);
}

static void is_achievement_message__message_is_presence_false_returned(void) {
    //  Arrange.
    const char *message =
        "{\"devicetype\":\"XboxOne\",\"titleid\":0,\"string1\":\"The Outer Worlds 2\",\"string2\":\"\",\"presenceState\":\"Online\",\"presenceText\":\"The Outer Worlds 2\",\"presenceDetails\":[{\"isBroadcasting\":false,\"device\":\"Scarlett\",\"presenceText\":\"Accueil\",\"state\":\"Active\",\"titleId\":\"750323071\",\"isGame\":false,\"isPrimary\":false,\"richPresenceText\":\"\"},{\"isBroadcasting\":false,\"device\":\"Scarlett\",\"presenceText\":\"The Outer Worlds 2\",\"state\":\"Active\",\"titleId\":\"1879711255\",\"isGame\":true,\"isPrimary\":true,\"richPresenceText\":\"\"}],\"xuid\":2533274953419891}";

    //  Act.
    bool actual = is_achievement_message(message);

    //  Assert.
    TEST_ASSERT_FALSE(actual);
}

//  Test parse_game

static void parse_game__message_is_null_null_returned(void) {
    //  Arrange.
    const char *message = NULL;

    //  Act.
    game_t *actual = parse_game(message);

    //  Assert.
    TEST_ASSERT_NULL(actual);
}

static void parse_game__message_is_empty_null_returned(void) {
    //  Arrange.
    const char *message = " ";

    //  Act.
    game_t *actual = parse_game(message);

    //  Assert.
    TEST_ASSERT_NULL(actual);
}

static void parse_game__message_is_not_json_null_returned(void) {
    //  Arrange.
    const char *message = "this-is-not-a-json";

    //  Act.
    game_t *actual = parse_game(message);

    //  Assert.
    TEST_ASSERT_NULL(actual);
}

static void parse_game__message_is_achievement_null_returned(void) {
    //  Arrange.
    const char *message =
        "{\"serviceConfigId\":\"00000000-0000-0000-0000-00007972ac43\",\"progression\":[{\"id\":\"1\",\"requirements\":[{\"id\":\"00000000-0000-0000-0000-000000000000\",\"current\":\"100\",\"target\":\"100\",\"operationType\":\"Sum\",\"valueType\":\"Integer\",\"ruleParticipationType\":\"Individual\"}],\"progressState\":\"Achieved\",\"timeUnlocked\":\"2026-01-18T02:48:21.707Z\"}],\"contractVersion\":1}";

    //  Act.
    game_t *actual = parse_game(message);

    //  Assert.
    TEST_ASSERT_NULL(actual);
}

static void parse_game__message_is_presence_game_returned(void) {
    //  Arrange.
    const char *message =
        "{\"devicetype\":\"XboxOne\",\"titleid\":0,\"string1\":\"The Outer Worlds 2\",\"string2\":\"\",\"presenceState\":\"Online\",\"presenceText\":\"The Outer Worlds 2\",\"presenceDetails\":[{\"isBroadcasting\":false,\"device\":\"Scarlett\",\"presenceText\":\"Accueil\",\"state\":\"Active\",\"titleId\":\"750323071\",\"isGame\":false,\"isPrimary\":false,\"richPresenceText\":\"\"},{\"isBroadcasting\":false,\"device\":\"Scarlett\",\"presenceText\":\"The Outer Worlds 2\",\"state\":\"Active\",\"titleId\":\"1879711255\",\"isGame\":true,\"isPrimary\":true,\"richPresenceText\":\"\"}],\"xuid\":2533274953419891}";

    //  Act.
    game_t *actual = parse_game(message);

    //  Assert.
    TEST_ASSERT_NOT_NULL(actual);
    TEST_ASSERT_EQUAL_STRING(actual->id, "1879711255");
    TEST_ASSERT_EQUAL_STRING(actual->title, "The Outer Worlds 2");
}

//  Test parse_achievement

static void parse_achievements_progress__message_is_null_null_returned(void) {
    //  Arrange.
    const char *message = NULL;

    //  Act.
    achievements_progress_t *actual = parse_achievement_progress(message);

    //  Assert.
    TEST_ASSERT_NULL(actual);
}

static void parse_achievements_progress__message_is_empty_null_returned(void) {
    //  Arrange.
    const char *message = " ";

    //  Act.
    achievements_progress_t *actual = parse_achievement_progress(message);

    //  Assert.
    TEST_ASSERT_NULL(actual);
}

static void parse_achievements_progress__message_is_not_json_null_returned(void) {
    //  Arrange.
    const char *message = "this-is-not-a-json";

    //  Act.
    achievements_progress_t *actual = parse_achievement_progress(message);

    //  Assert.
    TEST_ASSERT_NULL(actual);
}

static void parse_achievements_progress__message_is_achievement_achievement_returned(void) {
    //  Arrange.
    const char *message =
        "{\"serviceConfigId\":\"00000000-0000-0000-0000-00007972ac43\",\"progression\":[{\"id\":\"1\",\"requirements\":[{\"id\":\"00000000-0000-0000-0000-000000000000\",\"current\":\"100\",\"target\":\"100\",\"operationType\":\"Sum\",\"valueType\":\"Integer\",\"ruleParticipationType\":\"Individual\"}],\"progressState\":\"Achieved\",\"timeUnlocked\":\"2026-01-18T02:48:21.707Z\"}],\"contractVersion\":1}";

    //  Act.
    achievements_progress_t *actual = parse_achievement_progress(message);

    //  Assert.
    TEST_ASSERT_NOT_NULL(actual);
    TEST_ASSERT_EQUAL_STRING(actual->service_config_id, "00000000-0000-0000-0000-00007972ac43");
    TEST_ASSERT_EQUAL_STRING(actual->id, "1");
    TEST_ASSERT_EQUAL_STRING(actual->progress_state, "Achieved");
    TEST_ASSERT_NULL(actual->next);
}

static void parse_achievements_progress__message_is_multiple_achievements_achievements_returned(void) {
    //  Arrange.
    const char *message =
        "{\"serviceConfigId\":\"00000000-0000-0000-0000-00007972ac43\",\"progression\":[{\"id\":\"1\",\"requirements\":[{\"id\":\"00000000-0000-0000-0000-000000000000\",\"current\":\"100\",\"target\":\"100\",\"operationType\":\"Sum\",\"valueType\":\"Integer\",\"ruleParticipationType\":\"Individual\"}],\"progressState\":\"Achieved\",\"timeUnlocked\":\"2026-01-18T02:48:21.707Z\"}, {\"id\":\"2\",\"requirements\":[{\"id\":\"00000000-0000-0000-0000-000000000000\",\"current\":\"100\",\"target\":\"100\",\"operationType\":\"Sum\",\"valueType\":\"Integer\",\"ruleParticipationType\":\"Individual\"}],\"progressState\":\"NotAchieved\",\"timeUnlocked\":\"2026-01-18T02:48:21.707Z\"}],\"contractVersion\":1}";

    //  Act.
    achievements_progress_t *actual = parse_achievement_progress(message);

    //  Assert.
    TEST_ASSERT_NOT_NULL(actual);
    TEST_ASSERT_EQUAL_STRING(actual->service_config_id, "00000000-0000-0000-0000-00007972ac43");
    TEST_ASSERT_EQUAL_STRING(actual->id, "1");
    TEST_ASSERT_EQUAL_STRING(actual->progress_state, "Achieved");
    TEST_ASSERT_NOT_NULL(actual->next);
    TEST_ASSERT_EQUAL_STRING(actual->next->service_config_id, "00000000-0000-0000-0000-00007972ac43");
    TEST_ASSERT_EQUAL_STRING(actual->next->id, "2");
    TEST_ASSERT_EQUAL_STRING(actual->next->progress_state, "NotAchieved");
}

//  Test parse_achievements

static void parse_achievements__message_is_multiple_achievements_achievements_returned(void) {
    //  Arrange.
    const char *message =
        "{\"achievements\":[{\"id\":\"1\",\"serviceConfigId\":\"00000000-0000-0000-0000-00007972ac43\",\"name\":\"Daddy's Glasses\",\"titleAssociations\":[{\"name\":\"My Friend Peppa Pig\",\"id\":2037558339}],\"progressState\":\"Achieved\",\"progression\":{\"requirements\":[],\"timeUnlocked\":\"2026-01-18T02:48:21.7070000Z\"},\"mediaAssets\":[{\"name\":\"cf486b2a-3a9e-4c14-b18c-c91e0bb56926\",\"type\":\"Icon\",\"url\":\"https://images-eds-ssl.xboxlive.com/image?url=27S1DHqE.cHkmFg4nspsdzttpqR9mABLoi_h264Ah_brT_74D18wvss1Tpl1Hv0V.ZRAXkfWjJILaiyZZyI_J2paDrXdC_1Gly_3Cnd9yC7IDl0y2ssMo_dvyQ_OhHyuW60ck5614OfHrmzXJvVaS2vM4efPU6iwu2_vBB1TeAE-\"}],\"platforms\":[\"XboxOne\"],\"isSecret\":false,\"description\":\"You found Daddy Pig's Glasses.\",\"lockedDescription\":\"Find Daddy Pig's Glasses.\",\"productId\":\"00000000-0000-0000-0000-00007972ac43\",\"achievementType\":\"Persistent\",\"participationType\":\"Individual\",\"timeWindow\":null,\"rewards\":[{\"name\":null,\"description\":null,\"value\":\"80\",\"type\":\"Gamerscore\",\"mediaAsset\":null,\"valueType\":\"Int\"}],\"estimatedTime\":\"00:00:00\",\"deeplink\":\"\",\"isRevoked\":false},{\"id\":\"2\",\"serviceConfigId\":\"00000000-0000-0000-0000-00007972ac43\",\"name\":\"Where's Mr. Dinosaur?\",\"titleAssociations\":[{\"name\":\"My Friend Peppa Pig\",\"id\":2037558339}],\"progressState\":\"NotStarted\",\"progression\":{\"requirements\":[{\"id\":\"00000000-0000-0000-0000-000000000000\",\"current\":\"0\",\"target\":\"100\",\"operationType\":\"Sum\",\"valueType\":\"Integer\",\"ruleParticipationType\":\"Individual\"}],\"timeUnlocked\":\"0001-01-01T00:00:00.0000000Z\"},\"mediaAssets\":[{\"name\":\"09f94026-8896-4c8a-9b0c-aeb6371e88f0\",\"type\":\"Icon\",\"url\":\"https://images-eds-ssl.xboxlive.com/image?url=27S1DHqE.cHkmFg4nspsdzokISnshkl.YcYqCmweQJubIDDIVJtokZHSoEQgyASVwuVT1yj8cEV8HdUg07CxZIU7xq2U11afQQ26YbPJi4Hr0GTE81qqxgULNGGK4HLbQoUFccQ4orGzYT5WJdvS3Rj.19DADjcNoFcU9ugzoEk-\"}],\"platforms\":[\"XboxOne\"],\"isSecret\":false,\"description\":\"You recovered Mr. Dinosaur for George.\",\"lockedDescription\":\"Recover Mr. Dinosaur for George.\",\"productId\":\"00000000-0000-0000-0000-00007972ac43\",\"achievementType\":\"Persistent\",\"participationType\":\"Individual\",\"timeWindow\":null,\"rewards\":[{\"name\":null,\"description\":null,\"value\":\"80\",\"type\":\"Gamerscore\",\"mediaAsset\":null,\"valueType\":\"Int\"}],\"estimatedTime\":\"00:00:00\",\"deeplink\":\"\",\"isRevoked\":false},{\"id\":\"3\",\"serviceConfigId\":\"00000000-0000-0000-0000-00007972ac43\",\"name\":\"Whose tracks are these?\",\"titleAssociations\":[{\"name\":\"My Friend Peppa Pig\",\"id\":2037558339}],\"progressState\":\"NotStarted\",\"progression\":{\"requirements\":[{\"id\":\"00000000-0000-0000-0000-000000000000\",\"current\":\"0\",\"target\":\"100\",\"operationType\":\"Sum\",\"valueType\":\"Integer\",\"ruleParticipationType\":\"Individual\"}],\"timeUnlocked\":\"0001-01-01T00:00:00.0000000Z\"},\"mediaAssets\":[{\"name\":\"f0045535-229f-43fa-a597-7221cc75a49e\",\"type\":\"Icon\",\"url\":\"https://images-eds-ssl.xboxlive.com/image?url=27S1DHqE.cHkmFg4nspsd5XrL8tQY.MwWYIrfIlaoTapO0RHdDXslvnCBWfl8yoo0ZWDVpcYOKq2azXdVdxQiCKgnAZuGvvtQ33u632vocTNfQkynPR2EgVhYK51rjukn1CH232.4s4mJ859BihrO4wC3sc9NFfV.qv9ykMyqyM-\"}],\"platforms\":[\"XboxOne\"],\"isSecret\":false,\"description\":\"You followed the tracks in the forest and find out who left them.\",\"lockedDescription\":\"Follow the tracks in the forest and find out who left them.\",\"productId\":\"00000000-0000-0000-0000-00007972ac43\",\"achievementType\":\"Persistent\",\"participationType\":\"Individual\",\"timeWindow\":null,\"rewards\":[{\"name\":null,\"description\":null,\"value\":\"80\",\"type\":\"Gamerscore\",\"mediaAsset\":null,\"valueType\":\"Int\"}],\"estimatedTime\":\"00:00:00\",\"deeplink\":\"\",\"isRevoked\":false},{\"id\":\"4\",\"serviceConfigId\":\"00000000-0000-0000-0000-00007972ac43\",\"name\":\"The Best Snowman Ever!\",\"titleAssociations\":[{\"name\":\"My Friend Peppa Pig\",\"id\":2037558339}],\"progressState\":\"NotStarted\",\"progression\":{\"requirements\":[{\"id\":\"00000000-0000-0000-0000-000000000000\",\"current\":\"0\",\"target\":\"100\",\"operationType\":\"Sum\",\"valueType\":\"Integer\",\"ruleParticipationType\":\"Individual\"}],\"timeUnlocked\":\"0001-01-01T00:00:00.0000000Z\"},\"mediaAssets\":[{\"name\":\"06131097-0d93-4b3c-9bf3-2df074c9d7da\",\"type\":\"Icon\",\"url\":\"https://images-eds-ssl.xboxlive.com/image?url=27S1DHqE.cHkmFg4nspsdwidOMhIK6i8yoxIzzG_hp2nj0JqLJgmdEkolMM7aQLA6ffb.9TCmrB5mYexWdu59xwWID84Gu9yiP6yhpgX2ARtT.uvKjV2V51TSqfyiLsH0JAUQBXTmVBXAqK0Q0dc4iGFdJhGBdEm7vzWVyTtrzs-\"}],\"platforms\":[\"XboxOne\"],\"isSecret\":false,\"description\":\"You built a snowman in Snowy Mountain.\",\"lockedDescription\":\"Build a snowman in Snowy Mountain.\",\"productId\":\"00000000-0000-0000-0000-00007972ac43\",\"achievementType\":\"Persistent\",\"participationType\":\"Individual\",\"timeWindow\":null,\"rewards\":[{\"name\":null,\"description\":null,\"value\":\"80\",\"type\":\"Gamerscore\",\"mediaAsset\":null,\"valueType\":\"Int\"}],\"estimatedTime\":\"00:00:00\",\"deeplink\":\"\",\"isRevoked\":false}],\"pagingInfo\":{\"continuationToken\":null,\"totalRecords\":11}}";

    //  Act.
    achievement_t *actual = parse_achievements(message);

    //  Assert.
    TEST_ASSERT_NOT_NULL(actual);

    achievement_t *current_achievement = actual;
    int            achievements_count  = 0;
    while (current_achievement != NULL) {
        achievements_count++;
        current_achievement = current_achievement->next;
    }
    TEST_ASSERT_EQUAL_INT(4, achievements_count);
}

int main(void) {
    UNITY_BEGIN();
    //  Test is_presence_message
    RUN_TEST(is_presence_message__message_is_null_false_returned);
    RUN_TEST(is_presence_message__message_is_empty_false_returned);
    RUN_TEST(is_presence_message__message_is_not_json_false_returned);
    RUN_TEST(is_presence_message__message_is_achievement_false_returned);
    RUN_TEST(is_presence_message__message_is_presence_true_returned);
    //  Test is_achievement_message
    RUN_TEST(is_achievement_message__message_is_null_false_returned);
    RUN_TEST(is_achievement_message__message_is_empty_false_returned);
    RUN_TEST(is_achievement_message__message_is_not_json_false_returned);
    RUN_TEST(is_achievement_message__message_is_achievement_true_returned);
    RUN_TEST(is_achievement_message__message_is_presence_false_returned);
    //  Test parse_game
    RUN_TEST(parse_game__message_is_null_null_returned);
    RUN_TEST(parse_game__message_is_empty_null_returned);
    RUN_TEST(parse_game__message_is_not_json_null_returned);
    RUN_TEST(parse_game__message_is_achievement_null_returned);
    RUN_TEST(parse_game__message_is_presence_game_returned);
    //  Test parse_achievements_progress
    RUN_TEST(parse_achievements_progress__message_is_null_null_returned);
    RUN_TEST(parse_achievements_progress__message_is_empty_null_returned);
    RUN_TEST(parse_achievements_progress__message_is_not_json_null_returned);
    RUN_TEST(parse_achievements_progress__message_is_achievement_achievement_returned);
    RUN_TEST(parse_achievements_progress__message_is_multiple_achievements_achievements_returned);
    //  Test parse_achievements
    RUN_TEST(parse_achievements__message_is_multiple_achievements_achievements_returned);
    return UNITY_END();
}
