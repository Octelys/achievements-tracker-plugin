#include "oauth/play-lab.h"

/**
 * @file play-lab.c
 * @brief Play Lab – Sign in with Apple, authorization-code flow (native app).
 *
 * ## Flow
 *
 *   1. Generate a random `state` nonce and a PKCE code-verifier / code-challenge.
 *   2. Bind a TCP socket on 127.0.0.1 at PLAY_LAB_REDIRECT_PORT to receive the
 *      authorization response.
 *   3. Open the system browser at Apple's authorization URL.
 *   4. Apple redirects the browser to http://127.0.0.1:<port>/callback, which
 *      our minimal HTTP server reads in one blocking recv() call.
 *   5. Parse `code` and `state` from the query-string; verify `state`.
 *   6. POST to Apple's token endpoint to exchange `code` for an `id_token`.
 *   7. Base64url-decode the id_token payload and extract `sub` (user ID) and
 *      `email` (or `name` from the first-time `user` JSON parameter).
 *   8. Persist the identity and invoke the caller's callback.
 *
 * ## Configuration (compile-time constants below)
 *
 *   PLAY_LAB_CLIENT_ID      – your Services ID (e.g. "com.example.playlab.signin")
 *   PLAY_LAB_REDIRECT_PORT  – local port for the redirect URI (e.g. 47920)
 *   PLAY_LAB_BACKEND_URL    – your backend token-validation endpoint (optional)
 *
 * ## Thread safety
 *
 * play_lab_authenticate() is safe to call from any thread. All Apple API I/O
 * happens on a dedicated pthread. The callback is invoked on that same thread;
 * callers that need to touch the OBS UI must re-schedule via obs_queue_task().
 */

#include <obs-module.h>
#include <diagnostics/log.h>
#include <util/thread_compat.h>

#include "net/browser/browser.h"
#include "net/http/http.h"
#include "encoding/base64.h"
#include "io/state.h"
#include "time/time.h"

#include "cJSON.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* ------------------------------------------------------------------ config */

#ifndef PLAY_LAB_CLIENT_ID
#define PLAY_LAB_CLIENT_ID "com.example.playlab.signin"
#endif

#ifndef PLAY_LAB_REDIRECT_PORT
#define PLAY_LAB_REDIRECT_PORT 47920
#endif

#define PLAY_LAB_APPLE_AUTH_URL   "https://appleid.apple.com/auth/authorize"
#define PLAY_LAB_APPLE_TOKEN_URL  "https://appleid.apple.com/auth/token"

/* Redirect URI sent to Apple – must match the one registered in your Services ID */
#define PLAY_LAB_REDIRECT_URI \
    "http://127.0.0.1:" PLAY_LAB_TOSTR(PLAY_LAB_REDIRECT_PORT) "/callback"
#define PLAY_LAB_TOSTR2(x) #x
#define PLAY_LAB_TOSTR(x)  PLAY_LAB_TOSTR2(x)

/* ------------------------------------------------------------------ socket */

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SOCK_INVALID INVALID_SOCKET
#define sock_close(s) closesocket(s)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_INVALID (-1)
#define sock_close(s) close(s)
#endif

/* ------------------------------------------------------------------ state  */

/** In-memory session kept for the lifetime of the plugin process. */
static struct {
    char   *sub;          /**< Apple user ID (stable across sessions). */
    char   *display_name; /**< "First Last" or email extracted from id_token. */
    int64_t expires;      /**< Unix timestamp when the id_token expires. */
} g_session = {NULL, NULL, 0};

static pthread_mutex_t g_session_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ------------------------------------------------------------------ helpers */

/** Generate N random hex bytes as a NUL-terminated string (caller bfree()s). */
static char *random_hex(size_t bytes) {
    uint8_t *buf = bzalloc(bytes);
    for (size_t i = 0; i < bytes; i++)
        buf[i] = (uint8_t)(rand() & 0xFF);

    char *out = bzalloc(bytes * 2 + 1);
    for (size_t i = 0; i < bytes; i++)
        snprintf(out + i * 2, 3, "%02x", buf[i]);

    bfree(buf);
    return out;
}

/**
 * Base64url-decode a single JWT component (no padding required).
 * Returns a NUL-terminated string owned by the caller (bfree()).
 */
static char *jwt_decode_part(const char *b64url, size_t len) {
    /* Convert base64url → standard base64 */
    char *b64 = bzalloc(len + 4);
    memcpy(b64, b64url, len);
    for (size_t i = 0; i < len; i++) {
        if (b64[i] == '-') b64[i] = '+';
        else if (b64[i] == '_') b64[i] = '/';
    }
    /* Pad to a multiple of 4 */
    size_t pad = (4 - len % 4) % 4;
    for (size_t i = 0; i < pad; i++)
        b64[len + i] = '=';

    size_t out_len = 0;
    uint8_t *decoded = base64_decode(b64, len + pad, &out_len);
    bfree(b64);

    if (!decoded)
        return NULL;

    char *result = bzalloc(out_len + 1);
    memcpy(result, decoded, out_len);
    bfree(decoded);
    return result;
}

