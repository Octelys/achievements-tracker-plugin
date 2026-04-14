#include "xbox_monitor.h"

/**
 * @file xbox_monitor.c
 * @brief Xbox Live RTA (Real-Time Activity) monitor implementation.
 *
 * When built with libwebsockets (HAVE_LIBWEBSOCKETS), this module:
 *  - Connects to the Xbox Live RTA WebSocket endpoint.
 *  - Adds the XBL3.0 Authorization header during the handshake.
 *  - Subscribes to presence and achievement progression channels.
 *  - Parses incoming RTA messages and emits higher-level events.
 *
 * Threading:
 *  - The monitor runs a background pthread that calls lws_service().
 *  - Incoming messages are parsed and subscriber callbacks are invoked from that
 *    thread.
 *
 * Ownership/lifetime:
 *  - Callback parameters (game/progress/gamerscore) generally point to objects
 *    owned by the monitor/session and are valid until the next update.
 *  - If a subscriber needs to keep data, it must make a deep copy.
 *
 * Build variants:
 *  - If HAVE_LIBWEBSOCKETS is not defined, stub implementations are provided
 *    that report monitoring is unavailable.
 */

#include <obs-module.h>
#include <diagnostics/log.h>

#ifdef HAVE_LIBWEBSOCKETS

#include "xbox_client.h"
#include "xbox_session.h"

#include <libwebsockets.h>
#include <util/thread_compat.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "external/cjson/cJSON.h"

#include "io/state.h"
#include "integrations/xbox/oauth/xbox-live.h"

#include <text/parsers.h>

#define RTA_HOST "rta.xboxlive.com"
#define RTA_PATH "/connect"
#define RTA_PORT 443
#define PRESENCE_SUBSCRIPTION_MESSAGE "[%d,1,\"https://userpresence.xboxlive.com/users/xuid(%s)/richpresence\"]"

#define PROTOCOL "rta.xboxlive.com.V2"

#define SUBSCRIBE 1
#define UNSUBSCRIBE 1
#define LOOP_CHECK_MS 50
#define INITIAL_RETRY_DELAY_MS 1000
#define MAX_RETRY_DELAY_MS 60000

/**
 * @brief Subscription node for game-played events.
 */
typedef struct game_played_subscription {
    on_xbox_game_played_t            callback;
    struct game_played_subscription *next;
} game_played_subscription_t;

static game_played_subscription_t *g_game_played_subscriptions = NULL;

/**
 * @brief Subscription node for achievement progress events.
 */
typedef struct achievements_updated_subscription {
    on_xbox_achievements_progressed_t         callback;
    struct achievements_updated_subscription *next;
} achievements_updated_subscription_t;

static achievements_updated_subscription_t *g_achievements_updated_subscriptions = NULL;

/**
 * @brief Subscription node for connection status change events.
 */
typedef struct connection_changed_subscription {
    on_xbox_connection_changed_t            callback;
    struct connection_changed_subscription *next;
} connection_changed_subscription_t;

static connection_changed_subscription_t *g_connection_changed_subscriptions = NULL;

/**
 * @brief Subscription node for session-ready events.
 */
typedef struct session_ready_subscription {
    on_xbox_session_ready_t            callback;
    struct session_ready_subscription *next;
} session_ready_subscription_t;

static session_ready_subscription_t *g_session_ready_subscriptions = NULL;

/**
 * @brief Monitor thread state.
 *
 * This struct is allocated when monitoring starts and owned by the module-level
 * @c g_monitoring_context pointer.
 *
 * Ownership/lifetime:
 * - @c identity is an xbox_identity_t instance returned by state/xbox_live helpers.
 *   It is owned by the state subsystem; do not free it here.
 * - @c auth_token is a heap-allocated "XBL3.0 x=<uhs>;<token>" header value.
 *   It is owned by this context and must be freed when replaced or on stop.
 */
typedef struct monitoring_context {
    /** libwebsockets context (event loop) */
    struct lws_context *context;

    /** Websocket instance */
    struct lws *wsi;

    /** Background thread running lws_service() */
    pthread_t thread;

    /** True while monitoring is active */
    bool running;

    /** True once websocket has reached the "established" state */
    bool connected;

    bool last_status_notified;

    /**
     * Current identity used.
     *
     * Used to determine whether the token is expired and to refresh it when needed.
     */
    xbox_identity_t *identity;

    /**
     * Authorization token used during the handshake ("XBL3.0 x=uhs;token").
     *
     * Note: this is the full header value (without the "Authorization:" prefix).
     */
    char *auth_token;

    /** Receive buffer used to accumulate websocket fragments */
    char  *rx_buffer;
    size_t rx_buffer_size;
    size_t rx_buffer_used;
} monitoring_context_t;

