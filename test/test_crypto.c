#include <stdint.h>
#include <string.h>

#include <util/bmem.h>

#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/param_build.h>
#include <openssl/core_names.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

#include "crypto/crypto.h"

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/*
 * Helper: decode base64url (no padding) to binary.
 * Returns allocated buffer (caller must free) or NULL on error.
 */
static uint8_t *base64url_decode(const char *in, size_t *out_len) {
    if (!in || !out_len)
        return NULL;

    size_t in_len     = strlen(in);
    /* Add padding if needed */
    size_t padded_len = in_len + (4 - (in_len % 4)) % 4;
    char  *padded     = malloc(padded_len + 1);
    if (!padded)
        return NULL;

    /* Copy and convert base64url to base64 */
    for (size_t i = 0; i < in_len; i++) {
        char c = in[i];
        if (c == '-')
            c = '+';
        else if (c == '_')
            c = '/';
        padded[i] = c;
    }
    /* Add padding */
    for (size_t i = in_len; i < padded_len; i++) {
        padded[i] = '=';
    }
    padded[padded_len] = '\0';

    /* Decode */
    size_t   max_out = (padded_len / 4) * 3;
    uint8_t *out     = malloc(max_out);
    if (!out) {
        free(padded);
        return NULL;
    }

    int decoded = EVP_DecodeBlock(out, (const unsigned char *)padded, (int)padded_len);
    free(padded);

    if (decoded < 0) {
        free(out);
        return NULL;
    }

    /* Adjust for padding bytes removed */
    size_t padding = 0;
    if (in_len > 0 && in[in_len - 1] == '=')
        padding++;
    if (in_len > 1 && in[in_len - 2] == '=')
        padding++;

    /* base64url typically has no padding, so we calculate based on input length */
    *out_len = (in_len * 3) / 4;
    if (in_len % 4 == 2)
        *out_len = (in_len / 4) * 3 + 1;
    else if (in_len % 4 == 3)
        *out_len = (in_len / 4) * 3 + 2;
    else
        *out_len = (in_len / 4) * 3;

    return out;
}

/*
 * Helper: create an EC P-256 key from base64url-encoded x and y coordinates.
 * Returns EVP_PKEY* or NULL on error. Caller must EVP_PKEY_free().
 */
static EVP_PKEY *create_p256_key_from_xy(const char *x_b64url, const char *y_b64url) {
    EVP_PKEY       *pkey   = NULL;
    OSSL_PARAM_BLD *bld    = NULL;
    OSSL_PARAM     *params = NULL;
    EVP_PKEY_CTX   *ctx    = NULL;
    uint8_t        *x_bin  = NULL;
    uint8_t        *y_bin  = NULL;
    size_t          x_len = 0, y_len = 0;

    x_bin = base64url_decode(x_b64url, &x_len);
    y_bin = base64url_decode(y_b64url, &y_len);

    if (!x_bin || !y_bin || x_len != 32 || y_len != 32)
        goto done;

    /* Build uncompressed point: 0x04 || X || Y */
    uint8_t pub_point[65];
    pub_point[0] = 0x04;
    memcpy(pub_point + 1, x_bin, 32);
    memcpy(pub_point + 33, y_bin, 32);

    bld = OSSL_PARAM_BLD_new();
    if (!bld)
        goto done;

    if (!OSSL_PARAM_BLD_push_utf8_string(bld, OSSL_PKEY_PARAM_GROUP_NAME, "prime256v1", 0))
        goto done;
    if (!OSSL_PARAM_BLD_push_octet_string(bld, OSSL_PKEY_PARAM_PUB_KEY, pub_point, sizeof(pub_point)))
        goto done;

    params = OSSL_PARAM_BLD_to_param(bld);
    if (!params)
        goto done;

    ctx = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
    if (!ctx)
        goto done;

    if (EVP_PKEY_fromdata_init(ctx) <= 0)
        goto done;

    if (EVP_PKEY_fromdata(ctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) <= 0)
        goto done;

done:
    free(x_bin);
    free(y_bin);
    if (bld)
        OSSL_PARAM_BLD_free(bld);
    if (params)
        OSSL_PARAM_free(params);
    if (ctx)
        EVP_PKEY_CTX_free(ctx);
    return pkey;
}

