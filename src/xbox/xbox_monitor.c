#include "xbox_monitor.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#ifdef HAVE_LIBWEBSOCKETS

#include "xbox_client.h"

#include <libwebsockets.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "external/cjson/cJSON.h"
#include "external/cjson/cJSON_Utils.h"

#include "io/state.h"

#include <text/parsers.h>

#define RTA_HOST "rta.xboxlive.com"
#define RTA_PATH "/connect"
#define RTA_PORT 443

#define PROTOCOL "rta.xboxlive.com.V2"

#define SUBSCRIBE 1
#define UNSUBSCRIBE 1

typedef struct game_played_subscription {
    on_xbox_game_played_t            callback;
    struct game_played_subscription *next;
} game_played_subscription_t;

static game_played_subscription_t *g_game_played_subscriptions = NULL;

typedef struct achievements_updated_subscription {
    on_xbox_achievements_progressed_t         callback;
    struct achievements_updated_subscription *next;
} achievements_updated_subscription_t;

static achievements_updated_subscription_t *g_achievements_updated_subscriptions = NULL;

typedef struct connection_changed_subscription {
    on_xbox_connection_changed_t            callback;
    struct connection_changed_subscription *next;
} connection_changed_subscription_t;

static connection_changed_subscription_t *g_connection_changed_subscriptions = NULL;

typedef struct monitoring_context {
    struct lws_context *context;
    struct lws         *wsi;
    pthread_t           thread;
    bool                running;
    bool                connected;

    /* Authentication */
    char *auth_token;

    /* Receive buffer */
    char  *rx_buffer;
    size_t rx_buffer_size;
    size_t rx_buffer_used;
} monitoring_context_t;

static monitoring_context_t *g_monitoring_context        = NULL;
static game_t               *g_current_game              = NULL;
static achievement_t        *g_current_game_achievements = NULL;

static void notify_game_played(const game_t *game) {
    obs_log(LOG_INFO, "Notifying game played: %s (%s)", game->title, game->id);

    game_played_subscription_t *subscriptions = g_game_played_subscriptions;

    while (subscriptions) {
        subscriptions->callback(game);
        subscriptions = subscriptions->next;
    }
}

static void notify_achievements_progressed(const achievements_progress_t *achievements_progress) {

    obs_log(LOG_INFO, "Notifying achievements progress: %s", achievements_progress->service_config_id);

    achievements_updated_subscription_t *subscription = g_achievements_updated_subscriptions;

    while (subscription) {
        subscription->callback(achievements_progress);
        subscription = subscription->next;
    }
}

static void notify_connection_changed(bool connected, const char *error_message) {

    UNUSED_PARAMETER(error_message);

    obs_log(LOG_INFO,
            "Notifying of a connection changed: %s (%s)",
            connected ? "Connected" : "Not connected",
            error_message);

    connection_changed_subscription_t *node = g_connection_changed_subscriptions;

    while (node) {
        node->callback(connected, error_message);
        node = node->next;
    }
}

/**
 * Send a message over the WebSocket connection
 * @param message The message to send
 * @return true if message was queued for sending, false otherwise
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

    /* RTA subscription message format:
     * [<sequence_id>, <subscribe_action>, "<resource_uri>"]
     * Action 1 = subscribe
     * Resource URI format: https://notify.xboxlive.com/users/xuid(<xuid>)/deviceId/current/titleId/current
     */
    char message[512];
    snprintf(message,
             sizeof(message),
             "[%d,1,\"https://userpresence.xboxlive.com/users/xuid(%s)/richpresence\"]",
             SUBSCRIBE,
             identity->xid);

    obs_log(LOG_INFO, "Monitoring | Subscribing for presence changes for XUID %s", identity->xid);
    return send_websocket_message(message);
}

static bool xbox_presence_unsubscribe(const char *subscription_id) {
    if (!subscription_id || !*subscription_id) {
        obs_log(LOG_ERROR, "Monitoring | Invalid subscription ID for unsubscribe");
        return false;
    }

    if (!g_monitoring_context || !g_monitoring_context->connected) {
        obs_log(LOG_ERROR, "Monitoring | Cannot unsubscribe - not connected");
        return false;
    }

    /* RTA unsubscribe message format:
     * [<sequence_id>, <unsubscribe_action>, "<subscription_id>"]
     * Action 2 = unsubscribe
     */
    char message[256];
    snprintf(message, sizeof(message), "[%d,1,\"%s\"]", UNSUBSCRIBE, subscription_id);

    obs_log(LOG_INFO, "Monitoring | Unsubscribing from %s", subscription_id);
    return send_websocket_message(message);
}

