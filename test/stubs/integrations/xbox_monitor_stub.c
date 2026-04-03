/**
 * @file xbox_monitor_stub.c
 * @brief Stub implementations for xbox_monitor.h, xbox_client.h, and the
 *        state_get_xbox_identity() / xbox_fetch_gamerpic() calls made by
 *        monitoring_service.c.
 *
 * Registers/stores the callbacks that monitoring_service.c installs via
 * xbox_subscribe_*() and lets tests fire them on demand via the mock_* helpers.
 */

#include "test/stubs/integrations/xbox_monitor_stub.h"

#include "integrations/xbox/xbox_monitor.h"
#include "integrations/xbox/xbox_client.h"
#include "integrations/xbox/entities/xbox_identity.h"
#include "io/state.h"
#include "common/game.h"
#include "common/gamerscore.h"

#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Registered callbacks (set by monitoring_service via xbox_subscribe_*)
 * ---------------------------------------------------------------------- */

static on_xbox_connection_changed_t      s_cb_connection_changed      = NULL;
static on_xbox_game_played_t             s_cb_game_played             = NULL;
static on_xbox_achievements_progressed_t s_cb_achievements_progressed = NULL;
static on_xbox_session_ready_t           s_cb_session_ready           = NULL;

/* Identity returned by state_get_xbox_identity() */
static xbox_identity_t *s_xbox_identity = NULL;

/* -------------------------------------------------------------------------
 * xbox_subscribe_* — called by monitoring_service during monitoring_start()
 * ---------------------------------------------------------------------- */

void xbox_subscribe_connected_changed(on_xbox_connection_changed_t callback) {
    s_cb_connection_changed = callback;
}

void xbox_subscribe_game_played(on_xbox_game_played_t callback) {
    s_cb_game_played = callback;
}

void xbox_subscribe_achievements_progressed(on_xbox_achievements_progressed_t callback) {
    s_cb_achievements_progressed = callback;
}

void xbox_subscribe_session_ready(on_xbox_session_ready_t callback) {
    s_cb_session_ready = callback;
}

/* -------------------------------------------------------------------------
 * Lifecycle stubs — no-ops in tests
 * ---------------------------------------------------------------------- */

bool xbox_monitoring_start(void) {
    return true;
}
void xbox_monitoring_stop(void) {}
bool xbox_monitoring_is_active(void) {
    return false;
}

/* -------------------------------------------------------------------------
 * xbox_monitor.h data getters — return NULL / 0 in tests
 * ---------------------------------------------------------------------- */

const gamerscore_t *get_current_gamerscore(void) {
    return NULL;
}
const game_t *get_current_game(void) {
    return NULL;
}
const xbox_achievement_t *get_current_game_achievements(void) {
    return NULL;
}

/* -------------------------------------------------------------------------
 * xbox_client.h stubs
 * ---------------------------------------------------------------------- */

bool xbox_fetch_gamerscore(int64_t *out_gamerscore) {
    if (out_gamerscore)
        *out_gamerscore = 0;
    return false;
}

game_t *xbox_get_current_game(void) {
    return NULL;
}

xbox_achievement_t *xbox_get_game_achievements(const game_t *game) {
    (void)game;
    return NULL;
}

char *xbox_get_game_cover(const game_t *game) {
    (void)game;
    return NULL;
}

char *xbox_fetch_gamerpic(void) {
    return NULL;
}

/* -------------------------------------------------------------------------
 * state_get_xbox_identity() — returns the identity set by the test
 * ---------------------------------------------------------------------- */

xbox_identity_t *state_get_xbox_identity(void) {
    if (!s_xbox_identity)
        return NULL;

    /* Return a fresh copy each time — monitoring_service.c calls free_identity()
     * on the pointer it receives, so we must not return the original. */
    return copy_xbox_identity(s_xbox_identity);
}

/* -------------------------------------------------------------------------
 * Mock control helpers
 * ---------------------------------------------------------------------- */

void mock_xbox_monitor_set_identity(xbox_identity_t *identity) {
    /* Free the previously stored identity before replacing it. */
    free_identity(&s_xbox_identity);
    s_xbox_identity = identity; /* Takes ownership. */
}

void mock_xbox_monitor_fire_connection_changed(bool connected, const char *error_message) {
    if (s_cb_connection_changed)
        s_cb_connection_changed(connected, error_message);
}

void mock_xbox_monitor_fire_game_played(const game_t *game) {
    if (s_cb_game_played)
        s_cb_game_played(game);
}

void mock_xbox_monitor_reset(void) {
    free_identity(&s_xbox_identity);
    s_cb_connection_changed      = NULL;
    s_cb_game_played             = NULL;
    s_cb_achievements_progressed = NULL;
    s_cb_session_ready           = NULL;
}
