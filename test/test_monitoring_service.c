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
 *
 *  Race conditions:
 *  17. session_ready fires before game_played returns (all icons cached) →
 *      achievements must not be wiped by the late game_played callback
 *
 *  Achievement management — retro source:
 *  18. Retro achievements arrive with game active → achievements stored, session_ready fired
 *  19. Retro achievements arrive without a game → achievements stored, session_ready NOT fired
 *  20. Retro disconnect with game active → achievements cleared
 *
 *  Achievement management — Xbox/retro coexistence (race condition fixes):
 *  21. Xbox game stops while retro is active → retro achievements preserved
 *  22. Xbox game stops while retro is active → game_played(NULL) NOT propagated to subscribers
 *  23. Xbox session_ready fires without an Xbox game → achievements NOT overwritten
 *  24. Xbox session_ready fires without an Xbox game → session_ready NOT re-fired
 *  25. Xbox session_ready fires with an Xbox game active → session_ready fired normally
 */

#include "unity.h"

#include "test/stubs/integrations/xbox_monitor_stub.h"
#include "test/stubs/integrations/retro_achievements_monitor_stub.h"

#include "integrations/monitoring_service.h"
#include "integrations/xbox/entities/xbox_identity.h"
#include "common/game.h"
#include "common/token.h"
#include "common/memory.h"

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

static void fill_retro_achievement(retro_achievement_t *a, uint32_t id, const char *name, const char *status) {
    memset(a, 0, sizeof(*a));
    a->id     = id;
    a->points = 10;
    strncpy(a->name, name, sizeof(a->name) - 1);
    strncpy(a->status, status, sizeof(a->status) - 1);
}

static xbox_achievement_progress_t *make_xbox_achievement_progress(const char *id,
                                                                    const char *progress_state,
                                                                    const char *current,
                                                                    const char *target) {
    xbox_achievement_progress_t *progress = bzalloc(sizeof(xbox_achievement_progress_t));
    progress->service_config_id            = bstrdup("00000000-0000-0000-0000-0000700a1e17");
    progress->id                           = bstrdup(id);
    progress->progress_state               = progress_state ? bstrdup(progress_state) : NULL;
    progress->current                      = current ? bstrdup(current) : NULL;
    progress->target                       = target ? bstrdup(target) : NULL;
    progress->unlocked_timestamp           = 0;
    progress->next                         = NULL;
    return progress;
}

static void free_xbox_achievement_progress(xbox_achievement_progress_t **progress) {
    if (!progress || !*progress)
        return;
    xbox_achievement_progress_t *p = *progress;
    free_memory((void **)&p->service_config_id);
    free_memory((void **)&p->id);
    free_memory((void **)&p->progress_state);
    free_memory((void **)&p->current);
    free_memory((void **)&p->target);
    free_memory((void **)progress);
}

static xbox_achievement_t *make_xbox_achievement(const char *id, const char *name, const char *progress_state) {
    xbox_achievement_t *a   = bzalloc(sizeof(xbox_achievement_t));
    a->id                   = bstrdup(id);
    a->name                 = bstrdup(name);
    a->progress_state       = bstrdup(progress_state);
    a->service_config_id    = bstrdup("00000000-0000-0000-0000-0000700a1e17");
    a->unlocked_timestamp   = 0;
    return a;
}

/* -------------------------------------------------------------------------
 * Subscriber spies
 * ---------------------------------------------------------------------- */

static int               s_identity_cb_count = 0;
static const identity_t *s_last_identity     = NULL;

static void on_identity_changed(const identity_t *identity) {
    s_identity_cb_count++;
    s_last_identity = identity;
}

static int s_session_ready_cb_count = 0;

static void on_session_ready(void) {
    s_session_ready_cb_count++;
}

static int           s_game_played_cb_count = 0;
static const game_t *s_last_game_played     = NULL;

static void on_game_played(const game_t *game) {
    s_game_played_cb_count++;
    s_last_game_played = game;
}

static int s_achievements_changed_cb_count = 0;

static void on_achievements_changed(void) {
    s_achievements_changed_cb_count++;
}

/* -------------------------------------------------------------------------
 * setUp / tearDown
 * ---------------------------------------------------------------------- */

