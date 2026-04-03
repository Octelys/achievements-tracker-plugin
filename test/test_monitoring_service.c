/**
 * @file test_monitoring_service.c
 * @brief Unit tests for monitoring_service.c — active-identity notifications.
 *
 * The xbox_monitor, xbox_client, and retro_achievements_monitor are fully
 * stubbed.  Tests drive their callbacks directly via the mock_* helpers and
 * assert that on_monitoring_active_identity_changed_t subscribers receive the
 * correct identity (or NULL) in every scenario.
 *
 * Scenarios covered:
 *  Xbox:
 *   1. Xbox connects without a game → no identity notification
 *   2. Xbox connects, game played → identity notified
 *   3. Xbox game played before connection → NULL (identity not cached yet)
 *   4. Xbox disconnects → NULL notified
 *   5. Xbox disconnects with retro game active → retro identity takes over
 *   6. Xbox game stops (game_played NULL) → active identity becomes NULL
 *
 *  RetroAchievements:
 *   7. Retro user + game → retro identity notified
 *   8. Retro game before user → NULL, then identity once user arrives
 *   9. Retro user before game → NULL, then identity once game arrives
 *  10. Retro no_game → active identity callback fires with NULL
 *  11. Retro no_user (game active) → active identity callback fires with NULL
 *  12. Retro game stops (no_game) after active identity → NULL notified
 *
 *  Last-game-source priority:
 *  13. Xbox game then retro game → retro identity active
 *  14. Retro game then Xbox game → Xbox identity active
 *  15. Xbox game, then retro game, then Xbox game again → Xbox identity active
 *  16. Retro no_user while Xbox game also active → Xbox identity takes over
 */

#include "unity.h"

#include "test/stubs/integrations/xbox_monitor_stub.h"
#include "test/stubs/integrations/retro_achievements_monitor_stub.h"

#include "integrations/monitoring_service.h"
#include "integrations/xbox/entities/xbox_identity.h"
#include "common/game.h"
#include "common/token.h"

#include <string.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Helpers — build lightweight test fixtures
 * ---------------------------------------------------------------------- */

static xbox_identity_t *make_xbox_identity(const char *gamertag) {
    token_t *token = bzalloc(sizeof(token_t));
    token->value   = bstrdup("test-token");
    token->expires = 9999999999LL;

    xbox_identity_t *id = bzalloc(sizeof(xbox_identity_t));
    id->gamertag        = bstrdup(gamertag);
    id->xid             = bstrdup("xuid-123");
    id->uhs             = bstrdup("uhs-abc");
    id->token           = token;
    return id;
}

static game_t *make_xbox_game(const char *id, const char *title) {
    game_t *g = bzalloc(sizeof(game_t));
    g->id     = bstrdup(id);
    g->title  = bstrdup(title);
    return g;
}

static void fill_retro_user(retro_user_t *u, const char *username, const char *display_name) {
    memset(u, 0, sizeof(*u));
    strncpy(u->username, username, sizeof(u->username) - 1);
    strncpy(u->display_name, display_name, sizeof(u->display_name) - 1);
    u->score          = 1500;
    u->score_softcore = 800;
}

static void fill_retro_game(retro_game_t *g, const char *id, const char *name) {
    memset(g, 0, sizeof(*g));
    strncpy(g->game_id, id, sizeof(g->game_id) - 1);
    strncpy(g->game_name, name, sizeof(g->game_name) - 1);
    strncpy(g->console_name, "SNES", sizeof(g->console_name) - 1);
}

/* -------------------------------------------------------------------------
 * Subscriber spy
 * ---------------------------------------------------------------------- */

static int               s_identity_cb_count = 0;
static const identity_t *s_last_identity     = NULL;

static void on_identity_changed(const identity_t *identity) {
    s_identity_cb_count++;
    s_last_identity = identity;
}

/* -------------------------------------------------------------------------
 * setUp / tearDown
 * ---------------------------------------------------------------------- */

void setUp(void) {
    s_identity_cb_count = 0;
    s_last_identity     = NULL;

    mock_xbox_monitor_reset();
    mock_retro_monitor_reset();

    monitoring_start();
    monitoring_subscribe_active_identity(on_identity_changed);

    /* Consume the immediate callback fired by monitoring_subscribe_active_identity
     * (returns current identity, which is NULL at start). */
    s_identity_cb_count = 0;
    s_last_identity     = NULL;
}

void tearDown(void) {
    monitoring_stop();
    mock_xbox_monitor_reset();
    mock_retro_monitor_reset();
}

