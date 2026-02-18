#pragma once

#include <openssl/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Device identity and proof-of-ownership material.
 *
 * This struct groups together identifiers for the local device (UUID and serial
 * number) and an OpenSSL keypair used as a proof-of-ownership credential.
 *
 * Ownership/lifetime:
 * - This header does not define constructors/destructors for @c device_t.
 *   Callers are responsible for managing the lifetime of the pointed-to strings
 *   and the OpenSSL key object.
 * - @c keys is an OpenSSL @c EVP_PKEY pointer. Whether it is borrowed or owned
 *   depends on the code that populates the struct. If you take ownership,
 *   release it with @c EVP_PKEY_free().
 */
typedef struct device {
    /** Unique identifier for the device (typically a UUID string). */
    char     *uuid;
    /** Device serial number string. */
    char     *serial_number;
    /** Proof-of-ownership key pair. */
    EVP_PKEY *keys;
} device_t;

/**
 * @brief Free a device structure and its contents.
 *
 * Frees the EVP_PKEY and the device_t structure itself.
 * Safe to call with NULL.
 *
 * @param device Device structure to free. Set to NULL after freeing.
 */
void free_device(device_t **device);

#ifdef __cplusplus
}
#endif
