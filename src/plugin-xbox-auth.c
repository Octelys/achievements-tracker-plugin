/*
Plugin Name

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/


#include "plugin-xbox-auth.h"

#include <obs-module.h>
#include <plugin-support.h>

#include "plugin-browser.h"
#include "plugin-http.h"
#include "plugin-oauth-util.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define OAUTH_CODE_MAX 4096
#define OAUTH_STATE_MAX 128
#define OAUTH_VERIFIER_MAX 128
#define OAUTH_CHALLENGE_MAX 128

struct oauth_loopback_ctx {
	int server_fd;
	int port;
	pthread_t thread;
	bool thread_started;

	char state[OAUTH_STATE_MAX];
	char code_verifier[OAUTH_VERIFIER_MAX];
	char code_challenge[OAUTH_CHALLENGE_MAX];

	char auth_code[OAUTH_CODE_MAX];
	bool got_code;
};

/* Forward declarations (C99 forbids implicit function declarations). */
static bool oauth_loopback_start(struct oauth_loopback_ctx *ctx);
static void oauth_loopback_stop(struct oauth_loopback_ctx *ctx);
static bool oauth_open_and_wait_for_code(
	struct oauth_loopback_ctx *ctx,
	const char *client_id,
	const char *scope,
	char redirect_uri[256]);

static void http_send_response(int client_fd, const char *body)
{
	char header[512];
	int body_len = (int)strlen(body);
	int n = snprintf(header, sizeof(header),
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html; charset=utf-8\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n\r\n",
		body_len);
	if (n > 0)
		(void)send(client_fd, header, (size_t)n, 0);
	(void)send(client_fd, body, (size_t)body_len, 0);
}

static bool parse_query_param(const char *query, const char *key, char *out, size_t out_size)
{
	if (!query || !key || !out || out_size == 0)
		return false;
	const size_t key_len = strlen(key);
	const char *p = query;
	while (p && *p) {
		const char *eq = strchr(p, '=');
		if (!eq)
			break;
		const char *amp = strchr(p, '&');
		const char *key_end = eq;
		if ((size_t)(key_end - p) == key_len && strncmp(p, key, key_len) == 0) {
			const char *val_start = eq + 1;
			const char *val_end = amp ? amp : (p + strlen(p));
			size_t len = (size_t)(val_end - val_start);
			if (len >= out_size)
				len = out_size - 1;
			memcpy(out, val_start, len);
			out[len] = '\0';
			return true;
		}
		p = amp ? (amp + 1) : NULL;
	}
	return false;
}

static void *oauth_loopback_thread(void *arg)
{
	struct oauth_loopback_ctx *ctx = arg;
	if (!ctx)
		return NULL;

	struct sockaddr_in cli;
	socklen_t cli_len = sizeof(cli);
	int client = accept(ctx->server_fd, (struct sockaddr *)&cli, &cli_len);
	if (client < 0) {
		obs_log(LOG_WARNING, "OAuth loopback: accept() failed");
		return NULL;
	}

	char buf[8192];
	ssize_t r = recv(client, buf, sizeof(buf) - 1, 0);
	if (r <= 0) {
		close(client);
		return NULL;
	}
	buf[r] = '\0';

	char method[8] = {0};
	char pathq[4096] = {0};
	if (sscanf(buf, "%7s %4095s", method, pathq) == 2) {
		const char *q = strchr(pathq, '?');
		if (q) {
			const char *query = q + 1;
			char state[OAUTH_STATE_MAX] = {0};
			char code[OAUTH_CODE_MAX] = {0};
			(void)parse_query_param(query, "state", state, sizeof(state));
			if (parse_query_param(query, "code", code, sizeof(code))) {
				if (state[0] && strncmp(state, ctx->state, sizeof(ctx->state)) == 0) {
					snprintf(ctx->auth_code, sizeof(ctx->auth_code), "%s", code);
					ctx->got_code = true;
					obs_log(LOG_INFO, "OAuth loopback: captured authorization code (len=%zu)", strlen(ctx->auth_code));
					http_send_response(client,
						"<html><body><h3>Signed in</h3><p>You can close this window and return to OBS.</p></body></html>");
				} else {
					obs_log(LOG_WARNING, "OAuth loopback: state mismatch");
					http_send_response(client,
						"<html><body><h3>Sign-in failed</h3><p>Invalid state. You can close this window.</p></body></html>");
				}
			} else {
				http_send_response(client,
					"<html><body><h3>Sign-in failed</h3><p>No authorization code received.</p></body></html>");
			}
		} else {
			http_send_response(client,
				"<html><body><h3>Sign-in failed</h3><p>Missing query string.</p></body></html>");
		}
	}

	close(client);
	return NULL;
}

