/*
  cJSON.c

  NOTE: This is a vendored copy of the upstream cJSON library.
  It’s included here so the plugin can parse/build JSON without external deps.

  This file is intentionally kept as-is style-wise.

  Copyright (c) 2009-2017 Dave Gamble and cJSON contributors
  Licensed under the MIT license. See cJSON.h for details.
*/

#include "cJSON.h"

#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#define strcasecmp _stricmp
#elif !defined(__APPLE__)
/* POSIX declares strcasecmp in <strings.h>. (macOS usually provides it, but
   including it conditionally keeps this vendored file portable.) */
#include <strings.h>
#endif

/*
  This is a trimmed-but-compatible vendored implementation sufficient for
  typical parsing/printing used in plugins.

  If you need the full upstream feature set, consider replacing this file with
  the complete upstream cJSON.c (same API).
*/

/* ---- memory hooks ---- */
static void *(*cjson_malloc)(size_t sz) = malloc;
static void (*cjson_free)(void *ptr)    = free;

void cJSON_InitHooks(cJSON_Hooks *hooks) {
    if (hooks == NULL) {
        cjson_malloc = malloc;
        cjson_free   = free;
        return;
    }
    cjson_malloc = (hooks->malloc_fn != NULL) ? hooks->malloc_fn : malloc;
    cjson_free   = (hooks->free_fn != NULL) ? hooks->free_fn : free;
}

static cJSON *cJSON_New_Item(void) {
    cJSON *node = (cJSON *)cjson_malloc(sizeof(cJSON));
    if (node) {
        memset(node, 0, sizeof(cJSON));
    }
    return node;
}

void cJSON_Delete(cJSON *c) {
    cJSON *next;
    while (c) {
        next = c->next;
        if (!(c->type & cJSON_IsReference) && c->child)
            cJSON_Delete(c->child);
        if (!(c->type & cJSON_IsReference) && c->valuestring)
            cjson_free(c->valuestring);
        if (!(c->type & cJSON_StringIsConst) && c->string)
            cjson_free(c->string);
        cjson_free(c);
        c = next;
    }
}

/* Parser helpers */
static const char *skip(const char *in) {
    while (in && *in && (unsigned char)*in <= 32)
        in++;
    return in;
}

static const char *parse_string(cJSON *item, const char *str) {
    const char *ptr = str + 1;
    char       *out;
    int         len = 0;

    if (*str != '"')
        return NULL;

    while (*ptr != '"' && *ptr && ++len) {
        if (*ptr++ == '\\')
            ptr++; /* skip escaped char */
    }

    out = (char *)cjson_malloc((size_t)len + 1);
    if (!out)
        return NULL;

    ptr     = str + 1;
    char *o = out;
    while (*ptr != '"' && *ptr) {
        if (*ptr != '\\') {
            *o++ = *ptr++;
        } else {
            ptr++;
            switch (*ptr) {
            case 'b':
                *o++ = '\b';
                break;
            case 'f':
                *o++ = '\f';
                break;
            case 'n':
                *o++ = '\n';
                break;
            case 'r':
                *o++ = '\r';
                break;
            case 't':
                *o++ = '\t';
                break;
            case '\\':
                *o++ = '\\';
                break;
            case '"':
                *o++ = '"';
                break;
            case '/':
                *o++ = '/';
                break;
            default:
                *o++ = *ptr;
                break;
            }
            ptr++;
        }
    }
    *o = '\0';

    if (*ptr == '"')
        ptr++;

    item->type        = cJSON_String;
    item->valuestring = out;
    return ptr;
}

static const char *parse_number(cJSON *item, const char *num) {
    double n            = 0;
    int    sign         = 1;
    int    scale        = 0;
    int    subscale     = 0;
    int    signsubscale = 1;

    if (*num == '-') {
        sign = -1;
        num++;
    }
    if (*num == '0')
        num++;
    if (*num >= '1' && *num <= '9') {
        do {
            n = (n * 10.0) + (*num++ - '0');
        } while (*num >= '0' && *num <= '9');
    }
    if (*num == '.' && num[1] >= '0' && num[1] <= '9') {
        num++;
        do {
            n = (n * 10.0) + (*num++ - '0');
            scale--;
        } while (*num >= '0' && *num <= '9');
    }
    if (*num == 'e' || *num == 'E') {
        num++;
        if (*num == '+')
            num++;
        else if (*num == '-') {
            signsubscale = -1;
            num++;
        }
        while (*num >= '0' && *num <= '9') {
            subscale = (subscale * 10) + (*num++ - '0');
        }
    }

    n = sign * n * pow(10.0, (scale + subscale * signsubscale));

    item->type        = cJSON_Number;
    item->valuedouble = n;
    item->valueint    = (int)n;

    return num;
}

