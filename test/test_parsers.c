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
    return UNITY_END();
}
