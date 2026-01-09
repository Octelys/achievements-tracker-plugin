#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* OpenSSL key generation helpers. */
#include <openssl/evp.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Generate an EC P-256 keypair. Caller must EVP_PKEY_free(). */
EVP_PKEY *crypto_generate_p256_keypair(void);

char *crypto_key_to_string(EVP_PKEY *pkey);

/*
 * Build the authentication policy header and sign it (ES256 / IEEE-P1363).
 *
 * Returns a newly allocated buffer (bzalloc). Caller must bfree().
 */
uint8_t *crypto_sign_policy_header(
	EVP_PKEY *private_key,
	const char *url,
	const char *authorization_token,
	const char *payload,
	size_t *out_len
);

#ifdef __cplusplus
}
#endif