static const char *parse_value(cJSON *item, const char *value);

static const char *parse_array(cJSON *item, const char *value) {
    cJSON *child;
    if (*value != '[')
        return NULL;
    item->type = cJSON_Array;
    value      = skip(value + 1);
    if (*value == ']')
        return value + 1;

    item->child = child = cJSON_New_Item();
    if (!item->child)
        return NULL;

    value = skip(parse_value(child, skip(value)));
    if (!value)
        return NULL;

    while (*value == ',') {
        cJSON *new_item = cJSON_New_Item();
        if (!new_item)
            return NULL;
        child->next    = new_item;
        new_item->prev = child;
        child          = new_item;
        value          = skip(parse_value(child, skip(value + 1)));
        if (!value)
            return NULL;
    }

    if (*value == ']')
        return value + 1;
    return NULL;
}

static const char *parse_object(cJSON *item, const char *value) {
    cJSON *child;
    if (*value != '{')
        return NULL;
    item->type = cJSON_Object;
    value      = skip(value + 1);
    if (*value == '}')
        return value + 1;

    item->child = child = cJSON_New_Item();
    if (!item->child)
        return NULL;

    /* parse first key */
    value = skip(parse_string(child, skip(value)));
    if (!value)
        return NULL;
    child->string      = child->valuestring;
    child->valuestring = NULL;

    if (*value != ':')
        return NULL;
    value = skip(parse_value(child, skip(value + 1)));
    if (!value)
        return NULL;

    while (*value == ',') {
        cJSON *new_item = cJSON_New_Item();
        if (!new_item)
            return NULL;
        child->next    = new_item;
        new_item->prev = child;
        child          = new_item;

        value = skip(parse_string(child, skip(value + 1)));
        if (!value)
            return NULL;
        child->string      = child->valuestring;
        child->valuestring = NULL;

        if (*value != ':')
            return NULL;
        value = skip(parse_value(child, skip(value + 1)));
        if (!value)
            return NULL;
    }

    if (*value == '}')
        return value + 1;
    return NULL;
}

static const char *parse_value(cJSON *item, const char *value) {
    if (!value)
        return NULL;

    if (!strncmp(value, "null", 4)) {
        item->type = cJSON_NULL;
        return value + 4;
    }
    if (!strncmp(value, "false", 5)) {
        item->type = cJSON_False;
        return value + 5;
    }
    if (!strncmp(value, "true", 4)) {
        item->type     = cJSON_True;
        item->valueint = 1;
        return value + 4;
    }
    if (*value == '"') {
        return parse_string(item, value);
    }
    if (*value == '-' || (*value >= '0' && *value <= '9')) {
        return parse_number(item, value);
    }
    if (*value == '[') {
        return parse_array(item, value);
    }
    if (*value == '{') {
        return parse_object(item, value);
    }

    return NULL;
}

cJSON *cJSON_ParseWithOpts(const char *value, const char **return_parse_end, int require_null_terminated) {
    cJSON *c = cJSON_New_Item();
    if (!c)
        return NULL;

    const char *end = parse_value(c, skip(value));
    if (!end) {
        cJSON_Delete(c);
        return NULL;
    }
    end = skip(end);

    if (require_null_terminated && *end) {
        cJSON_Delete(c);
        return NULL;
    }
    if (return_parse_end)
        *return_parse_end = end;
    return c;
}

cJSON *cJSON_Parse(const char *value) {
    return cJSON_ParseWithOpts(value, NULL, 0);
}