/* =========================================================================
 * Xbox tests
 * ====================================================================== */

/* 1. Xbox connects without a game → no identity notification yet */
static void monitoring_subscribe_active_identity__xbox_connected_without_game__identity_not_notified(void) {

    mock_xbox_monitor_set_identity(make_xbox_identity("MasterChief"));

    mock_xbox_monitor_fire_connection_changed(true, NULL);

    TEST_ASSERT_EQUAL_INT(0, s_identity_cb_count);
    TEST_ASSERT_NULL(s_last_identity);
}

/* 2. Xbox connects, then game played → identity is still Xbox */
static void monitoring_subscribe_active_identity__xbox_connected_and_game_played__xbox_identity_notified(void) {
    mock_xbox_monitor_set_identity(make_xbox_identity("MasterChief"));
    mock_xbox_monitor_fire_connection_changed(true, NULL);

    s_identity_cb_count = 0;

    game_t *game = make_xbox_game("game-1", "Halo Infinite");
    mock_xbox_monitor_fire_game_played(game);
    free_game(&game);

    TEST_ASSERT_EQUAL_INT(1, s_identity_cb_count);
    TEST_ASSERT_NOT_NULL(s_last_identity);
    TEST_ASSERT_EQUAL_STRING("MasterChief", s_last_identity->name);
}

/* 3. Game played before connection (no identity cached yet) → NULL */
static void monitoring_subscribe_active_identity__xbox_game_played_before_connect__null_notified(void) {
    /* No identity set in the stub — state_get_xbox_identity() returns NULL. */
    game_t *game = make_xbox_game("game-1", "Halo Infinite");
    mock_xbox_monitor_fire_game_played(game);
    free_game(&game);

    TEST_ASSERT_EQUAL_INT(1, s_identity_cb_count);
    TEST_ASSERT_NULL(s_last_identity);
}

/* 4. Xbox disconnects → NULL notified */
static void monitoring_subscribe_active_identity__xbox_disconnected__null_notified(void) {
    mock_xbox_monitor_set_identity(make_xbox_identity("MasterChief"));
    mock_xbox_monitor_fire_connection_changed(true, NULL);
    s_identity_cb_count = 0;

    mock_xbox_monitor_fire_connection_changed(false, NULL);

    TEST_ASSERT_EQUAL_INT(1, s_identity_cb_count);
    TEST_ASSERT_NULL(s_last_identity);
}

/* 5. Xbox disconnects while a retro game is active → retro identity takes over */
static void
monitoring_subscribe_active_identity__xbox_disconnected_with_retro_game_active__retro_identity_notified(void) {
    /* Set up retro first */
    retro_user_t retro_user;
    fill_retro_user(&retro_user, "RetroUser", "Retro User");
    mock_retro_monitor_fire_connection_changed(true, NULL);
    mock_retro_monitor_fire_user(&retro_user);

    retro_game_t retro_game;
    fill_retro_game(&retro_game, "rom-crc-01", "Super Metroid");
    mock_retro_monitor_fire_game_playing(&retro_game);

    /* Then Xbox connects and plays */
    mock_xbox_monitor_set_identity(make_xbox_identity("MasterChief"));
    mock_xbox_monitor_fire_connection_changed(true, NULL);

    game_t *xbox_game = make_xbox_game("game-1", "Halo Infinite");
    mock_xbox_monitor_fire_game_played(xbox_game);
    free_game(&xbox_game);

    s_identity_cb_count = 0;

    /* Xbox disconnects → retro should become active */
    mock_xbox_monitor_fire_connection_changed(false, NULL);

    TEST_ASSERT_EQUAL_INT(1, s_identity_cb_count);
    TEST_ASSERT_NOT_NULL(s_last_identity);
    TEST_ASSERT_EQUAL_STRING("Retro User", s_last_identity->name);
    TEST_ASSERT_EQUAL_INT(IDENTITY_SOURCE_RETRO, s_last_identity->source);
}

/* 6. Xbox game stops (game_played NULL) → active identity becomes NULL */
static void monitoring_subscribe_active_identity__xbox_no_game__null_notified(void) {
    mock_xbox_monitor_set_identity(make_xbox_identity("MasterChief"));
    mock_xbox_monitor_fire_connection_changed(true, NULL);

    game_t *game = make_xbox_game("game-1", "Halo Infinite");
    mock_xbox_monitor_fire_game_played(game);
    free_game(&game);

    s_identity_cb_count = 0;

    /* Xbox signals no game by firing game_played with NULL */
    mock_xbox_monitor_fire_game_played(NULL);

    TEST_ASSERT_EQUAL_INT(1, s_identity_cb_count);
    TEST_ASSERT_NULL(s_last_identity);
}