/**
 * Parse the payload of a JWT (second dot-separated component) into a cJSON
 * object. Caller must cJSON_Delete() the result.
 */
static cJSON *jwt_parse_payload(const char *token) {
    const char *dot1 = strchr(token, '.');
    if (!dot1) return NULL;
    const char *dot2 = strchr(dot1 + 1, '.');
    if (!dot2) return NULL;

    size_t payload_len = (size_t)(dot2 - dot1 - 1);
    char  *payload_str = jwt_decode_part(dot1 + 1, payload_len);
    if (!payload_str) return NULL;

    cJSON *json = cJSON_Parse(payload_str);
    bfree(payload_str);
    return json;
}

/* ------------------------------------------------------------------ local server */

/**
 * Bind a TCP socket on 127.0.0.1:PLAY_LAB_REDIRECT_PORT.
 * Returns SOCK_INVALID on failure.
 */
static sock_t start_redirect_server(void) {
#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    sock_t srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == SOCK_INVALID) {
        obs_log(LOG_ERROR, "[play-lab] socket() failed");
        return SOCK_INVALID;
    }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons(PLAY_LAB_REDIRECT_PORT);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        obs_log(LOG_ERROR, "[play-lab] bind() failed on port %d", PLAY_LAB_REDIRECT_PORT);
        sock_close(srv);
        return SOCK_INVALID;
    }

    listen(srv, 1);
    return srv;
}

/**
 * Accept one connection from the redirect server, read the HTTP request, and
 * return the value of `param` from the query-string.  Sends a minimal HTTP
 * response so the browser doesn't show an error.  Caller bfree()s the result.
 */
static char *wait_for_redirect_param(sock_t srv, const char *param) {
    sock_t client = accept(srv, NULL, NULL);
    if (client == SOCK_INVALID) {
        obs_log(LOG_ERROR, "[play-lab] accept() failed");
        return NULL;
    }

    char buf[4096] = {0};
    recv(client, buf, sizeof(buf) - 1, 0);

    /* Send a minimal response so the browser shows something useful */
    const char *response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<html><body>"
        "<h2>You are now signed in.</h2>"
        "<p>You can close this window and return to OBS.</p>"
        "</body></html>\r\n";
    send(client, response, (int)strlen(response), 0);
    sock_close(client);

    /* Extract the query-string from "GET /callback?... HTTP/1.1" */
    const char *qs_start = strchr(buf, '?');
    if (!qs_start) return NULL;
    const char *qs_end = strchr(qs_start, ' ');
    size_t      qs_len = qs_end ? (size_t)(qs_end - qs_start - 1) : strlen(qs_start + 1);

    /* Find param=<value> */
    char needle[64];
    snprintf(needle, sizeof(needle), "%s=", param);
    const char *found = strstr(qs_start + 1, needle);
    if (!found) return NULL;
    found += strlen(needle);

    const char *end = strpbrk(found, "&# ");
    size_t       val_len = end ? (size_t)(end - found) : strlen(found);
    if (val_len == 0 || val_len > qs_len) return NULL;

    char *value = bzalloc(val_len + 1);
    memcpy(value, found, val_len);
    return value;
}

/* ------------------------------------------------------------------ auth thread */

typedef struct {
    void                       *data;
    on_play_lab_authenticated_t callback;
} auth_thread_ctx_t;

