#include "plugin-oauth-util.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#endif

static void base64url_encode_bytes(const uint8_t *bytes, size_t bytes_len, char *out, size_t out_size)
{
	static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	char tmp[256];
	size_t j = 0;

	if (!out || out_size == 0)
		return;

	for (size_t i = 0; i < bytes_len; i += 3) {
		uint32_t v = ((uint32_t)bytes[i]) << 16;
		if (i + 1 < bytes_len)
			v |= ((uint32_t)bytes[i + 1]) << 8;
		if (i + 2 < bytes_len)
			v |= (uint32_t)bytes[i + 2];

		tmp[j++] = b64[(v >> 18) & 63];
		tmp[j++] = b64[(v >> 12) & 63];
		if (i + 1 < bytes_len)
			tmp[j++] = b64[(v >> 6) & 63];
		if (i + 2 < bytes_len)
			tmp[j++] = b64[v & 63];

		if (j + 4 >= sizeof(tmp))
			break;
	}

	size_t k = 0;
	for (size_t i = 0; i < j && k + 1 < out_size; i++) {
		char c = tmp[i];
		if (c == '+')
			c = '-';
		else if (c == '/')
			c = '_';
		out[k++] = c;
	}
	out[k] = '\0';
}

void oauth_random_state(char *out, size_t out_size)
{
	static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	if (!out || out_size == 0)
		return;

	srand((unsigned)time(NULL) ^ (unsigned)getpid());
	/* Aim for 32 chars like before, but respect caller buffer size. */
	size_t n = 32;
	if (n + 1 > out_size)
		n = out_size - 1;
	for (size_t i = 0; i < n; i++)
		out[i] = alphabet[(size_t)(rand() % (sizeof(alphabet) - 1))];
	out[n] = '\0';
}

void oauth_pkce_verifier(char *out, size_t out_size)
{
	static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~";
	if (!out || out_size == 0)
		return;

	/* Aim for 64 chars like before, but respect caller buffer size. */
	size_t n = 64;
	if (n + 1 > out_size)
		n = out_size - 1;
	for (size_t i = 0; i < n; i++)
		out[i] = alphabet[(size_t)(rand() % (sizeof(alphabet) - 1))];
	out[n] = '\0';
}

void oauth_pkce_challenge_s256(const char *verifier, char *out, size_t out_size)
{
	if (!verifier || !out || out_size == 0) {
		if (out && out_size)
			out[0] = '\0';
		return;
	}

#ifdef __APPLE__
	uint8_t digest[CC_SHA256_DIGEST_LENGTH];
	CC_SHA256((const unsigned char *)verifier, (CC_LONG)strlen(verifier), digest);
	base64url_encode_bytes(digest, CC_SHA256_DIGEST_LENGTH, out, out_size);
#else
	/* Placeholder for non-Apple builds (current plugin is macOS-focused). */
	out[0] = '\0';
#endif
}