/* =========================================================================
 * RetroAchievements tests
 * ====================================================================== */

/* 7. Retro user + game → retro identity notified */
static void monitoring_subscribe_active_identity__retro_user_and_game__retro_identity_notified(void) {
    retro_user_t user;
    fill_retro_user(&user, "octelys", "Octelys");
    mock_retro_monitor_fire_connection_changed(true, NULL);
    mock_retro_monitor_fire_user(&user);

    retro_game_t game;
    fill_retro_game(&game, "crc-abc", "Chrono Trigger");
    mock_retro_monitor_fire_game_playing(&game);

    TEST_ASSERT_NOT_NULL(s_last_identity);
    TEST_ASSERT_EQUAL_STRING("Octelys", s_last_identity->name);
    TEST_ASSERT_EQUAL_INT(IDENTITY_SOURCE_RETRO, s_last_identity->source);
}

/* 8. Retro game arrives before user: NULL while waiting, correct identity after */
static void monitoring_subscribe_active_identity__retro_game_before_user__null_then_identity_notified(void) {
    retro_game_t game;
    fill_retro_game(&game, "crc-abc", "Chrono Trigger");
    mock_retro_monitor_fire_game_playing(&game);

    /* No user yet → NULL */
    TEST_ASSERT_EQUAL_INT(1, s_identity_cb_count);
    TEST_ASSERT_NULL(s_last_identity);

    s_identity_cb_count = 0;

    /* User arrives → identity now known */
    retro_user_t user;
    fill_retro_user(&user, "octelys", "Octelys");
    mock_retro_monitor_fire_user(&user);

    TEST_ASSERT_EQUAL_INT(1, s_identity_cb_count);
    TEST_ASSERT_NOT_NULL(s_last_identity);
    TEST_ASSERT_EQUAL_STRING("Octelys", s_last_identity->name);
}

/* 9. Retro user arrives before game: no notification until the game arrives */
static void monitoring_subscribe_active_identity__retro_user_before_game__identity_notified_on_game(void) {
    retro_user_t user;
    fill_retro_user(&user, "octelys", "Octelys");
    mock_retro_monitor_fire_connection_changed(true, NULL);
    mock_retro_monitor_fire_user(&user);

    /* User arrived but no game yet → no active-identity notification */
    TEST_ASSERT_EQUAL_INT(0, s_identity_cb_count);

    /* Game arrives → identity notified */
    retro_game_t game;
    fill_retro_game(&game, "crc-abc", "Chrono Trigger");
    mock_retro_monitor_fire_game_playing(&game);

    TEST_ASSERT_EQUAL_INT(1, s_identity_cb_count);
    TEST_ASSERT_NOT_NULL(s_last_identity);
    TEST_ASSERT_EQUAL_STRING("Octelys", s_last_identity->name);
}

/* 10. Retro no_game → active identity becomes NULL and callback fires */
static void monitoring_subscribe_active_identity__retro_no_game__null_returned(void) {
    retro_user_t user;
    fill_retro_user(&user, "octelys", "Octelys");
    mock_retro_monitor_fire_connection_changed(true, NULL);
    mock_retro_monitor_fire_user(&user);

    retro_game_t game;
    fill_retro_game(&game, "crc-abc", "Chrono Trigger");
    mock_retro_monitor_fire_game_playing(&game);

    s_identity_cb_count = 0;
    mock_retro_monitor_fire_no_game();

    TEST_ASSERT_EQUAL_INT(1, s_identity_cb_count);
    TEST_ASSERT_NULL(s_last_identity);
    TEST_ASSERT_NULL(monitoring_get_current_active_identity());
}

/* 11. Retro no_user (while game is active) → active identity becomes NULL and callback fires */
static void monitoring_subscribe_active_identity__retro_no_user__null_returned(void) {
    retro_user_t user;
    fill_retro_user(&user, "octelys", "Octelys");
    mock_retro_monitor_fire_connection_changed(true, NULL);
    mock_retro_monitor_fire_user(&user);

    retro_game_t game;
    fill_retro_game(&game, "crc-abc", "Chrono Trigger");
    mock_retro_monitor_fire_game_playing(&game);

    s_identity_cb_count = 0;
    mock_retro_monitor_fire_no_user();

    TEST_ASSERT_EQUAL_INT(1, s_identity_cb_count);
    TEST_ASSERT_NULL(s_last_identity);
    TEST_ASSERT_NULL(monitoring_get_current_active_identity());
}

