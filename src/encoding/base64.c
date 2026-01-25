#include "encoding/base64.h"

#include <openssl/evp.h>
#include <stdint.h>
#include <stddef.h>

#include <util/bmem.h>

/**
 * @brief Encode a byte buffer to a standard Base64 string.
 *
 * Uses OpenSSL's EVP_EncodeBlock(), producing RFC 4648 "standard" Base64
 * (A–Z a–z 0–9 + /) with '=' padding and no line breaks.
 *
 * @param data Input bytes to encode.
 * @param len  Number of bytes in @p data.
 * @return Newly allocated NUL-terminated Base64 string (caller must free with
 *         bfree()), or NULL if @p data is NULL, @p len is 0, allocation fails,
 *         or the encoding operation fails.
 */
char *base64_encode(const uint8_t *data, size_t len) {
    if (!data || len == 0) {
        return NULL;
    }

    // OpenSSL output size: 4 * ceil(len/3), plus NUL
    const size_t out_len = 4 * ((len + 2) / 3);
    char        *out     = bzalloc(out_len + 1);

    if (!out) {
        return NULL;
    }

    const int written = EVP_EncodeBlock((unsigned char *)out, (const unsigned char *)data, (int)len);

    if (written <= 0) {
        bfree(out);
        return NULL;
    }

    out[written] = '\0';
    return out;
}