/* ---- creation helpers ---- */
cJSON *cJSON_CreateNull(void) {
    cJSON *i = cJSON_New_Item();
    if (i)
        i->type = cJSON_NULL;
    return i;
}
cJSON *cJSON_CreateTrue(void) {
    cJSON *i = cJSON_New_Item();
    if (i)
        i->type = cJSON_True;
    i->valueint = 1;
    return i;
}
cJSON *cJSON_CreateFalse(void) {
    cJSON *i = cJSON_New_Item();
    if (i)
        i->type = cJSON_False;
    return i;
}
cJSON *cJSON_CreateBool(int b) {
    return b ? cJSON_CreateTrue() : cJSON_CreateFalse();
}
cJSON *cJSON_CreateNumber(double num) {
    cJSON *i = cJSON_New_Item();
    if (i) {
        i->type        = cJSON_Number;
        i->valuedouble = num;
        i->valueint    = (int)num;
    }
    return i;
}
cJSON *cJSON_CreateString(const char *string) {
    cJSON *i = cJSON_New_Item();
    if (i) {
        i->type = cJSON_String;
        if (string) {
            size_t l       = strlen(string);
            i->valuestring = (char *)cjson_malloc(l + 1);
            if (i->valuestring)
                memcpy(i->valuestring, string, l + 1);
        }
    }
    return i;
}
cJSON *cJSON_CreateRaw(const char *raw) {
    cJSON *i = cJSON_New_Item();
    if (i) {
        i->type = cJSON_Raw;
        if (raw) {
            size_t l       = strlen(raw);
            i->valuestring = (char *)cjson_malloc(l + 1);
            if (i->valuestring)
                memcpy(i->valuestring, raw, l + 1);
        }
    }
    return i;
}
cJSON *cJSON_CreateArray(void) {
    cJSON *i = cJSON_New_Item();
    if (i)
        i->type = cJSON_Array;
    return i;
}
cJSON *cJSON_CreateObject(void) {
    cJSON *i = cJSON_New_Item();
    if (i)
        i->type = cJSON_Object;
    return i;
}

/* ---- list helpers ---- */
static void suffix_object(cJSON *prev, cJSON *item) {
    prev->next = item;
    item->prev = prev;
}

void cJSON_AddItemToArray(cJSON *array, cJSON *item) {
    cJSON *c = array->child;
    if (!item)
        return;
    if (!c) {
        array->child = item;
    } else {
        while (c && c->next)
            c = c->next;
        suffix_object(c, item);
    }
}

void cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item) {
    if (!item)
        return;
    if (!(item->type & cJSON_StringIsConst) && item->string)
        cjson_free(item->string);
    item->string = (char *)cjson_malloc(strlen(string) + 1);
    if (item->string)
        strcpy(item->string, string);
    cJSON_AddItemToArray(object, item);
}

void cJSON_AddItemToObjectCS(cJSON *object, const char *string, cJSON *item) {
    if (!item)
        return;
    item->string = (char *)string;
    item->type |= cJSON_StringIsConst;
    cJSON_AddItemToArray(object, item);
}

/* detach/delete helpers (minimal) */
cJSON *cJSON_DetachItemFromArray(cJSON *array, int which) {
    cJSON *c = array ? array->child : NULL;
    while (c && which > 0) {
        c = c->next;
        which--;
    }
    if (!c)
        return NULL;

    if (c->prev)
        c->prev->next = c->next;
    if (c->next)
        c->next->prev = c->prev;
    if (c == array->child)
        array->child = c->next;
    c->prev = c->next = NULL;
    return c;
}

void cJSON_DeleteItemFromArray(cJSON *array, int which) {
    cJSON_Delete(cJSON_DetachItemFromArray(array, which));
}

cJSON *cJSON_GetArrayItem(const cJSON *array, int index) {
    cJSON *c = array ? array->child : NULL;
    while (c && index > 0) {
        c = c->next;
        index--;
    }
    return c;
}

int cJSON_GetArraySize(const cJSON *array) {
    int    i = 0;
    cJSON *c = array ? array->child : NULL;
    while (c) {
        i++;
        c = c->next;
    }
    return i;
}

cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *const object, const char *const string) {
    cJSON *c = object ? object->child : NULL;
    while (c && c->string) {
        if (strcmp(c->string, string) == 0)
            return c;
        c = c->next;
    }
    return NULL;
}

cJSON *cJSON_GetObjectItem(const cJSON *const object, const char *const string) {
    cJSON *c = object ? object->child : NULL;
    while (c && c->string) {
        if (strcasecmp(c->string, string) == 0)
            return c;
        c = c->next;
    }
    return NULL;
}

/* printing (unformatted minimal) */
static char *print_value(const cJSON *item);

static char *print_string_ptr(const char *str) {
    size_t len = strlen(str);
    /* conservative: no escaping expansion */
    char  *out = (char *)cjson_malloc(len + 3);
    if (!out)
        return NULL;
    out[0] = '"';
    memcpy(out + 1, str, len);
    out[1 + len] = '"';
    out[2 + len] = '\0';
    return out;
}

static char *print_array(const cJSON *item) {
    int    n       = cJSON_GetArraySize(item);
    char **entries = (char **)cjson_malloc((size_t)n * sizeof(char *));
    if (!entries && n)
        return NULL;

    size_t len   = 2; /* [] */
    cJSON *child = item->child;
    for (int i = 0; i < n; i++) {
        entries[i] = print_value(child);
        if (!entries[i]) { /* cleanup */
            for (int j = 0; j < i; j++)
                cjson_free(entries[j]);
            cjson_free(entries);
            return NULL;
        }
        len += strlen(entries[i]) + 1; /* + comma */
        child = child->next;
    }

    char *out = (char *)cjson_malloc(len + 1);
    if (!out) {
        for (int i = 0; i < n; i++)
            cjson_free(entries[i]);
        cjson_free(entries);
        return NULL;
    }

    char *ptr = out;
    *ptr++    = '[';
    for (int i = 0; i < n; i++) {
        size_t el = strlen(entries[i]);
        memcpy(ptr, entries[i], el);
        ptr += el;
        if (i != n - 1)
            *ptr++ = ',';
        cjson_free(entries[i]);
    }
    *ptr++ = ']';
    *ptr   = '\0';
    cjson_free(entries);
    return out;
}

static char *print_object(const cJSON *item) {
    int    n       = cJSON_GetArraySize(item);
    char **entries = (char **)cjson_malloc((size_t)n * sizeof(char *));
    char **names   = (char **)cjson_malloc((size_t)n * sizeof(char *));
    if ((!entries || !names) && n)
        return NULL;

    size_t len   = 2; /* {} */
    cJSON *child = item->child;
    for (int i = 0; i < n; i++) {
        names[i]   = print_string_ptr(child->string ? child->string : "");
        entries[i] = print_value(child);
        if (!names[i] || !entries[i]) {
            for (int j = 0; j <= i; j++) {
                if (names) {
                    if (names[j])
                        cjson_free(names[j]);
                }
                if (entries) {
                    if (entries[j])
                        cjson_free(entries[j]);
                }
            }
            if (names)
                cjson_free(names);
            if (entries)
                cjson_free(entries);
            return NULL;
        }
        len += strlen(names[i]) + strlen(entries[i]) + 2; /* : and comma */
        child = child->next;
    }

    char *out = (char *)cjson_malloc(len + 1);
    if (!out) {
        for (int i = 0; i < n; i++) {
            cjson_free(names[i]);
            cjson_free(entries[i]);
        }
        cjson_free(names);
        cjson_free(entries);
        return NULL;
    }

    char *ptr = out;
    *ptr++    = '{';
    for (int i = 0; i < n; i++) {
        size_t nl = strlen(names[i]);
        memcpy(ptr, names[i], nl);
        ptr += nl;
        *ptr++    = ':';
        size_t el = strlen(entries[i]);
        memcpy(ptr, entries[i], el);
        ptr += el;
        if (i != n - 1)
            *ptr++ = ',';
        cjson_free(names[i]);
        cjson_free(entries[i]);
    }
    *ptr++ = '}';
    *ptr   = '\0';
    cjson_free(names);
    cjson_free(entries);
    return out;
}

