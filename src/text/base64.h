#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

char *base64_encode(
	const uint8_t *data,
	size_t len
);

#ifdef __cplusplus
}
#endif
