#pragma once

#include <stdbool.h>
#include <openssl/evp.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FREE(p)	\
if (p)			\
    bfree(p);

#define COPY_OR_FREE(src, dst)	\
if (dst)					    \
    *dst = src;				    \
else						    \
    FREE(src);

typedef struct device {
    /* unique identifier for the device */
    const char     *uuid;
    const char     *serial_number;
    /* proof of ownership key pair */
    const EVP_PKEY *keys;
} device_t;

typedef struct token {
    const char *value;
    /* unix timestamp */
    int64_t     expires;
} token_t;

typedef struct xbox_live_authenticate_result {
    const char *error_message;
} xbox_live_authenticate_result_t;

typedef struct xbox_identity {
    const char    *gamertag;
    const char    *xid;
    const char    *uhs;
    const token_t *token;
} xbox_identity_t;

typedef struct game {
    const char *id;
    const char *title;
} game_t;

bool is_token_expired(const token_t *token);

#ifdef __cplusplus
}
#endif
