#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief POST application/x-www-form-urlencoded data.
 *
 * @param url           Target URL.
 * @param post_fields   Form-encoded request body (e.g. "a=1&b=2").
 * @param out_http_code Optional output for HTTP status code.
 *
 * @return Response body as a NUL-terminated string (caller must bfree()), or
 *         NULL on libcurl errors.
 */
char *http_post_form(const char *url, const char *post_fields, long *out_http_code);

/**
 * @brief POST a raw request body with optional extra headers.
 *
 * @param url           Target URL.
 * @param body          Request body string passed to libcurl (may be NULL).
 * @param extra_headers Optional additional headers, one per line (LF or CRLF),
 *                      as expected by libcurl's curl_slist (e.g.
 *                      "Authorization: Bearer ...\nX-Foo: bar\n").
 * @param out_http_code Optional output for HTTP status code.
 *
 * @return Response body as a NUL-terminated string (caller must bfree()), or
 *         NULL on libcurl errors.
 */
char *http_post(const char *url, const char *body, const char *extra_headers, long *out_http_code);

/**
 * @brief POST a JSON request body with optional extra headers.
 *
 * This sets "Content-Type: application/json" automatically.
 *
 * @param url           Target URL.
 * @param json_body     JSON request body.
 * @param extra_headers Optional additional headers, one per line (LF or CRLF).
 * @param out_http_code Optional output for HTTP status code.
 *
 * @return Response body as a NUL-terminated string (caller must bfree()), or
 *         NULL on libcurl errors.
 */
char *http_post_json(const char *url, const char *json_body, const char *extra_headers, long *out_http_code);

/**
 * @brief Perform an HTTP GET request with optional headers.
 *
 * @param url           Target URL.
 * @param extra_headers Optional additional headers, one per line (LF or CRLF).
 * @param post_fields   Optional body passed to libcurl via CURLOPT_POSTFIELDS.
 *                      Note: providing a body on GET requests is non-standard
 *                      but is supported by libcurl.
 * @param out_http_code Optional output for HTTP status code.
 *
 * @return Response body as a NUL-terminated string (caller must bfree()), or
 *         NULL on libcurl errors.
 */
char *http_get(const char *url, const char *extra_headers, const char *post_fields, long *out_http_code);

/**
 * @brief Download a resource into a raw byte buffer.
 *
 * The returned buffer is allocated with OBS' allocator and must be freed with
 * bfree().
 *
 * @param url      Resource URL.
 * @param out_data Receives a newly allocated buffer containing the downloaded bytes.
 * @param out_size Receives @p out_data size in bytes.
 *
 * @return true on success, false on failure.
 */
bool http_download(const char *url, uint8_t **out_data, size_t *out_size);

/**
 * @brief URL-encode a string (percent-encoding).
 *
 * Uses libcurl's escaping rules.
 *
 * @param in Input string.
 * @return Newly allocated encoded string (caller must bfree()), or NULL on error.
 */
char *http_urlencode(const char *in);

#ifdef __cplusplus
}
#endif
