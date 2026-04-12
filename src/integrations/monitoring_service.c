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

/* --------------------------------------------------------------------------
 * Achievements-changed subscription list
 * ----------------------------------------------------------------------- */

typedef struct achievements_changed_subscription {
    on_monitoring_achievements_changed_t      callback;
    struct achievements_changed_subscription *next;
} achievements_changed_subscription_t;

static achievements_changed_subscription_t *g_achievements_changed_subscriptions = NULL;

static void notify_achievements_changed(void) {
    achievements_changed_subscription_t *node = g_achievements_changed_subscriptions;
    while (node) {
        node->callback();
        node = node->next;
    }
}

static void clear_achievements_changed_subscriptions(void) {
    achievements_changed_subscription_t *node = g_achievements_changed_subscriptions;
    while (node) {
        achievements_changed_subscription_t *next = node->next;
        bfree(node);
        node = next;
    }
    g_achievements_changed_subscriptions = NULL;
}

/* --------------------------------------------------------------------------
 * Session-ready subscription list
 * ----------------------------------------------------------------------- */

typedef struct session_ready_subscription {
    on_monitoring_session_ready_t      callback;
    struct session_ready_subscription *next;
} session_ready_subscription_t;

static session_ready_subscription_t *g_session_ready_subscriptions = NULL;

static void notify_session_ready(void) {
    session_ready_subscription_t *node = g_session_ready_subscriptions;
    while (node) {
        node->callback();
        node = node->next;
    }
}

static void clear_session_ready_subscriptions(void) {
    session_ready_subscription_t *node = g_session_ready_subscriptions;
    while (node) {
        session_ready_subscription_t *next = node->next;
        bfree(node);
        node = next;
    }
    g_session_ready_subscriptions = NULL;
}

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

    notify_achievements_changed();
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

        a->name              = bstrdup(r->name);
        a->description       = bstrdup(r->description);
        a->icon_url          = bstrdup(r->badge_url);
        a->measured_progress = (r->measured_progress[0] != '\0') ? bstrdup(r->measured_progress) : NULL;
        a->is_secret         = false;
        a->value             = (int)r->points;
        /* Use the real unlock timestamp when available; fall back to 1 (a
         * non-zero sentinel meaning "unlocked at unknown time") when the
         * server reports status="unlocked" but omits unlock_time. */
        if (r->unlock_time > 0) {
            a->unlocked_timestamp = (int64_t)r->unlock_time;
        } else if (strcmp(r->status, "unlocked") == 0) {
            a->unlocked_timestamp = 1;
        } else {
            a->unlocked_timestamp = 0;
        }
        a->source = ACHIEVEMENT_SOURCE_RETRO;

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

    if (!g_xbox_game) {
        /* Game stopped — clear achievements immediately since on_xbox_session_ready
         * will not fire for a NULL game. */
        replace_current_achievements(NULL);

        notify_active_identity(get_current_active_identity());
        notify_game_played(NULL);
        return;
    }

    if (!g_xbox_game->cover_url || g_xbox_game->cover_url[0] == '\0') {
        g_xbox_game->cover_url = xbox_get_game_cover(g_xbox_game);
    }

    obs_log(LOG_INFO, "[MonitoringService] Xbox game cached: %s", g_xbox_game->title);

    g_last_game_source = IDENTITY_SOURCE_XBOX;

    /* Do NOT clear g_current_achievements here. xbox_session_change_game() has
     * already cleared the session's internal achievement list before firing this
     * callback. on_xbox_session_ready() — which may have already been called on
     * the prefetch thread before this callback runs — is the sole owner of
     * g_current_achievements for Xbox. Clearing here would wipe achievements
     * that on_xbox_session_ready() already set (race when all icons are cached). */

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

    notify_session_ready();
}

/* --------------------------------------------------------------------------
 * RetroAchievements callbacks
 * ----------------------------------------------------------------------- */

