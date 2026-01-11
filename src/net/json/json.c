#include "net/json/json.h"

#include <obs-module.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

long *json_get_long_value(const char *json, const char *key) {
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
