#include "cJSON.h"
#include "cJSON_Utils.h"

#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/pem.h>
#include <openssl/core_names.h>
#include <openssl/param_build.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

#include <obs-module.h>
#include <diagnostics/log.h>

#include "common/types.h"

void crypto_print_keys(const EVP_PKEY *pkey) {

    if (!pkey) {
        obs_log(LOG_ERROR, "[xbl] failed to export EC key pair for debug: pkey is NULL");
        return;
    }

    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio) {
        obs_log(LOG_ERROR, "[xbl] failed to export EC key pair for debug: BIO_new failed");
        return;
    }

    /* Export public key (SPKI format) */
    obs_log(LOG_WARNING, "=== XboxTokenManager ProofOfPossession Key (PUBLIC, PEM) ===");
    printf("=== XboxTokenManager ProofOfPossession Key (PUBLIC, PEM) ===");
    if (PEM_write_bio_PUBKEY(bio, pkey)) {
        char *pem_data = NULL;
        long  pem_len  = BIO_get_mem_data(bio, &pem_data);
        if (pem_len > 0 && pem_data) {
            char *pub_pem = bzalloc(pem_len + 1);
            memcpy(pub_pem, pem_data, pem_len);
            pub_pem[pem_len] = '\0';
            obs_log(LOG_WARNING, "%s", pub_pem);
            printf("%s", pub_pem);
            bfree(pub_pem);
        }
    } else {
        obs_log(LOG_ERROR, "[xbl] failed to export public key");
    }

    /* Reset BIO for reuse */
    BIO_reset(bio);

    /* Export private key (PKCS8 format) */
    obs_log(LOG_WARNING, "=== XboxTokenManager ProofOfPossession Key (PRIVATE, PEM) ===");
    printf("=== XboxTokenManager ProofOfPossession Key (PRIVATE, PEM) ===");
    if (PEM_write_bio_PrivateKey(bio, pkey, NULL, NULL, 0, NULL, NULL)) {
        char *pem_data = NULL;
        long  pem_len  = BIO_get_mem_data(bio, &pem_data);
        if (pem_len > 0 && pem_data) {
            char *priv_pem = bzalloc(pem_len + 1);
            memcpy(priv_pem, pem_data, pem_len);
            priv_pem[pem_len] = '\0';
            obs_log(LOG_WARNING, "%s", priv_pem);
            printf("%s", priv_pem);
            bfree(priv_pem);
        }
    } else {
        obs_log(LOG_ERROR, "[xbl] failed to export private key");
    }

    BIO_free(bio);
}

static char *b64url_encode(const unsigned char *in, size_t inlen) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    size_t outlen = (inlen + 2) / 3 * 4;
    char  *out    = (char *)malloc(outlen + 1);

    if (!out)
        return NULL;

    size_t i = 0, o = 0;

    while (i + 2 < inlen) {
        unsigned v = (unsigned)in[i] << 16 | (unsigned)in[i + 1] << 8 | (unsigned)in[i + 2];
        out[o++]   = tbl[(v >> 18) & 63];
        out[o++]   = tbl[(v >> 12) & 63];
        out[o++]   = tbl[(v >> 6) & 63];
        out[o++]   = tbl[v & 63];
        i += 3;
    }

    if (i < inlen) {
        unsigned v = (unsigned)in[i] << 16;
        out[o++]   = tbl[(v >> 18) & 63];

        if (i + 1 < inlen) {
            v |= (unsigned)in[i + 1] << 8;
            out[o++] = tbl[(v >> 12) & 63];
            out[o++] = tbl[(v >> 6) & 63];
        } else {
            out[o++] = tbl[(v >> 12) & 63];
        }
        /* no '=' padding in base64url */
    }

    out[o] = '\0';
    return out;
}

/* Extract uncompressed EC public point (04 || X || Y) from an EVP_PKEY using
 * OpenSSL 3 params API. */
