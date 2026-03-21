#include "integrations/monitoring_service.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include "integrations/xbox/xbox_monitor.h"
#include "integrations/xbox/xbox_client.h"
#include "integrations/retro-achievements/retro_achievements_monitor.h"
#include "common/identity.h"
#include "common/game.h"
#include "common/gamerscore.h"
#include "common/memory.h"
#include "io/state.h"

/* --------------------------------------------------------------------------
 * Active-identity subscription list
 * ----------------------------------------------------------------------- */

typedef struct active_identity_subscription {
    on_monitoring_active_identity_changed_t callback;
    struct active_identity_subscription    *next;
} active_identity_subscription_t;

static active_identity_subscription_t *g_active_identity_subscriptions = NULL;

static void notify_active_identity(const identity_t *identity) {
    active_identity_subscription_t *node = g_active_identity_subscriptions;
    while (node) {
        node->callback(identity);
        node = node->next;
    }
}

static void clear_active_identity_subscriptions(void) {
    active_identity_subscription_t *node = g_active_identity_subscriptions;
    while (node) {
        active_identity_subscription_t *next = node->next;
        bfree(node);
        node = next;
    }
    g_active_identity_subscriptions = NULL;
}

/* --------------------------------------------------------------------------
 * Module state
 * ----------------------------------------------------------------------- */

static on_monitoring_connection_changed_t g_connection_changed_callback = NULL;

static identity_t *g_xbox_identity  = NULL;
static identity_t *g_retro_identity = NULL;

static game_t *g_xbox_game  = NULL;
static game_t *g_retro_game = NULL;

/* --------------------------------------------------------------------------
 * Xbox callbacks
 * ----------------------------------------------------------------------- */

/**
 * @brief Compute the current Xbox gamerscore and store it into @p identity.
 *
 * Centralises the call to get_current_gamerscore() + gamerscore_compute() so
 * the same logic is shared between the connection-changed and
 * achievements-progressed paths.
 */
static void refresh_xbox_score(identity_t *identity) {
    if (!identity)
        return;

    const gamerscore_t *gs = get_current_gamerscore();
    identity->score        = gs ? (uint32_t)gamerscore_compute(gs) : 0;
}

static void on_xbox_connection_changed(bool connected, const char *error_message) {
    if (connected) {
        free_identity_t(&g_xbox_identity);

        xbox_identity_t *xbox = state_get_xbox_identity();
        if (xbox) {
            g_xbox_identity = identity_from_xbox(xbox, get_current_gamerscore());
            free_identity(&xbox);

            if (g_xbox_identity) {
                free_memory((void **)&g_xbox_identity->avatar_url);
                g_xbox_identity->avatar_url = xbox_fetch_gamerpic();
                obs_log(LOG_INFO,
                        "[MonitoringService] Xbox identity cached: %s (score: %u, avatar: %s)",
                        g_xbox_identity->name,
                        g_xbox_identity->score,
                        g_xbox_identity->avatar_url ? g_xbox_identity->avatar_url : "(none)");
            }
        }
    } else {
        free_identity_t(&g_xbox_identity);
    }

    if (g_connection_changed_callback)
        g_connection_changed_callback(connected, error_message);
}

static void on_xbox_achievements_progressed(const gamerscore_t *gamerscore, const achievement_progress_t *progress) {
    UNUSED_PARAMETER(gamerscore);
    UNUSED_PARAMETER(progress);

    if (!g_xbox_identity)
        return;

    refresh_xbox_score(g_xbox_identity);
    obs_log(LOG_INFO, "[MonitoringService] Xbox score updated: %u", g_xbox_identity->score);

    if (g_xbox_game)
        notify_active_identity(g_xbox_identity);
}

static void on_xbox_game_played(const game_t *game) {
    free_game(&g_xbox_game);
    g_xbox_game = copy_game(game);
    obs_log(LOG_INFO, "[MonitoringService] Xbox game cached: %s", g_xbox_game ? g_xbox_game->title : "(null)");

    notify_active_identity(g_xbox_identity);
}

