#pragma once

#include <stddef.h>

void *bzalloc(size_t size);
void bfree(void *ptr);