static int get_ec_public_point_uncompressed(const EVP_PKEY *pkey, unsigned char *out, size_t out_size,
                                            size_t *out_len) {
    if (out_len)
        *out_len = 0;

    if (!pkey || !out || out_size == 0 || !out_len)
        return 0;

    size_t len = 0;

    /* Query required size */
    if (EVP_PKEY_get_octet_string_param(pkey, OSSL_PKEY_PARAM_PUB_KEY, NULL, 0, &len) != 1)
        return 0;

    if (len == 0 || len > out_size)
        return 0;

    if (EVP_PKEY_get_octet_string_param(pkey, OSSL_PKEY_PARAM_PUB_KEY, out, out_size, &len) != 1)
        return 0;

    *out_len = len;

    return 1;
}

/*
 * Extract the P-256 private scalar as a fixed 32-byte big-endian value.
 *
 * Some OpenSSL builds/providers don't expose OSSL_PKEY_PARAM_PRIV_KEY via the params
 * API for all EVP_PKEY instances, even if a private key is present. In that case,
 * fall back to the legacy EC_KEY accessor.
 */
static int get_p256_private_scalar_32(const EVP_PKEY *pkey, unsigned char out32[32]) {
    if (!pkey || !out32)
        return 0;

#if defined(OPENSSL_VERSION_MAJOR) && OPENSSL_VERSION_MAJOR >= 3
    /* OpenSSL 3+: Prefer the non-deprecated params API. */
    BIGNUM *d = NULL;
    if (EVP_PKEY_get_bn_param((EVP_PKEY *)pkey, OSSL_PKEY_PARAM_PRIV_KEY, &d) == 1 && d) {
        int ok = 0;
        memset(out32, 0, 32);
        ok = (BN_bn2binpad(d, out32, 32) == 32);
        BN_clear_free(d);
        return ok;
    }

    /* Some provider-backed keys may not expose the private scalar. */
    return 0;
#else
    /* OpenSSL 1.1.x: legacy EC_KEY private BIGNUM */
    const EC_KEY *ec = EVP_PKEY_get0_EC_KEY((EVP_PKEY *)pkey);
    if (!ec)
        return 0;

    const BIGNUM *d = EC_KEY_get0_private_key(ec);
    if (!d)
        return 0;

    memset(out32, 0, 32);
    /* BN_bn2binpad returns number of bytes written or -1 on error */
    return (BN_bn2binpad(d, out32, 32) == 32);
#endif
}

/**
 *
 * @param pkey
 * @param include_private
 * @return
 */
char *crypto_to_string(const EVP_PKEY *pkey, bool include_private) {
    unsigned char point[1 + 32 + 32];
    size_t        point_len         = 0;
    char         *x64               = NULL;
    char         *y64               = NULL;
    char         *d64               = NULL;
    char         *returned_key_json = NULL;

    if (!pkey)
        return NULL;

    if (!get_ec_public_point_uncompressed(pkey, point, sizeof(point), &point_len))
        goto done;

    /* Expect uncompressed point: 0x04 || X(32) || Y(32) for P-256 */
    if (point_len != sizeof(point) || point[0] != 0x04)
        goto done;

    x64 = b64url_encode(point + 1, 32);
    y64 = b64url_encode(point + 1 + 32, 32);

    if (!x64 || !y64)
        goto done;

    if (include_private) {
        unsigned char priv32[32];

        if (!get_p256_private_scalar_32(pkey, priv32))
            goto done;

        d64 = b64url_encode(priv32, sizeof(priv32));
        if (!d64)
            goto done;
    }

    char key_json[4096];

    if (include_private) {
        snprintf(key_json,
                 sizeof(key_json),
                 "{\"kty\":\"EC\",\"x\":\"%s\",\"y\":\"%s\",\"d\":\"%s\",\"crv\":\"P-256\",\"alg\":\"ES256\",\"use\":\"sig\"}",
                 x64,
                 y64,
                 d64);
    } else {
        snprintf(key_json,
                 sizeof(key_json),
                 "{\"kty\":\"EC\",\"x\":\"%s\",\"y\":\"%s\",\"crv\":\"P-256\",\"alg\":\"ES256\",\"use\":\"sig\"}",
                 x64,
                 y64);
    }

    returned_key_json = bstrdup(key_json);

done:
    free(x64);
    free(y64);
    free(d64);
    return returned_key_json;
}