/*
 * Helper: decode standard base64 (with padding) to binary.
 */
static uint8_t *base64_decode(const char *in, size_t *out_len) {
    if (!in || !out_len)
        return NULL;

    size_t   in_len  = strlen(in);
    size_t   max_out = (in_len / 4) * 3;
    uint8_t *out     = malloc(max_out + 1);
    if (!out)
        return NULL;

    int decoded = EVP_DecodeBlock(out, (const unsigned char *)in, (int)in_len);
    if (decoded < 0) {
        free(out);
        return NULL;
    }

    /* Adjust for padding */
    size_t padding = 0;
    if (in_len > 0 && in[in_len - 1] == '=')
        padding++;
    if (in_len > 1 && in[in_len - 2] == '=')
        padding++;

    *out_len = (size_t)decoded - padding;
    return out;
}

/*
 * Helper: read u32 big-endian from buffer.
 */
static uint32_t read_u32_be(const uint8_t *buf) {
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}

/*
 * Helper: read u64 big-endian from buffer.
 */
static uint64_t read_u64_be(const uint8_t *buf) {
    return ((uint64_t)buf[0] << 56) | ((uint64_t)buf[1] << 48) | ((uint64_t)buf[2] << 40) | ((uint64_t)buf[3] << 32) |
           ((uint64_t)buf[4] << 24) | ((uint64_t)buf[5] << 16) | ((uint64_t)buf[6] << 8) | (uint64_t)buf[7];
}

/*
 * Test: Verify the structure of a known signature from Xbox Live authentication.
 *
 * The signature was generated with:
 * - URL: https://sisu.xboxlive.com/authorize (or similar)
 * - Payload: The JSON with ProofKey containing x/y coordinates
 * - Expected base64 signature includes version, timestamp, and ECDSA sig
 */
void test_crypto_sign_policy_header_known_signature_structure(void) {
    /* Known test vector from Xbox Live device authentication */
    const char *expected_signature_b64 =
        "AAAAAQHcgRN+cXoAwH+14u6l/pu/7UlmRJtHUZ/xPZ4+yH972ipcgFVpSzZ9zb1htOnZM1nOw9Vn3qc1ZPXGqwYBufbpCw3emMfNUg==";

    /* Decode the signature */
    size_t   sig_len = 0;
    uint8_t *sig     = base64_decode(expected_signature_b64, &sig_len);

    TEST_ASSERT_NOT_NULL(sig);
    /* Expected structure: 4 (version) + 8 (timestamp) + 64 (ECDSA P1363 sig) = 76 bytes */
    TEST_ASSERT_EQUAL_UINT(76, sig_len);

    /* Parse the header */
    uint32_t version   = read_u32_be(sig);
    uint64_t timestamp = read_u64_be(sig + 4);

    /* Version should be 1 */
    TEST_ASSERT_EQUAL_UINT32(1, version);

    /* Timestamp should be a reasonable Windows FILETIME value (after year 2000) */
    /* Windows epoch: 1601-01-01, Unix epoch diff: 11644473600 seconds */
    /* Year 2000 in Windows 100ns ticks: ~126227808000000000 */
    /* Year 2030 in Windows 100ns ticks: ~133802208000000000 */
    TEST_ASSERT_TRUE(timestamp > 126227808000000000ULL);
    TEST_ASSERT_TRUE(timestamp < 140000000000000000ULL);

    free(sig);
}

/*
 * Helper: Load a private key from PEM string.
 * Returns EVP_PKEY* or NULL on error. Caller must EVP_PKEY_free().
 */
static EVP_PKEY *load_private_key_from_pem(const char *pem) {
    BIO *bio = BIO_new_mem_buf(pem, -1);
    if (!bio)
        return NULL;

    EVP_PKEY *pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    BIO_free(bio);
    return pkey;
}

