#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* OpenSSL key generation helpers. */
#include <openssl/evp.h>

/* Generate an EC P-256 keypair. Caller must EVP_PKEY_free(). */
EVP_PKEY *crypto_generate_p256_keypair(void);

/* Print public JWK {kty,crv,x,y,alg,use} for ES256 to stdout. */
bool crypto_print_public_jwk_es256(EVP_PKEY *pkey);

#ifdef __cplusplus
}
#endif
