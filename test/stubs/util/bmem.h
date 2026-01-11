#pragma once

#include <stddef.h>
#include <string.h>

void *bzalloc(size_t size);
void *bmalloc(size_t size);
void *brealloc(void *ptr, size_t size);
void  bfree(void *ptr);
char *bstrdup(const char *str);