static monitoring_context_t *g_monitoring_context = NULL;

/* Keeps track of the game, achievements and gamerscore */
static xbox_session_t g_current_session;

static const lws_retry_bo_t retry_policy = {
    .secs_since_valid_ping   = 10, /* send ping after 10s idle */
    .secs_since_valid_hangup = 20, /* hang up if no response after 20s */
};

/**
 * @brief Build the WebSocket "Authorization" header value for a given Xbox identity.
 *
 * The returned string is of the form:
 * `XBL3.0 x=<uhs>;<token>`
 *
 * Ownership:
 * - The returned string is heap-allocated and must be freed by the caller (bfree).
 *
 * @param identity Identity containing @c uhs and a valid token.
 * @return Newly allocated header value, or NULL on error.
 */
static char *build_authorization_header(const xbox_identity_t *identity) {

    if (!identity) {
        return NULL;
    }

    char auth_header[4096];
    snprintf(auth_header, sizeof(auth_header), "XBL3.0 x=%s;%s", identity->uhs, identity->token->value);

    return bstrdup(auth_header);
}

static void refresh_token_if_needed() {
    if (g_monitoring_context->identity && !token_is_expired(g_monitoring_context->identity->token)) {
        return;
    }

    obs_log(LOG_INFO, "[XboxMonitor] Token expired, refreshing");

    free_memory((void **)&g_monitoring_context->auth_token);
    free_identity(&g_monitoring_context->identity);

    g_monitoring_context->identity = xbox_live_get_identity();

    if (!g_monitoring_context->identity) {
        obs_log(LOG_ERROR, "[XboxMonitor] Failed to refresh the token");
        return;
    }

    /* Replace the cached auth header for future handshakes */
    g_monitoring_context->auth_token = build_authorization_header(g_monitoring_context->identity);

    obs_log(LOG_INFO, "[XboxMonitor] Token refreshed");
}

/**
 * @brief Invoke all registered game-played subscribers.
 */
static void notify_game_played(const game_t *game) {

    if (!game) {
        obs_log(LOG_INFO, "[XboxMonitor] Game stopped");
    } else {
        obs_log(LOG_INFO, "[XboxMonitor] Game played: %s (%s)", game->title, game->id);
    }

    game_played_subscription_t *subscriptions = g_game_played_subscriptions;

    while (subscriptions) {
        subscriptions->callback(game);
        subscriptions = subscriptions->next;
    }
}

/**
 * @brief Invoke all registered achievement progressed subscribers.
 */
static void notify_achievements_progressed(const xbox_achievement_progress_t *achievements_progress) {

    obs_log(LOG_INFO,
            "[XboxMonitor] Achievement progress received for service config %s",
            achievements_progress->service_config_id);

    achievements_updated_subscription_t *subscription = g_achievements_updated_subscriptions;

    while (subscription) {
        subscription->callback(g_current_session.gamerscore, achievements_progress);
        subscription = subscription->next;
    }
}

/**
 * @brief Invoke all registered connection state subscribers.
 */
static void notify_connection_changed(const char *error_message) {

    if (g_monitoring_context->last_status_notified == g_monitoring_context->connected) {
        return;
    }

    obs_log(LOG_INFO,
            "[XboxMonitor] Connection changed: %s",
            g_monitoring_context->connected ? "connected" : "disconnected");

    if (error_message) {
        obs_log(LOG_WARNING, "[XboxMonitor] Connection error: %s", error_message);
    }

    connection_changed_subscription_t *node = g_connection_changed_subscriptions;

    while (node) {
        node->callback(g_monitoring_context->connected, error_message);
        node = node->next;
    }

    g_monitoring_context->last_status_notified = g_monitoring_context->connected;
}

/**
 * @brief Invoke all registered session-ready subscribers.
 *
 * Called when the game achievements are loaded and ready for cycling.
 */
static void notify_session_ready(void) {

    obs_log(LOG_INFO, "[XboxMonitor] Session ready");

    session_ready_subscription_t *node = g_session_ready_subscriptions;

    while (node) {
        node->callback();
        node = node->next;
    }
}