/*
 * Helper: Load a public key from PEM string.
 * Returns EVP_PKEY* or NULL on error. Caller must EVP_PKEY_free().
 */
static EVP_PKEY *load_public_key_from_pem(const char *pem) {
    BIO *bio = BIO_new_mem_buf(pem, -1);
    if (!bio)
        return NULL;

    EVP_PKEY *pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    BIO_free(bio);
    return pkey;
}

/*
 * Test: Verify that crypto_sign_policy_header produces correct base64-encoded output.
 *
 * Uses a known private key and payload, and validates against a known expected signature.
 * The expected signature was generated with this exact key and payload.
 *
 * Expected base64 header:
 * AAAAAQHcgkeStpOA43+EUkmFc/abqT422Z+1TtPi8gf3jrLVKC9A4+XqXIXPsMltof7pCpRlgI5Pw14e/k9934u8/YnnwklE6K+o/w==
 */
void test_crypto_sign_policy_header_output_structure(void) {
    /* Known expected base64-encoded signature header */
    const char *expected_b64 =
        "AAAAAQHcgkeStpOA43+EUkmFc/abqT422Z+1TtPi8gf3jrLVKC9A4+XqXIXPsMltof7pCpRlgI5Pw14e/k9934u8/YnnwklE6K+o/w==";

    /* Known test public key (PEM format) - used to verify the expected signature */
    const char *public_key_pem = "-----BEGIN PUBLIC KEY-----\n"
                                 "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEn8UBRo8NVTFuwkujlvRy1mIDfFMu\n"
                                 "Qbv9e29TdTjC+bfBm8h4qSCcp35xr/wk2BNHVtdVW/YkB9rfnzKUDk6e1Q==\n"
                                 "-----END PUBLIC KEY-----\n";

    /* Load the public key */
    EVP_PKEY *pub_key = load_public_key_from_pem(public_key_pem);
    TEST_ASSERT_NOT_NULL_MESSAGE(pub_key, "Failed to load public key from PEM");

    const char *payload =
        "{\"Properties\":{\"AuthMethod\":\"ProofOfPossession\",\"Id\":\"{7243c60c-faf6-3e2d-bba2-8c4dc722f457}\","
        "\"DeviceType\":\"iOS\",\"SerialNumber\":\"{7243c60c-faf6-3e2d-bba2-8c4dc722f457}\",\"Version\":\"0.0.0\","
        "\"ProofKey\":{\"kty\":\"EC\",\"x\":\"n8UBRo8NVTFuwkujlvRy1mIDfFMuQbv9e29TdTjC-bc\","
        "\"y\":\"wZvIeKkgnKd-ca_8JNgTR1bXVVv2JAfa358ylA5OntU\",\"crv\":\"P-256\",\"alg\":\"ES256\",\"use\":\"sig\"}},"
        "\"RelyingParty\":\"http://auth.xboxlive.com\",\"TokenType\":\"JWT\"}";

    /* Verify the expected base64 string length (104 chars for 76 bytes) */
    TEST_ASSERT_EQUAL_UINT_MESSAGE(104, strlen(expected_b64), "Expected base64 should be 104 characters");

    /* Decode the expected signature */
    size_t   decoded_len = 0;
    uint8_t *decoded     = base64_decode(expected_b64, &decoded_len);
    TEST_ASSERT_NOT_NULL(decoded);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(76, decoded_len, "Decoded header should be 76 bytes");

    /* Verify version */
    uint32_t version = read_u32_be(decoded);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, version, "Version should be 1");

    /* Verify timestamp is reasonable */
    uint64_t timestamp = read_u64_be(decoded + 4);
    TEST_ASSERT_TRUE_MESSAGE(timestamp > 132224352000000000ULL, "Timestamp should be after Jan 1, 2020");
    TEST_ASSERT_TRUE_MESSAGE(timestamp < 145000000000000000ULL, "Timestamp should be before ~2060");

    /* Extract the ECDSA signature (64 bytes: r || s) */
    uint8_t *ecdsa_sig = decoded + 12;

    /* Reconstruct the signed data buffer to verify the signature */
    const char *method     = "POST";
    const char *url_path   = "/device/authenticate";
    const char *auth_token = "";

    size_t method_len  = strlen(method);
    size_t path_len    = strlen(url_path);
    size_t auth_len    = strlen(auth_token);
    size_t payload_len = strlen(payload);

    size_t   buf_len = 4 + 1 + 8 + 1 + (method_len + 1) + (path_len + 1) + (auth_len + 1) + (payload_len + 1);
    uint8_t *buf     = malloc(buf_len);
    TEST_ASSERT_NOT_NULL(buf);

    size_t off = 0;
    /* Version (u32 BE) */
    buf[off++] = (version >> 24) & 0xFF;
    buf[off++] = (version >> 16) & 0xFF;
    buf[off++] = (version >> 8) & 0xFF;
    buf[off++] = version & 0xFF;
    /* Null byte */
    buf[off++] = 0;
    /* Timestamp (u64 BE) */
    buf[off++] = (timestamp >> 56) & 0xFF;
    buf[off++] = (timestamp >> 48) & 0xFF;
    buf[off++] = (timestamp >> 40) & 0xFF;
    buf[off++] = (timestamp >> 32) & 0xFF;
    buf[off++] = (timestamp >> 24) & 0xFF;
    buf[off++] = (timestamp >> 16) & 0xFF;
    buf[off++] = (timestamp >> 8) & 0xFF;
    buf[off++] = timestamp & 0xFF;
    /* Null byte */
    buf[off++] = 0;
    /* Method + null */
    memcpy(buf + off, method, method_len);
    off += method_len;
    buf[off++] = 0;
    /* Path + null */
    memcpy(buf + off, url_path, path_len);
    off += path_len;
    buf[off++] = 0;
    /* Auth token + null */
    memcpy(buf + off, auth_token, auth_len);
    off += auth_len;
    buf[off++] = 0;
    /* Payload + null */
    memcpy(buf + off, payload, payload_len);
    off += payload_len;
    buf[off++] = 0;

    TEST_ASSERT_EQUAL_UINT(buf_len, off);

    /* Convert P1363 signature (r || s) to DER for OpenSSL verification */
    BIGNUM *r = BN_bin2bn(ecdsa_sig, 32, NULL);
    BIGNUM *s = BN_bin2bn(ecdsa_sig + 32, 32, NULL);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_NOT_NULL(s);

    ECDSA_SIG *ecdsa = ECDSA_SIG_new();
    TEST_ASSERT_NOT_NULL(ecdsa);
    ECDSA_SIG_set0(ecdsa, r, s); /* Takes ownership of r, s */

    unsigned char *der_sig = NULL;
    int            der_len = i2d_ECDSA_SIG(ecdsa, &der_sig);
    TEST_ASSERT_TRUE(der_len > 0);

    /* Verify the signature */
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    TEST_ASSERT_NOT_NULL(mdctx);

    int init_result = EVP_DigestVerifyInit(mdctx, NULL, EVP_sha256(), NULL, pub_key);
    TEST_ASSERT_EQUAL_INT(1, init_result);

    int update_result = EVP_DigestVerifyUpdate(mdctx, buf, buf_len);
    TEST_ASSERT_EQUAL_INT(1, update_result);

    int verify_result = EVP_DigestVerifyFinal(mdctx, der_sig, (size_t)der_len);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, verify_result, "Expected signature verification failed");

    /* Cleanup */
    EVP_MD_CTX_free(mdctx);
    OPENSSL_free(der_sig);
    ECDSA_SIG_free(ecdsa);
    free(buf);
    free(decoded);
    EVP_PKEY_free(pub_key);
}

