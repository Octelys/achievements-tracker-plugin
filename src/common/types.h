#pragma once

#include <openssl/evp.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FREE(p)	\
if (p)			\
    bfree(p);

typedef struct device {
    /* unique identifier for the device */
    const char     *uuid;
    const char     *serial_number;
    /* proof of ownership key pair */
    const EVP_PKEY *keys;
} device_t;

typedef struct device_token {
    const char *token;
} device_token_t;

typedef struct xbox_live_authenticate_result {
    const char *error_message;
} xbox_live_authenticate_result_t;

#ifdef __cplusplus
}
#endif