/**
 * @brief Send a JSON-ish RTA control message over the websocket.
 *
 * @param message Message to send.
 * @return true if successfully written; false otherwise.
 */
static bool send_websocket_message(const char *message) {

    unsigned char *buf       = NULL;
    bool           succeeded = false;

    if (!g_monitoring_context || !g_monitoring_context->wsi || !g_monitoring_context->connected) {
        obs_log(LOG_ERROR, "[XboxMonitor] Cannot send message - not connected");
        goto cleanup;
    }

    size_t len = strlen(message);

    /* Allocate buffer with LWS_PRE padding */
    buf = bmalloc(LWS_PRE + len);

    if (!buf) {
        obs_log(LOG_ERROR, "[XboxMonitor] Failed to allocate send buffer");
        goto cleanup;
    }

    memcpy(buf + LWS_PRE, message, len);

    int written = lws_write(g_monitoring_context->wsi, buf + LWS_PRE, len, LWS_WRITE_TEXT);

    if (written < (int)len) {
        obs_log(LOG_ERROR, "[XboxMonitor] Failed to send message (wrote %d of %zu bytes)", written, len);
        goto cleanup;
    }

    obs_log(LOG_DEBUG, "[XboxMonitor] Sent message: %s", message);
    succeeded = true;

cleanup:
    free_memory((void **)&buf);

    return succeeded;
}

/**
 * @brief Subscribe to rich presence changes for the current user.
 */
static bool xbox_presence_subscribe() {

    bool result = false;

    xbox_identity_t *identity = state_get_xbox_identity();

    if (!identity) {
        obs_log(LOG_ERROR, "[XboxMonitor] Invalid Xbox identity for subscription");
        goto cleanup;
    }

    if (!g_monitoring_context || !g_monitoring_context->connected) {
        obs_log(LOG_WARNING, "[XboxMonitor] Cannot subscribe to presence - not connected");
        goto cleanup;
    }

    char message[512];
    snprintf(message, sizeof(message), PRESENCE_SUBSCRIPTION_MESSAGE, SUBSCRIBE, identity->xid);

    obs_log(LOG_DEBUG, "[XboxMonitor] Subscribing to presence for XUID %s", identity->xid);
    result = send_websocket_message(message);

cleanup:
    free_identity(&identity);

    return result;
}

/**
 * @brief Unsubscribe from a previously subscribed RTA channel.
 *
 * @param subscription_id Subscription ID/path to unsubscribe from.
 */
static bool xbox_presence_unsubscribe(const char *subscription_id) {
    if (!subscription_id || !*subscription_id) {
        obs_log(LOG_WARNING, "[XboxMonitor] Cannot unsubscribe from presence - no subscription ID");
        return false;
    }

    if (!g_monitoring_context || !g_monitoring_context->connected) {
        obs_log(LOG_WARNING, "[XboxMonitor] Cannot unsubscribe from presence - not connected");
        return false;
    }

    char message[256];
    snprintf(message, sizeof(message), "[%d,1,\"%s\"]", UNSUBSCRIBE, subscription_id);

    obs_log(LOG_DEBUG, "[XboxMonitor] Unsubscribing from presence channel %s", subscription_id);
    return send_websocket_message(message);
}

/**
 * @brief Subscribe to achievement progression updates for the current session's game.
 */
static bool xbox_achievements_progress_subscribe(const xbox_session_t *session) {

    if (!session) {
        obs_log(LOG_ERROR, "[XboxMonitor] No session specified");
        return false;
    }

    if (!session->game) {
        obs_log(LOG_DEBUG, "[XboxMonitor] No game being played, skipping achievement subscription");
        return false;
    }

    const xbox_achievement_t *achievements = session->achievements;

    if (!achievements) {
        obs_log(LOG_WARNING, "[XboxMonitor] No achievements available for current game, skipping subscription");
        return false;
    }

    const char *service_config_id = achievements->service_config_id;

    if (!g_monitoring_context || !g_monitoring_context->connected) {
        obs_log(LOG_WARNING, "[XboxMonitor] Cannot subscribe to achievements - not connected");
        return false;
    }

    char message[512];
    snprintf(message,
             sizeof(message),
             "[%d,1,\"https://achievements.xboxlive.com/users/xuid(%s)/achievements/%s\"]",
             SUBSCRIBE,
             g_monitoring_context->identity->xid,
             service_config_id);

    obs_log(LOG_DEBUG, "[XboxMonitor] Subscribing to achievement updates for service config %s", service_config_id);

    return send_websocket_message(message);
}

