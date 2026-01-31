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
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "external/cjson/cJSON.h"

#include "io/state.h"
#include "oauth/xbox-live.h"

#include <text/parsers.h>

#define RTA_HOST "rta.xboxlive.com"
#define RTA_PATH "/connect"
#define RTA_PORT 443

#define PROTOCOL "rta.xboxlive.com.V2"

#define SUBSCRIBE 1
#define UNSUBSCRIBE 1

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

/**
 * @brief Invoke all registered game-played subscribers.
 */
static void notify_game_played(const game_t *game) {

    if (!game) {
        obs_log(LOG_DEBUG, "No notification to be sent: no game is being played");
        return;
    }

    obs_log(LOG_INFO, "Notifying game played: %s (%s)", game->title, game->id);

    game_played_subscription_t *subscriptions = g_game_played_subscriptions;

    while (subscriptions) {
        subscriptions->callback(game);
        subscriptions = subscriptions->next;
    }
}

/**
 * @brief Invoke all registered achievement progressed subscribers.
 */
static void notify_achievements_progressed(const achievement_progress_t *achievements_progress) {

    obs_log(LOG_INFO, "Notifying achievements progress: %s", achievements_progress->service_config_id);

    achievements_updated_subscription_t *subscription = g_achievements_updated_subscriptions;

    while (subscription) {
        subscription->callback(g_current_session.gamerscore, achievements_progress);
        subscription = subscription->next;
    }
}

/**
 * @brief Invoke all registered connection state subscribers.
 */
