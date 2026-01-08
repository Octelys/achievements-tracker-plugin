#include "oauth/callback-server.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#if defined(_WIN32)
/* Windows build: loopback server not implemented yet (POSIX sockets + pthread).
 * We provide stubs so the plugin compiles; interactive auth will fail gracefully.
 */
bool oauth_loopback_start(struct oauth_loopback_ctx *ctx, uint16_t fixed_port) {
	UNUSED_PARAMETER(fixed_port);
	if (!ctx)
		return false;
	memset(ctx, 0, sizeof(*ctx));
	ctx->server_fd = -1;
	ctx->port = 0;
	ctx->got_code = false;
	obs_log(LOG_WARNING, "OAuth loopback server is not supported on Windows yet");
	return false;
}

void oauth_loopback_stop(struct oauth_loopback_ctx *ctx) {
	if (!ctx)
		return;
	ctx->server_fd = -1;
	ctx->thread_started = false;
	ctx->thread = NULL;
}

#else

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void http_send_response(int client_fd, const char *body) {
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

static bool parse_query_param(const char *query, const char *key, char *out, size_t out_size) {
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

static void *oauth_loopback_thread(void *arg) {
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
					http_send_response(
						client,
						"<html><body><h3>Signed in</h3><p>You can close this window and return to OBS.</p></body></html>");
				} else {
					obs_log(LOG_WARNING, "OAuth loopback: state mismatch");
					http_send_response(
						client,
						"<html><body><h3>Sign-in failed</h3><p>Invalid state. You can close this window.</p></body></html>");
				}
			} else {
				http_send_response(
					client, "<html><body><h3>Sign-in failed</h3><p>No authorization code received.</p></body></html>");
			}
		} else {
			http_send_response(client, "<html><body><h3>Sign-in failed</h3><p>Missing query string.</p></body></html>");
		}
	}

	close(client);
	return NULL;
}

bool oauth_loopback_start(struct oauth_loopback_ctx *ctx, uint16_t fixed_port) {
	if (!ctx)
		return false;

	memset(ctx, 0, sizeof(*ctx));
	ctx->server_fd = -1;

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
	addr.sin_port = htons(fixed_port);

	if (bind(ctx->server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		obs_log(LOG_WARNING, "OAuth loopback: bind() failed for fixed port %u", (unsigned)fixed_port);
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

	ctx->port = (int)fixed_port;

	pthread_t tid;
	if (pthread_create(&tid, NULL, oauth_loopback_thread, ctx) != 0) {
		obs_log(LOG_WARNING, "OAuth loopback: pthread_create() failed");
		close(ctx->server_fd);
		ctx->server_fd = -1;
		return false;
	}
	ctx->thread = (void *)tid;
	ctx->thread_started = true;

	return true;
}

void oauth_loopback_stop(struct oauth_loopback_ctx *ctx) {
	if (!ctx)
		return;
	if (ctx->server_fd >= 0)
		close(ctx->server_fd);
	ctx->server_fd = -1;
	if (ctx->thread_started) {
		pthread_t tid = (pthread_t)ctx->thread;
		(void)pthread_join(tid, NULL);
		ctx->thread_started = false;
		ctx->thread = NULL;
	}
}

#endif
