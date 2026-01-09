#include "text/base64.h"

#include <obs-module.h>
#include <stdint.h>

char *base64_encode(
	const uint8_t *data,
	size_t len
) {
	static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	if (!data && len != 0)
		return NULL;

	/* 4 * ceil(n/3) + NUL */
	size_t out_len = ((len + 2) / 3) * 4;
	char *out = (char *)bzalloc(out_len + 1);

	if (!out)
		return NULL;

	size_t i = 0;
	size_t o = 0;
	while (i < len) {
		uint32_t v = 0;
		int n = 0;
		v |= (uint32_t)data[i++] << 16;
		n++;
		if (i < len) {
			v |= (uint32_t)data[i++] << 8;
			n++;
		}
		if (i < len) {
			v |= (uint32_t)data[i++];
			n++;
		}

		out[o++] = b64[(v >> 18) & 63];
		out[o++] = b64[(v >> 12) & 63];
		out[o++] = (n >= 2) ? b64[(v >> 6) & 63] : '=';
		out[o++] = (n >= 3) ? b64[v & 63] : '=';
	}

	out[o] = '\0';

	return out;
}
