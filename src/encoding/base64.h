#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Encode a byte buffer to a standard Base64 string.
 *
 * Uses OpenSSL's EVP_EncodeBlock(), producing RFC 4648 "standard" Base64:
 *  - alphabet: A–Z a–z 0–9 + /
 *  - '=' padding included as needed
 *  - no line breaks
 *
 * @param data Input bytes to encode.
 * @param len  Number of bytes in @p data.
 * @return Newly allocated NUL-terminated Base64 string (caller must free with
 *         bfree()), or NULL if @p data is NULL, @p len is 0, allocation fails,
 *         or the encoding operation fails.
 */
char *base64_encode(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