/* 12. Retro game stops (no_game) after having an active identity → NULL notified */
static void monitoring_subscribe_active_identity__retro_no_game_after_active__null_notified(void) {
    retro_user_t user;
    fill_retro_user(&user, "octelys", "Octelys");
    mock_retro_monitor_fire_connection_changed(true, NULL);
    mock_retro_monitor_fire_user(&user);

    retro_game_t game;
    fill_retro_game(&game, "crc-abc", "Chrono Trigger");
    mock_retro_monitor_fire_game_playing(&game);

    /* Verify identity was active */
    TEST_ASSERT_NOT_NULL(s_last_identity);
    TEST_ASSERT_EQUAL_STRING("Octelys", s_last_identity->name);

    s_identity_cb_count = 0;
    s_last_identity     = NULL;

    mock_retro_monitor_fire_no_game();

    /* After no_game the callback must have fired with NULL */
    TEST_ASSERT_EQUAL_INT(1, s_identity_cb_count);
    TEST_ASSERT_NULL(s_last_identity);
    TEST_ASSERT_NULL(monitoring_get_current_active_identity());
}

/* 16. Retro no_user while Xbox game also active → Xbox identity takes over */
static void monitoring_subscribe_active_identity__retro_no_user_with_xbox_game_active__xbox_identity_notified(void) {
    /* Both Xbox and retro connected and playing */
    mock_xbox_monitor_set_identity(make_xbox_identity("MasterChief"));
    mock_xbox_monitor_fire_connection_changed(true, NULL);
    game_t *xbox_game = make_xbox_game("game-1", "Halo Infinite");
    mock_xbox_monitor_fire_game_played(xbox_game);
    free_game(&xbox_game);

    retro_user_t user;
    fill_retro_user(&user, "octelys", "Octelys");
    mock_retro_monitor_fire_connection_changed(true, NULL);
    mock_retro_monitor_fire_user(&user);
    retro_game_t retro_game;
    fill_retro_game(&retro_game, "crc-abc", "Chrono Trigger");
    mock_retro_monitor_fire_game_playing(&retro_game);

    /* Retro is now the last game source */
    TEST_ASSERT_NOT_NULL(s_last_identity);
    TEST_ASSERT_EQUAL_STRING("Octelys", s_last_identity->name);

    s_identity_cb_count = 0;

    /* Retro loses user → Xbox identity should become active */
    mock_retro_monitor_fire_no_user();

    TEST_ASSERT_EQUAL_INT(1, s_identity_cb_count);
    TEST_ASSERT_NOT_NULL(s_last_identity);
    TEST_ASSERT_EQUAL_STRING("MasterChief", s_last_identity->name);
    TEST_ASSERT_EQUAL_INT(IDENTITY_SOURCE_XBOX, s_last_identity->source);
}

/* =========================================================================
 * Last-game-source priority tests
 * ====================================================================== */

/* 13. Xbox game then retro game → retro identity is active */
static void monitoring_subscribe_active_identity__xbox_game_then_retro_game__retro_identity_active(void) {
    mock_xbox_monitor_set_identity(make_xbox_identity("MasterChief"));
    mock_xbox_monitor_fire_connection_changed(true, NULL);

    game_t *xbox_game = make_xbox_game("game-1", "Halo Infinite");
    mock_xbox_monitor_fire_game_played(xbox_game);
    free_game(&xbox_game);

    /* Now retro connects */
    retro_user_t user;
    fill_retro_user(&user, "octelys", "Octelys");
    mock_retro_monitor_fire_connection_changed(true, NULL);
    mock_retro_monitor_fire_user(&user);

    s_identity_cb_count = 0;

    retro_game_t retro_game;
    fill_retro_game(&retro_game, "crc-abc", "Chrono Trigger");
    mock_retro_monitor_fire_game_playing(&retro_game);

    TEST_ASSERT_EQUAL_INT(1, s_identity_cb_count);
    TEST_ASSERT_NOT_NULL(s_last_identity);
    TEST_ASSERT_EQUAL_STRING("Octelys", s_last_identity->name);
    TEST_ASSERT_EQUAL_INT(IDENTITY_SOURCE_RETRO, s_last_identity->source);
}