static void on_retro_connection_changed(bool connected, const char *error_message) {
    if (!connected) {
        /* Treat a disconnect the same as no_game + no_user so that all
         * subscribers (game cover, achievement sources, identity sources)
         * are cleared immediately instead of staying stale. */
        free_identity_t(&g_retro_identity);

        if (g_retro_game) {
            free_game(&g_retro_game);

            /* Fire game_played(NULL) BEFORE clearing achievements so that the
             * achievement cycle sets g_session_ready = false first, preventing
             * reset_display_cycle() from running with an empty list. */
            notify_game_played(NULL);
            notify_active_identity(get_current_active_identity());
            replace_current_achievements(NULL);
        } else {
            /* No game was active, but still re-evaluate the active identity
             * in case the retro identity was the one being shown. */
            notify_active_identity(get_current_active_identity());
        }
    }

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

    /* If retro was the active source, losing the identity means the active
     * identity is now NULL (or falls back to Xbox if an Xbox game is active). */
    if (g_last_game_source == IDENTITY_SOURCE_RETRO)
        notify_active_identity(get_current_active_identity());
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

    /* Notify game_played BEFORE clearing achievements so that the achievement
     * cycle can mark the session as not-ready before on_achievements_changed
     * fires. If we cleared achievements first while g_session_ready was still
     * true, reset_display_cycle() would run with an empty list and notify all
     * subscribers with NULL, causing the name/description sources to blank out
     * permanently until the new session becomes ready. */
    notify_active_identity(get_current_active_identity());
    notify_game_played(g_retro_game);

    /* Clear cached achievements — they belong to the previous game.
     * Done after notify_game_played so achievement_cycle has already set
     * g_session_ready = false, making the resulting on_achievements_changed
     * call a no-op inside reset_display_cycle(). */
    replace_current_achievements(NULL);
}

static void on_retro_no_game(void) {
    free_game(&g_retro_game);

    /* Notify game_played BEFORE clearing achievements so that the achievement
     * cycle sets g_session_ready = false first.  Otherwise replace_current_achievements(NULL)
     * would fire on_achievements_changed → reset_display_cycle() while
     * g_session_ready is still true, blanking out the sources. */
    notify_game_played(NULL);
    notify_active_identity(get_current_active_identity());

    /* Clear achievements now that the cycle is no longer active. */
    replace_current_achievements(NULL);
}

/**
 * @brief RetroAchievements callback invoked when the achievements' list is received.
 *
 * Only fires notify_session_ready() when a game is actually active.
 * RetroArch may send an achievements message with count=0 after a no_game
 * event (initial state dump on connection). Treating that as session-ready
 * would set g_session_ready = true with no game context, which could then
 * block the next real game's session from starting correctly.
 */
static void on_retro_achievements(const retro_achievement_t *achievements, size_t count) {
    replace_current_achievements(retro_to_achievements(achievements, count));

    if (g_retro_game) {
        notify_session_ready();
    }
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
    clear_achievements_changed_subscriptions();
    clear_session_ready_subscriptions();
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
    if (!callback) {
        clear_achievements_changed_subscriptions();
        return;
    }

    achievements_changed_subscription_t *node = bzalloc(sizeof(achievements_changed_subscription_t));
    if (!node) {
        obs_log(LOG_ERROR, "[MonitoringService] Failed to allocate achievements-changed subscription");
        return;
    }

    node->callback                       = callback;
    node->next                           = g_achievements_changed_subscriptions;
    g_achievements_changed_subscriptions = node;
}

void monitoring_subscribe_session_ready(on_monitoring_session_ready_t callback) {
    if (!callback) {
        clear_session_ready_subscriptions();
        return;
    }

    session_ready_subscription_t *node = bzalloc(sizeof(session_ready_subscription_t));
    if (!node) {
        obs_log(LOG_ERROR, "[MonitoringService] Failed to allocate session-ready subscription");
        return;
    }

    node->callback                = callback;
    node->next                    = g_session_ready_subscriptions;
    g_session_ready_subscriptions = node;
}

const identity_t *monitoring_get_current_active_identity(void) {
    return get_current_active_identity();
}

const achievement_t *monitoring_get_current_game_achievements(void) {
    return g_current_achievements;
}