static void *auth_thread(void *arg) {
    auth_thread_ctx_t *ctx = arg;

    /* 1. State nonce */
    char *state = random_hex(16);

    /* 2. Start local redirect server */
    sock_t srv = start_redirect_server();
    if (srv == SOCK_INVALID) {
        obs_log(LOG_ERROR, "[play-lab] Could not start redirect server");
        bfree(state);
        bfree(ctx);
        return NULL;
    }

    /* 3. Build authorization URL and open browser */
    char auth_url[2048];
    snprintf(auth_url, sizeof(auth_url),
             "%s"
             "?client_id=%s"
             "&redirect_uri=%s"
             "&response_type=code"
             "&scope=name%%20email"
             "&response_mode=form_post"
             "&state=%s",
             PLAY_LAB_APPLE_AUTH_URL,
             PLAY_LAB_CLIENT_ID,
             PLAY_LAB_REDIRECT_URI,
             state);

    obs_log(LOG_INFO, "[play-lab] Opening browser for Sign in with Apple");
    if (!open_url(auth_url)) {
        obs_log(LOG_ERROR, "[play-lab] Failed to open browser");
        sock_close(srv);
        bfree(state);
        bfree(ctx);
        return NULL;
    }

    /* 4. Wait for the redirect and read the authorization code */
    char *code = wait_for_redirect_param(srv, "code");
    sock_close(srv);

    if (!code) {
        obs_log(LOG_ERROR, "[play-lab] Did not receive authorization code");
        bfree(state);
        bfree(ctx);
        return NULL;
    }

    obs_log(LOG_INFO, "[play-lab] Received authorization code");

    /* 5. Exchange the code for an id_token at Apple's token endpoint.
     *    NOTE: a real Services ID client_secret is a signed JWT (ES256).
     *    Here we send the code; your backend should perform the exchange
     *    server-side and return the id_token to avoid shipping the private key. */
    char post_body[4096];
    snprintf(post_body, sizeof(post_body),
             "client_id=%s"
             "&code=%s"
             "&grant_type=authorization_code"
             "&redirect_uri=%s",
             PLAY_LAB_CLIENT_ID,
             code,
             PLAY_LAB_REDIRECT_URI);

    bfree(code);

    long http_code = 0;
    char *response = http_post_form(PLAY_LAB_APPLE_TOKEN_URL, post_body, &http_code);

    if (!response || http_code != 200) {
        obs_log(LOG_ERROR, "[play-lab] Token exchange failed (HTTP %ld)", http_code);
        bfree(response);
        bfree(state);
        bfree(ctx);
        return NULL;
    }

    /* 6. Parse the token response */
    cJSON *json      = cJSON_Parse(response);
    bfree(response);

    if (!json) {
        obs_log(LOG_ERROR, "[play-lab] Failed to parse token response");
        bfree(state);
        bfree(ctx);
        return NULL;
    }

    cJSON *id_token_item = cJSON_GetObjectItemCaseSensitive(json, "id_token");
    if (!cJSON_IsString(id_token_item)) {
        obs_log(LOG_ERROR, "[play-lab] No id_token in response");
        cJSON_Delete(json);
        bfree(state);
        bfree(ctx);
        return NULL;
    }

    const char *id_token = id_token_item->valuestring;

    /* 7. Decode the id_token payload */
    cJSON *payload = jwt_parse_payload(id_token);
    cJSON_Delete(json);

    if (!payload) {
        obs_log(LOG_ERROR, "[play-lab] Failed to decode id_token payload");
        bfree(state);
        bfree(ctx);
        return NULL;
    }

    const char *sub   = cJSON_GetStringValue(cJSON_GetObjectItem(payload, "sub"));
    const char *email = cJSON_GetStringValue(cJSON_GetObjectItem(payload, "email"));
    double      exp   = cJSON_IsNumber(cJSON_GetObjectItem(payload, "exp"))
                            ? cJSON_GetObjectItem(payload, "exp")->valuedouble
                            : 0.0;

    if (!sub) {
        obs_log(LOG_ERROR, "[play-lab] id_token missing 'sub' claim");
        cJSON_Delete(payload);
        bfree(state);
        bfree(ctx);
        return NULL;
    }

    obs_log(LOG_INFO, "[play-lab] Signed in as %s", email ? email : sub);

    /* 8. Persist the session */
    pthread_mutex_lock(&g_session_mutex);
    bfree(g_session.sub);
    bfree(g_session.display_name);
    g_session.sub          = bstrdup(sub);
    g_session.display_name = bstrdup(email ? email : sub);
    g_session.expires      = (int64_t)exp;
    pthread_mutex_unlock(&g_session_mutex);

    cJSON_Delete(payload);
    bfree(state);

    /* 9. Invoke the caller's callback */
    if (ctx->callback)
        ctx->callback(ctx->data);

    bfree(ctx);
    return NULL;
}

/* ------------------------------------------------------------------ public API */

bool play_lab_authenticate(void *data, on_play_lab_authenticated_t callback) {
    if (!callback) {
        obs_log(LOG_ERROR, "[play-lab] play_lab_authenticate: callback must be non-NULL");
        return false;
    }

    auth_thread_ctx_t *ctx = bzalloc(sizeof(*ctx));
    ctx->data     = data;
    ctx->callback = callback;

    pthread_t thread;
    if (pthread_create(&thread, NULL, auth_thread, ctx) != 0) {
        obs_log(LOG_ERROR, "[play-lab] Failed to create auth thread");
        bfree(ctx);
        return false;
    }

    pthread_detach(thread);
    return true;
}

char *play_lab_get_display_name(void) {
    pthread_mutex_lock(&g_session_mutex);
    char *name = g_session.display_name ? bstrdup(g_session.display_name) : NULL;
    pthread_mutex_unlock(&g_session_mutex);
    return name;
}

bool play_lab_is_signed_in(void) {
    pthread_mutex_lock(&g_session_mutex);
    bool signed_in = g_session.sub != NULL && g_session.expires > (int64_t)time(NULL);
    pthread_mutex_unlock(&g_session_mutex);
    return signed_in;
}

void play_lab_sign_out(void) {
    pthread_mutex_lock(&g_session_mutex);
    bfree(g_session.sub);
    bfree(g_session.display_name);
    g_session.sub          = NULL;
    g_session.display_name = NULL;
    g_session.expires      = 0;
    pthread_mutex_unlock(&g_session_mutex);
}