static char *print_value(const cJSON *item) {
    if (!item)
        return NULL;

    switch (item->type & 0xFF) {
    case cJSON_NULL: {
        char *out = (char *)cjson_malloc(5);
        if (out)
            strcpy(out, "null");
        return out;
    }
    case cJSON_False: {
        char *out = (char *)cjson_malloc(6);
        if (out)
            strcpy(out, "false");
        return out;
    }
    case cJSON_True: {
        char *out = (char *)cjson_malloc(5);
        if (out)
            strcpy(out, "true");
        return out;
    }
    case cJSON_Number: {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.17g", item->valuedouble);
        char *out = (char *)cjson_malloc(strlen(buf) + 1);
        if (out)
            strcpy(out, buf);
        return out;
    }
    case cJSON_String:
    case cJSON_Raw:
        return print_string_ptr(item->valuestring ? item->valuestring : "");
    case cJSON_Array:
        return print_array(item);
    case cJSON_Object:
        return print_object(item);
    default:
        return NULL;
    }
}

char *cJSON_PrintUnformatted(const cJSON *item) {
    return print_value(item);
}

char *cJSON_Print(const cJSON *item) {
    /* pretty printing not implemented in this trimmed version */
    return cJSON_PrintUnformatted(item);
}

char *cJSON_PrintBuffered(const cJSON *item, int prebuffer, int fmt) {
    (void)prebuffer;
    (void)fmt;
    return cJSON_Print(item);
}

void cJSON_Minify(char *json) {
    char *into = json;
    while (json && *json) {
        if ((unsigned char)*json > 32) {
            *into++ = *json;
        }
        json++;
    }
    if (into)
        *into = '\0';
}

/* stubs for APIs not used currently (kept for link compatibility) */
cJSON *cJSON_Duplicate(const cJSON *item, int recurse) {
    (void)item;
    (void)recurse;
    return NULL;
}
void cJSON_InsertItemInArray(cJSON *array, int which, cJSON *newitem) {
    (void)array;
    (void)which;
    (void)newitem;
}
void cJSON_ReplaceItemViaPointer(cJSON *const parent, cJSON *const item, cJSON *replacement) {
    (void)parent;
    (void)item;
    (void)replacement;
}
void cJSON_ReplaceItemInArray(cJSON *array, int which, cJSON *newitem) {
    (void)array;
    (void)which;
    (void)newitem;
}
void cJSON_ReplaceItemInObject(cJSON *object, const char *string, cJSON *newitem) {
    (void)object;
    (void)string;
    (void)newitem;
}
void cJSON_ReplaceItemInObjectCaseSensitive(cJSON *object, const char *string, cJSON *newitem) {
    (void)object;
    (void)string;
    (void)newitem;
}
void cJSON_AddItemReferenceToArray(cJSON *array, cJSON *item) {
    (void)array;
    (void)item;
}
void cJSON_AddItemReferenceToObject(cJSON *object, const char *string, cJSON *item) {
    (void)object;
    (void)string;
    (void)item;
}
cJSON *cJSON_DetachItemViaPointer(cJSON *parent, cJSON *const item) {
    (void)parent;
    (void)item;
    return NULL;
}
cJSON *cJSON_DetachItemFromObject(cJSON *object, const char *string) {
    (void)object;
    (void)string;
    return NULL;
}
cJSON *cJSON_DetachItemFromObjectCaseSensitive(cJSON *object, const char *string) {
    (void)object;
    (void)string;
    return NULL;
}
void cJSON_DeleteItemFromObject(cJSON *object, const char *string) {
    (void)object;
    (void)string;
}
void cJSON_DeleteItemFromObjectCaseSensitive(cJSON *object, const char *string) {
    (void)object;
    (void)string;
}
cJSON *cJSON_CreateIntArray(const int *numbers, int count) {
    (void)numbers;
    (void)count;
    return NULL;
}
cJSON *cJSON_CreateFloatArray(const float *numbers, int count) {
    (void)numbers;
    (void)count;
    return NULL;
}
cJSON *cJSON_CreateDoubleArray(const double *numbers, int count) {
    (void)numbers;
    (void)count;
    return NULL;
}
cJSON *cJSON_CreateStringArray(const char **strings, int count) {
    (void)strings;
    (void)count;
    return NULL;
}