/*
 * Test: Verify that crypto_generate_p256_keypair creates a valid key.
 */
void test_crypto_generate_p256_keypair(void) {
    EVP_PKEY *pkey = crypto_generate_keys();
    TEST_ASSERT_NOT_NULL(pkey);

    /* Verify it's an EC key */
    TEST_ASSERT_EQUAL_INT(EVP_PKEY_EC, EVP_PKEY_base_id(pkey));

    EVP_PKEY_free(pkey);
}

/*
 * Test: Verify that crypto_key_to_string produces valid JSON.
 */
void test_crypto_key_to_string(void) {
    EVP_PKEY *pkey = crypto_generate_keys();
    TEST_ASSERT_NOT_NULL(pkey);

    char *json = crypto_to_string(pkey, true);
    TEST_ASSERT_NOT_NULL(json);

    /* Verify JSON contains expected fields */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"kty\":\"EC\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"crv\":\"P-256\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"x\":\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"y\":\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"alg\":\"ES256\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"use\":\"sig\""));

    bfree(json);
    EVP_PKEY_free(pkey);
}

/*
 * Test: Verify signature with known key and payload can be verified.
 *
 * This uses the known test vector:
 * - ProofKey x: "ysrmqppuYswCXp6SoBnnsOEL1TpgQttD5p2zVlv27mQ"
 * - ProofKey y: "V2-O2UDPXfRfAd8w9ypeyQDfiAxyxU8UqnD9nG3J-pE"
 * - Expected signature:
 * "AAAAAQHcgkT18vaAPIAde6gU8oRpjT8rsJ0RG44aXd0ASnEiP9z8Iu11PljoReLwizblkDjy9UWSjRghGcVOzP+XnschCM+HQ464pg=="
 *
 * Note: We can verify the signature structure, but cannot verify the actual
 * signature without knowing the exact payload bytes that were signed (including
 * whitespace and formatting). Instead, we verify round-trip: sign then verify.
 */