/**
 * @brief Unsubscribe from achievement progression updates for the current session's game.
 */
static bool xbox_achievements_progress_unsubscribe(const xbox_session_t *session) {

    if (!session) {
        obs_log(LOG_ERROR, "[XboxMonitor] No session specified");
        return false;
    }

    if (!session->game) {
        obs_log(LOG_DEBUG, "[XboxMonitor] No game being played, skipping achievement unsubscription");
        return false;
    }

    const xbox_achievement_t *achievements = session->achievements;

    if (!achievements) {
        obs_log(LOG_WARNING, "[XboxMonitor] No achievements available for current game, skipping unsubscription");
        return false;
    }

    if (!g_monitoring_context || !g_monitoring_context->connected) {
        obs_log(LOG_WARNING, "[XboxMonitor] Cannot unsubscribe from achievements - not connected");
        return false;
    }

    char message[512];
    snprintf(message,
             sizeof(message),
             "[%d,1,\"https://achievements.xboxlive.com/users/xuid(%s)/achievements/%s\"]",
             UNSUBSCRIBE,
             g_monitoring_context->identity->xid,
             achievements->service_config_id);

    obs_log(LOG_DEBUG,
            "[XboxMonitor] Unsubscribing from achievement updates for service config %s",
            achievements->service_config_id);

    return send_websocket_message(message);
}

/**
 * @brief Update the current game (including sessions and subscriptions) and notify listeners.
 *
 * This:
 *  - unsubscribes from previous achievements
 *  - refreshes the session (fetch achievements for a new game)
 *  - subscribes to achievement updates for the new game (if any)
 *  - notifies listeners
 */
static void xbox_change_game(game_t *game) {

    /* First, let's make sure we unsubscribe from the previous achievements */
    xbox_achievements_progress_unsubscribe(&g_current_session);

    /*
     * Notify subscribers that a new game is being loaded BEFORE starting the
     * session change.  This ensures achievement_cycle sets g_session_ready=false
     * before the prefetch thread can complete and fire the session-ready event.
     * If notify_game_played were called after xbox_session_change_game, the
     * prefetch thread could finish synchronously (all icons already cached) and
     * fire notify_session_ready before notify_game_played runs, causing
     * achievement_cycle to reset g_session_ready back to false and clear the
     * display just after it had been populated.
     */
    notify_game_played(game);

    /* Change the game: fetch achievements, notify session-ready, and prefetch
     * icons in the background. */
    xbox_session_change_game(&g_current_session, game, &notify_session_ready);

    /* Now let's subscribe to the new achievements */
    xbox_achievements_progress_subscribe(&g_current_session);
}

/**
 * @brief Handle a parsed game update message.
 */
static void on_game_update_received(game_t *game) {

    xbox_change_game(game);
}

/**
 * @brief Handle a parsed achievement progress message.
 */
static void on_achievement_progress_received(const xbox_achievement_progress_t *progress) {

    if (!progress) {
        /* No change */
        return;
    }

    /* TODO Progress is not necessarily achieved */

    xbox_session_unlock_achievement(&g_current_session, progress);

    notify_achievements_progressed(progress);
}

/**
 * @brief Called when the websocket transitions to a connected state.
 *
 * Fetches the initial gamerscore, sets up subscriptions, and notifies listeners.
 */
static void on_websocket_connected() {

    g_monitoring_context->connected = true;

    int64_t gamerscore_value;
    xbox_fetch_gamerscore(&gamerscore_value);

    if (!g_current_session.gamerscore) {
        g_current_session.gamerscore             = bzalloc(sizeof(gamerscore_t));
        g_current_session.gamerscore->base_value = (int)gamerscore_value;
    }

    xbox_presence_subscribe();

    /* Notify subscribers that we are connected BEFORE fetching the current
     * game.  This ensures that monitoring_service.c has g_xbox_identity set
     * (via on_xbox_connection_changed) when the subsequent game-played and
     * session-ready notifications arrive, so the gamertag source never
     * briefly shows "Not connected". */
    notify_connection_changed(NULL);

    /* Immediately retrieves the game being played, if any */
    game_t *current_game = xbox_get_current_game();
    xbox_change_game(current_game);
    free_game(&current_game);

    /* And retrieves the achievements, if any */
    if (g_current_session.game != NULL) {
        xbox_achievements_progress_subscribe(&g_current_session);
    }
}

