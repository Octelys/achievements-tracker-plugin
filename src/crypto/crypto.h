#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <openssl/evp.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Debug helper that exports an EC keypair to PEM and logs/prints it.
 *
 * @warning This is intended for local debugging only. It logs/prints private key
 *          material, which is unsafe for production builds.
 *
 * @param pkey The key to export.
 */
void crypto_print_keys(const EVP_PKEY *pkey);

/**
 * @brief Serialize an EC P-256 key to a compact JSON structure.
 *
 * The returned JSON is compatible with crypto_from_string() and contains a
 * JWK-like representation of the key:
 *  - kty: "EC"
 *  - crv: "P-256"
 *  - x, y: base64url-encoded public affine coordinates
 *  - d: base64url-encoded private scalar (optional)
 *
 * @param pkey            Source key.
 * @param include_private If true, include the private scalar field "d".
 * @return Heap-allocated JSON string (caller must free with bfree()), or NULL on failure.
 */
char *crypto_to_string(const EVP_PKEY *pkey, bool include_private);

/**
 * @brief Parse a JSON-serialized EC P-256 key into an OpenSSL EVP_PKEY.
 *
 * The input JSON is expected to match the format returned by crypto_to_string().
 * If @p expect_private is true, the JSON must include the private scalar field
 * "d".
 *
 * @param key_json       JSON string containing the key.
 * @param expect_private If true, require/import a private key.
 * @return Newly created EVP_PKEY on success (caller must EVP_PKEY_free), or NULL on failure.
 */
EVP_PKEY *crypto_from_string(const char *key_json, bool expect_private);

/**
 * @brief Generate a fresh EC P-256 keypair.
 *
 * @return Newly generated EVP_PKEY on success (caller must EVP_PKEY_free), or NULL on failure.
 */
EVP_PKEY *crypto_generate_keys(void);

/**
 * @brief Create the binary signature header required by the Xbox request policy.
 *
 * Builds a canonical "to-be-signed" buffer from request parameters and signs it
 * using ECDSA P-256 with SHA-256. The returned header includes the policy version,
 * timestamp, and signature.
 *
 * @param private_key         EC P-256 private key.
 * @param url                 Full request URL.
 * @param authorization_token Authorization token string.
 * @param payload             Request payload string.
 * @param out_len             Receives the length of the returned header.
 * @return Newly allocated header buffer (caller must bfree()), or NULL on error.
 */
uint8_t *crypto_sign(const EVP_PKEY *private_key, const char *url, const char *authorization_token, const char *payload,
                     size_t *out_len);

#ifdef __cplusplus
}
#endif