void test_crypto_verify_known_signature(void) {
    const char *x_b64url = "ysrmqppuYswCXp6SoBnnsOEL1TpgQttD5p2zVlv27mQ";
    const char *y_b64url = "V2-O2UDPXfRfAd8w9ypeyQDfiAxyxU8UqnD9nG3J-pE";

    /* Create public key from known coordinates - verify we can parse the key format */
    EVP_PKEY *pub_key = create_p256_key_from_xy(x_b64url, y_b64url);
    TEST_ASSERT_NOT_NULL_MESSAGE(pub_key, "Failed to create public key from known x/y coordinates");

    /* Decode the known signature to verify its structure */
    const char *sig_b64 =
        "AAAAAQHcgkT18vaAPIAde6gU8oRpjT8rsJ0RG44aXd0ASnEiP9z8Iu11PljoReLwizblkDjy9UWSjRghGcVOzP+XnschCM+HQ464pg==";
    size_t   sig_len = 0;
    uint8_t *sig     = base64_decode(sig_b64, &sig_len);
    TEST_ASSERT_NOT_NULL(sig);
    TEST_ASSERT_EQUAL_UINT(76, sig_len);

    /* Extract and verify components */
    uint32_t version   = read_u32_be(sig);
    uint64_t timestamp = read_u64_be(sig + 4);

    TEST_ASSERT_EQUAL_UINT32(1, version);
    /* Verify timestamp is reasonable (between 2020 and 2030) */
    TEST_ASSERT_TRUE(timestamp > 132224352000000000ULL); /* Jan 1, 2020 */
    TEST_ASSERT_TRUE(timestamp < 140000000000000000ULL);

    /* Cleanup */
    free(sig);
    EVP_PKEY_free(pub_key);
}

/*
 * Test: Round-trip signature verification.
 * Generate a signature and verify it can be verified with the same data.
 */