static void notify_connection_changed(bool is_connected, const char *error_message) {

    UNUSED_PARAMETER(error_message);

    obs_log(LOG_INFO,
            "Notifying of a connection changed: %s (%s)",
            is_connected ? "Connected" : "Not connected",
            error_message);

    connection_changed_subscription_t *node = g_connection_changed_subscriptions;

    while (node) {
        node->callback(is_connected, error_message);
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

    if (!g_monitoring_context || !g_monitoring_context->wsi || !g_monitoring_context->connected) {
        obs_log(LOG_ERROR, "Monitoring | Cannot send message - not connected");
        return false;
    }

    size_t len = strlen(message);

    /* Allocate buffer with LWS_PRE padding */
    unsigned char *buf = (unsigned char *)bmalloc(LWS_PRE + len);
    if (!buf) {
        obs_log(LOG_ERROR, "Monitoring | Failed to allocate send buffer");
        return false;
    }

    memcpy(buf + LWS_PRE, message, len);

    int written = lws_write(g_monitoring_context->wsi, buf + LWS_PRE, len, LWS_WRITE_TEXT);
    bfree(buf);

    if (written < (int)len) {
        obs_log(LOG_ERROR, "Monitoring | Failed to send message (wrote %d of %zu bytes)", written, len);
        return false;
    }

    obs_log(LOG_INFO, "Monitoring | Sent message: %s", message);
    return true;
}

/**
 * @brief Subscribe to rich presence changes for the current user.
 */
static bool xbox_presence_subscribe() {

    xbox_identity_t *identity = state_get_xbox_identity();

    if (!identity) {
        obs_log(LOG_ERROR, "Monitoring | Invalid Xbox identity for subscription");
        return false;
    }

    if (!g_monitoring_context || !g_monitoring_context->connected) {
        obs_log(LOG_ERROR, "Monitoring | Cannot subscribe - not connected");
        return false;
    }

    char message[512];
    snprintf(message,
             sizeof(message),
             "[%d,1,\"https://userpresence.xboxlive.com/users/xuid(%s)/richpresence\"]",
             SUBSCRIBE,
             identity->xid);

    obs_log(LOG_INFO, "Monitoring | Subscribing for presence changes for XUID %s", identity->xid);
    return send_websocket_message(message);
}

/**
 * @brief Unsubscribe from a previously subscribed RTA channel.
 *
 * @param subscription_id Subscription ID/path to unsubscribe from.
 */
static bool xbox_presence_unsubscribe(const char *subscription_id) {
    if (!subscription_id || !*subscription_id) {
        obs_log(LOG_ERROR, "Monitoring | Invalid subscription ID for unsubscribe");
        return false;
    }

    if (!g_monitoring_context || !g_monitoring_context->connected) {
        obs_log(LOG_ERROR, "Monitoring | Cannot unsubscribe - not connected");
        return false;
    }

    char message[256];
    snprintf(message, sizeof(message), "[%d,1,\"%s\"]", UNSUBSCRIBE, subscription_id);

    obs_log(LOG_INFO, "Monitoring | Unsubscribing from %s", subscription_id);
    return send_websocket_message(message);
}

/**
 * @brief Subscribe to achievement progression updates for the current session's game.
 */
static bool xbox_achievements_progress_subscribe(const xbox_session_t *session) {

    if (!session) {
        obs_log(LOG_ERROR, "Monitoring | No session specified");
        return false;
    }

    const achievement_t *achievements = session->achievements;

    if (!achievements) {
        obs_log(LOG_ERROR, "Monitoring | No achievements specified");
        return false;
    }

    const char *service_config_id = achievements->service_config_id;

    xbox_identity_t *identity = state_get_xbox_identity();

    if (!identity) {
        obs_log(LOG_ERROR, "Monitoring | Invalid Xbox identity for subscription");
        return false;
    }

    if (!g_monitoring_context || !g_monitoring_context->connected) {
        obs_log(LOG_ERROR, "Monitoring | Cannot subscribe - not connected");
        return false;
    }

    char message[512];
    snprintf(message,
             sizeof(message),
             "[%d,1,\"https://achievements.xboxlive.com/users/xuid(%s)/achievements/%s\"]",
             SUBSCRIBE,
             identity->xid,
             service_config_id);

    obs_log(LOG_INFO,
            "Monitoring | Subscribing for achievement updates for service config id %s (XUID %s)",
            service_config_id,
            identity->xid);

    return send_websocket_message(message);
}

/**
 * @brief Unsubscribe from achievement progression updates for the current session's game.
 */
static bool xbox_achievements_progress_unsubscribe(const xbox_session_t *session) {

    if (!session) {
        obs_log(LOG_ERROR, "Monitoring | No session specified");
        return false;
    }

    const achievement_t *achievements = session->achievements;

    if (!achievements) {
        obs_log(LOG_ERROR, "Monitoring | No achievements specified");
        return false;
    }

    xbox_identity_t *identity = state_get_xbox_identity();

    if (!identity) {
        obs_log(LOG_ERROR, "Monitoring | Invalid Xbox identity for subscription");
        return false;
    }

    if (!g_monitoring_context || !g_monitoring_context->connected) {
        obs_log(LOG_ERROR, "Monitoring | Cannot subscribe - not connected");
        return false;
    }

    char message[512];
    snprintf(message,
             sizeof(message),
             "[%d,1,\"https://achievements.xboxlive.com/users/xuid(%s)/achievements/%s\"]",
             UNSUBSCRIBE,
             identity->xid,
             achievements->service_config_id);

    obs_log(LOG_INFO,
            "Monitoring | Unsubscribing from achievement updates for service config id %s (XUID %s)",
            achievements->service_config_id,
            identity->xid);

    return send_websocket_message(message);
}

/**
 * @brief Update the current game (including sessions and subscriptions) and notify listeners.
 *
 * This:
 *  - unsubscribes from previous achievements
 *  - refreshes the session (fetch achievements for new game)
 *  - subscribes to achievement updates for the new game (if any)
 *  - notifies listeners
 */
static void xbox_change_game(game_t *game) {

    if (game && xbox_session_is_game_played(&g_current_session, game)) {
        /* No change */
        return;
    }

    /* First, let's make sure we unsubscribe from the previous achievements */
    xbox_achievements_progress_unsubscribe(&g_current_session);

    /* Change the game which includes getting the new list of achievements */
    xbox_session_change_game(&g_current_session, game);

    if (game) {
        /* Now let's subscribe to the new achievements */
        xbox_achievements_progress_subscribe(&g_current_session);
    }

    /* And finally notify the subscribers */
    notify_game_played(game);
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
static void on_achievement_progress_received(const achievement_progress_t *progress) {

    if (!progress) {
        /* No change */
        return;
    }

    /* TODO Progress is not necessarily achieved */

    xbox_session_unlock_achievement(&g_current_session, progress);

    notify_achievements_progressed(progress);
}

/**
 * @brief Called when the websocket transitions to connected state.
 *
 * Fetches the initial gamerscore, sets up subscriptions, and notifies listeners.
 */
static void on_websocket_connected() {

    int64_t gamerscore_value;
    xbox_fetch_gamerscore(&gamerscore_value);

    if (!g_current_session.gamerscore) {
        g_current_session.gamerscore             = bzalloc(sizeof(gamerscore_t));
        g_current_session.gamerscore->base_value = (int)gamerscore_value;
    }

    xbox_presence_subscribe();

    xbox_achievements_progress_subscribe(&g_current_session);

    notify_connection_changed(true, NULL);
}

/**
 * @brief Called when the websocket disconnects.
 */
static void on_websocket_disconnected() {

    xbox_session_clear(&g_current_session);

    notify_connection_changed(false, NULL);
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
    char   *message       = NULL;
    cJSON  *root          = NULL;

    if (!buffer) {
        return;
    }

    obs_log(LOG_DEBUG, "New buffer received %s", buffer);

    /* Parse the buffer [X,X,X] */
    root = cJSON_Parse(buffer);

    if (!root) {
        return;
    }

    /* Retrieves the presence message at index 2 */
    presence_item = cJSON_GetArrayItem(root, 2);

    if (!presence_item) {
        obs_log(LOG_WARNING, "No presence item found");
        goto cleanup;
    }

    message = cJSON_PrintUnformatted(presence_item);

    if (strlen(message) < 5) {
        obs_log(LOG_DEBUG, "No message");
        goto cleanup;
    }

    obs_log(LOG_DEBUG, "Message is %s", message);

    if (is_presence_message(message)) {
        obs_log(LOG_DEBUG, "Message is a presence message");
        game = parse_game(message);
        on_game_update_received(game);
        goto cleanup;
    }

    if (is_achievement_message(message)) {
        obs_log(LOG_DEBUG, "Message is an achievement message");
        const achievement_progress_t *progress = parse_achievement_progress(message);
        on_achievement_progress_received(progress);
    }

cleanup:
    FREE(message);
    FREE_JSON(root);
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
                obs_log(LOG_ERROR, "Monitoring | Failed to add Authorization header");
                return -1;
            }

            obs_log(LOG_DEBUG, "Monitoring | Added Authorization header to handshake");
        }
        break;

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        obs_log(LOG_DEBUG, "Monitoring | WebSocket connection established");
        ctx->connected = true;
        on_websocket_connected();
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
        obs_log(LOG_DEBUG, "Monitoring | Received %zu bytes", len);

        /* Ensure the buffer has enough space */
        size_t needed = ctx->rx_buffer_used + len + 1;
        if (needed > ctx->rx_buffer_size) {
            size_t new_size   = needed * 2;
            char  *new_buffer = (char *)brealloc(ctx->rx_buffer, new_size);
            if (!new_buffer) {
                obs_log(LOG_ERROR, "Monitoring | Failed to allocate receive buffer");
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

            obs_log(LOG_DEBUG, "Monitoring | Complete message received: %s", ctx->rx_buffer);

            on_buffer_received(ctx->rx_buffer);

            /* Reset buffer for next message */
            ctx->rx_buffer_used = 0;
        }
        break;

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
        obs_log(LOG_DEBUG, "Monitoring | Checking token");

        if (g_monitoring_context->identity && !token_is_expired(g_monitoring_context->identity->token)) {
            break;
        }

        obs_log(LOG_INFO, "Monitoring | Refreshing token");

        free_memory((void **)&g_monitoring_context->auth_token);
        free_memory((void **)&g_monitoring_context->identity);

        g_monitoring_context->identity = xbox_live_get_identity();

        if (!g_monitoring_context->identity) {
            obs_log(LOG_ERROR, "Monitoring | Failed to refresh the token");
            break;
        }

        /* Replace the cached auth header for future handshakes */
        g_monitoring_context->auth_token = build_authorization_header(g_monitoring_context->identity);

        obs_log(LOG_INFO, "Monitoring | Token refreshed");

        break;
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        obs_log(LOG_ERROR, "Monitoring | Connection error: %s", in ? (char *)in : "unknown");
        ctx->connected = false;
        notify_connection_changed(false, in ? (char *)in : "Connection error");
        break;

    case LWS_CALLBACK_CLIENT_CLOSED:
        obs_log(LOG_INFO, "Monitoring | Connection closed");
        ctx->connected = false;
        ctx->wsi       = NULL;
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
 * libwebsockets requires a NULL-terminated array of protocols. We register a
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
        obs_log(LOG_ERROR, "Monitoring | Failed to create WebSocket context");
        notify_connection_changed(false, "Failed to create WebSocket context");
        return (void *)1;
    }

    struct lws_client_connect_info ccinfo;
    memset(&ccinfo, 0, sizeof(ccinfo));

    ccinfo.context        = ctx->context;
    ccinfo.address        = RTA_HOST;
    ccinfo.port           = RTA_PORT;
    ccinfo.path           = RTA_PATH;
    ccinfo.host           = ccinfo.address;
    ccinfo.origin         = ccinfo.address;
    ccinfo.protocol       = PROTOCOL;
    ccinfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;

    obs_log(LOG_INFO, "Monitoring | Connecting to wss://%s:%d%s", RTA_HOST, RTA_PORT, RTA_PATH);

    ctx->wsi = lws_client_connect_via_info(&ccinfo);

    if (!ctx->wsi) {
        obs_log(LOG_ERROR, "Monitoring | Failed to connect");
        notify_connection_changed(false, "Failed to connect");
        lws_context_destroy(ctx->context);
        ctx->context = NULL;
        return (void *)1;
    }

    /* Immediately retrieves the game */
    game_t *current_game = xbox_get_current_game();
    xbox_change_game(current_game);

    /* Service the WebSocket connection */
    while (ctx->running && ctx->context) {
        lws_service(ctx->context, 50);
    }

    obs_log(LOG_INFO, "Monitoring | Monitoring thread shutting down");

    if (ctx->context) {
        lws_context_destroy(ctx->context);
        ctx->context = NULL;
    }

    return 0;
}

