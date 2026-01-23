#pragma once

#include <obs-module.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FREE(p)	                \
if (p) {			            \
    void *pointer = (void*)p;   \
    bfree(pointer);              \
}

static void free_memory(void **ptr) {

    if (!ptr || !*ptr) {
        return;
    }

    bfree(*ptr);
    *ptr = NULL;
}

#ifdef __cplusplus
}
#endif