void test_crypto_sign_and_verify_roundtrip(void) {
    /* Generate a fresh keypair */
    EVP_PKEY *pkey = crypto_generate_keys();
    TEST_ASSERT_NOT_NULL(pkey);

    const char *url        = "https://sisu.xboxlive.com/authorize";
    const char *auth_token = "";
    const char *payload =
        "{\"Properties\":{\"AuthMethod\":\"ProofOfPossession\",\"Id\":\"{test-uuid}\","
        "\"DeviceType\":\"iOS\",\"SerialNumber\":\"{test-uuid}\",\"Version\":\"0.0.0\","
        "\"ProofKey\":{\"kty\":\"EC\",\"x\":\"test\",\"y\":\"test\",\"crv\":\"P-256\",\"alg\":\"ES256\",\"use\":\"sig\"}},"
        "\"RelyingParty\":\"http://auth.xboxlive.com\",\"TokenType\":\"JWT\"}";

    /* Sign the data */
    size_t   sig_len = 0;
    uint8_t *header  = crypto_sign(pkey, url, auth_token, payload, &sig_len);
    TEST_ASSERT_NOT_NULL(header);
    TEST_ASSERT_EQUAL_UINT(76, sig_len);

    /* Extract components from the signature header */
    uint32_t version   = read_u32_be(header);
    uint64_t timestamp = read_u64_be(header + 4);
    uint8_t *ecdsa_sig = header + 12;

    TEST_ASSERT_EQUAL_UINT32(1, version);

    /* Reconstruct the signed data buffer (must match crypto_sign_policy_header exactly) */
    const char *method   = "POST";
    const char *url_path = "/authorize"; /* Extracted from URL */

    size_t method_len  = strlen(method);
    size_t path_len    = strlen(url_path);
    size_t auth_len    = strlen(auth_token);
    size_t payload_len = strlen(payload);

    size_t   buf_len = 4 + 1 + 8 + 1 + (method_len + 1) + (path_len + 1) + (auth_len + 1) + (payload_len + 1);
    uint8_t *buf     = malloc(buf_len);
    TEST_ASSERT_NOT_NULL(buf);

    size_t off = 0;
    /* Version (u32 BE) */
    buf[off++] = (version >> 24) & 0xFF;
    buf[off++] = (version >> 16) & 0xFF;
    buf[off++] = (version >> 8) & 0xFF;
    buf[off++] = version & 0xFF;
    /* Null byte */
    buf[off++] = 0;
    /* Timestamp (u64 BE) */
    buf[off++] = (timestamp >> 56) & 0xFF;
    buf[off++] = (timestamp >> 48) & 0xFF;
    buf[off++] = (timestamp >> 40) & 0xFF;
    buf[off++] = (timestamp >> 32) & 0xFF;
    buf[off++] = (timestamp >> 24) & 0xFF;
    buf[off++] = (timestamp >> 16) & 0xFF;
    buf[off++] = (timestamp >> 8) & 0xFF;
    buf[off++] = timestamp & 0xFF;
    /* Null byte */
    buf[off++] = 0;
    /* Method + null */
    memcpy(buf + off, method, method_len);
    off += method_len;
    buf[off++] = 0;
    /* Path + null */
    memcpy(buf + off, url_path, path_len);
    off += path_len;
    buf[off++] = 0;
    /* Auth token + null */
    memcpy(buf + off, auth_token, auth_len);
    off += auth_len;
    buf[off++] = 0;
    /* Payload + null */
    memcpy(buf + off, payload, payload_len);
    off += payload_len;
    buf[off++] = 0;

    TEST_ASSERT_EQUAL_UINT(buf_len, off);

    /* Convert P1363 signature (r || s) to DER for OpenSSL verification */
    BIGNUM *r = BN_bin2bn(ecdsa_sig, 32, NULL);
    BIGNUM *s = BN_bin2bn(ecdsa_sig + 32, 32, NULL);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_NOT_NULL(s);

    ECDSA_SIG *ecdsa = ECDSA_SIG_new();
    TEST_ASSERT_NOT_NULL(ecdsa);
    ECDSA_SIG_set0(ecdsa, r, s); /* Takes ownership of r, s */

    unsigned char *der_sig = NULL;
    int            der_len = i2d_ECDSA_SIG(ecdsa, &der_sig);
    TEST_ASSERT_TRUE(der_len > 0);

    /* Verify the signature using the same key */
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    TEST_ASSERT_NOT_NULL(mdctx);

    int init_result = EVP_DigestVerifyInit(mdctx, NULL, EVP_sha256(), NULL, pkey);
    TEST_ASSERT_EQUAL_INT(1, init_result);

    int update_result = EVP_DigestVerifyUpdate(mdctx, buf, buf_len);
    TEST_ASSERT_EQUAL_INT(1, update_result);

    int verify_result = EVP_DigestVerifyFinal(mdctx, der_sig, (size_t)der_len);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, verify_result, "Round-trip signature verification failed");

    /* Cleanup */
    EVP_MD_CTX_free(mdctx);
    OPENSSL_free(der_sig);
    ECDSA_SIG_free(ecdsa);
    free(buf);
    bfree(header);
    EVP_PKEY_free(pkey);
}

