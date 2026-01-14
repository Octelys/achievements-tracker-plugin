/*
  cJSON_Utils.h

  This is a vendored copy of the upstream cJSON_Utils helpers.

  Copyright (c) 2009-2017 Dave Gamble and cJSON contributors
  Licensed under the MIT license. See cJSON.h for details.
*/

#ifndef cJSON_Utils__h
#define cJSON_Utils__h

#ifdef __cplusplus
extern "C"
{
#endif

#include "cJSON.h"

/*
 * JSON Pointer (RFC6901)
 *
 * Example pointer: "/Person/Habits/0/Value"
 *  - object member access via token strings
 *  - array element access via numeric token (0-based)
 *
 * Returns a pointer to an existing node within `object` (no allocation),
 * or NULL if not found / invalid.
 */
cJSON *cJSONUtils_GetPointer(cJSON *object, const char *pointer);
const cJSON *cJSONUtils_GetPointerCaseSensitive(const cJSON *object, const char *pointer);

/*
 * Apply a JSON Patch (RFC6902) to `object`.
 * Returns a newly allocated JSON object (or NULL on error).
 */
cJSON *cJSONUtils_ApplyPatches(cJSON *object, const cJSON *patches);

/*
 * Generate a JSON Patch (RFC6902) from `from` to `to`.
 * Returns a newly allocated JSON array of patch operations.
 */
cJSON *cJSONUtils_GeneratePatches(const cJSON *from, const cJSON *to);

#ifdef __cplusplus
}
#endif

#endif
