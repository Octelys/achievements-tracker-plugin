#include "retro_achievements_monitor.h"

/**
 * @file retro_achievements_monitor.c
 * @brief WebSocket client implementation for the RetroArch game-state server.
 *
 * When built with libwebsockets (HAVE_LIBWEBSOCKETS), this module:
 *  - Connects to the RetroArch WebSocket server on 127.0.0.1:55437.
 *  - Receives JSON game-state messages and dispatches them to subscribers.
 *  - Automatically reconnects with exponential back-off on disconnect.
 *
 * Build variants:
 *  - If HAVE_LIBWEBSOCKETS is not defined, stub implementations are provided
 *    that report monitoring is unavailable.
 */

#include <obs-module.h>
#include <diagnostics/log.h>

#ifdef HAVE_LIBWEBSOCKETS

#include <libwebsockets.h>
#include <util/thread_compat.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "common/types.h"
#include "external/cjson/cJSON.h"

/* -------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------- */

#define RA_WS_PATH         "/"
#define RA_PROTOCOL_NAME   "retroarch"

#define RA_LOOP_CHECK_MS         50
#define RA_INITIAL_RETRY_DELAY_MS 1000
#define RA_MAX_RETRY_DELAY_MS     60000

/* -------------------------------------------------------------------------
 * Subscriber linked-list nodes
 * ---------------------------------------------------------------------- */

typedef struct game_playing_subscription {
    on_retro_game_playing_t           callback;
    struct game_playing_subscription *next;
} game_playing_subscription_t;

static game_playing_subscription_t *g_game_playing_subscriptions = NULL;

typedef struct no_game_subscription {
    on_retro_no_game_t           callback;
    struct no_game_subscription *next;
} no_game_subscription_t;

static no_game_subscription_t *g_no_game_subscriptions = NULL;

typedef struct connection_changed_subscription {
    on_retro_connection_changed_t           callback;
    struct connection_changed_subscription *next;
} connection_changed_subscription_t;

static connection_changed_subscription_t *g_connection_changed_subscriptions = NULL;

static bool json_item_is_string(const cJSON *item) {
    return item != NULL && (item->type & 0xFF) == cJSON_String && item->valuestring != NULL;
}

/* -------------------------------------------------------------------------
 * Monitor context
 * ---------------------------------------------------------------------- */

/**
 * @brief Internal state for the monitor background thread.
 */
typedef struct monitor_context {
    /** libwebsockets event-loop context. */
    struct lws_context *context;

    /** Active WebSocket connection instance. */
    struct lws *wsi;

    /** Background thread running lws_service(). */
    pthread_t thread;

    /** True while the monitor should remain active. */
    bool running;

    /** True once the WebSocket handshake has completed. */
    bool connected;

    /** Last connection status notified to subscribers. */
    bool last_status_notified;

    /** Receive buffer used to accumulate WebSocket fragments. */
    char  *rx_buffer;
    size_t rx_buffer_size;
    size_t rx_buffer_used;
} monitor_context_t;

static monitor_context_t *g_monitor_context = NULL;

/* -------------------------------------------------------------------------
 * Notification helpers
 * ---------------------------------------------------------------------- */

static void notify_game_playing(const retro_game_t *game) {
    obs_log(LOG_INFO, "[RetroAchievements] Game playing: %s (%s)", game->game_name, game->game_id);

    game_playing_subscription_t *node = g_game_playing_subscriptions;
    while (node) {
        node->callback(game);
        node = node->next;
    }
}

static void notify_no_game(void) {
    obs_log(LOG_INFO, "[RetroAchievements] No game playing");

    no_game_subscription_t *node = g_no_game_subscriptions;
    while (node) {
        node->callback();
        node = node->next;
    }
}

static void notify_connection_changed(const char *error_message) {
    if (!g_monitor_context) {
        return;
    }

    if (g_monitor_context->last_status_notified == g_monitor_context->connected) {
        return;
    }

    obs_log(LOG_INFO,
            "[RetroAchievements] Connection changed: %s",
            g_monitor_context->connected ? "connected" : "disconnected");

    if (error_message) {
        obs_log(LOG_ERROR, "[RetroAchievements] Connection error: %s", error_message);
    }

    connection_changed_subscription_t *node = g_connection_changed_subscriptions;
    while (node) {
        node->callback(g_monitor_context->connected, error_message);
        node = node->next;
    }

    g_monitor_context->last_status_notified = g_monitor_context->connected;
}

/* -------------------------------------------------------------------------
 * Message parsing
 * ---------------------------------------------------------------------- */