static bool xbox_achievements_progress_subscribe(const achievement_t *achievements) {

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

static bool xbox_achievements_progress_unsubscribe(const achievement_t *achievements) {

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

static void on_game_update_received(game_t *game) {

    if (game && g_current_game && strcasecmp(game->id, g_current_game->id) == 0) {
        /* No change */
        return;
    }

    xbox_achievements_progress_unsubscribe(g_current_game_achievements);

    /* Updates the current game and notifies the subscribers */
    FREE(g_current_game_achievements);
    g_current_game_achievements = NULL;

    FREE(g_current_game);
    g_current_game = game;

    notify_game_played(game);

    /* Let's get the achievements of the game */
    if (game) {
        g_current_game_achievements = xbox_get_game_achievements(game);
        xbox_achievements_progress_subscribe(g_current_game_achievements);
    }
}

static void on_achievements_progress_received(const achievements_progress_t *progress) {

    if (!progress) {
        /* No change */
        return;
    }

    /* TODO Merge progress with g_current_game_achievements */

    notify_achievements_progressed(progress);
}

static void progress_buffer(const char *buffer) {

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
        const achievements_progress_t *progress = parse_achievements_progress(message);
        on_achievements_progress_received(progress);
    }

cleanup:
    FREE(message);
    FREE_JSON(root);
}

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
                                            strlen(ctx->auth_token),
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
        xbox_presence_subscribe();
        notify_connection_changed(true, NULL);
        xbox_achievements_progress_subscribe(g_current_game_achievements);
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
        obs_log(LOG_DEBUG, "Monitoring | Received %zu bytes", len);

        /* Ensure buffer has enough space */
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

            progress_buffer(ctx->rx_buffer);

            /* Reset buffer for next message */
            ctx->rx_buffer_used = 0;
        }
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
        notify_connection_changed(false, NULL);
        break;

    case LWS_CALLBACK_WSI_DESTROY:
        ctx->wsi = NULL;
        break;

    default:
        break;
    }

    return 0;
}

static const struct lws_protocols protocols[] = {{"xbox-rta", websocket_callback, 0, 4096, 0, NULL, 0},
                                                 {NULL, NULL, 0, 0, 0, NULL, 0}};

static void *monitoring_thread(void *arg) {
    monitoring_context_t *ctx = (monitoring_context_t *)arg;

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
        return NULL;
    }

    struct lws_client_connect_info ccinfo;
    memset(&ccinfo, 0, sizeof(ccinfo));

    ccinfo.context        = ctx->context;
    ccinfo.address        = RTA_HOST;
    ccinfo.port           = RTA_PORT;
    ccinfo.path           = RTA_PATH;
    ccinfo.host           = ccinfo.address;
    ccinfo.origin         = ccinfo.address;
    ccinfo.protocol       = "rta.xboxlive.com.V2";
    ccinfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;

    obs_log(LOG_INFO, "Monitoring | Connecting to wss://%s:%d%s", RTA_HOST, RTA_PORT, RTA_PATH);

    ctx->wsi = lws_client_connect_via_info(&ccinfo);
    if (!ctx->wsi) {
        obs_log(LOG_ERROR, "Monitoring | Failed to connect");
        notify_connection_changed(false, "Failed to connect");
        lws_context_destroy(ctx->context);
        ctx->context = NULL;
        return NULL;
    }

    /* Immediately retrieves the game */
    g_current_game = xbox_get_current_game();

    if (g_current_game) {
        notify_game_played(g_current_game);

        g_current_game_achievements = xbox_get_game_achievements(g_current_game);

        if (g_current_game_achievements) {
            xbox_achievements_progress_subscribe(g_current_game_achievements);
        }
    }

    /* Service the WebSocket connection */
    while (ctx->running && ctx->context) {
        lws_service(ctx->context, 50);
    }

    obs_log(LOG_INFO, "Monitoring | Monitoring thread shutting down");

    if (ctx->context) {
        lws_context_destroy(ctx->context);
        ctx->context = NULL;
    }

    return NULL;
}

//  --------------------------------------------------------------------------------------------------------------------
//	Public functions
//  --------------------------------------------------------------------------------------------------------------------

bool xbox_monitoring_start() {

    if (g_monitoring_context) {
        obs_log(LOG_WARNING, "Monitoring | Monitoring already active");
        return false;
    }

    /* Get the authorization token from state */
    xbox_identity_t *identity = state_get_xbox_identity();
    if (!identity) {
        obs_log(LOG_ERROR, "Monitoring | No identity available");
        return false;
    }

    g_monitoring_context = (monitoring_context_t *)bzalloc(sizeof(monitoring_context_t));
    if (!g_monitoring_context) {
        obs_log(LOG_ERROR, "Monitoring | Failed to allocate context");
        return false;
    }

    char auth_header[4096];
    snprintf(auth_header, sizeof(auth_header), "XBL3.0 x=%s;%s", identity->uhs, identity->token->value);

    g_monitoring_context->running    = true;
    g_monitoring_context->connected  = false;
    g_monitoring_context->auth_token = bstrdup(auth_header);

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

bool xbox_monitoring_is_active(void) {
    if (!g_monitoring_context) {
        return false;
    }

    return g_monitoring_context->running;
}

const game_t *get_current_game() {
    return g_current_game;
}

const achievement_t *get_current_game_achievements() {
    return g_current_game_achievements;
}

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

    if (g_current_game) {
        callback(g_current_game);
    }
}

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

    callback(g_monitoring_context->connected, "");
}

#else /* !HAVE_LIBWEBSOCKETS */

/* Stub implementations when libwebsockets is not available */

bool xbox_monitoring_start(on_xbox_game_played_t on_game_played) {
    (void)on_game_played;

    obs_log(LOG_WARNING, "Monitoring | WebSockets support not available, monitoring not started");

    return false;
}

void xbox_monitoring_stop(void) {}

bool xbox_monitoring_is_active(void) {
    return false;
}

const game_t *get_current_game() {
    return NULL;
}

void xbox_subscribe_game_played(const on_xbox_game_played_t callback) {
    (void)callback;
}

void xbox_subscribe_connected_changed(const on_xbox_connection_changed_t callback) {
    (void)callback;
}

#endif /* HAVE_LIBWEBSOCKETS */