/**
 * @brief Called when the websocket disconnects.
 */
static void on_websocket_disconnected() {

    g_monitoring_context->connected = false;
    g_monitoring_context->wsi       = NULL;

    xbox_session_clear(&g_current_session);

    notify_connection_changed(NULL);

    notify_game_played(NULL);
}

/**
 * @brief Called when the websocket throws an error.
 */
static void on_websocket_error(const char *in) {

    g_monitoring_context->connected = false;
    g_monitoring_context->wsi       = NULL;

    notify_connection_changed(in ? (char *)in : "Connection error");
}

/**
 * @brief Process a single, complete websocket message buffer.
 *
 * Xbox RTA messages are arrays; this function extracts index 2 and interprets it as
 * a JSON message. Known message types are dispatched to the relevant parsers.
 */
static void on_buffer_received(const char *buffer) {

    cJSON  *presence_item = NULL;
    game_t *game          = NULL;
    char   *game_id       = NULL;
    char   *message       = NULL;
    cJSON  *root          = NULL;

    if (!buffer) {
        return;
    }

    obs_log(LOG_DEBUG, "[XboxMonitor] Message received (%zu bytes)", strlen(buffer));

    /* Parse the buffer [X,X,X] */
    root = cJSON_Parse(buffer);

    if (!root) {
        return;
    }

    /* Retrieves the presence message at index 2 */
    presence_item = cJSON_GetArrayItem(root, 2);

    if (!presence_item) {
        obs_log(LOG_DEBUG, "[XboxMonitor] No payload at index 2, skipping");
        goto cleanup;
    }

    message = cJSON_PrintUnformatted(presence_item);

    if (strlen(message) < 5) {
        obs_log(LOG_DEBUG, "[XboxMonitor] Message payload too short, skipping");
        goto cleanup;
    }

    if (is_presence_message(message)) {

        obs_log(LOG_DEBUG, "[XboxMonitor] Message is a presence message");

        /* Parse the rich presence information however, we only want the game ID since
         * the presence game does not provide the game title; just a rich presence text */
        game_id = parse_presence_game_id(message);

        if (g_current_session.game != NULL && game_id != NULL && strcasecmp(game_id, g_current_session.game->id) == 0) {
            obs_log(LOG_DEBUG, "[XboxMonitor] Game ID has not changed: %s", game_id);
            goto cleanup;
        }

        game = xbox_get_current_game();
        on_game_update_received(game);
        goto cleanup;
    }

    if (is_achievement_message(message)) {
        obs_log(LOG_DEBUG, "[XboxMonitor] Message is an achievement message");
        xbox_achievement_progress_t *progress = parse_achievement_progress(message);
        on_achievement_progress_received(progress);
        xbox_free_achievement_progress(&progress);
    }

cleanup:
    if (message) {
        free(message);
    }
    free_memory((void **)&game_id);
    free_game(&game);
    free_json_memory((void **)&root);
}

/**
 * @brief libwebsockets callback for websocket events.
 */