//  --------------------------------------------------------------------------------------------------------------------
//  Public functions
//  --------------------------------------------------------------------------------------------------------------------

/**
 * @brief Start the Xbox RTA monitor.
 *
 * Allocates a global monitoring context, builds the auth header from the
 * persisted Xbox identity, and starts the background networking thread.
 */
bool xbox_monitoring_start() {

    if (g_monitoring_context) {
        obs_log(LOG_WARNING, "Monitoring | Monitoring already active");
        return false;
    }

    /* Get the authorization token from state */
    xbox_identity_t *identity = xbox_live_get_identity();

    if (!identity) {
        obs_log(LOG_ERROR, "Monitoring | No identity available");
        return false;
    }

    g_monitoring_context = (monitoring_context_t *)bzalloc(sizeof(monitoring_context_t));

    if (!g_monitoring_context) {
        obs_log(LOG_ERROR, "Monitoring | Failed to allocate context");
        return false;
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
        obs_log(LOG_ERROR, "Monitoring | Failed to allocate receive buffer");
        bfree(g_monitoring_context->auth_token);
        bfree(g_monitoring_context);
        g_monitoring_context = NULL;
        return false;
    }

    if (pthread_create(&g_monitoring_context->thread, NULL, monitoring_thread, g_monitoring_context) != 0) {
        obs_log(LOG_ERROR, "Monitoring | Failed to create monitoring thread");
        bfree(g_monitoring_context->rx_buffer);
        bfree(g_monitoring_context->auth_token);
        bfree(g_monitoring_context);
        g_monitoring_context = NULL;
        return false;
    }

    obs_log(LOG_INFO, "Monitoring | Monitoring started");

    return true;
}