/* Minimal base64url decode for unpadded input. Returns 1 on success. */
static int b64url_decode_32(const char *in, uint8_t out[32]) {
    if (!in || !out)
        return 0;

    uint8_t buf[32];
    size_t  o = 0;

    /* base64url: A-Z a-z 0-9 - _ ; no '=' padding expected */
    unsigned acc  = 0;
    int      bits = 0;

    for (const unsigned char *p = (const unsigned char *)in; *p; ++p) {
        unsigned v;
        if (*p >= 'A' && *p <= 'Z')
            v = (unsigned)(*p - 'A');
        else if (*p >= 'a' && *p <= 'z')
            v = (unsigned)(*p - 'a' + 26);
        else if (*p >= '0' && *p <= '9')
            v = (unsigned)(*p - '0' + 52);
        else if (*p == '-')
            v = 62;
        else if (*p == '_')
            v = 63;
        else
            return 0; /* unexpected char */

        acc = (acc << 6) | v;
        bits += 6;

        while (bits >= 8) {
            bits -= 8;
            if (o >= sizeof(buf))
                return 0;
            buf[o++] = (uint8_t)((acc >> bits) & 0xff);
        }
    }

    /* For unpadded base64url, leftover bits must be zero-equivalent. */
    if (bits != 0) {
        if ((acc & ((1u << bits) - 1u)) != 0)
            return 0;
    }

    if (o != 32)
        return 0;
    memcpy(out, buf, 32);
    return 1;
}

/**
 * Parses an EC P-256 key from a JSON string and generates an EVP_PKEY structure.
 *
 * @param key_json A JSON string containing the key attributes. It must include at least "kty", "crv", "x", and "y".
 *                 If `expect_private` is true, it must also contain "d".
 * @param expect_private A boolean flag indicating whether to expect a private key ("d" attribute) in the JSON string.
 * @return A pointer to an `EVP_PKEY` structure representing the parsed key if successful, or NULL on failure.
 */
