/*
 * tests/stubs/bmem.h
 *
 * Stub header for OBS bmem functions for unit testing.
 */

#pragma once

#include <stddef.h>

void *bzalloc(size_t size);
void bfree(void *ptr);

