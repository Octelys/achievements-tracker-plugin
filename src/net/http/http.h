#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Tiny HTTP helpers (libcurl + OBS memory helpers).
 *
 * All returned strings are allocated with bzalloc/bstrdup and must be freed with bfree().
 */

char *http_post_form(const char *url, const char *postfields, long *out_http_code);

/* Perform an HTTP POST with optional extra headers (CRLF-separated). No Content-Type is set. */
char *http_post(const char *url, const char *body, const char *extra_headers, long *out_http_code);

/* Perform an HTTP POST (JSON) with optional extra headers (CRLF-separated). */
char *http_post_json(const char *url, const char *json_body, const char *extra_headers, long *out_http_code);

/* Perform an HTTP GET with optional extra headers (CRLF-separated). */
char *http_get(const char *url, const char *extra_headers, const char *postfields, long *out_http_code);

char *http_urlencode(const char *in);

#ifdef __cplusplus
}
#endif
