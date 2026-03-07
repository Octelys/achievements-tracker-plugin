#include "oauth/google.h"

/**
 * @file google.c
 * @brief Google – OAuth 2.0 authorization-code flow with PKCE (native / public client).
 *
 * ## Security model
 *
 * A desktop application must NEVER embed a client secret (it can be trivially
 * extracted from the binary). This implementation therefore uses the PKCE
 * extension and delegates the token exchange to a trusted backend server that
 * holds the secret:
 *
 *   Desktop                    Browser / Google               Backend
 *   ──────                     ────────────────               ───────
 *   generate state + PKCE  ──▶  /o/oauth2/v2/auth
 *                          ◀──  redirect ?code=…
 *   POST /auth/google/exchange  ──────────────────────────▶
 *     { code, code_verifier,                                exchange with Google
 *       redirect_uri }                                       (uses secret server-side)
 *                          ◀──────────────────────────────  { sub, name, email, exp }
 *   store identity locally
 *
 * ## Flow
 *
 *   1. Generate a random `state` nonce and a PKCE code-verifier / code-challenge.
 *   2. Bind a TCP socket on 127.0.0.1 at GOOGLE_REDIRECT_PORT to receive the
 *      authorization response.
 *   3. Open the system browser at Google's authorization URL.
 *   4. Google redirects the browser to http://127.0.0.1:<port>/callback, which
 *      our minimal HTTP server reads in one blocking recv() call.
 *   5. Parse `code` and `state` from the query-string; verify `state`.
 *   6. POST `{ code, code_verifier, redirect_uri }` to GOOGLE_BACKEND_EXCHANGE_URL.
 *      The backend performs the client-secret-bearing token exchange server-side
 *      and returns `{ sub, name, email, expires_at }`.
 *   7. Persist the identity and invoke the caller's callback.
 *
 * ## Configuration (compile-time constants below)
 *
 *   GOOGLE_CLIENT_ID            – your OAuth 2.0 client ID (Desktop / public client)
 *   GOOGLE_BACKEND_EXCHANGE_URL – your backend endpoint that performs the token exchange
 *   GOOGLE_REDIRECT_PORT        – local port for the redirect URI (e.g. 47921)
 *
 * ## Thread safety
 *
 * google_authenticate() is safe to call from any thread. All I/O happens on a
 * dedicated pthread. The callback is invoked on that same thread; callers that
 * need to touch the OBS UI must re-schedule via obs_queue_task().
 */

#include <obs-module.h>
#include <diagnostics/log.h>
#include <util/thread_compat.h>

#include "net/browser/browser.h"
#include "net/http/http.h"
#include "oauth/util.h"

#include "cJSON.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* ------------------------------------------------------------------ config */

#ifndef GOOGLE_CLIENT_ID
#define GOOGLE_CLIENT_ID "534781958267-tsdrn04kj54qh0ef8qd9arfid92ftcqe.apps.googleusercontent.com"
#endif

/* The backend URL that accepts { code, code_verifier, redirect_uri } and
 * returns { sub, name, email, expires_at }. The client secret never leaves
 * your server. */
#ifndef GOOGLE_BACKEND_EXCHANGE_URL
#define GOOGLE_BACKEND_EXCHANGE_URL "https://your-backend.example.com/auth/google/exchange"
#endif

#ifndef GOOGLE_REDIRECT_PORT
#define GOOGLE_REDIRECT_PORT 47921
#endif

#define GOOGLE_AUTH_URL "https://accounts.google.com/o/oauth2/v2/auth"

#define GOOGLE_TOSTR2(x) #x
#define GOOGLE_TOSTR(x)  GOOGLE_TOSTR2(x)

/* Redirect URI sent to Google – must match the one registered in your client */
#define GOOGLE_REDIRECT_URI \
    "http://127.0.0.1:" GOOGLE_TOSTR(GOOGLE_REDIRECT_PORT) "/callback"

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
    char   *sub;          /**< Google user ID (stable across sessions). */
    char   *display_name; /**< "First Last" or email extracted from id_token. */
    int64_t expires;      /**< Unix timestamp when the id_token expires. */
} g_google_session = {NULL, NULL, 0};

