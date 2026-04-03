#include "integrations/monitoring_service.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include "integrations/xbox/xbox_monitor.h"
#include "integrations/xbox/xbox_client.h"
#include "integrations/xbox/contracts/xbox_achievement.h"
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
 * Game-played subscription list
 * ----------------------------------------------------------------------- */

typedef struct game_played_subscription {
    on_monitoring_game_played_t      callback;
    struct game_played_subscription *next;
} game_played_subscription_t;

static game_played_subscription_t *g_game_played_subscriptions = NULL;

static void notify_game_played(const game_t *game) {
    game_played_subscription_t *node = g_game_played_subscriptions;
    while (node) {
        node->callback(game);
        node = node->next;
    }
}

static void clear_game_played_subscriptions(void) {
    game_played_subscription_t *node = g_game_played_subscriptions;
    while (node) {
        game_played_subscription_t *next = node->next;
        bfree(node);
        node = next;
    }
    g_game_played_subscriptions = NULL;
}

/* --------------------------------------------------------------------------
 * Module state
 * ----------------------------------------------------------------------- */

static on_monitoring_connection_changed_t g_connection_changed_callback = NULL;

static identity_t *g_xbox_identity  = NULL;
static identity_t *g_retro_identity = NULL;

static game_t *g_xbox_game  = NULL;
static game_t *g_retro_game = NULL;

/**
 * @brief Tracks which integration produced the most recent game event.
 *
 * Updated every time on_xbox_game_played or on_retro_game_playing fires.
 * Used by get_current_active_identity() to return the identity that belongs
 * to the last active game rather than applying a fixed source priority.
 */
static identity_source_t g_last_game_source = IDENTITY_SOURCE_XBOX;

static const identity_t *get_current_active_identity(void) {
    /* When both sources have an active game, the one that reported a game
     * most recently takes priority. */
    if (g_last_game_source == IDENTITY_SOURCE_XBOX) {
        if (g_xbox_game && g_xbox_identity)
            return g_xbox_identity;
        if (g_retro_game && g_retro_identity)
            return g_retro_identity;
    } else {
        if (g_retro_game && g_retro_identity)
            return g_retro_identity;
        if (g_xbox_game && g_xbox_identity)
            return g_xbox_identity;
    }

    /* No game is active on either source. */
    return NULL;
}

/** Cached generic achievements for the current game (owned by this module). */
static achievement_t *g_current_achievements = NULL;

static on_monitoring_achievements_changed_t g_achievements_changed_callback = NULL;
static on_monitoring_session_ready_t        g_session_ready_callback        = NULL;

/**
 * @brief Replace the cached achievements list with a new one.
 *
 * Frees the old list and stores @p new_achievements. Then fires the
 * achievements-changed callback if one is registered.
 *
 * @param new_achievements New list to cache (ownership transferred to this module).
 */
static void replace_current_achievements(achievement_t *new_achievements) {
    free_achievement(&g_current_achievements);
    g_current_achievements = new_achievements;

    if (g_achievements_changed_callback)
        g_achievements_changed_callback();
}

/**
 * @brief Convert RetroAchievements records to a generic achievement_t linked list.
 */
static achievement_t *retro_to_achievements(const retro_achievement_t *retro, size_t count) {
    achievement_t *root     = NULL;
    achievement_t *previous = NULL;

    for (size_t i = 0; i < count; i++) {
        const retro_achievement_t *r = &retro[i];

        achievement_t *a = bzalloc(sizeof(achievement_t));

        /* retro_achievement_t.id is a uint32_t – convert to string */
        char id_buf[16];
        snprintf(id_buf, sizeof(id_buf), "%u", r->id);
        a->id = bstrdup(id_buf);

        a->name               = bstrdup(r->name);
        a->description        = bstrdup(r->description);
        a->icon_url           = bstrdup(r->badge_url);
        a->is_secret          = false;
        a->value              = (int)r->points;
        a->unlocked_timestamp = (strcmp(r->status, "unlocked") == 0) ? 1 : 0;
        a->source             = ACHIEVEMENT_SOURCE_RETRO;

        if (previous) {
            previous->next = a;
        } else {
            root = a;
        }
        previous = a;
    }

    return root;
}

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

                /* Identity is cached here but not notified yet — a game must
                 * start before the identity becomes active. The notification
                 * will fire from on_xbox_game_played. */
            }
        }
    } else {
        free_identity_t(&g_xbox_identity);
        free_game(&g_xbox_game);

        /* Xbox disconnected — re-evaluate the active identity. If a retro
         * game is active it will take over; otherwise NULL is notified. */
        notify_active_identity(get_current_active_identity());
    }

    if (g_connection_changed_callback)
        g_connection_changed_callback(connected, error_message);
}

static void on_xbox_achievements_progressed(const gamerscore_t                *gamerscore,
                                            const xbox_achievement_progress_t *progress) {
    UNUSED_PARAMETER(gamerscore);
    UNUSED_PARAMETER(progress);

    if (!g_xbox_identity)
        return;

    refresh_xbox_score(g_xbox_identity);
    obs_log(LOG_INFO, "[MonitoringService] Xbox score updated: %u", g_xbox_identity->score);

    /* Refresh the cached generic achievements. */
    replace_current_achievements(xbox_to_achievements(get_current_game_achievements()));

    /* Re-notify so subscribers receive the updated score. */
    if (g_xbox_game)
        notify_active_identity(get_current_active_identity());
}

