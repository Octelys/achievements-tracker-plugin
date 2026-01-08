#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OAUTH_CODE_MAX 4096
#define OAUTH_STATE_MAX 128
#define OAUTH_VERIFIER_MAX 128
#define OAUTH_CHALLENGE_MAX 128

struct oauth_loopback_ctx {
	int server_fd;
	int port;
	void *thread; /* pthread_t, kept opaque to avoid pthread include in header */
	bool thread_started;

	char state[OAUTH_STATE_MAX];
	char code_verifier[OAUTH_VERIFIER_MAX];
	char code_challenge[OAUTH_CHALLENGE_MAX];

	char auth_code[OAUTH_CODE_MAX];
	bool got_code;
};

bool oauth_loopback_start(struct oauth_loopback_ctx *ctx, uint16_t fixed_port);
void oauth_loopback_stop(struct oauth_loopback_ctx *ctx);

#ifdef __cplusplus
}
#endif
