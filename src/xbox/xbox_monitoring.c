#include "xbox_monitoring.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#ifdef HAVE_LIBWEBSOCKETS

#include <libwebsockets.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "io/state.h"

#define RTA_HOST "rta.xboxlive.com"
#define RTA_PATH "/connect"
#define RTA_PORT 443

#define PROTOCOL "rta.xboxlive.com.V2"

typedef struct xbox_monitoring_ctx {
    struct lws_context *context;
    struct lws         *wsi;
    pthread_t           thread;
    bool                running;
    bool                connected;

    /* Callbacks */
    on_xbox_rta_message_received_t  on_message;
    on_xbox_rta_connection_status_t on_status;

    /* Authentication */
    char *auth_token;

    /* Receive buffer */
    char  *rx_buffer;
    size_t rx_buffer_size;
    size_t rx_buffer_used;
} xbox_monitoring_ctx_t;

static xbox_monitoring_ctx_t *g_ctx = NULL;

static int websocket_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {

    UNUSED_PARAMETER(user);

    xbox_monitoring_ctx_t *ctx = (xbox_monitoring_ctx_t *)lws_context_user(lws_get_context(wsi));

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
                obs_log(LOG_ERROR, "Xbox RTA: Failed to add Authorization header");
                return -1;
            }

            obs_log(LOG_INFO, "Xbox RTA: Added Authorization header to handshake");
        }
        break;

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        obs_log(LOG_INFO, "Xbox RTA: WebSocket connection established");
        ctx->connected = true;
        if (ctx->on_status) {
            ctx->on_status(true, NULL);
        }
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
        obs_log(LOG_DEBUG, "Xbox RTA: Received %zu bytes", len);

        /* Ensure buffer has enough space */
        size_t needed = ctx->rx_buffer_used + len + 1;
        if (needed > ctx->rx_buffer_size) {
            size_t new_size   = needed * 2;
            char  *new_buffer = (char *)brealloc(ctx->rx_buffer, new_size);
            if (!new_buffer) {
                obs_log(LOG_ERROR, "Xbox RTA: Failed to allocate receive buffer");
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

            obs_log(LOG_DEBUG, "Xbox RTA: Complete message received: %s", ctx->rx_buffer);

            if (ctx->on_message) {
                ctx->on_message(ctx->rx_buffer);
            }

            /* Reset buffer for next message */
            ctx->rx_buffer_used = 0;
        }
        break;

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        obs_log(LOG_ERROR, "Xbox RTA: Connection error: %s", in ? (char *)in : "unknown");
        ctx->connected = false;
        if (ctx->on_status) {
            ctx->on_status(false, in ? (char *)in : "Connection error");
        }
        break;

    case LWS_CALLBACK_CLIENT_CLOSED:
        obs_log(LOG_INFO, "Xbox RTA: Connection closed");
        ctx->connected = false;
        ctx->wsi       = NULL;
        if (ctx->on_status) {
            ctx->on_status(false, NULL);
        }
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
    xbox_monitoring_ctx_t *ctx = (xbox_monitoring_ctx_t *)arg;

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.port      = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.user      = ctx;
    info.options   = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    ctx->context = lws_create_context(&info);
    if (!ctx->context) {
        obs_log(LOG_ERROR, "Xbox RTA: Failed to create WebSocket context");
        if (ctx->on_status) {
            ctx->on_status(false, "Failed to create WebSocket context");
        }
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

    obs_log(LOG_INFO, "Xbox RTA: Connecting to wss://%s:%d%s", RTA_HOST, RTA_PORT, RTA_PATH);

    ctx->wsi = lws_client_connect_via_info(&ccinfo);
    if (!ctx->wsi) {
        obs_log(LOG_ERROR, "Xbox RTA: Failed to connect");
        if (ctx->on_status) {
            ctx->on_status(false, "Failed to connect");
        }
        lws_context_destroy(ctx->context);
        ctx->context = NULL;
        return NULL;
    }

    /* Service the WebSocket connection */
    while (ctx->running && ctx->context) {
        lws_service(ctx->context, 50);
    }

    obs_log(LOG_INFO, "Xbox RTA: Monitoring thread shutting down");

    if (ctx->context) {
        lws_context_destroy(ctx->context);
        ctx->context = NULL;
    }

    return NULL;
}

bool xbox_monitoring_start(on_xbox_rta_message_received_t on_message, on_xbox_rta_connection_status_t on_status) {

    if (g_ctx) {
        obs_log(LOG_WARNING, "Xbox RTA: Monitoring already active");
        return false;
    }

    /* Get the authorization token from state */
    xbox_identity_t *identity = state_get_xbox_identity();
    if (!identity) {
        obs_log(LOG_ERROR, "Xbox RTA: No identity available");
        return false;
    }

    g_ctx = (xbox_monitoring_ctx_t *)bzalloc(sizeof(xbox_monitoring_ctx_t));
    if (!g_ctx) {
        obs_log(LOG_ERROR, "Xbox RTA: Failed to allocate context");
        return false;
    }

    char auth_header[4096];
    snprintf(auth_header, sizeof(auth_header), "XBL3.0 x=%s;%s", identity->uhs, identity->token->value);

    g_ctx->on_message = on_message;
    g_ctx->on_status  = on_status;
    g_ctx->running    = true;
    g_ctx->connected  = false;
    g_ctx->auth_token = bstrdup(auth_header);

    /* Allocate initial receive buffer */
    g_ctx->rx_buffer_size = 4096;
    g_ctx->rx_buffer      = (char *)bmalloc(g_ctx->rx_buffer_size);
    g_ctx->rx_buffer_used = 0;

    if (!g_ctx->rx_buffer) {
        obs_log(LOG_ERROR, "Xbox RTA: Failed to allocate receive buffer");
        bfree(g_ctx->auth_token);
        bfree(g_ctx);
        g_ctx = NULL;
        return false;
    }

    if (pthread_create(&g_ctx->thread, NULL, monitoring_thread, g_ctx) != 0) {
        obs_log(LOG_ERROR, "Xbox RTA: Failed to create monitoring thread");
        bfree(g_ctx->rx_buffer);
        bfree(g_ctx->auth_token);
        bfree(g_ctx);
        g_ctx = NULL;
        return false;
    }

    obs_log(LOG_INFO, "Xbox RTA: Monitoring started");
    return true;
}

void xbox_monitoring_stop(void) {
    if (!g_ctx) {
        return;
    }

    obs_log(LOG_INFO, "Xbox RTA: Stopping monitoring");

    g_ctx->running = false;

    if (g_ctx->context) {
        lws_cancel_service(g_ctx->context);
    }

    pthread_join(g_ctx->thread, NULL);

    if (g_ctx->auth_token) {
        bfree(g_ctx->auth_token);
    }

    if (g_ctx->rx_buffer) {
        bfree(g_ctx->rx_buffer);
    }

    bfree(g_ctx);
    g_ctx = NULL;

    obs_log(LOG_INFO, "Xbox RTA: Monitoring stopped");
}

bool xbox_monitoring_is_active(void) {
    return g_ctx != NULL && g_ctx->running;
}

#else /* !HAVE_LIBWEBSOCKETS */

/* Stub implementations when libwebsockets is not available */

bool xbox_monitoring_start(on_xbox_rta_message_received_t on_message, on_xbox_rta_connection_status_t on_status) {
    (void)on_message;
    (void)on_status;

    obs_log(LOG_WARNING, "Xbox RTA: WebSockets support not available, monitoring not started");

    return false;
}

void xbox_monitoring_stop(void) {}

bool xbox_monitoring_is_active(void) {
    return false;
}

#endif /* HAVE_LIBWEBSOCKETS */