static void on_xbox_game_played(const game_t *game) {
    free_game(&g_xbox_game);
    g_xbox_game = copy_game(game);

    if (g_xbox_game && (!g_xbox_game->cover_url || g_xbox_game->cover_url[0] == '\0')) {
        g_xbox_game->cover_url = xbox_get_game_cover(g_xbox_game);
    }

    obs_log(LOG_INFO, "[MonitoringService] Xbox game cached: %s", g_xbox_game ? g_xbox_game->title : "(null)");

    g_last_game_source = IDENTITY_SOURCE_XBOX;

    /* Clear cached achievements — they belong to the previous game. */
    replace_current_achievements(NULL);

    /* Notify with the current active identity. g_xbox_identity may be NULL
     * here if the game-played event arrives before the connection-changed
     * event (which caches the identity), in which case NULL is notified. */
    notify_active_identity(get_current_active_identity());

    notify_game_played(g_xbox_game);
}

/**
 * @brief Xbox monitor callback invoked when the session is fully ready.
 *
 * Converts the Xbox achievements to generic form and caches them, then
 * notifies subscribers.
 */
static void on_xbox_session_ready(void) {
    replace_current_achievements(xbox_to_achievements(get_current_game_achievements()));

    if (g_session_ready_callback)
        g_session_ready_callback();
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

    /* The user message may arrive before or after the game message. Only
     * notify if a retro game is already active and retro is the last game
     * source, otherwise the notification will come from on_retro_game_playing. */
    if (g_retro_game && g_last_game_source == IDENTITY_SOURCE_RETRO)
        notify_active_identity(get_current_active_identity());
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
    g_retro_game->cover_url    = bstrdup(retro_game->cover_url);

    obs_log(LOG_INFO, "[MonitoringService] Retro game cached: %s (%s)", g_retro_game->title, g_retro_game->console_name);

    g_last_game_source = IDENTITY_SOURCE_RETRO;

    /* Clear cached achievements — they belong to the previous game. */
    replace_current_achievements(NULL);

    /* Retro is now the last game source. get_current_active_identity() will
     * return the retro identity if it is cached, or NULL if the user message
     * has not arrived yet (it will re-notify via on_retro_user). */
    notify_active_identity(get_current_active_identity());

    notify_game_played(g_retro_game);
}

static void on_retro_no_game(void) {
    free_game(&g_retro_game);
    /* Clear achievements and notify game-played subscribers that no game is
     * active. The active-identity is not re-notified here; callers can query
     * monitoring_get_current_active_identity() to confirm it is now NULL. */
    replace_current_achievements(NULL);
    notify_game_played(NULL);
}

/**
 * @brief RetroAchievements callback invoked when the achievements' list is received.
 */
static void on_retro_achievements(const retro_achievement_t *achievements, size_t count) {
    replace_current_achievements(retro_to_achievements(achievements, count));

    if (g_session_ready_callback)
        g_session_ready_callback();
}

/* --------------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

void monitoring_start(void) {
    xbox_subscribe_connected_changed(on_xbox_connection_changed);
    xbox_subscribe_achievements_progressed(on_xbox_achievements_progressed);
    xbox_subscribe_game_played(on_xbox_game_played);
    xbox_subscribe_session_ready(on_xbox_session_ready);

    retro_achievements_subscribe_connection_changed(on_retro_connection_changed);
    retro_achievements_subscribe_user(on_retro_user);
    retro_achievements_subscribe_no_user(on_retro_no_user);
    retro_achievements_subscribe_game_playing(on_retro_game_playing);
    retro_achievements_subscribe_no_game(on_retro_no_game);
    retro_achievements_subscribe_achievements(on_retro_achievements);

    xbox_monitoring_start();
    retro_achievements_monitor_start();
}

void monitoring_stop(void) {
    retro_achievements_monitor_stop();
    xbox_monitoring_stop();

    xbox_subscribe_connected_changed(NULL);
    xbox_subscribe_achievements_progressed(NULL);
    xbox_subscribe_game_played(NULL);
    xbox_subscribe_session_ready(NULL);

    retro_achievements_subscribe_connection_changed(NULL);
    retro_achievements_subscribe_user(NULL);
    retro_achievements_subscribe_no_user(NULL);
    retro_achievements_subscribe_game_playing(NULL);
    retro_achievements_subscribe_no_game(NULL);
    retro_achievements_subscribe_achievements(NULL);

    free_identity_t(&g_xbox_identity);
    free_identity_t(&g_retro_identity);
    free_game(&g_xbox_game);
    free_game(&g_retro_game);
    free_achievement(&g_current_achievements);

    clear_active_identity_subscriptions();
    clear_game_played_subscriptions();

    g_achievements_changed_callback = NULL;
    g_session_ready_callback        = NULL;
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

    callback(get_current_active_identity());
}

void monitoring_subscribe_game_played(on_monitoring_game_played_t callback) {
    if (!callback) {
        clear_game_played_subscriptions();
        return;
    }

    game_played_subscription_t *node = bzalloc(sizeof(game_played_subscription_t));
    if (!node) {
        obs_log(LOG_ERROR, "[MonitoringService] Failed to allocate game-played subscription");
        return;
    }

    node->callback              = callback;
    node->next                  = g_game_played_subscriptions;
    g_game_played_subscriptions = node;
}

void monitoring_subscribe_achievements_changed(on_monitoring_achievements_changed_t callback) {
    g_achievements_changed_callback = callback;
}

void monitoring_subscribe_session_ready(on_monitoring_session_ready_t callback) {
    g_session_ready_callback = callback;
}

const identity_t *monitoring_get_current_active_identity(void) {
    return get_current_active_identity();
}

const achievement_t *monitoring_get_current_game_achievements(void) {
    return g_current_achievements;
}
