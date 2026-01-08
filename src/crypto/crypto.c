#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/pem.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#include <stdio.h>

#include <obs-module.h>
#include <diagnostics/log.h>

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

static int bn_to_fixed_32(const BIGNUM *bn, unsigned char out[32]) {
	if (!bn)
		return 0;

	memset(out, 0, 32);

	int n = BN_num_bytes(bn);

	if (n < 0 || n > 32)
		return 0;

	/* right-align (big-endian) */
	if (BN_bn2bin(bn, out + (32 - n)) != n)
		return 0;

	return 1;
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

bool crypto_print_public_jwk_es256(EVP_PKEY *pkey) {
	int ok = 0;
	unsigned char point[1 + 32 + 32];
	size_t point_len = 0;
	char *x64 = NULL;
	char *y64 = NULL;

	if (!pkey)
		return 0;

	if (!get_ec_public_point_uncompressed(pkey, point, sizeof(point), &point_len))
		goto done;

	/* Expect uncompressed point: 0x04 || X(32) || Y(32) for P-256 */
	if (point_len != sizeof(point) || point[0] != 0x04)
		goto done;

	x64 = b64url_encode(point + 1, 32);
	y64 = b64url_encode(point + 1 + 32, 32);

	if (!x64 || !y64)
		goto done;

	obs_log(LOG_WARNING, "Key");
	obs_log(LOG_WARNING,
			"{\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\"%s\",\"y\":\"%s\",\"alg\":\"ES256\",\"use\":\"sig\"}\n", x64,
			y64);

	ok = 1;

done:
	free(x64);
	free(y64);
	return ok;
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

	crypto_print_public_jwk_es256(pkey);

	return pkey;

fail:
	EVP_PKEY_CTX_free(ctx);
	EVP_PKEY_free(pkey);
	return NULL;
}

/* Example: write private/public PEM (similar to exporting in Node). */
static int write_pem_files(EVP_PKEY *pkey) {
	FILE *fp = fopen("ec_p256_private.pem", "wb");

	if (!fp)
		return 0;

	if (!PEM_write_PrivateKey(fp, pkey, NULL, NULL, 0, NULL, NULL)) {
		fclose(fp);
		return 0;
	}

	fclose(fp);

	fp = fopen("ec_p256_public.pem", "wb");

	if (!fp)
		return 0;

	if (!PEM_write_PUBKEY(fp, pkey)) {
		fclose(fp);
		return 0;
	}

	fclose(fp);

	return 1;
}

/*
int main(void)
{
    EVP_PKEY *pkey = crypto_generate_p256_keypair();
    if (!pkey) {
        fprintf(stderr, "Failed to generate P-256 keypair\n");
        return 1;
    }

    if (!write_pem_files(pkey)) {
        fprintf(stderr, "Failed to write PEM files\n");
        EVP_PKEY_free(pkey);
        return 1;
    }

    EVP_PKEY_free(pkey);
    return 0;
}
*/
