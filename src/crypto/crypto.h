#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <openssl/evp.h>
#include <stdbool.h>
#include <stdint.h>

void crypto_print_keys(const EVP_PKEY *pkey);

char     *crypto_to_string(const EVP_PKEY *pkey, bool include_private);
EVP_PKEY *crypto_from_string(const char *key_json, bool expect_private);

EVP_PKEY *crypto_generate_keys(void);
uint8_t *crypto_sign(const EVP_PKEY *private_key, const char *url, const char *authorization_token, const char *payload,
                     size_t *out_len);

#ifdef __cplusplus
}
#endif
