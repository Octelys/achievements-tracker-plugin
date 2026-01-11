#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/pem.h>
#include <openssl/core_names.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

#include <obs-module.h>
#include <diagnostics/log.h>

void crypto_debug_print_keypair(EVP_PKEY *pkey) {
	if (!pkey) {
		obs_log(4, "[xbl] failed to export EC key pair for debug: pkey is NULL");
		return;
	}

	BIO *bio = BIO_new(BIO_s_mem());
	if (!bio) {
		obs_log(4, "[xbl] failed to export EC key pair for debug: BIO_new failed");
		return;
	}

	/* Export public key (SPKI format) */
	obs_log(4, "=== XboxTokenManager ProofOfPossession Key (PUBLIC, PEM) ===");
	if (PEM_write_bio_PUBKEY(bio, pkey)) {
		char *pem_data = NULL;
		long pem_len = BIO_get_mem_data(bio, &pem_data);
		if (pem_len > 0 && pem_data) {
			char *pub_pem = bzalloc(pem_len + 1);
			memcpy(pub_pem, pem_data, pem_len);
			pub_pem[pem_len] = '\0';
			printf("%s", pub_pem);
			bfree(pub_pem);
		}
	} else {
		obs_log(4, "[xbl] failed to export public key");
	}

	/* Reset BIO for reuse */
	BIO_reset(bio);

	/* Export private key (PKCS8 format) */
	obs_log(4, "=== XboxTokenManager ProofOfPossession Key (PRIVATE, PEM) ===");
	if (PEM_write_bio_PrivateKey(bio, pkey, NULL, NULL, 0, NULL, NULL)) {
		char *pem_data = NULL;
		long pem_len = BIO_get_mem_data(bio, &pem_data);
		if (pem_len > 0 && pem_data) {
			char *priv_pem = bzalloc(pem_len + 1);
			memcpy(priv_pem, pem_data, pem_len);
			priv_pem[pem_len] = '\0';
			printf("%s", priv_pem);
			bfree(priv_pem);
		}
	} else {
		obs_log(4, "[xbl] failed to export private key");
	}

	BIO_free(bio);
}

static char *b64url_encode(const unsigned char *in, size_t inlen) {
	static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

	size_t outlen = (inlen + 2) / 3 * 4;
	char *out = (char *)malloc(outlen + 1);

	if (!out)
		return NULL;

	size_t i = 0, o = 0;

	while (i + 2 < inlen) {
		unsigned v = (unsigned)in[i] << 16 | (unsigned)in[i + 1] << 8 | (unsigned)in[i + 2];
		out[o++] = tbl[(v >> 18) & 63];
		out[o++] = tbl[(v >> 12) & 63];
		out[o++] = tbl[(v >> 6) & 63];
		out[o++] = tbl[v & 63];
		i += 3;
	}

	if (i < inlen) {
		unsigned v = (unsigned)in[i] << 16;
		out[o++] = tbl[(v >> 18) & 63];

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

/* Extract uncompressed EC public point (04 || X || Y) from an EVP_PKEY using OpenSSL 3 params API. */
static int get_ec_public_point_uncompressed(EVP_PKEY *pkey, unsigned char *out, size_t out_size, size_t *out_len) {
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

char *crypto_key_to_string(EVP_PKEY *pkey) {
	unsigned char point[1 + 32 + 32];
	size_t point_len = 0;
	char *x64 = NULL;
	char *y64 = NULL;
	char *returned_key_json = NULL;

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

	char key_json[4096];
	snprintf(key_json, sizeof(key_json),
			 "{\"kty\":\"EC\",\"x\":\"%s\",\"y\":\"%s\",\"crv\":\"P-256\",\"alg\":\"ES256\",\"use\":\"sig\"}", x64,
			 y64);

	returned_key_json = bstrdup(key_json);

done:
	free(x64);
	free(y64);
	return returned_key_json;
}

/* Generate an EC P-256 keypair. Returns NULL on failure. */
EVP_PKEY *crypto_generate_p256_keypair(void) {
	EVP_PKEY *pkey = NULL;
	EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);

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

	crypto_debug_print_keypair(pkey);

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
	*out_len = 0;

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
		*out_len = 1;
		return true;
	}

	*out_begin = slash;
	*out_len = strlen(slash);

	return true;
}

static bool ecdsa_sign_p1363_sha256(EVP_PKEY *pkey, const uint8_t *data, size_t data_len, uint8_t out_sig64[64]) {

	bool ok = false;
	EVP_MD_CTX *mdctx = NULL;
	EVP_PKEY_CTX *pkey_ctx = NULL;
	unsigned char *der = NULL;
	size_t der_len = 0;
	ECDSA_SIG *sig = NULL;
	const BIGNUM *r = NULL;
	const BIGNUM *s = NULL;

	if (!pkey || !data || !out_sig64)
		return false;

	mdctx = EVP_MD_CTX_new();
	if (!mdctx)
		goto done;

	if (EVP_DigestSignInit(mdctx, &pkey_ctx, EVP_sha256(), NULL, pkey) != 1)
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

uint8_t *crypto_sign_policy_header(
	EVP_PKEY *private_key, const char *url, const char *authorization_token, const char *payload, size_t *out_len) {

	if (out_len)
		*out_len = 0;

	if (!private_key || !url || !authorization_token || !payload || !out_len)
		return NULL;

	const uint32_t policy_version = 1;
	const uint64_t unix_seconds = (uint64_t)time(NULL);
	const uint64_t windows_ts_100ns = unix_seconds_to_windows_100ns(unix_seconds);

	const char *path_begin = NULL;
	size_t path_len = 0;

	if (!parse_url_path_and_query(url, &path_begin, &path_len))
		return NULL;

	const char method[] = "POST";
	const size_t method_len = 4;
	const size_t auth_len = strlen(authorization_token);
	const size_t payload_len = strlen(payload);

	/* Buffer: u32 + u8 + u64 + u8 + method\0 + path\0 + auth\0 + payload\0 */
	const size_t buf_len = 4 + 1 + 8 + 1 + (method_len + 1) + (path_len + 1) + (auth_len + 1) + (payload_len + 1);
	uint8_t *buf = (uint8_t *)bzalloc(buf_len);

	if (!buf)
		return NULL;

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
		bfree(buf);
		return NULL;
	}

	/* Debug: print the buffer right before signing */
	char *hex = bzalloc(buf_len * 2 + 1);
	if (hex) {
		for (size_t i = 0; i < buf_len; i++) {
			sprintf(hex + i * 2, "%02x", buf[i]);
		}
		printf("[sign] Buffer to sign (hex): %s\n", hex);
		printf("[sign] Buffer to sign (length): %zu\n\n", buf_len);
		bfree(hex);
	}

	uint8_t sig64[64];
	if (!ecdsa_sign_p1363_sha256(private_key, buf, buf_len, sig64)) {
		bfree(buf);
		return NULL;
	}

	bfree(buf);

	/* Header: u32 + u64 + sig */
	const size_t header_len = 4 + 8 + sizeof(sig64);
	uint8_t *header = (uint8_t *)bzalloc(header_len);
	if (!header)
		return NULL;

	write_u32_be(header, policy_version);
	write_u64_be(header + 4, windows_ts_100ns);
	memcpy(header + 12, sig64, sizeof(sig64));

	*out_len = header_len;

	return header;
}


