#include "uuid.h"

#ifdef _WIN32

void uuid_get_random(char random_uuid[37]) {
    UUID     uuid;
    RPC_CSTR uuid_str = NULL;

    /* UuidCreate should always succeed in normal environments; if it doesn't,
       return an empty string to keep callers safe. */
    if (UuidCreate(&uuid) != RPC_S_OK) {
        random_uuid[0] = '\0';
        return;
    }

    if (UuidToStringA(&uuid, &uuid_str) != RPC_S_OK || uuid_str == NULL) {
        random_uuid[0] = '\0';
        return;
    }

    /* uuid_str is a NUL-terminated ANSI string in the canonical 36-char form. */
    for (int i = 0; i < 37; i++) {
        random_uuid[i] = (char)uuid_str[i];
        if (uuid_str[i] == '\0') {
            break;
        }
    }

    RpcStringFreeA(&uuid_str);
}

#else

void uuid_get_random(char random_uuid[37]) {
    uuid_t uuid;
    uuid_generate_random(uuid);
    uuid_unparse_lower(uuid, random_uuid);
}

#endif
