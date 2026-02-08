#include "net/json/json.h"

#include <obs-module.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @file json.c
 * @brief Implementation of lightweight JSON string extraction helpers.
 *
 * These functions intentionally avoid bringing in a full JSON DOM parser for a
 * few narrow extraction cases (reading known fields from API responses).
 *
 * @warning This is not a full JSON parser. Limitations include:
 *  - No array support.
 *  - Only very simple key lookup and object traversal.
 *  - No decoding of JSON escape sequences in returned strings.
 *  - First-match semantics when a key appears multiple times.
 *
 * Ownership:
 *  - All returned strings / values are allocated with OBS' allocator and must be
 *    freed with bfree().
 */

char *json_read_string(const char *json, const char *key) {
    if (!json || !key)
        return NULL;

    char needle[256];
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    const char *p = strstr(json, needle);

    if (!p)
        return NULL;

    p = strchr(p + strlen(needle), ':');

    if (!p)
        return NULL;

    p++;

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;

    if (*p != '"')
        return NULL;
    p++;

    const char *start = p;

    while (*p && *p != '"')
        p++;

    if (*p != '"')
        return NULL;

    size_t len = (size_t)(p - start);

    char *out = bzalloc(len + 1);

    memcpy(out, start, len);

    out[len] = '\0';

    return out;
}

long *json_read_long(const char *json, const char *key) {
    if (!json || !key)
        return NULL;

    char needle[256];
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    const char *p = strstr(json, needle);

    if (!p)
        return NULL;

    p = strchr(p + strlen(needle), ':');

    if (!p)
        return NULL;

    p++; /* after ':' */

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;

    /* Reject quoted values (string) */
    if (*p == '"')
        return NULL;

    char *end = NULL;
    long  v   = strtol(p, &end, 10);

    /* No digits parsed */
    if (end == p)
        return NULL;

    long *out_value = bzalloc(sizeof(long));
    *out_value      = v;

    return out_value;
}

/**
 * @brief Duplicate a character range into a newly allocated NUL-terminated string.
 *
 * @param start First character in the range (inclusive).
 * @param end   One past the last character in the range (exclusive).
 * @return Newly allocated string (caller must bfree()), or NULL on invalid range.
 */
static char *dup_range(const char *start, const char *end) {
    if (!start || !end || end < start)
        return NULL;

    size_t len = (size_t)(end - start);
    char  *out = bzalloc(len + 1);
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

/**
 * @brief Extract an object value (as a JSON substring) for a given key.
 *
 * Returns a substring that starts at '{' and ends right after the matching '}'.
 * Handles nested objects and skips over quoted strings so braces inside strings
 * don't affect depth counting.
 *
 * @param json JSON text.
 * @param key  Object key to extract.
 * @return Newly allocated JSON substring (caller must bfree()), or NULL on failure.
 */
static char *json_read_object_subjson(const char *json, const char *key) {
    if (!json || !key)
        return NULL;

    const size_t needle_cap = strlen(key) + 3; /* quotes + NUL */
    char        *needle     = bzalloc(needle_cap);
    if (!needle)
        return NULL;

    snprintf(needle, needle_cap, "\"%s\"", key);

    const char *p = strstr(json, needle);
    if (!p) {
        bfree(needle);
        return NULL;
    }

    p = strchr(p + strlen(needle), ':');
    if (!p) {
        bfree(needle);
        return NULL;
    }

    bfree(needle);

    p++; /* after ':' */
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;

    if (*p != '{')
        return NULL;

    const char *start = p;

    int depth = 0;
    while (*p) {
        if (*p == '"') {
            /* Skip quoted strings so braces inside strings don't count */
            p++;
            while (*p) {
                if (*p == '\\' && p[1]) {
                    p += 2;
                    continue;
                }
                if (*p == '"') {
                    p++;
                    break;
                }
                p++;
            }
            continue;
        }

        if (*p == '{')
            depth++;
        else if (*p == '}')
            depth--;

        p++;

        if (depth == 0) {
            const char *end = p; /* one past '}' */
            return dup_range(start, end);
        }
    }

    return NULL;
}

char *json_read_string_from_path(const char *json, const char *path) {
    if (!json || !path || !*path)
        return NULL;

    const char *seg_start = path;
    const char *seg_end   = path;

    char *current_json = NULL; /* allocated when we descend */
    char *result       = NULL;

    while (1) {
        while (*seg_end && *seg_end != '.')
            seg_end++;

        if (seg_end == seg_start)
            goto cleanup;

        char   key[256];
        size_t klen = (size_t)(seg_end - seg_start);
        if (klen >= sizeof(key))
            goto cleanup;

        memcpy(key, seg_start, klen);
        key[klen] = '\0';

        const char *use_json = current_json ? current_json : json;

        if (*seg_end == '\0') {
            result = json_read_string(use_json, key);
            goto cleanup;
        } else {
            char *next_json = json_read_object_subjson(use_json, key);
            if (!next_json)
                goto cleanup;

            if (current_json)
                bfree(current_json);
            current_json = next_json;

            seg_end++; /* skip '.' */
            seg_start = seg_end;
        }
    }

cleanup:
    if (current_json)
        bfree(current_json);

    return result;
}
