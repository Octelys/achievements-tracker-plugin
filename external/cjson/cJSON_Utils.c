/*
  cJSON_Utils.c

  NOTE: This is a lightweight vendored subset of upstream cJSON_Utils.
  It provides the symbols needed when a project expects to link against
  cJSON_Utils.

  This file implements a small, practical subset:
  - JSON Pointer (RFC6901) traversal (GetPointer)

  If you need full RFC6902 patch generation/apply, replace this file with the
  complete upstream cJSON_Utils.c (same API).
*/

#include "cJSON_Utils.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------
 * JSON Pointer helpers (RFC6901)
 * ------------------------------
 */

static int is_array(const cJSON *item)
{
    return item && ((item->type & 0xFF) == cJSON_Array);
}

static int is_object(const cJSON *item)
{
    return item && ((item->type & 0xFF) == cJSON_Object);
}

static int token_equals(const char *a, size_t a_len, const char *b, int case_sensitive)
{
    if (!a || !b)
        return 0;

    size_t b_len = strlen(b);
    if (a_len != b_len)
        return 0;

    if (case_sensitive)
        return memcmp(a, b, a_len) == 0;

    for (size_t i = 0; i < a_len; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (tolower(ca) != tolower(cb))
            return 0;
    }

    return 1;
}

static int decode_pointer_token(const char *in, size_t in_len, char *out, size_t out_cap, size_t *out_len)
{
    /* JSON Pointer decoding: ~1 -> /, ~0 -> ~ */
    if (!out || out_cap == 0)
        return 0;

    size_t o = 0;
    for (size_t i = 0; i < in_len; i++) {
        char c = in[i];
        if (c == '~') {
            if (i + 1 >= in_len)
                return 0;
            char n = in[i + 1];
            if (n == '0') c = '~';
            else if (n == '1') c = '/';
            else return 0;
            i++;
        }
        if (o + 1 >= out_cap)
            return 0;
        out[o++] = c;
    }

    out[o] = '\0';
    if (out_len)
        *out_len = o;
    return 1;
}

static cJSON *get_object_child_by_token(cJSON *object, const char *token, size_t token_len, int case_sensitive)
{
    if (!is_object(object))
        return NULL;

    for (cJSON *child = object->child; child; child = child->next) {
        if (child->string && token_equals(token, token_len, child->string, case_sensitive))
            return child;
    }

    return NULL;
}

static int parse_array_index(const char *token, size_t token_len, int *out_index)
{
    if (!out_index || !token || token_len == 0)
        return 0;

    /* RFC6901: array index is a decimal number (no leading +, allow 0). */
    int idx = 0;
    for (size_t i = 0; i < token_len; i++) {
        unsigned char c = (unsigned char)token[i];
        if (c < '0' || c > '9')
            return 0;
        /* avoid overflow in a very conservative way */
        if (idx > (2147483647 / 10))
            return 0;
        idx = (idx * 10) + (int)(c - '0');
    }

    *out_index = idx;
    return 1;
}

static cJSON *get_array_child_by_index(cJSON *array, int index)
{
    if (!is_array(array) || index < 0)
        return NULL;
    return cJSON_GetArrayItem(array, index);
}

static cJSON *get_pointer_impl(cJSON *root, const char *pointer, int case_sensitive)
{
    if (!root || !pointer)
        return NULL;

    /* Per RFC6901: empty string points to the whole document. */
    if (pointer[0] == '\0')
        return root;

    /* Must start with '/' */
    if (pointer[0] != '/')
        return NULL;

    cJSON *current = root;
    const char *p = pointer;

    while (*p) {
        /* p points at '/' */
        if (*p != '/')
            return NULL;
        p++;

        const char *token_start = p;
        while (*p && *p != '/')
            p++;
        size_t token_len = (size_t)(p - token_start);

        /* decode token into a scratch buffer */
        char decoded[256];
        size_t decoded_len = 0;
        if (token_len >= sizeof(decoded))
            return NULL;
        if (!decode_pointer_token(token_start, token_len, decoded, sizeof(decoded), &decoded_len))
            return NULL;

        if (is_object(current)) {
            current = get_object_child_by_token(current, decoded, decoded_len, case_sensitive);
        } else if (is_array(current)) {
            int idx = 0;
            if (!parse_array_index(decoded, decoded_len, &idx))
                return NULL;
            current = get_array_child_by_index(current, idx);
        } else {
            return NULL;
        }

        if (!current)
            return NULL;

        /* if *p == '/', loop continues; if *p == '\0', end */
    }

    return current;
}

cJSON *cJSONUtils_GetPointer(cJSON *object, const char *pointer)
{
    return get_pointer_impl(object, pointer, 0);
}

const cJSON *cJSONUtils_GetPointerCaseSensitive(const cJSON *object, const char *pointer)
{
    return get_pointer_impl((cJSON *)object, pointer, 1);
}

/* ------------------------------
 * Patch stubs (not implemented)
 * ------------------------------
 */

cJSON *cJSONUtils_ApplyPatches(cJSON *object, const cJSON *patches)
{
    (void)object;
    (void)patches;
    return NULL;
}

cJSON *cJSONUtils_GeneratePatches(const cJSON *from, const cJSON *to)
{
    (void)from;
    (void)to;
    return NULL;
}
