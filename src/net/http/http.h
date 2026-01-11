#pragma once

#ifdef __cplusplus
extern "C" {
#endif

char *http_post_form(const char *url, const char *post_fields, long *out_http_code);

char *http_post(const char *url, const char *body, const char *extra_headers, long *out_http_code);

char *http_post_json(const char *url, const char *json_body, const char *extra_headers, long *out_http_code);

char *http_get(const char *url, const char *extra_headers, const char *post_fields, long *out_http_code);

char *http_urlencode(const char *in);

#ifdef __cplusplus
}
#endif