EVP_PKEY *crypto_from_string(const char *key_json, bool expect_private) {

    if (!key_json)
        return NULL;

    obs_log(LOG_WARNING, "JSON key: %s", key_json);

    cJSON    *json_root      = NULL;
    cJSON    *d64_local_node = NULL;
    EVP_PKEY *pkey           = NULL;

    json_root = cJSON_Parse(key_json);

    if (!json_root) {
        return NULL;
    }

    //  Remove the JSON and use cJSON
    cJSON *kty_node = cJSONUtils_GetPointer(json_root, "/kty");

    if (!kty_node) {
        goto done;
    }

    if (strcmp(kty_node->valuestring, "EC") != 0) {
        goto done;
    }

    cJSON *crv_node = cJSONUtils_GetPointer(json_root, "/crv");

    if (!crv_node) {
        goto done;
    }

    if (strcmp(crv_node->valuestring, "P-256") != 0) {
        goto done;
    }

    cJSON *x64_node = cJSONUtils_GetPointer(json_root, "/x");

    if (!x64_node) {
        goto done;
    }

    cJSON *y64_node = cJSONUtils_GetPointer(json_root, "/y");

    if (!y64_node) {
        goto done;
    }

    if (expect_private) {
        d64_local_node = cJSONUtils_GetPointer(json_root, "/d");

        if (!d64_local_node) {
            goto done;
        }
    }

    uint8_t x[32], y[32];
    if (!b64url_decode_32(x64_node->valuestring, x)) {
        goto done;
    }

    if (!b64url_decode_32(y64_node->valuestring, y)) {
        goto done;
    }

    uint8_t pub[1 + 32 + 32];
    pub[0] = 0x04;
    memcpy(pub + 1, x, 32);
    memcpy(pub + 1 + 32, y, 32);

    uint8_t priv[32];
    if (expect_private) {
        if (!b64url_decode_32(d64_local_node->valuestring, priv))
            goto done;
    }

    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
    if (!ctx)
        goto done;

    if (EVP_PKEY_fromdata_init(ctx) != 1) {
        EVP_PKEY_CTX_free(ctx);
        goto done;
    }

    if (expect_private) {
        /* EC private key import: use OSSL_PARAM_BLD to properly construct the BIGNUM param. */
        BIGNUM *d_bn = BN_bin2bn(priv, (int)sizeof(priv), NULL);
        if (!d_bn) {
            EVP_PKEY_CTX_free(ctx);
            goto done;
        }

        OSSL_PARAM_BLD *bld = OSSL_PARAM_BLD_new();
        if (!bld) {
            BN_free(d_bn);
            EVP_PKEY_CTX_free(ctx);
            goto done;
        }

        if (!OSSL_PARAM_BLD_push_utf8_string(bld, OSSL_PKEY_PARAM_GROUP_NAME, "prime256v1", 0) ||
            !OSSL_PARAM_BLD_push_octet_string(bld, OSSL_PKEY_PARAM_PUB_KEY, pub, sizeof(pub)) ||
            !OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_PRIV_KEY, d_bn)) {
            OSSL_PARAM_BLD_free(bld);
            BN_free(d_bn);
            EVP_PKEY_CTX_free(ctx);
            goto done;
        }

        OSSL_PARAM *params = OSSL_PARAM_BLD_to_param(bld);
        OSSL_PARAM_BLD_free(bld);
        BN_free(d_bn);

        if (!params) {
            EVP_PKEY_CTX_free(ctx);
            goto done;
        }

        if (EVP_PKEY_fromdata(ctx, &pkey, EVP_PKEY_KEYPAIR, params) != 1) {
            OSSL_PARAM_free(params);
            EVP_PKEY_CTX_free(ctx);
            goto done;
        }

        OSSL_PARAM_free(params);
    } else {
        OSSL_PARAM params[3];
        params[0] = OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME, (char *)"prime256v1", 0);
        params[1] = OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY, pub, sizeof(pub));
        params[2] = OSSL_PARAM_construct_end();

        if (EVP_PKEY_fromdata(ctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) != 1) {
            EVP_PKEY_CTX_free(ctx);
            goto done;
        }
    }

    EVP_PKEY_CTX_free(ctx);

done:
    FREE_JSON(json_root);

    return pkey;
}

/**
 * Generates a new EC P-256 key pair.
 *
 * @return
 */
EVP_PKEY *crypto_generate_keys(void) {
    EVP_PKEY     *pkey = NULL;
    EVP_PKEY_CTX *ctx  = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);

    if (!ctx)
        return NULL;

    if (EVP_PKEY_keygen_init(ctx) <= 0)
        goto fail;

    /* P-256 == prime256v1 in OpenSSL */
    if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1) <= 0)
        goto fail;

    if (EVP_PKEY_CTX_set_ec_param_enc(ctx, OPENSSL_EC_NAMED_CURVE) <= 0)
        goto fail;

    if (EVP_PKEY_keygen(ctx, &pkey) <= 0)
        goto fail;

    EVP_PKEY_CTX_free(ctx);

    return pkey;

fail:
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return NULL;
}

static uint64_t unix_seconds_to_windows_100ns(uint64_t unix_seconds) {
    /* Windows epoch starts at 1601-01-01; Unix at 1970-01-01. */
    const uint64_t EPOCH_DIFF_SECONDS = 11644473600ULL;
    return (unix_seconds + EPOCH_DIFF_SECONDS) * 10000000ULL;
}

