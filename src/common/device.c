#include "device.h"

#include "memory.h"

#include <openssl/evp.h>

void free_device(device_t **device) {

    if (!device || !*device) {
        return;
    }

    if ((*device)->keys) {
        EVP_PKEY_free((EVP_PKEY *)(*device)->keys);
    }

    free_memory((void **)&device);
}