/**
 * @brief Stop the Xbox RTA monitor and free resources.
 */
void xbox_monitoring_stop(void) {

    if (!g_monitoring_context) {
        return;
    }

    obs_log(LOG_INFO, "Monitoring | Stopping monitoring");

    g_monitoring_context->running = false;

    if (g_monitoring_context->context) {
        lws_cancel_service(g_monitoring_context->context);
    }

    pthread_join(g_monitoring_context->thread, NULL);

    if (g_monitoring_context->auth_token) {
        bfree(g_monitoring_context->auth_token);
    }

    if (g_monitoring_context->rx_buffer) {
        bfree(g_monitoring_context->rx_buffer);
    }

    bfree(g_monitoring_context);
    g_monitoring_context = NULL;

    obs_log(LOG_INFO, "Monitoring | Monitoring stopped");
}

/**
 * @brief Return whether monitoring is currently active.
 */
bool xbox_monitoring_is_active(void) {
    if (!g_monitoring_context) {
        return false;
    }

    return g_monitoring_context->running;
}

/**
 * @brief Get the currently cached game from the active session.
 *
 * Ownership/lifetime: the returned pointer is owned by the monitor/session and
 * may be replaced when the current game changes or when monitoring stops.
 *
 * @return Cached game, or NULL if no game is currently known.
 */