/* 14. Retro game then Xbox game → Xbox identity is active */
static void monitoring_subscribe_active_identity__retro_game_then_xbox_game__xbox_identity_active(void) {
    retro_user_t user;
    fill_retro_user(&user, "octelys", "Octelys");
    mock_retro_monitor_fire_connection_changed(true, NULL);
    mock_retro_monitor_fire_user(&user);

    retro_game_t retro_game;
    fill_retro_game(&retro_game, "crc-abc", "Chrono Trigger");
    mock_retro_monitor_fire_game_playing(&retro_game);

    mock_xbox_monitor_set_identity(make_xbox_identity("MasterChief"));
    mock_xbox_monitor_fire_connection_changed(true, NULL);

    s_identity_cb_count = 0;

    game_t *xbox_game = make_xbox_game("game-1", "Halo Infinite");
    mock_xbox_monitor_fire_game_played(xbox_game);
    free_game(&xbox_game);

    TEST_ASSERT_EQUAL_INT(1, s_identity_cb_count);
    TEST_ASSERT_NOT_NULL(s_last_identity);
    TEST_ASSERT_EQUAL_STRING("MasterChief", s_last_identity->name);
    TEST_ASSERT_EQUAL_INT(IDENTITY_SOURCE_XBOX, s_last_identity->source);
}

/* 15. Xbox → retro → Xbox again → Xbox identity is active */
static void monitoring_subscribe_active_identity__xbox_retro_xbox__xbox_identity_active(void) {
    mock_xbox_monitor_set_identity(make_xbox_identity("MasterChief"));
    mock_xbox_monitor_fire_connection_changed(true, NULL);
    game_t *xbox_game = make_xbox_game("game-1", "Halo Infinite");
    mock_xbox_monitor_fire_game_played(xbox_game);
    free_game(&xbox_game);

    retro_user_t user;
    fill_retro_user(&user, "octelys", "Octelys");
    mock_retro_monitor_fire_connection_changed(true, NULL);
    mock_retro_monitor_fire_user(&user);
    retro_game_t retro_game;
    fill_retro_game(&retro_game, "crc-abc", "Chrono Trigger");
    mock_retro_monitor_fire_game_playing(&retro_game);

    s_identity_cb_count = 0;

    game_t *xbox_game2 = make_xbox_game("game-2", "Forza Horizon 5");
    mock_xbox_monitor_fire_game_played(xbox_game2);
    free_game(&xbox_game2);

    TEST_ASSERT_EQUAL_INT(1, s_identity_cb_count);
    TEST_ASSERT_NOT_NULL(s_last_identity);
    TEST_ASSERT_EQUAL_STRING("MasterChief", s_last_identity->name);
    TEST_ASSERT_EQUAL_INT(IDENTITY_SOURCE_XBOX, s_last_identity->source);
}

/* -------------------------------------------------------------------------
 * Test runner
 * ---------------------------------------------------------------------- */

int main(void) {
    UNITY_BEGIN();

    /* Xbox */
    RUN_TEST(monitoring_subscribe_active_identity__xbox_connected_without_game__identity_not_notified);
    RUN_TEST(monitoring_subscribe_active_identity__xbox_connected_and_game_played__xbox_identity_notified);
    RUN_TEST(monitoring_subscribe_active_identity__xbox_game_played_before_connect__null_notified);
    RUN_TEST(monitoring_subscribe_active_identity__xbox_disconnected__null_notified);
    RUN_TEST(monitoring_subscribe_active_identity__xbox_disconnected_with_retro_game_active__retro_identity_notified);
    RUN_TEST(monitoring_subscribe_active_identity__xbox_no_game__null_notified);

    /* RetroAchievements */
    RUN_TEST(monitoring_subscribe_active_identity__retro_user_and_game__retro_identity_notified);
    RUN_TEST(monitoring_subscribe_active_identity__retro_game_before_user__null_then_identity_notified);
    RUN_TEST(monitoring_subscribe_active_identity__retro_user_before_game__identity_notified_on_game);
    RUN_TEST(monitoring_subscribe_active_identity__retro_no_game__null_returned);
    RUN_TEST(monitoring_subscribe_active_identity__retro_no_user__null_returned);
    RUN_TEST(monitoring_subscribe_active_identity__retro_no_game_after_active__null_notified);

    /* Priority */
    RUN_TEST(monitoring_subscribe_active_identity__xbox_game_then_retro_game__retro_identity_active);
    RUN_TEST(monitoring_subscribe_active_identity__retro_game_then_xbox_game__xbox_identity_active);
    RUN_TEST(monitoring_subscribe_active_identity__xbox_retro_xbox__xbox_identity_active);
    RUN_TEST(monitoring_subscribe_active_identity__retro_no_user_with_xbox_game_active__xbox_identity_notified);

    return UNITY_END();
}