/* --------------------------------------------------------------------------
 * RetroAchievements callbacks
 * ----------------------------------------------------------------------- */

static void on_retro_connection_changed(bool connected, const char *error_message) {
    if (!connected)
        free_identity_t(&g_retro_identity);

    if (g_connection_changed_callback)
        g_connection_changed_callback(connected, error_message);
}

static void on_retro_user(const retro_user_t *user) {
    free_identity_t(&g_retro_identity);
    g_retro_identity = identity_from_retro(user);
    obs_log(LOG_INFO,
            "[MonitoringService] Retro identity cached: %s (avatar: %s)",
            g_retro_identity ? g_retro_identity->name : "(null)",
            g_retro_identity ? g_retro_identity->avatar_url : "(none)");

    /* Fire if a game is already active so sources update regardless of
     * whether the user message arrives before or after the game message. */
    if (g_retro_game)
        notify_active_identity(g_retro_identity);
}

static void on_retro_no_user(void) {
    free_identity_t(&g_retro_identity);
}

static void on_retro_game_playing(const retro_game_t *retro_game) {
    free_game(&g_retro_game);

    g_retro_game               = bzalloc(sizeof(game_t));
    g_retro_game->id           = bstrdup(retro_game->game_id);
    g_retro_game->title        = bstrdup(retro_game->game_name);
    g_retro_game->console_name = bstrdup(retro_game->console_name);

    obs_log(LOG_INFO, "[MonitoringService] Retro game cached: %s (%s)", g_retro_game->title, g_retro_game->console_name);

    notify_active_identity(g_retro_identity);
}

static void on_retro_no_game(void) {
    free_game(&g_retro_game);
}

/* --------------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

void monitoring_start(void) {
    xbox_subscribe_connected_changed(on_xbox_connection_changed);
    xbox_subscribe_achievements_progressed(on_xbox_achievements_progressed);
    xbox_subscribe_game_played(on_xbox_game_played);

    retro_achievements_subscribe_connection_changed(on_retro_connection_changed);
    retro_achievements_subscribe_user(on_retro_user);
    retro_achievements_subscribe_no_user(on_retro_no_user);
    retro_achievements_subscribe_game_playing(on_retro_game_playing);
    retro_achievements_subscribe_no_game(on_retro_no_game);

    xbox_monitoring_start();
    retro_achievements_monitor_start();
}

void monitoring_stop(void) {
    retro_achievements_monitor_stop();
    xbox_monitoring_stop();

    xbox_subscribe_connected_changed(NULL);
    xbox_subscribe_achievements_progressed(NULL);
    xbox_subscribe_game_played(NULL);

    retro_achievements_subscribe_connection_changed(NULL);
    retro_achievements_subscribe_user(NULL);
    retro_achievements_subscribe_no_user(NULL);
    retro_achievements_subscribe_game_playing(NULL);
    retro_achievements_subscribe_no_game(NULL);

    free_identity_t(&g_xbox_identity);
    free_identity_t(&g_retro_identity);
    free_game(&g_xbox_game);
    free_game(&g_retro_game);

    clear_active_identity_subscriptions();
}

void monitoring_subscribe_connection_changed(on_monitoring_connection_changed_t callback) {
    g_connection_changed_callback = callback;
}

void monitoring_subscribe_active_identity(on_monitoring_active_identity_changed_t callback) {
    if (!callback) {
        clear_active_identity_subscriptions();
        return;
    }

    active_identity_subscription_t *node = bzalloc(sizeof(active_identity_subscription_t));
    if (!node) {
        obs_log(LOG_ERROR, "[MonitoringService] Failed to allocate active identity subscription");
        return;
    }

    node->callback                  = callback;
    node->next                      = g_active_identity_subscriptions;
    g_active_identity_subscriptions = node;
}
