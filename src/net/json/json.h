#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file json.h
 * @brief Small JSON string helpers used by the plugin.
 *
 * These helpers perform very lightweight string-based extraction for a couple of
 * simple use-cases (mostly reading known string/number fields from API
 * responses). They are not full JSON parsers and have known limitations.
 *
 * Allocation/ownership:
 *  - Strings returned by this module are allocated with OBS' allocator and must
 *    be freed with bfree().
 *  - json_read_long() returns a heap-allocated long* that must be freed with
 *    bfree().
 */

/**
 * @brief Read a JSON string property by key.
 *
 * Expects a simple JSON fragment containing a key/value pair like:
 *   "key": "value"
 *
 * Limitations:
 *  - Only supports string values enclosed in double quotes.
 *  - Does not unescape JSON escape sequences (e.g. "\\n", "\\uXXXX").
 *  - Matches the first occurrence of "key" in the input text.
 *
 * @param json JSON text.
 * @param key  Property name to search.
 * @return Newly allocated string value (caller must bfree()), or NULL if the
 *         key is missing or not a string.
 */
char *json_read_string(const char *json, const char *key);

/**
 * @brief Read a JSON string property using a dotted path.
 *
 * The path is a dot-separated list of object keys, for example:
 *   "AuthorizationToken.Token"
 *
 * Each segment before the final one must resolve to an object value.
 * The final segment must resolve to a string value.
 *
 * Limitations:
 *  - Only supports object traversal (no arrays).
 *  - Only supports string leaf values.
 *  - Does not unescape JSON escape sequences.
 *
 * @param json JSON text.
 * @param path Dot-separated path (e.g. "a.b.c").
 * @return Newly allocated string value (caller must bfree()), or NULL if not found.
 */
char *json_read_string_from_path(const char *json, const char *path);

/**
 * @brief Read a JSON integer property by key.
 *
 * Expects a simple JSON fragment containing a key/value pair like:
 *   "key": 123
 *
 * @param json JSON text.
 * @param key  Property name to search.
 * @return Newly allocated long value (caller must bfree()), or NULL if the key
 *         is missing or the value is not an unquoted integer.
 */
long *json_read_long(const char *json, const char *key);

#ifdef __cplusplus
}
#endif