static bool oauth_loopback_start(struct oauth_loopback_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));

	ctx->server_fd = (int)socket(AF_INET, SOCK_STREAM, 0);
	if (ctx->server_fd < 0) {
		obs_log(LOG_WARNING, "OAuth loopback: socket() failed");
		return false;
	}

	int opt = 1;
	(void)setsockopt(ctx->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	/* Use a fixed port so the redirect URI is stable and can be registered exactly. */
	const uint16_t kFixedPort = 53125;
	addr.sin_port = htons(kFixedPort);

	if (bind(ctx->server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		obs_log(LOG_WARNING, "OAuth loopback: bind() failed for fixed port %u", (unsigned)kFixedPort);
		close(ctx->server_fd);
		ctx->server_fd = -1;
		return false;
	}

	if (listen(ctx->server_fd, 1) != 0) {
		obs_log(LOG_WARNING, "OAuth loopback: listen() failed");
		close(ctx->server_fd);
		ctx->server_fd = -1;
		return false;
	}

	ctx->port = (int)kFixedPort;

	oauth_random_state(ctx->state, sizeof(ctx->state));
	oauth_pkce_verifier(ctx->code_verifier, sizeof(ctx->code_verifier));
	oauth_pkce_challenge_s256(ctx->code_verifier, ctx->code_challenge, sizeof(ctx->code_challenge));

	if (pthread_create(&ctx->thread, NULL, oauth_loopback_thread, ctx) != 0) {
		obs_log(LOG_WARNING, "OAuth loopback: pthread_create() failed");
		close(ctx->server_fd);
		ctx->server_fd = -1;
		return false;
	}
	ctx->thread_started = true;
	return true;
}

static void oauth_loopback_stop(struct oauth_loopback_ctx *ctx)
{
	if (!ctx)
		return;
	if (ctx->server_fd >= 0)
		close(ctx->server_fd);
	ctx->server_fd = -1;
	if (ctx->thread_started) {
		(void)pthread_join(ctx->thread, NULL);
		ctx->thread_started = false;
	}
}

static bool oauth_open_and_wait_for_code(
	struct oauth_loopback_ctx *ctx,
	const char *client_id,
	const char *scope,
	char redirect_uri[256])
{
	snprintf(redirect_uri, 256, "http://localhost:%d/callback", ctx->port);

	obs_log(LOG_INFO, "Starting Xbox sign-in in browser. Redirect URI: %s", redirect_uri);

	char *redirect_uri_enc = plugin_http_urlencode(redirect_uri);
	char *scope_enc = plugin_http_urlencode(scope);
	if (!redirect_uri_enc || !scope_enc) {
		bfree(redirect_uri_enc);
		bfree(scope_enc);
		obs_log(LOG_WARNING, "Failed to URL-encode OAuth parameters");
		return false;
	}

	char auth_url[4096];
	snprintf(auth_url, sizeof(auth_url),
		"https://login.microsoftonline.com/consumers/oauth2/v2.0/authorize"
		"?client_id=%s"
		"&response_type=code"
		"&redirect_uri=%s"
		"&response_mode=query"
		"&scope=%s"
		"&state=%s"
		"&code_challenge=%s"
		"&code_challenge_method=S256",
		client_id, redirect_uri_enc, scope_enc, ctx->state, ctx->code_challenge);

	bfree(redirect_uri_enc);
	bfree(scope_enc);

	obs_log(LOG_INFO, "OAuth authorization URL: %s", auth_url);

	/* Open the authorization URL in the default browser. */
	if (!plugin_open_url_in_browser(auth_url)) {
		obs_log(LOG_WARNING, "Failed to open browser for OAuth authorization");
		return false;
	}

	/* Wait for the authorization code to be received via the loopback server. */
	time_t start_time = time(NULL);
	while (time(NULL) - start_time < 30) {
		if (ctx->got_code)
			return true;
		usleep(100000); // 100 ms
	}

	obs_log(LOG_WARNING, "OAuth sign-in timed out");
	return false;
}

static char *oauth_exchange_code_for_access_token(
	const char *client_id,
	const char *redirect_uri,
	const char *code,
	const char *code_verifier)
{
	char *code_enc = plugin_http_urlencode(code);
	char *redir_enc = plugin_http_urlencode(redirect_uri);
	char *verifier_enc = plugin_http_urlencode(code_verifier);
	if (!code_enc || !redir_enc || !verifier_enc) {
		bfree(code_enc);
		bfree(redir_enc);
		bfree(verifier_enc);
		obs_log(LOG_WARNING, "Failed to URL-encode token exchange parameters");
		return NULL;
	}

	/*
	 * Public client token exchange (no client_secret).
	 * PKCE is what protects this flow.
	 *
	 * IMPORTANT: Azure will return AADSTS70002 (client_secret required) if the
	 * app registration is configured as a confidential client (or otherwise
	 * requires a secret for the token endpoint). In that case, the fix is to
	 * register the app as a Public client (mobile/desktop) and enable the
	 * redirect URI we use.
	 */
	char postfields[8192];
	snprintf(postfields, sizeof(postfields),
		"client_id=%s&grant_type=authorization_code&code=%s&redirect_uri=%s&code_verifier=%s",
		client_id, code_enc, redir_enc, verifier_enc);
	bfree(code_enc);
	bfree(redir_enc);
	bfree(verifier_enc);

	long http_code = 0;
	/*
	 * IMPORTANT: The authorize and token endpoints must match the app audience.
	 *
	 * - If the app is "Accounts in any organizational directory" (AAD), /common
	 *   works.
	 * - If the app is "Personal Microsoft accounts only" (MSA), Azure requires
	 *   /consumers and will return AADSTS9002331 if you use /common.
	 */
	char *token_json = plugin_http_post_form(
		"https://login.microsoftonline.com/consumers/oauth2/v2.0/token",
		postfields,
		&http_code);
	if (!token_json) {
		obs_log(LOG_WARNING, "Token exchange failed (no response)");
		return NULL;
	}
	if (http_code < 200 || http_code >= 300) {
		obs_log(LOG_WARNING, "Token exchange HTTP %ld: %s", http_code, token_json);
		if (strstr(token_json, "AADSTS70002"))
			obs_log(LOG_WARNING,
				"Token exchange indicates client_secret is required. Configure your Azure app as a Public client (no secret) and enable the loopback redirect URI.");
		if (strstr(token_json, "AADSTS9002331"))
			obs_log(LOG_WARNING,
				"Token exchange endpoint mismatch: this app is configured for Microsoft Accounts only; use the /consumers endpoint for both authorize and token.");
		bfree(token_json);
		return NULL;
	}

	char *access_token = plugin_json_get_string_value(token_json, "access_token");
	if (!access_token) {
		obs_log(LOG_WARNING, "Could not parse access_token from token response");
		obs_log(LOG_DEBUG, "Token response: %s", token_json);
	}

	bfree(token_json);
	return access_token;
}

static bool xbl_authenticate(
	const char *ms_access_token,
	char **out_xbl_token,
	char **out_uhs)
{
	if (out_xbl_token)
		*out_xbl_token = NULL;
	if (out_uhs)
		*out_uhs = NULL;

	char body[8192];
	snprintf(body, sizeof(body),
		"{"
		"\"Properties\":{\"AuthMethod\":\"RPS\",\"SiteName\":\"user.auth.xboxlive.com\",\"RpsTicket\":\"d=%s\"},"
		"\"RelyingParty\":\"http://auth.xboxlive.com\","
		"\"TokenType\":\"JWT\""
		"}",
		ms_access_token);

	long http_code = 0;
	char *xbl_json = plugin_http_post_json(
		"https://user.auth.xboxlive.com/user/authenticate",
		body,
		NULL,
		&http_code);
	if (!xbl_json) {
		obs_log(LOG_WARNING, "XBL authenticate failed (no response)");
		return false;
	}
	if (http_code < 200 || http_code >= 300) {
		obs_log(LOG_WARNING, "XBL authenticate HTTP %ld: %s", http_code, xbl_json);
		bfree(xbl_json);
		return false;
	}

	char *xbl_token = plugin_json_get_string_value(xbl_json, "Token");
	char *uhs = NULL;
	const char *uhs_pos = strstr(xbl_json, "\"uhs\"");
	if (uhs_pos)
		uhs = plugin_json_get_string_value(uhs_pos, "uhs");

	if (!xbl_token || !uhs) {
		obs_log(LOG_WARNING, "Could not parse XBL token / uhs");
		obs_log(LOG_DEBUG, "XBL response: %s", xbl_json);
		bfree(xbl_json);
		bfree(xbl_token);
		bfree(uhs);
		return false;
	}

	bfree(xbl_json);
	if (out_xbl_token)
		*out_xbl_token = xbl_token;
	else
		bfree(xbl_token);

	if (out_uhs)
		*out_uhs = uhs;
	else
		bfree(uhs);

	return true;
}

static char *xsts_authorize(const char *xbl_token)
{
	char body[8192];
	snprintf(body, sizeof(body),
		"{"
		"\"Properties\":{\"SandboxId\":\"RETAIL\",\"UserTokens\":[\"%s\"]},"
		"\"RelyingParty\":\"http://xboxlive.com\","
		"\"TokenType\":\"JWT\""
		"}",
		xbl_token);

	long http_code = 0;
	char *xsts_json = plugin_http_post_json(
		"https://xsts.auth.xboxlive.com/xsts/authorize",
		body,
		NULL,
		&http_code);
	if (!xsts_json) {
		obs_log(LOG_WARNING, "XSTS authorize failed (no response)");
		return NULL;
	}
	if (http_code < 200 || http_code >= 300) {
		/*
		 * XSTS errors are usually JSON like:
		 * {"XErr":2148916233,"Message":"...","Redirect":"..."}
		 */
		char *message = plugin_json_get_string_value(xsts_json, "Message");
		xbl_token = xbl_token; /* silence unused in some builds */
		obs_log(LOG_WARNING, "XSTS authorize HTTP %ld: %s", http_code, xsts_json[0] ? xsts_json : "<empty body>");
		if (message) {
			obs_log(LOG_WARNING, "XSTS error message: %s", message);
			bfree(message);
		}
		bfree(xsts_json);
		return NULL;
	}

	char *xsts_token = plugin_json_get_string_value(xsts_json, "Token");
	if (!xsts_token) {
		obs_log(LOG_WARNING, "Could not parse XSTS token");
		obs_log(LOG_DEBUG, "XSTS response: %s", xsts_json);
	}

	bfree(xsts_json);
	return xsts_token;
}

bool xbox_auth_interactive_get_xsts(
	const char *client_id,
	const char *scope,
	char **out_uhs,
	char **out_xsts_token)
{
	if (out_uhs)
		*out_uhs = NULL;
	if (out_xsts_token)
		*out_xsts_token = NULL;

	if (!client_id || !*client_id) {
		obs_log(LOG_WARNING, "Xbox auth: missing client_id");
		return false;
	}
	if (!scope || !*scope)
		scope = "XboxLive.signin offline_access";

	/* libcurl init is handled inside plugin-http.c */

	struct oauth_loopback_ctx ctx;
	if (!oauth_loopback_start(&ctx))
		return false;

	char redirect_uri[256];
	if (!oauth_open_and_wait_for_code(&ctx, client_id, scope, redirect_uri)) {
		obs_log(LOG_WARNING, "OAuth sign-in did not return an authorization code");
		oauth_loopback_stop(&ctx);
		return false;
	}

	char *ms_access_token = oauth_exchange_code_for_access_token(
		client_id, redirect_uri, ctx.auth_code, ctx.code_verifier);
	if (!ms_access_token) {
		oauth_loopback_stop(&ctx);
		return false;
	}
	obs_log(LOG_INFO, "Microsoft access_token received (len=%zu)", strlen(ms_access_token));

	char *xbl_token = NULL;
	char *uhs = NULL;
	if (!xbl_authenticate(ms_access_token, &xbl_token, &uhs)) {
		bfree(ms_access_token);
		oauth_loopback_stop(&ctx);
		return false;
	}
	bfree(ms_access_token);

	char *xsts_token = xsts_authorize(xbl_token);
	bfree(xbl_token);
	if (!xsts_token) {
		bfree(uhs);
		oauth_loopback_stop(&ctx);
		return false;
	}

	obs_log(LOG_INFO, "XSTS token received (len=%zu).", strlen(xsts_token));

	if (out_uhs)
		*out_uhs = uhs;
	else
		bfree(uhs);
	if (out_xsts_token)
		*out_xsts_token = xsts_token;
	else
		bfree(xsts_token);

	oauth_loopback_stop(&ctx);
	return true;
}