static int websocket_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {

    UNUSED_PARAMETER(user);

    monitoring_context_t *ctx = lws_context_user(lws_get_context(wsi));

    if (!ctx) {
        return 0;
    }

    switch (reason) {
    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
        /* Add Authorization header during WebSocket handshake */
        if (ctx->auth_token) {
            unsigned char **p   = (unsigned char **)in;
            unsigned char  *end = (*p) + len;

            /* Add Authorization header */
            if (lws_add_http_header_by_name(wsi,
                                            (unsigned char *)"Authorization:",
                                            (unsigned char *)ctx->auth_token,
                                            (int)strlen(ctx->auth_token),
                                            p,
                                            end)) {
                obs_log(LOG_ERROR, "[XboxMonitor] Failed to add Authorization header");
                return -1;
            }

            obs_log(LOG_DEBUG, "[XboxMonitor] Added Authorization header to handshake");
        }
        break;

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        obs_log(LOG_INFO, "[XboxMonitor] WebSocket connection established");
        on_websocket_connected();
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE: {
        /* Ensure the buffer has enough space */
        size_t needed = ctx->rx_buffer_used + len + 1;
        if (needed > ctx->rx_buffer_size) {
            size_t new_size   = needed * 2;
            char  *new_buffer = (char *)brealloc(ctx->rx_buffer, new_size);
            if (!new_buffer) {
                obs_log(LOG_ERROR, "[XboxMonitor] Failed to allocate receive buffer");
                return -1;
            }
            ctx->rx_buffer      = new_buffer;
            ctx->rx_buffer_size = new_size;
        }

        /* Append received data */
        memcpy(ctx->rx_buffer + ctx->rx_buffer_used, in, len);
        ctx->rx_buffer_used += len;

        /* Check if this is the final fragment */
        if (lws_is_final_fragment(wsi)) {
            ctx->rx_buffer[ctx->rx_buffer_used] = '\0';

            obs_log(LOG_DEBUG, "[XboxMonitor] Dispatching message: %s", ctx->rx_buffer);

            on_buffer_received(ctx->rx_buffer);

            /* Reset buffer for the next message */
            ctx->rx_buffer_used = 0;
        }
        break;
    }

    case LWS_CALLBACK_CLIENT_RECEIVE_PONG:
        /*
         * Periodic pong received.
         *
         * We use this as a lightweight place to refresh credentials if needed.
         * Note: libwebsockets does not automatically update handshake headers
         * for an already-established connection. Refreshing @c ctx->auth_token
         * here prepares the next connection attempt (reconnect) to use fresh
         * credentials.
         */
        refresh_token_if_needed();
        break;
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        obs_log(LOG_ERROR, "[XboxMonitor] Connection error: %s", in ? (char *)in : "unknown");
        on_websocket_error(in ? (char *)in : "Connection error");
        break;

    case LWS_CALLBACK_CLIENT_CLOSED:
        obs_log(LOG_INFO, "[XboxMonitor] WebSocket connection closed");
        on_websocket_disconnected();
        break;

    case LWS_CALLBACK_WSI_DESTROY:
        ctx->wsi = NULL;
        break;

    default:
        break;
    }

    return 0;
}

/**
 * @brief libwebsockets protocol table.
 *
 * `libwebsockets` requires a NULL-terminated array of protocols. We register a
 * single protocol that forwards events to websocket_callback().
 */
static const struct lws_protocols protocols[] = {
    {"xbox-rta", websocket_callback, 0, 4096, 0, NULL, 0},
    {NULL, NULL, 0, 0, 0, NULL, 0},
};

/**
 * @brief Background thread entry point.
 *
 * Creates the libwebsockets context, connects to the RTA endpoint, performs an
 * initial fetch of the current game, then runs the websocket event loop until
 * stopped.
 */
static void *monitoring_thread(void *arg) {

    monitoring_context_t *ctx = arg;

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.port      = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.user      = ctx;
    info.options   = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    ctx->context = lws_create_context(&info);

    if (!ctx->context) {
        obs_log(LOG_ERROR, "[XboxMonitor] Failed to create WebSocket context");
        g_monitoring_context->connected = false;
        notify_connection_changed("Failed to create WebSocket context");
        return (void *)1;
    }

    struct lws_client_connect_info ccinfo;
    memset(&ccinfo, 0, sizeof(ccinfo));

    ccinfo.context               = ctx->context;
    ccinfo.address               = RTA_HOST;
    ccinfo.port                  = RTA_PORT;
    ccinfo.path                  = RTA_PATH;
    ccinfo.host                  = ccinfo.address;
    ccinfo.origin                = ccinfo.address;
    ccinfo.protocol              = PROTOCOL;
    ccinfo.retry_and_idle_policy = &retry_policy;
    ccinfo.ssl_connection        = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;

    obs_log(LOG_DEBUG, "[XboxMonitor] Connecting to wss://%s:%d%s", RTA_HOST, RTA_PORT, RTA_PATH);

    ctx->wsi = lws_client_connect_via_info(&ccinfo);

    if (!ctx->wsi) {
        obs_log(LOG_ERROR, "[XboxMonitor] Failed to connect");
        g_monitoring_context->connected = false;
        notify_connection_changed("Failed to connect");
        lws_context_destroy(ctx->context);
        ctx->context = NULL;
        return (void *)1;
    }

    /* Start at 1 second */
    int retry_delay_ms = INITIAL_RETRY_DELAY_MS;

    /* Service the WebSocket connection */
    while (ctx->running && ctx->context) {
        lws_service(ctx->context, LOOP_CHECK_MS);

        /* Reconnect if the connection was lost */
        if (ctx->running && !ctx->wsi && ctx->context) {
            obs_log(LOG_INFO, "[XboxMonitor] Connection lost, retrying in %d ms...", retry_delay_ms);

            /* Sleep for retry_delay_ms while keeping lws alive (50ms increments) */
            int iterations = retry_delay_ms / LOOP_CHECK_MS;
            for (int i = 0; i < iterations && ctx->running; i++) {
                sleep_ms(LOOP_CHECK_MS);
            }

            obs_log(LOG_INFO, "[XboxMonitor] Reconnecting...");

            ctx->wsi = lws_client_connect_via_info(&ccinfo);

            if (!ctx->wsi) {
                obs_log(LOG_WARNING,
                        "[XboxMonitor] Reconnect attempt failed, next retry in %d ms",
                        retry_delay_ms * 2 > MAX_RETRY_DELAY_MS ? MAX_RETRY_DELAY_MS : retry_delay_ms * 2);

                /* Double the delay, capped at max */
                retry_delay_ms = retry_delay_ms * 2;
                if (retry_delay_ms > MAX_RETRY_DELAY_MS) {
                    retry_delay_ms = MAX_RETRY_DELAY_MS;
                }
            } else {
                obs_log(LOG_INFO, "[XboxMonitor] Connection reestablished");
                /* Reset delay on successful connection attempt */
                retry_delay_ms = INITIAL_RETRY_DELAY_MS;
            }
        }
    }

    if (ctx->context) {
        lws_context_destroy(ctx->context);
        ctx->context = NULL;
    }

    obs_log(LOG_INFO, "[XboxMonitor] Monitor thread stopped");

    return 0;
}

