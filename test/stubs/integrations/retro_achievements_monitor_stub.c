/**
 * @file retro_achievements_monitor_stub.c
 * @brief Stub implementations for retro_achievements_monitor.h.
 *
 * Stores the callbacks that monitoring_service.c installs via
 * retro_achievements_subscribe_*() and exposes mock_retro_monitor_fire_*()
 * helpers so tests can drive those callbacks directly.
 */

#include "test/stubs/integrations/retro_achievements_monitor_stub.h"
#include "integrations/retro-achievements/retro_achievements_monitor.h"

/* -------------------------------------------------------------------------
 * Registered callbacks
 * ---------------------------------------------------------------------- */

static on_retro_connection_changed_t s_cb_connection_changed = NULL;
static on_retro_user_t               s_cb_user               = NULL;
static on_retro_no_user_t            s_cb_no_user            = NULL;
static on_retro_game_playing_t       s_cb_game_playing       = NULL;
static on_retro_no_game_t            s_cb_no_game            = NULL;
static on_retro_achievements_t       s_cb_achievements       = NULL;

/* -------------------------------------------------------------------------
 * retro_achievements_subscribe_* — called by monitoring_service
 * ---------------------------------------------------------------------- */

void retro_achievements_subscribe_connection_changed(on_retro_connection_changed_t callback) {
    s_cb_connection_changed = callback;
}

void retro_achievements_subscribe_user(on_retro_user_t callback) {
    s_cb_user = callback;
}

void retro_achievements_subscribe_no_user(on_retro_no_user_t callback) {
    s_cb_no_user = callback;
}

void retro_achievements_subscribe_game_playing(on_retro_game_playing_t callback) {
    s_cb_game_playing = callback;
}

void retro_achievements_subscribe_no_game(on_retro_no_game_t callback) {
    s_cb_no_game = callback;
}

void retro_achievements_subscribe_achievements(on_retro_achievements_t callback) {
    s_cb_achievements = callback;
}

/* -------------------------------------------------------------------------
 * Lifecycle stubs — no-ops in tests
 * ---------------------------------------------------------------------- */

bool retro_achievements_monitor_start(void) {
    return true;
}
void retro_achievements_monitor_stop(void) {}
bool retro_achievements_monitor_is_active(void) {
    return false;
}

/* -------------------------------------------------------------------------
 * Mock control helpers
 * ---------------------------------------------------------------------- */

void mock_retro_monitor_fire_connection_changed(bool connected, const char *error_message) {
    if (s_cb_connection_changed)
        s_cb_connection_changed(connected, error_message);
}

void mock_retro_monitor_fire_user(const retro_user_t *user) {
    if (s_cb_user)
        s_cb_user(user);
}

void mock_retro_monitor_fire_no_user(void) {
    if (s_cb_no_user)
        s_cb_no_user();
}

void mock_retro_monitor_fire_game_playing(const retro_game_t *game) {
    if (s_cb_game_playing)
        s_cb_game_playing(game);
}

void mock_retro_monitor_fire_no_game(void) {
    if (s_cb_no_game)
        s_cb_no_game();
}

void mock_retro_monitor_reset(void) {
    s_cb_connection_changed = NULL;
    s_cb_user               = NULL;
    s_cb_no_user            = NULL;
    s_cb_game_playing       = NULL;
    s_cb_no_game            = NULL;
    s_cb_achievements       = NULL;
}