static void write_u32_be(uint8_t *dst, uint32_t v) {
    dst[0] = (uint8_t)((v >> 24) & 0xff);
    dst[1] = (uint8_t)((v >> 16) & 0xff);
    dst[2] = (uint8_t)((v >> 8) & 0xff);
    dst[3] = (uint8_t)(v & 0xff);
}

static void write_u64_be(uint8_t *dst, uint64_t v) {
    dst[0] = (uint8_t)((v >> 56) & 0xff);
    dst[1] = (uint8_t)((v >> 48) & 0xff);
    dst[2] = (uint8_t)((v >> 40) & 0xff);
    dst[3] = (uint8_t)((v >> 32) & 0xff);
    dst[4] = (uint8_t)((v >> 24) & 0xff);
    dst[5] = (uint8_t)((v >> 16) & 0xff);
    dst[6] = (uint8_t)((v >> 8) & 0xff);
    dst[7] = (uint8_t)(v & 0xff);
}

static bool parse_url_path_and_query(const char *url, const char **out_begin, size_t *out_len) {
    if (!out_begin || !out_len) {
        return false;
    }
    *out_begin = NULL;
    *out_len   = 0;

    if (!url) {
        return false;
    }

    /* Very small URL parser: find the first '/' after scheme://host[:port] */
    const char *p = strstr(url, "://");
    if (p)
        p += 3;
    else
        p = url;

    const char *slash = strchr(p, '/');
    if (!slash) {
        /* No path -> use "/" */
        *out_begin = "/";
        *out_len   = 1;

        return true;
    }

    *out_begin = slash;
    *out_len   = strlen(slash);

    return true;
}

static bool ecdsa_sign_p1363_sha256(const EVP_PKEY *pkey, const uint8_t *data, size_t data_len, uint8_t out_sig64[64]) {

    bool           ok       = false;
    EVP_MD_CTX    *mdctx    = NULL;
    EVP_PKEY_CTX  *pkey_ctx = NULL;
    unsigned char *der      = NULL;
    size_t         der_len  = 0;
    ECDSA_SIG     *sig      = NULL;
    const BIGNUM  *r        = NULL;
    const BIGNUM  *s        = NULL;

    if (!pkey || !data || !out_sig64)
        return false;

    mdctx = EVP_MD_CTX_new();
    if (!mdctx)
        goto done;

    if (EVP_DigestSignInit(mdctx, &pkey_ctx, EVP_sha256(), NULL, (EVP_PKEY *)pkey) != 1)
        goto done;

    if (EVP_DigestSignUpdate(mdctx, data, data_len) != 1)
        goto done;

    /* Get DER signature size, then signature */
    if (EVP_DigestSignFinal(mdctx, NULL, &der_len) != 1)
        goto done;

    der = (unsigned char *)bzalloc(der_len);

    if (!der)
        goto done;

    if (EVP_DigestSignFinal(mdctx, der, &der_len) != 1)
        goto done;

    const unsigned char *der_p = der;

    sig = d2i_ECDSA_SIG(NULL, &der_p, (long)der_len);
    if (!sig)
        goto done;

    ECDSA_SIG_get0(sig, &r, &s);

    if (!r || !s)
        goto done;

    /* Convert r,s into fixed 32-byte big-endian each (P-256). */
    memset(out_sig64, 0, 64);
    int rlen = BN_num_bytes(r);
    int slen = BN_num_bytes(s);

    if (rlen < 0 || rlen > 32 || slen < 0 || slen > 32)
        goto done;

    BN_bn2bin(r, out_sig64 + (32 - rlen));
    BN_bn2bin(s, out_sig64 + 32 + (32 - slen));

    ok = true;

done:
    if (sig)
        ECDSA_SIG_free(sig);
    if (der)
        bfree(der);
    if (mdctx)
        EVP_MD_CTX_free(mdctx);
    return ok;
}

