#include "encoding/base64.h"

#include <openssl/evp.h>
#include <stdint.h>
#include <stddef.h>

#include <util/bmem.h>

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