const game_t *get_current_game() {
    return g_current_session.game;
}

/**
 * @brief Get the most recently cached gamerscore associated with the current session.
 *
 * This snapshot is initialized when the websocket connects (via an initial
 * gamerscore fetch) and then updated as achievement progression messages are
 * processed.
 *
 * Ownership/lifetime: the returned pointer is owned by the monitor/session and
 * remains valid until the next update or until monitoring stops.
 *
 * @return Cached gamerscore snapshot, or NULL if not available yet.
 */
const gamerscore_t *get_current_gamerscore(void) {
    return g_current_session.gamerscore;
}

/**
 * @brief Get the currently cached achievements list from the active session.
 *
 * Ownership/lifetime: the returned pointer is owned by the monitor/session and
 * may be replaced when the current game changes or when monitoring stops.
 *
 * @return Cached achievements list, or NULL if not available.
 */
const achievement_t *get_current_game_achievements() {
    return g_current_session.achievements;
}

/**
 * @brief Subscribe to game-played events.
 *
 * Current behavior:
 * - Each call registers an additional callback (fan-out).
 * - There is currently no unsubscribe API; callbacks live until process exit.
 * - If a game is already known at subscription time, the callback is invoked
 *   immediately with the cached game.
 *
 * Threading: callbacks may be invoked from the monitor thread.
 */
void xbox_subscribe_game_played(const on_xbox_game_played_t callback) {

    if (!callback) {
        return;
    }

    game_played_subscription_t *new_subscription = bzalloc(sizeof(game_played_subscription_t));

    if (!new_subscription) {
        obs_log(LOG_ERROR, "Failed to allocate subscription node");
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

/**
 * @brief Subscribe to achievement progression events.
 *
 * Current behavior:
 * - Each call registers an additional callback (fan-out).
 * - There is currently no unsubscribe API; callbacks live until process exit.
 *
 * Threading: callbacks may be invoked from the monitor thread.
 */
void xbox_subscribe_achievements_progressed(on_xbox_achievements_progressed_t callback) {

    if (!callback) {
        return;
    }

    achievements_updated_subscription_t *new_subscription = bzalloc(sizeof(achievements_updated_subscription_t));

    if (!new_subscription) {
        obs_log(LOG_ERROR, "Failed to allocate subscription node");
        return;
    }

    new_subscription->callback           = callback;
    new_subscription->next               = g_achievements_updated_subscriptions;
    g_achievements_updated_subscriptions = new_subscription;
}

/**
 * @brief Subscribe to connection state change events.
 *
 * Current behavior:
 * - Each call registers an additional callback (fan-out).
 * - There is currently no unsubscribe API; callbacks live until process exit.
 * - If the monitor context already exists, the callback is invoked immediately
 *   with the current connection state.
 *
 * Threading: callbacks may be invoked from the monitor thread.
 */
void xbox_subscribe_connected_changed(const on_xbox_connection_changed_t callback) {
    if (!callback) {
        return;
    }

    connection_changed_subscription_t *new_node = bzalloc(sizeof(connection_changed_subscription_t));

    if (!new_node) {
        obs_log(LOG_ERROR, "Failed to allocate subscription node");
        return;
    }

    new_node->callback                 = callback;
    new_node->next                     = g_connection_changed_subscriptions;
    g_connection_changed_subscriptions = new_node;

    if (g_monitoring_context) {
        callback(g_monitoring_context->connected, "");
    }
}

#else /* !HAVE_LIBWEBSOCKETS */

/* Stub implementations when libwebsockets is not available */

bool xbox_monitoring_start() {

    obs_log(LOG_WARNING, "Monitoring | WebSockets support not available, monitoring not started");

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

const achievement_t *get_current_game_achievements() {
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

#endif /* HAVE_LIBWEBSOCKETS */
