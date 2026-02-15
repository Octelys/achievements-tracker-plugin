#pragma once

#include "cJSON.h"

#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Frees an allocation using OBS's allocator.
 *
 * Convenience macro around @c bfree().
 *
 * Notes:
 * - This macro does NOT set @p p to NULL in the caller.
 * - It is safe to pass NULL.
 * - @p p must point to memory allocated with the matching OBS allocation
 *   functions (e.g., @c bzalloc(), @c bmalloc(), @c bstrdup()).
 *
 * Prefer @ref free_memory when you want the pointer to be nulled.
 */
#define FREE(p)                 \
if (p) {                        \
    void *pointer = (void*)p;   \
    bfree(pointer);             \
}

/**
 * @brief Frees an allocation and sets the caller's pointer to NULL.
 *
 * Safe to call with NULL or with @c *ptr == NULL.
 *
 * @param[in,out] ptr Address of the pointer to free.
 */
static void free_memory(void **ptr) {

    if (!ptr || !*ptr) {
        return;
    }

    bfree(*ptr);
    *ptr = NULL;
}

/**
 * @brief Frees an allocation and sets the caller's pointer to NULL.
 *
 * Safe to call with NULL or with @c *ptr == NULL.
 *
 * @param[in,out] ptr Address of the pointer to free.
 */
static void free_json_memory(void **ptr) {

    if (!ptr || !*ptr) {
        return;
    }

    cJSON_Delete(*ptr);
    *ptr = NULL;
}

#ifdef __cplusplus
}
#endif