/**
 * @brief Parse and dispatch a complete JSON message from the RetroArch server.
 *
 * Expected shapes:
 *   { "type": "game_playing", "game_id": "...", "game_name": "...",
 *     "game_path": "...", "console_id": "...", "console_name": "...",
 *     "core_name": "...", "db_name": "..." }
 *
 *   { "type": "no_game" }
 */
static void on_message_received(const char *buffer) {
    if (!buffer) {
        return;
    }

    obs_log(LOG_DEBUG, "[RetroAchievements] Message received: %s", buffer);

    cJSON *root = cJSON_Parse(buffer);
    if (!root) {
        obs_log(LOG_WARNING, "[RetroAchievements] Failed to parse JSON message");
        return;
    }

    cJSON *type_item = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!json_item_is_string(type_item)) {
        obs_log(LOG_WARNING, "[RetroAchievements] Missing or invalid \"type\" field");
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type_item->valuestring, "game_playing") == 0) {
        retro_game_t game;
        memset(&game, 0, sizeof(game));

        cJSON *field;

        field = cJSON_GetObjectItemCaseSensitive(root, "game_id");
        if (json_item_is_string(field))
            strncpy(game.game_id, field->valuestring, sizeof(game.game_id) - 1);

        field = cJSON_GetObjectItemCaseSensitive(root, "game_name");
        if (json_item_is_string(field))
            strncpy(game.game_name, field->valuestring, sizeof(game.game_name) - 1);

        field = cJSON_GetObjectItemCaseSensitive(root, "game_path");
        if (json_item_is_string(field))
            strncpy(game.game_path, field->valuestring, sizeof(game.game_path) - 1);

        field = cJSON_GetObjectItemCaseSensitive(root, "console_id");
        if (json_item_is_string(field))
            strncpy(game.console_id, field->valuestring, sizeof(game.console_id) - 1);

        field = cJSON_GetObjectItemCaseSensitive(root, "console_name");
        if (json_item_is_string(field))
            strncpy(game.console_name, field->valuestring, sizeof(game.console_name) - 1);

        field = cJSON_GetObjectItemCaseSensitive(root, "core_name");
        if (json_item_is_string(field))
            strncpy(game.core_name, field->valuestring, sizeof(game.core_name) - 1);

        field = cJSON_GetObjectItemCaseSensitive(root, "db_name");
        if (json_item_is_string(field))
            strncpy(game.db_name, field->valuestring, sizeof(game.db_name) - 1);

        notify_game_playing(&game);

    } else if (strcmp(type_item->valuestring, "no_game") == 0) {
        notify_no_game();

    } else {
        obs_log(LOG_DEBUG, "[RetroAchievements] Unknown message type: %s", type_item->valuestring);
    }

    cJSON_Delete(root);
}

/* -------------------------------------------------------------------------
 * Connection event handlers
 * ---------------------------------------------------------------------- */

static void on_websocket_connected(void) {
    g_monitor_context->connected = true;
    notify_connection_changed(NULL);
}

static void on_websocket_disconnected(void) {
    g_monitor_context->connected = false;
    g_monitor_context->wsi       = NULL;
    notify_connection_changed(NULL);
}

static void on_websocket_error(const char *in) {
    g_monitor_context->connected = false;
    g_monitor_context->wsi       = NULL;
    notify_connection_changed(in ? in : "Connection error");
}

/* -------------------------------------------------------------------------
 * libwebsockets callback
 * ---------------------------------------------------------------------- */