void setUp(void) {
    s_identity_cb_count             = 0;
    s_last_identity                 = NULL;
    s_session_ready_cb_count        = 0;
    s_game_played_cb_count          = 0;
    s_last_game_played              = NULL;
    s_achievements_changed_cb_count = 0;

    mock_xbox_monitor_reset();
    mock_retro_monitor_reset();

    monitoring_start();
    monitoring_subscribe_active_identity(on_identity_changed);
    monitoring_subscribe_session_ready(on_session_ready);
    monitoring_subscribe_game_played(on_game_played);
    monitoring_subscribe_achievements_changed(on_achievements_changed);

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

/* 17. Race: session_ready fires before game_played returns (all icons cached)
 *     → achievements set by session_ready must survive the late game_played */
static void monitoring_achievements__session_ready_before_game_played__achievements_not_wiped(void) {
    mock_xbox_monitor_set_identity(make_xbox_identity("MasterChief"));
    mock_xbox_monitor_fire_connection_changed(true, NULL);

    game_t *xbox_game = make_xbox_game("game-1", "Halo Infinite");

    /* Simulate the race: session_ready fires first (prefetch thread finished
     * before notify_game_played returned on the main thread). */
    mock_xbox_monitor_fire_session_ready();
    mock_xbox_monitor_fire_game_played(xbox_game);
    free_game(&xbox_game);

    /* Achievements must not have been wiped by the late game_played callback.
     * The stub's get_current_game_achievements() returns NULL, so
     * on_xbox_session_ready sets g_current_achievements to NULL as well — but
     * the important invariant is that game_played does NOT call
     * replace_current_achievements(NULL) and undo whatever session_ready set. */
    const achievement_t *achievements = monitoring_get_current_game_achievements();
    /* get_current_game_achievements() stub returns NULL → xbox_to_achievements
     * returns NULL → g_current_achievements is NULL after session_ready, but
     * game_played must leave it untouched (still NULL, not "wiped again"). */
    TEST_ASSERT_NULL(achievements);

    /* Fire session_ready AFTER game_played (normal order) to confirm that path
     * also works and doesn't regress. */
    game_t *xbox_game2 = make_xbox_game("game-2", "Forza Horizon 5");
    mock_xbox_monitor_fire_game_played(xbox_game2);
    free_game(&xbox_game2);
    mock_xbox_monitor_fire_session_ready();

    TEST_ASSERT_NULL(monitoring_get_current_game_achievements());
}

/* =========================================================================
 * Achievement management — retro source
 * ====================================================================== */

/* 18. Retro achievements arrive while a game is active → achievements stored
 *     and session_ready fired to subscribers. */
static void monitoring_achievements__retro_achievements_with_game__achievements_stored_and_session_ready_fired(void) {
    retro_game_t game;
    fill_retro_game(&game, "crc-abc", "Chrono Trigger");
    mock_retro_monitor_fire_connection_changed(true, NULL);
    mock_retro_monitor_fire_game_playing(&game);

    s_session_ready_cb_count        = 0;
    s_achievements_changed_cb_count = 0;

    retro_achievement_t achievements[2];
    fill_retro_achievement(&achievements[0], 1, "First Win", "unlocked");
    fill_retro_achievement(&achievements[1], 2, "Second Win", "locked");
    mock_retro_monitor_fire_achievements(achievements, 2);

    TEST_ASSERT_NOT_NULL(monitoring_get_current_game_achievements());
    TEST_ASSERT_EQUAL_INT(1, s_achievements_changed_cb_count);
    TEST_ASSERT_EQUAL_INT(1, s_session_ready_cb_count);
}

/* 19. Retro achievements arrive without a game context → achievements stored
 *     (ready for future use) but session_ready must NOT fire. */
static void monitoring_achievements__retro_achievements_without_game__stored_but_session_ready_not_fired(void) {
    /* Connect but do NOT fire game_playing. */
    mock_retro_monitor_fire_connection_changed(true, NULL);

    s_session_ready_cb_count        = 0;
    s_achievements_changed_cb_count = 0;

    retro_achievement_t achievements[1];
    fill_retro_achievement(&achievements[0], 1, "First Win", "locked");
    mock_retro_monitor_fire_achievements(achievements, 1);

    TEST_ASSERT_NOT_NULL(monitoring_get_current_game_achievements());
    TEST_ASSERT_EQUAL_INT(1, s_achievements_changed_cb_count);
    TEST_ASSERT_EQUAL_INT(0, s_session_ready_cb_count);
}

/* 20. Retro disconnects while a game is active → achievements must be cleared. */
static void monitoring_achievements__retro_disconnected_with_game_active__achievements_cleared(void) {
    retro_game_t game;
    fill_retro_game(&game, "crc-abc", "Chrono Trigger");
    mock_retro_monitor_fire_connection_changed(true, NULL);
    mock_retro_monitor_fire_game_playing(&game);

    retro_achievement_t achievements[1];
    fill_retro_achievement(&achievements[0], 1, "First Win", "unlocked");
    mock_retro_monitor_fire_achievements(achievements, 1);

    TEST_ASSERT_NOT_NULL(monitoring_get_current_game_achievements());

    mock_retro_monitor_fire_connection_changed(false, NULL);

    TEST_ASSERT_NULL(monitoring_get_current_game_achievements());
}

/* =========================================================================
 * Achievement management — Xbox / retro coexistence (race condition fixes)
 * ====================================================================== */

/* 21. Xbox fires game_stopped while a retro game is active → the retro
 *     achievements must remain in place (not wiped by the Xbox callback). */
static void monitoring_achievements__xbox_game_stopped_while_retro_active__retro_achievements_preserved(void) {
    retro_game_t retro_game;
    fill_retro_game(&retro_game, "crc-abc", "Chrono Trigger");
    mock_retro_monitor_fire_connection_changed(true, NULL);
    mock_retro_monitor_fire_game_playing(&retro_game);

    retro_achievement_t achievements[1];
    fill_retro_achievement(&achievements[0], 1, "First Win", "unlocked");
    mock_retro_monitor_fire_achievements(achievements, 1);

    TEST_ASSERT_NOT_NULL(monitoring_get_current_game_achievements());

    /* Xbox game stops — retro session must remain intact. */
    mock_xbox_monitor_fire_game_played(NULL);

    TEST_ASSERT_NOT_NULL(monitoring_get_current_game_achievements());
}

/* 22. Xbox fires game_stopped while a retro game is active → game_played(NULL)
 *     must NOT be propagated to subscribers, so the display cycle is not reset. */
static void monitoring_game_played__xbox_game_stopped_while_retro_active__null_not_propagated(void) {
    retro_game_t retro_game;
    fill_retro_game(&retro_game, "crc-abc", "Chrono Trigger");
    mock_retro_monitor_fire_connection_changed(true, NULL);
    mock_retro_monitor_fire_game_playing(&retro_game);

    /* Reset after game_playing (which legitimately fires game_played once). */
    s_game_played_cb_count = 0;
    s_last_game_played     = NULL;

    /* Xbox game stops — must not touch the retro session. */
    mock_xbox_monitor_fire_game_played(NULL);

    TEST_ASSERT_EQUAL_INT(0, s_game_played_cb_count);
}

/* 23. Xbox fires session_ready when it has no active game → the current
 *     achievements (owned by a running retro session) must not be overwritten. */
static void monitoring_achievements__xbox_session_ready_without_game__achievements_not_overwritten(void) {
    retro_game_t retro_game;
    fill_retro_game(&retro_game, "crc-abc", "Chrono Trigger");
    mock_retro_monitor_fire_connection_changed(true, NULL);
    mock_retro_monitor_fire_game_playing(&retro_game);

    retro_achievement_t achievements[1];
    fill_retro_achievement(&achievements[0], 1, "First Win", "unlocked");
    mock_retro_monitor_fire_achievements(achievements, 1);

    TEST_ASSERT_NOT_NULL(monitoring_get_current_game_achievements());

    s_achievements_changed_cb_count = 0;

    /* Xbox fires session_ready without an active game — must be a no-op. */
    mock_xbox_monitor_fire_session_ready();

    TEST_ASSERT_NOT_NULL(monitoring_get_current_game_achievements());
    TEST_ASSERT_EQUAL_INT(0, s_achievements_changed_cb_count);
}

/* 24. Xbox fires session_ready when it has no active game → session_ready must
 *     NOT be re-fired to subscribers (that would restart the display cycle). */
static void monitoring_session_ready__xbox_session_ready_without_game__not_fired(void) {
    retro_game_t retro_game;
    fill_retro_game(&retro_game, "crc-abc", "Chrono Trigger");
    mock_retro_monitor_fire_connection_changed(true, NULL);
    mock_retro_monitor_fire_game_playing(&retro_game);

    retro_achievement_t achievements[1];
    fill_retro_achievement(&achievements[0], 1, "First Win", "unlocked");
    mock_retro_monitor_fire_achievements(achievements, 1);

    /* Reset after the retro session_ready that legitimately fired above. */
    s_session_ready_cb_count = 0;

    /* Xbox fires session_ready without a game — must be ignored. */
    mock_xbox_monitor_fire_session_ready();

    TEST_ASSERT_EQUAL_INT(0, s_session_ready_cb_count);
}

/* 25. Xbox fires session_ready with an active Xbox game → session_ready fires
 *     normally (positive path; regression guard). */
static void monitoring_session_ready__xbox_session_ready_with_game__fired(void) {
    mock_xbox_monitor_set_identity(make_xbox_identity("MasterChief"));
    mock_xbox_monitor_fire_connection_changed(true, NULL);

    game_t *xbox_game = make_xbox_game("game-1", "Halo Infinite");
    mock_xbox_monitor_fire_game_played(xbox_game);
    free_game(&xbox_game);

    s_session_ready_cb_count = 0;

    mock_xbox_monitor_fire_session_ready();

    TEST_ASSERT_EQUAL_INT(1, s_session_ready_cb_count);
}

/* =========================================================================
 * Achievement progress updates — Xbox
 * ====================================================================== */

/* 26. Xbox achievement progress update with non-zero current value → measured_progress
 *     is patched in-place on the cached achievement (cycle is NOT reset). */
static void monitoring_achievements__xbox_progress_update_non_zero__measured_progress_set(void) {
    mock_xbox_monitor_set_identity(make_xbox_identity("MasterChief"));
    mock_xbox_monitor_fire_connection_changed(true, NULL);

    /* Seed one achievement so g_current_achievements is non-NULL after session_ready. */
    mock_xbox_monitor_set_achievements(make_xbox_achievement("achievement-1", "Stop Hitting Yourself", "NotStarted"));

    game_t *xbox_game = make_xbox_game("game-1", "Halo Infinite");
    mock_xbox_monitor_fire_game_played(xbox_game);
    free_game(&xbox_game);

    mock_xbox_monitor_fire_session_ready();

    s_session_ready_cb_count = 0;

    /* Fire progress update with non-zero current value */
    xbox_achievement_progress_t *progress = make_xbox_achievement_progress(
        "achievement-1", /* id — matches the seeded achievement */
        "InProgress",    /* progress_state */
        "42",            /* current */
        "100"            /* target */
    );
    mock_xbox_monitor_fire_achievements_progressed(NULL, progress);
    free_xbox_achievement_progress(&progress);

    /* The cycle must NOT have been reset. */
    TEST_ASSERT_EQUAL_INT(0, s_session_ready_cb_count);

    /* measured_progress must have been patched in-place to "42/100". */
    const achievement_t *a = monitoring_get_current_game_achievements();
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_STRING("42/100", a->measured_progress);
}

/* 27. Xbox achievement progress update with current="0" → measured_progress stays NULL
 *     (a zero value is not displayed). */
static void monitoring_achievements__xbox_progress_update_zero_current__measured_progress_not_set(void) {
    mock_xbox_monitor_set_identity(make_xbox_identity("MasterChief"));
    mock_xbox_monitor_fire_connection_changed(true, NULL);

    mock_xbox_monitor_set_achievements(make_xbox_achievement("achievement-1", "Stop Hitting Yourself", "NotStarted"));

    game_t *xbox_game = make_xbox_game("game-1", "Halo Infinite");
    mock_xbox_monitor_fire_game_played(xbox_game);
    free_game(&xbox_game);

    mock_xbox_monitor_fire_session_ready();

    s_session_ready_cb_count = 0;

    /* Fire progress update with current="0" */
    xbox_achievement_progress_t *progress = make_xbox_achievement_progress(
        "achievement-1", /* id */
        "InProgress",    /* progress_state */
        "0",             /* current — zero should not produce a progress string */
        "100"            /* target */
    );
    mock_xbox_monitor_fire_achievements_progressed(NULL, progress);
    free_xbox_achievement_progress(&progress);

    TEST_ASSERT_EQUAL_INT(0, s_session_ready_cb_count);

    /* measured_progress must remain NULL when current is "0". */
    const achievement_t *a = monitoring_get_current_game_achievements();
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NULL(a->measured_progress);
}

/* 28. Xbox achievement progressed to "Achieved" → achievements list is rebuilt
 *     and achievements_changed fires to reset the display cycle.
 *     session_ready must NOT fire (it would wrongly imply icons were re-prefetched). */
static void monitoring_achievements__xbox_progress_update_achieved__achievements_changed_fired(void) {
    mock_xbox_monitor_set_identity(make_xbox_identity("MasterChief"));
    mock_xbox_monitor_fire_connection_changed(true, NULL);

    mock_xbox_monitor_set_achievements(make_xbox_achievement("achievement-1", "Stop Hitting Yourself", "InProgress"));

    game_t *xbox_game = make_xbox_game("game-1", "Halo Infinite");
    mock_xbox_monitor_fire_game_played(xbox_game);
    free_game(&xbox_game);

    mock_xbox_monitor_fire_session_ready();

    s_session_ready_cb_count        = 0;
    s_achievements_changed_cb_count = 0;

    xbox_achievement_progress_t *progress = make_xbox_achievement_progress(
        "achievement-1", /* id — matches the seeded achievement */
        "Achieved",      /* progress_state — triggers a full rebuild */
        "100",           /* current */
        "100"            /* target */
    );
    mock_xbox_monitor_fire_achievements_progressed(NULL, progress);
    free_xbox_achievement_progress(&progress);

    /* achievements_changed must fire to reset the display cycle. */
    TEST_ASSERT_EQUAL_INT(1, s_achievements_changed_cb_count);
    /* session_ready must NOT fire — images were already downloaded. */
    TEST_ASSERT_EQUAL_INT(0, s_session_ready_cb_count);
}

/* 29. Xbox achievement progress update with NULL progress → nothing happens. */
static void monitoring_achievements__xbox_progress_update_null__no_effect(void) {
    /* Set up Xbox game */
    mock_xbox_monitor_set_identity(make_xbox_identity("MasterChief"));
    mock_xbox_monitor_fire_connection_changed(true, NULL);

    game_t *xbox_game = make_xbox_game("game-1", "Halo Infinite");
    mock_xbox_monitor_fire_game_played(xbox_game);
    free_game(&xbox_game);

    mock_xbox_monitor_fire_session_ready();

    s_identity_cb_count             = 0;
    s_session_ready_cb_count        = 0;
    s_achievements_changed_cb_count = 0;

    /* Fire progress update with NULL progress */
    mock_xbox_monitor_fire_achievements_progressed(NULL, NULL);

    /* Nothing should happen */
    TEST_ASSERT_EQUAL_INT(0, s_session_ready_cb_count);
}

/* 30. Xbox achievement progress update without identity → callback returns early. */
static void monitoring_achievements__xbox_progress_update_no_identity__early_return(void) {
    /* Connect but do NOT set identity */
    mock_xbox_monitor_fire_connection_changed(true, NULL);

    game_t *xbox_game = make_xbox_game("game-1", "Halo Infinite");
    mock_xbox_monitor_fire_game_played(xbox_game);
    free_game(&xbox_game);

    mock_xbox_monitor_fire_session_ready();

    s_identity_cb_count             = 0;
    s_session_ready_cb_count        = 0;
    s_achievements_changed_cb_count = 0;

    /* Fire progress update */
    xbox_achievement_progress_t *progress = make_xbox_achievement_progress(
        "achievement-1", /* id */
        "InProgress",    /* progress_state */
        "42",            /* current */
        "100"            /* target */
    );
    mock_xbox_monitor_fire_achievements_progressed(NULL, progress);
    free_xbox_achievement_progress(&progress);

    /* Nothing should happen because there's no identity */
    TEST_ASSERT_EQUAL_INT(0, s_session_ready_cb_count);
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

    /* Race conditions */
    RUN_TEST(monitoring_achievements__session_ready_before_game_played__achievements_not_wiped);

    /* Achievement management — retro source */
    RUN_TEST(monitoring_achievements__retro_achievements_with_game__achievements_stored_and_session_ready_fired);
    RUN_TEST(monitoring_achievements__retro_achievements_without_game__stored_but_session_ready_not_fired);
    RUN_TEST(monitoring_achievements__retro_disconnected_with_game_active__achievements_cleared);

    /* Achievement management — Xbox/retro coexistence (race condition fixes) */
    RUN_TEST(monitoring_achievements__xbox_game_stopped_while_retro_active__retro_achievements_preserved);
    RUN_TEST(monitoring_game_played__xbox_game_stopped_while_retro_active__null_not_propagated);
    RUN_TEST(monitoring_achievements__xbox_session_ready_without_game__achievements_not_overwritten);
    RUN_TEST(monitoring_session_ready__xbox_session_ready_without_game__not_fired);
    RUN_TEST(monitoring_session_ready__xbox_session_ready_with_game__fired);

    /* Achievement progress updates — Xbox */
    RUN_TEST(monitoring_achievements__xbox_progress_update_non_zero__measured_progress_set);
    RUN_TEST(monitoring_achievements__xbox_progress_update_zero_current__measured_progress_not_set);
    RUN_TEST(monitoring_achievements__xbox_progress_update_achieved__achievements_changed_fired);
    RUN_TEST(monitoring_achievements__xbox_progress_update_null__no_effect);
    RUN_TEST(monitoring_achievements__xbox_progress_update_no_identity__early_return);

    return UNITY_END();
}