//  --------------------------------------------------------------------------------------------------------------------
//  Public API
//  --------------------------------------------------------------------------------------------------------------------

bool xbox_monitoring_start() {

    bool succeeded = false;

    if (g_monitoring_context) {
        obs_log(LOG_WARNING, "[XboxMonitor] Monitor is already running");
        goto done;
    }

    xbox_identity_t *identity = xbox_live_get_identity();

    if (!identity) {
        obs_log(LOG_ERROR, "[XboxMonitor] Cannot start monitor: no Xbox identity available");
        goto error;
    }

    g_monitoring_context = (monitoring_context_t *)bzalloc(sizeof(monitoring_context_t));

    if (!g_monitoring_context) {
        obs_log(LOG_ERROR, "[XboxMonitor] Failed to allocate context");
        goto error;
    }

    char *authorization_header = build_authorization_header(identity);

    g_monitoring_context->identity   = identity;
    g_monitoring_context->running    = true;
    g_monitoring_context->connected  = false;
    g_monitoring_context->auth_token = authorization_header;

    /* Allocate initial receive buffer */
    g_monitoring_context->rx_buffer_size = 4096;
    g_monitoring_context->rx_buffer      = (char *)bmalloc(g_monitoring_context->rx_buffer_size);
    g_monitoring_context->rx_buffer_used = 0;

    if (!g_monitoring_context->rx_buffer) {
        obs_log(LOG_ERROR, "[XboxMonitor] Failed to allocate receive buffer");
        goto error;
    }

    if (pthread_create(&g_monitoring_context->thread, NULL, monitoring_thread, g_monitoring_context) != 0) {
        obs_log(LOG_ERROR, "[XboxMonitor] Failed to create monitor thread");
        goto error;
    }

    obs_log(LOG_INFO, "[XboxMonitor] Monitor started");
    succeeded = true;
    goto done;

error:
    free_identity(&identity);
    free_identity(&g_monitoring_context->identity);
    free_memory((void **)&g_monitoring_context->rx_buffer);
    free_memory((void **)&g_monitoring_context->auth_token);
    free_memory((void **)&g_monitoring_context);

done:
    return succeeded;
}

void xbox_monitoring_stop(void) {

    if (!g_monitoring_context) {
        return;
    }

    obs_log(LOG_INFO, "[XboxMonitor] Stopping monitor");

    g_monitoring_context->running = false;

    if (g_monitoring_context->context) {
        lws_cancel_service(g_monitoring_context->context);
    }

    if (g_monitoring_context->thread) {
        pthread_join(g_monitoring_context->thread, NULL);
    }

    free_identity(&g_monitoring_context->identity);
    free_memory((void **)&g_monitoring_context->rx_buffer);
    free_memory((void **)&g_monitoring_context->auth_token);
    free_memory((void **)&g_monitoring_context);

    obs_log(LOG_INFO, "[XboxMonitor] Monitor stopped");
}