static int websocket_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    UNUSED_PARAMETER(user);

    monitor_context_t *ctx = lws_context_user(lws_get_context(wsi));

    if (!ctx) {
        return 0;
    }

    switch (reason) {

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        obs_log(LOG_DEBUG, "[RetroAchievements] WebSocket connection established");
        on_websocket_connected();
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
        obs_log(LOG_DEBUG, "[RetroAchievements] Received %zu bytes", len);

        /* Grow the receive buffer if needed. */
        {
            size_t needed = ctx->rx_buffer_used + len + 1;
            if (needed > ctx->rx_buffer_size) {
                size_t new_size   = needed * 2;
                char  *new_buffer = (char *)brealloc(ctx->rx_buffer, new_size);
                if (!new_buffer) {
                    obs_log(LOG_ERROR, "[RetroAchievements] Failed to grow receive buffer");
                    return -1;
                }
                ctx->rx_buffer      = new_buffer;
                ctx->rx_buffer_size = new_size;
            }

            memcpy(ctx->rx_buffer + ctx->rx_buffer_used, in, len);
            ctx->rx_buffer_used += len;

            /* Process the message once all fragments have arrived. */
            if (lws_is_final_fragment(wsi)) {
                ctx->rx_buffer[ctx->rx_buffer_used] = '\0';
                obs_log(LOG_DEBUG, "[RetroAchievements] Complete message: %s", ctx->rx_buffer);
                on_message_received(ctx->rx_buffer);
                ctx->rx_buffer_used = 0;
            }
        }
        break;

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        obs_log(LOG_ERROR, "[RetroAchievements] Connection error: %s", in ? (char *)in : "unknown");
        on_websocket_error(in ? (char *)in : "Connection error");
        break;

    case LWS_CALLBACK_CLIENT_CLOSED:
        obs_log(LOG_DEBUG, "[RetroAchievements] Connection closed");
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

/* -------------------------------------------------------------------------
 * Protocol table
 * ---------------------------------------------------------------------- */

static const struct lws_protocols protocols[] = {
    {RA_PROTOCOL_NAME, websocket_callback, 0, 4096, 0, NULL, 0},
    {NULL, NULL, 0, 0, 0, NULL, 0},
};

/* -------------------------------------------------------------------------
 * Background thread
 * ---------------------------------------------------------------------- */

static void *monitor_thread(void *arg) {
    monitor_context_t *ctx = arg;

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.port      = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.user      = ctx;

    ctx->context = lws_create_context(&info);
    if (!ctx->context) {
        obs_log(LOG_ERROR, "[RetroAchievements] Failed to create WebSocket context");
        ctx->connected = false;
        notify_connection_changed("Failed to create WebSocket context");
        return (void *)1;
    }

    struct lws_client_connect_info ccinfo;
    memset(&ccinfo, 0, sizeof(ccinfo));

    ccinfo.context  = ctx->context;
    ccinfo.address  = RETRO_ACHIEVEMENTS_WS_HOST;
    ccinfo.port     = RETRO_ACHIEVEMENTS_WS_PORT;
    ccinfo.path     = RA_WS_PATH;
    ccinfo.host     = ccinfo.address;
    ccinfo.origin   = ccinfo.address;
    ccinfo.protocol = RA_PROTOCOL_NAME;

    obs_log(LOG_DEBUG,
            "[RetroAchievements] Connecting to ws://%s:%d%s",
            RETRO_ACHIEVEMENTS_WS_HOST,
            RETRO_ACHIEVEMENTS_WS_PORT,
            RA_WS_PATH);

    ctx->wsi = lws_client_connect_via_info(&ccinfo);
    if (!ctx->wsi) {
        obs_log(LOG_ERROR, "[RetroAchievements] Failed to initiate connection");
        ctx->connected = false;
        notify_connection_changed("Failed to initiate connection");
        lws_context_destroy(ctx->context);
        ctx->context = NULL;
        return (void *)1;
    }

    int retry_delay_ms = RA_INITIAL_RETRY_DELAY_MS;

    while (ctx->running && ctx->context) {
        lws_service(ctx->context, RA_LOOP_CHECK_MS);

        /* Reconnect if the connection dropped while the monitor is still active. */
        if (ctx->running && !ctx->wsi && ctx->context) {
            obs_log(LOG_INFO, "[RetroAchievements] Connection lost, retrying in %d ms...", retry_delay_ms);

            int iterations = retry_delay_ms / RA_LOOP_CHECK_MS;
            for (int i = 0; i < iterations && ctx->running; i++) {
                sleep_ms(RA_LOOP_CHECK_MS);
            }

            obs_log(LOG_INFO, "[RetroAchievements] Reconnecting...");

            ctx->wsi = lws_client_connect_via_info(&ccinfo);

            if (!ctx->wsi) {
                obs_log(LOG_ERROR, "[RetroAchievements] Reconnect attempt failed");
                retry_delay_ms = retry_delay_ms * 2;
                if (retry_delay_ms > RA_MAX_RETRY_DELAY_MS) {
                    retry_delay_ms = RA_MAX_RETRY_DELAY_MS;
                }
            } else {
                obs_log(LOG_INFO, "[RetroAchievements] Connection reestablished");
                retry_delay_ms = RA_INITIAL_RETRY_DELAY_MS;
            }
        }
    }

    if (ctx->context) {
        lws_context_destroy(ctx->context);
        ctx->context = NULL;
    }

    obs_log(LOG_INFO, "[RetroAchievements] Monitor thread stopped");

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

bool retro_achievements_monitor_start(void) {
    bool succeeded = false;

    if (g_monitor_context) {
        obs_log(LOG_INFO, "[RetroAchievements] Monitor already running");
        goto done;
    }

    g_monitor_context = (monitor_context_t *)bzalloc(sizeof(monitor_context_t));
    if (!g_monitor_context) {
        obs_log(LOG_ERROR, "[RetroAchievements] Failed to allocate context");
        goto error;
    }

    g_monitor_context->running              = true;
    g_monitor_context->connected            = false;
    g_monitor_context->last_status_notified = false;

    g_monitor_context->rx_buffer_size = 4096;
    g_monitor_context->rx_buffer      = (char *)bmalloc(g_monitor_context->rx_buffer_size);
    g_monitor_context->rx_buffer_used = 0;

    if (!g_monitor_context->rx_buffer) {
        obs_log(LOG_ERROR, "[RetroAchievements] Failed to allocate receive buffer");
        goto error;
    }

    if (pthread_create(&g_monitor_context->thread, NULL, monitor_thread, g_monitor_context) != 0) {
        obs_log(LOG_ERROR, "[RetroAchievements] Failed to create background thread");
        goto error;
    }

    obs_log(LOG_INFO, "[RetroAchievements] Monitor started");
    succeeded = true;
    goto done;

error:
    bfree(g_monitor_context->rx_buffer);
    bfree(g_monitor_context);
    g_monitor_context = NULL;

done:
    return succeeded;
}

void retro_achievements_monitor_stop(void) {
    if (!g_monitor_context) {
        return;
    }

    obs_log(LOG_DEBUG, "[RetroAchievements] Stopping monitor");

    g_monitor_context->running = false;

    if (g_monitor_context->context) {
        lws_cancel_service(g_monitor_context->context);
    }

    if (g_monitor_context->thread) {
        pthread_join(g_monitor_context->thread, NULL);
    }

    bfree(g_monitor_context->rx_buffer);
    bfree(g_monitor_context);
    g_monitor_context = NULL;

    obs_log(LOG_INFO, "[RetroAchievements] Monitor stopped");
}

bool retro_achievements_monitor_is_active(void) {
    if (!g_monitor_context) {
        return false;
    }

    return g_monitor_context->running;
}

void retro_achievements_subscribe_game_playing(on_retro_game_playing_t callback) {
    if (!callback) {
        return;
    }

    game_playing_subscription_t *node = bzalloc(sizeof(game_playing_subscription_t));
    if (!node) {
        obs_log(LOG_ERROR, "[RetroAchievements] Failed to allocate subscription node");
        return;
    }

    node->callback               = callback;
    node->next                   = g_game_playing_subscriptions;
    g_game_playing_subscriptions = node;
}

void retro_achievements_subscribe_no_game(on_retro_no_game_t callback) {
    if (!callback) {
        return;
    }

    no_game_subscription_t *node = bzalloc(sizeof(no_game_subscription_t));
    if (!node) {
        obs_log(LOG_ERROR, "[RetroAchievements] Failed to allocate subscription node");
        return;
    }

    node->callback          = callback;
    node->next              = g_no_game_subscriptions;
    g_no_game_subscriptions = node;
}

void retro_achievements_subscribe_connection_changed(on_retro_connection_changed_t callback) {
    if (!callback) {
        return;
    }

    connection_changed_subscription_t *node = bzalloc(sizeof(connection_changed_subscription_t));
    if (!node) {
        obs_log(LOG_ERROR, "[RetroAchievements] Failed to allocate subscription node");
        return;
    }

    node->callback                     = callback;
    node->next                         = g_connection_changed_subscriptions;
    g_connection_changed_subscriptions = node;
}

#else /* !HAVE_LIBWEBSOCKETS */

/* -----------------------------------------------------------------
 * Stub implementations when libwebsockets is not available.
 * ----------------------------------------------------------------- */

bool retro_achievements_monitor_start(void) {
    obs_log(LOG_WARNING, "[RetroAchievements] Built without libwebsockets support – monitor unavailable");
    return false;
}

void retro_achievements_monitor_stop(void) {}

bool retro_achievements_monitor_is_active(void) {
    return false;
}

void retro_achievements_subscribe_game_playing(on_retro_game_playing_t callback) {
    UNUSED_PARAMETER(callback);
}

void retro_achievements_subscribe_no_game(on_retro_no_game_t callback) {
    UNUSED_PARAMETER(callback);
}

void retro_achievements_subscribe_connection_changed(on_retro_connection_changed_t callback) {
    UNUSED_PARAMETER(callback);
}

#endif /* HAVE_LIBWEBSOCKETS */
