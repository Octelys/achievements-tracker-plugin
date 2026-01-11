#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <openssl/evp.h>
#include <stdint.h>

EVP_PKEY *crypto_generate_p256_keypair(void);

void crypto_debug_print_keypair(EVP_PKEY *pkey);

char *crypto_key_to_string(EVP_PKEY *pkey);

uint8_t *crypto_sign(EVP_PKEY *private_key, const char *url, const char *authorization_token, const char *payload,
                     size_t *out_len);

#ifdef __cplusplus
}
#endif
