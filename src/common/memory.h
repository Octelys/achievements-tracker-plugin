#pragma once

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FREE(p)	                \
if (p) {			            \
    void *pointer = (void*)p;   \
    free(pointer);              \
}

static void free_memory(void **ptr) {

    if (!ptr || !*ptr) {
        return;
    }

    free(*ptr);
    *ptr = NULL;
}

#ifdef __cplusplus
}
#endif