/**
 * Creates a signature header for the given request parameters.
 *
 * @param private_key
 * @param url
 * @param authorization_token
 * @param payload
 * @param out_len
 * @return
 */
uint8_t *crypto_sign(const EVP_PKEY *private_key, const char *url, const char *authorization_token, const char *payload,
                     size_t *out_len) {

    if (out_len)
        *out_len = 0;

    if (!private_key || !url || !authorization_token || !payload || !out_len) {
        obs_log(LOG_ERROR, "Unable to create signature: invalid parameters");
        return NULL;
    }

    const uint32_t policy_version   = 1;
    const uint64_t unix_seconds     = (uint64_t)time(NULL);
    const uint64_t windows_ts_100ns = unix_seconds_to_windows_100ns(unix_seconds);

    const char *path_begin = NULL;
    size_t      path_len   = 0;

    if (!parse_url_path_and_query(url, &path_begin, &path_len)) {
        obs_log(LOG_ERROR, "Unable to create signature: unable to retrieve the URL's path");
        return NULL;
    }

    const char   method[]    = "POST";
    const size_t method_len  = 4;
    const size_t auth_len    = strlen(authorization_token);
    const size_t payload_len = strlen(payload);

    /* Buffer: u32 + u8 + u64 + u8 + method\0 + path\0 + auth\0 + payload\0 */
    const size_t buf_len = 4 + 1 + 8 + 1 + (method_len + 1) + (path_len + 1) + (auth_len + 1) + (payload_len + 1);
    uint8_t     *buf     = (uint8_t *)bzalloc(buf_len);

    if (!buf) {
        obs_log(LOG_ERROR, "Unable to create signature: unable to allocate memory for the buffer");
        return NULL;
    }

    size_t off = 0;
    write_u32_be(buf + off, policy_version);
    off += 4;
    buf[off++] = 0;
    write_u64_be(buf + off, windows_ts_100ns);
    off += 8;
    buf[off++] = 0;

    memcpy(buf + off, method, method_len);
    off += method_len;
    buf[off++] = 0;

    memcpy(buf + off, path_begin, path_len);
    off += path_len;
    buf[off++] = 0;

    memcpy(buf + off, authorization_token, auth_len);
    off += auth_len;
    buf[off++] = 0;

    memcpy(buf + off, payload, payload_len);
    off += payload_len;
    buf[off++] = 0;

    if (off != buf_len) {
        obs_log(LOG_ERROR, "Unable to create signature: the size of the buffer does not match the expectation");
        bfree(buf);
        return NULL;
    }

    /* Debug: print the buffer right before signing */
    char *hex = bzalloc(buf_len * 2 + 1);
    if (hex) {
        for (size_t i = 0; i < buf_len; i++) {
            sprintf(hex + i * 2, "%02x", buf[i]);
        }
        obs_log(LOG_DEBUG, "[sign] Buffer to sign (hex): %s\n", hex);
        obs_log(LOG_DEBUG, "[sign] Buffer to sign (length): %zu\n\n", buf_len);
        bfree(hex);
    }

    uint8_t sig64[64];
    if (!ecdsa_sign_p1363_sha256(private_key, buf, buf_len, sig64)) {
        obs_log(LOG_ERROR, "Unable to create signature: the signing of the buffer failed");
        bfree(buf);
        return NULL;
    }

    bfree(buf);

    /* Header: u32 + u64 + sig */
    const size_t header_len = 4 + 8 + sizeof(sig64);
    uint8_t     *header     = (uint8_t *)bzalloc(header_len);
    if (!header) {
        obs_log(LOG_ERROR, "Unable to create signature: unable to allocate memory for the header");
        return NULL;
    }

    write_u32_be(header, policy_version);
    write_u64_be(header + 4, windows_ts_100ns);
    memcpy(header + 12, sig64, sizeof(sig64));

    *out_len = header_len;

    return header;
}