bool xbox_monitoring_is_active(void) {
    if (!g_monitoring_context) {
        return false;
    }

    return g_monitoring_context->running;
}

const game_t *get_current_game() {
    return g_current_session.game;
}

const gamerscore_t *get_current_gamerscore(void) {
    return g_current_session.gamerscore;
}

const xbox_achievement_t *get_current_game_achievements() {
    return g_current_session.achievements;
}

void xbox_subscribe_game_played(const on_xbox_game_played_t callback) {

    if (!callback) {
        game_played_subscription_t *node = g_game_played_subscriptions;
        while (node) {
            game_played_subscription_t *next = node->next;
            bfree(node);
            node = next;
        }
        g_game_played_subscriptions = NULL;
        return;
    }

    game_played_subscription_t *new_subscription = bzalloc(sizeof(game_played_subscription_t));

    if (!new_subscription) {
        obs_log(LOG_ERROR, "[XboxMonitor] Failed to allocate subscription node");
        return;
    }

    new_subscription->callback  = callback;
    new_subscription->next      = g_game_played_subscriptions;
    g_game_played_subscriptions = new_subscription;

    /* Immediately sends the game if there is one being played */
    if (g_current_session.game) {
        callback(g_current_session.game);
    }
}

void xbox_subscribe_achievements_progressed(on_xbox_achievements_progressed_t callback) {

    if (!callback) {
        achievements_updated_subscription_t *node = g_achievements_updated_subscriptions;
        while (node) {
            achievements_updated_subscription_t *next = node->next;
            bfree(node);
            node = next;
        }
        g_achievements_updated_subscriptions = NULL;
        return;
    }

    achievements_updated_subscription_t *new_subscription = bzalloc(sizeof(achievements_updated_subscription_t));

    if (!new_subscription) {
        obs_log(LOG_ERROR, "[XboxMonitor] Failed to allocate subscription node");
        return;
    }

    new_subscription->callback           = callback;
    new_subscription->next               = g_achievements_updated_subscriptions;
    g_achievements_updated_subscriptions = new_subscription;
}

void xbox_subscribe_connected_changed(const on_xbox_connection_changed_t callback) {

    if (!callback) {
        return;
    }

    connection_changed_subscription_t *new_node = bzalloc(sizeof(connection_changed_subscription_t));

    if (!new_node) {
        obs_log(LOG_ERROR, "[XboxMonitor] Failed to allocate subscription node");
        return;
    }

    new_node->callback                 = callback;
    new_node->next                     = g_connection_changed_subscriptions;
    g_connection_changed_subscriptions = new_node;

    if (g_monitoring_context) {
        callback(g_monitoring_context->connected, "");
    }
}

void xbox_subscribe_session_ready(const on_xbox_session_ready_t callback) {

    if (!callback) {
        return;
    }

    session_ready_subscription_t *new_node = bzalloc(sizeof(session_ready_subscription_t));

    if (!new_node) {
        obs_log(LOG_ERROR, "[XboxMonitor] Failed to allocate subscription node");
        return;
    }

    new_node->callback            = callback;
    new_node->next                = g_session_ready_subscriptions;
    g_session_ready_subscriptions = new_node;
}

#else /* !HAVE_LIBWEBSOCKETS */

/* Stub implementations when libwebsockets is not available */

bool xbox_monitoring_start() {

    obs_log(LOG_WARNING, "[XboxMonitor] Built without libwebsockets support – monitor unavailable");

    return false;
}

void xbox_monitoring_stop(void) {}

bool xbox_monitoring_is_active(void) {
    return false;
}

const gamerscore_t *get_current_gamerscore(void) {
    return NULL;
}

const game_t *get_current_game() {
    return NULL;
}

const xbox_achievement_t *get_current_game_achievements() {
    return NULL;
}

void xbox_subscribe_game_played(const on_xbox_game_played_t callback) {
    (void)callback;
}

void xbox_subscribe_achievements_progressed(on_xbox_achievements_progressed_t callback) {
    (void)callback;
}

void xbox_subscribe_connected_changed(const on_xbox_connection_changed_t callback) {
    (void)callback;
}

void xbox_subscribe_session_ready(const on_xbox_session_ready_t callback) {
    (void)callback;
}

#endif /* HAVE_LIBWEBSOCKETS */
