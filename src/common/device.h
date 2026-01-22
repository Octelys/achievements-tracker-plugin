#pragma once

#include <openssl/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct device {
    /* unique identifier for the device */
    const char     *uuid;
    const char     *serial_number;
    /* proof of ownership key pair */
    const EVP_PKEY *keys;
} device_t;

#ifdef __cplusplus
}
#endif
