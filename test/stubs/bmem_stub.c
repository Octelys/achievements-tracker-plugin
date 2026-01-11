#include <stdlib.h>
#include <string.h>

void *bzalloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr)
        memset(ptr, 0, size);
    return ptr;
}

void *bmalloc(size_t size) {
    return malloc(size);
}

void *brealloc(void *ptr, size_t size) {
    return realloc(ptr, size);
}

void bfree(void *ptr) {
    free(ptr);
}

char *bstrdup(const char *str) {
    if (!str)
        return NULL;
    size_t len = strlen(str) + 1;
    char  *dup = malloc(len);
    if (dup)
        memcpy(dup, str, len);
    return dup;
}