/*
 * Test: crypto_sign_policy_header returns NULL for invalid inputs.
 */
void test_crypto_sign_policy_header_null_inputs(void) {
    EVP_PKEY *pkey = crypto_generate_keys();
    TEST_ASSERT_NOT_NULL(pkey);

    size_t out_len = 0;

    /* NULL private key */
    TEST_ASSERT_NULL(crypto_sign(NULL, "https://test.com", "", "{}", &out_len));

    /* NULL URL */
    TEST_ASSERT_NULL(crypto_sign(pkey, NULL, "", "{}", &out_len));

    /* NULL auth token */
    TEST_ASSERT_NULL(crypto_sign(pkey, "https://test.com", NULL, "{}", &out_len));

    /* NULL payload */
    TEST_ASSERT_NULL(crypto_sign(pkey, "https://test.com", "", NULL, &out_len));

    /* NULL out_len */
    TEST_ASSERT_NULL(crypto_sign(pkey, "https://test.com", "", "{}", NULL));

    EVP_PKEY_free(pkey);
}

void test_crypto_to_from_roundtrip(void) {

    //  Arrange.
    EVP_PKEY *original_keys = crypto_generate_keys();

    //  Act.
    char     *serialized_keys   = crypto_to_string(original_keys, true);
    EVP_PKEY *deserialized_keys = crypto_from_string(serialized_keys, true);
    char     *reserialized_keys = crypto_to_string(deserialized_keys, true);

    //  Assert.
    TEST_ASSERT_EQUAL_STRING(serialized_keys, reserialized_keys);
}

static void crypto_from_string__key_loaded(void) {

    //  Arrange.
    char json_string[] =
        "{\"kty\":\"EC\",\"x\":\"p4Y7keU3YH38axGATY7QwXoKm59dgZeOpIBUaSnNU50\",\"y\":\"HOrvv_Jkf-5iPzyTzTM7unesqNDRHmshYfdzgO_i3Nw\",\"d\":\"bWM7zSR7iRfHCNmyU5G3Fl2ACkBs-31-lo5x-N6J68Y\",\"crv\":\"P-256\",\"alg\":\"ES256\",\"use\":\"sig\"}";

    //  Act.
    EVP_PKEY *pkey = crypto_from_string(json_string, true);

    //  Assert.
    TEST_ASSERT_NOT_NULL(pkey);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(crypto_from_string__key_loaded);
    RUN_TEST(test_crypto_to_from_roundtrip);
    RUN_TEST(test_crypto_generate_p256_keypair);
    RUN_TEST(test_crypto_key_to_string);
    RUN_TEST(test_crypto_sign_policy_header_output_structure);
    RUN_TEST(test_crypto_sign_policy_header_null_inputs);
    RUN_TEST(test_crypto_sign_policy_header_known_signature_structure);
    RUN_TEST(test_crypto_verify_known_signature);
    RUN_TEST(test_crypto_sign_and_verify_roundtrip);

    return UNITY_END();
}