static pthread_mutex_t g_google_session_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ------------------------------------------------------------------ helpers (none needed for JWT) */

/* ------------------------------------------------------------------ local server */

/**
 * Bind a TCP socket on 127.0.0.1:GOOGLE_REDIRECT_PORT.
 * Returns SOCK_INVALID on failure.
 */
static sock_t google_start_redirect_server(void) {
#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    sock_t srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == SOCK_INVALID) {
        obs_log(LOG_ERROR, "[google] socket() failed");
        return SOCK_INVALID;
    }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons(GOOGLE_REDIRECT_PORT);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        obs_log(LOG_ERROR, "[google] bind() failed on port %d", GOOGLE_REDIRECT_PORT);
        sock_close(srv);
        return SOCK_INVALID;
    }

    listen(srv, 1);
    return srv;
}

/**
 * Accept one connection from the redirect server, read the HTTP request, and
 * return the value of `param` from the query-string. Sends a minimal HTTP
 * response so the browser doesn't show an error. Caller bfree()s the result.
 */
static char *google_wait_for_redirect_param(sock_t srv, const char *param) {
    sock_t client = accept(srv, NULL, NULL);
    if (client == SOCK_INVALID) {
        obs_log(LOG_ERROR, "[google] accept() failed");
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
        "<h2>You are now signed in with Google.</h2>"
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

    const char *end     = strpbrk(found, "&# ");
    size_t      val_len = end ? (size_t)(end - found) : strlen(found);
    if (val_len == 0 || val_len > qs_len) return NULL;

    char *value = bzalloc(val_len + 1);
    memcpy(value, found, val_len);
    return value;
}

/* ------------------------------------------------------------------ auth thread */

typedef struct {
    void                    *data;
    on_google_authenticated_t callback;
} google_auth_thread_ctx_t;

static void *google_auth_thread(void *arg) {
    google_auth_thread_ctx_t *ctx = arg;

    /* 1. State nonce + PKCE */
    char state[33];
    oauth_random_state(state, sizeof(state));

    char verifier[65];
    oauth_pkce_verifier(verifier, sizeof(verifier));

    char challenge[128];
    oauth_pkce_challenge_s256(verifier, challenge, sizeof(challenge));

    /* 2. Start local redirect server */
    sock_t srv = google_start_redirect_server();
    if (srv == SOCK_INVALID) {
        obs_log(LOG_ERROR, "[google] Could not start redirect server");
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
             "&scope=openid%%20email%%20profile"
             "&access_type=offline"
             "&state=%s"
             "&code_challenge=%s"
             "&code_challenge_method=S256",
             GOOGLE_AUTH_URL,
             GOOGLE_CLIENT_ID,
             GOOGLE_REDIRECT_URI,
             state,
             challenge);

    obs_log(LOG_INFO, "[google] Opening browser for Sign in with Google");
    if (!open_url(auth_url)) {
        obs_log(LOG_ERROR, "[google] Failed to open browser");
        sock_close(srv);
        bfree(ctx);
        return NULL;
    }

    /* 4. Wait for the redirect and read the authorization code */
    char *code          = google_wait_for_redirect_param(srv, "code");
    char *returned_state = google_wait_for_redirect_param(srv, "state");
    sock_close(srv);

    /* 5. Verify state to guard against CSRF */
    if (!returned_state || strcmp(state, returned_state) != 0) {
        obs_log(LOG_ERROR, "[google] State mismatch – possible CSRF attack");
        bfree(code);
        bfree(returned_state);
        bfree(ctx);
        return NULL;
    }
    bfree(returned_state);

    if (!code) {
        obs_log(LOG_ERROR, "[google] Did not receive authorization code");
        bfree(ctx);
        return NULL;
    }

    obs_log(LOG_INFO, "[google] Received authorization code");

    /* 6. Forward the code + PKCE verifier to the backend for server-side token
     *    exchange. The backend holds the client secret and returns the identity.
     *    The desktop never touches the client secret. */
    char json_body[4096];
    snprintf(json_body, sizeof(json_body),
             "{"
             "\"client_id\":\"%s\","
             "\"code\":\"%s\","
             "\"code_verifier\":\"%s\","
             "\"redirect_uri\":\"%s\""
             "}",
             GOOGLE_CLIENT_ID,
             code,
             verifier,
             GOOGLE_REDIRECT_URI);

    bfree(code);

    long  http_code = 0;
    char *response  = http_post_json(GOOGLE_BACKEND_EXCHANGE_URL, json_body, NULL, &http_code);

    if (!response || http_code != 200) {
        obs_log(LOG_ERROR, "[google] Backend exchange failed (HTTP %ld)", http_code);
        bfree(response);
        bfree(ctx);
        return NULL;
    }

    /* 7. Parse the backend response: { sub, name, email, expires_at } */
    cJSON *json = cJSON_Parse(response);
    bfree(response);

    if (!json) {
        obs_log(LOG_ERROR, "[google] Failed to parse backend response");
        bfree(ctx);
        return NULL;
    }

    const char *sub        = cJSON_GetObjectItem(json, "sub")->valuestring;
    const char *name       = cJSON_GetObjectItem(json, "name")->valuestring;
    const char *email      = cJSON_GetObjectItem(json, "email")->valuestring;
    double      expires_at = cJSON_GetObjectItem(json, "expires_at")
                                 ? cJSON_GetObjectItem(json, "expires_at")->valuedouble
                                 : 0.0;

    if (!sub) {
        obs_log(LOG_ERROR, "[google] Backend response missing 'sub' field");
        cJSON_Delete(json);
        bfree(ctx);
        return NULL;
    }

    /* Prefer full name, fall back to email, then sub */
    const char *display = name ? name : (email ? email : sub);
    obs_log(LOG_INFO, "[google] Signed in as %s", display);

    /* 8. Persist the session */
    pthread_mutex_lock(&g_google_session_mutex);
    bfree(g_google_session.sub);
    bfree(g_google_session.display_name);
    g_google_session.sub          = bstrdup(sub);
    g_google_session.display_name = bstrdup(display);
    g_google_session.expires      = (int64_t)expires_at;
    pthread_mutex_unlock(&g_google_session_mutex);

    cJSON_Delete(json);

    /* 9. Invoke the caller's callback */
    if (ctx->callback)
        ctx->callback(ctx->data);

    bfree(ctx);
    return NULL;
}

/* ------------------------------------------------------------------ public API */

bool google_authenticate(void *data, on_google_authenticated_t callback) {
    if (!callback) {
        obs_log(LOG_ERROR, "[google] google_authenticate: callback must be non-NULL");
        return false;
    }

    google_auth_thread_ctx_t *ctx = bzalloc(sizeof(*ctx));
    ctx->data     = data;
    ctx->callback = callback;

    pthread_t thread;
    if (pthread_create(&thread, NULL, google_auth_thread, ctx) != 0) {
        obs_log(LOG_ERROR, "[google] Failed to create auth thread");
        bfree(ctx);
        return false;
    }

    pthread_detach(thread);
    return true;
}

char *google_get_display_name(void) {
    pthread_mutex_lock(&g_google_session_mutex);
    char *name = g_google_session.display_name ? bstrdup(g_google_session.display_name) : NULL;
    pthread_mutex_unlock(&g_google_session_mutex);
    return name;
}

bool google_is_signed_in(void) {
    pthread_mutex_lock(&g_google_session_mutex);
    bool signed_in = g_google_session.sub != NULL &&
                     g_google_session.expires > (int64_t)time(NULL);
    pthread_mutex_unlock(&g_google_session_mutex);
    return signed_in;
}

void google_sign_out(void) {
    pthread_mutex_lock(&g_google_session_mutex);
    bfree(g_google_session.sub);
    bfree(g_google_session.display_name);
    g_google_session.sub          = NULL;
    g_google_session.display_name = NULL;
    g_google_session.expires      = 0;
    pthread_mutex_unlock(&g_google_session_mutex);
}

