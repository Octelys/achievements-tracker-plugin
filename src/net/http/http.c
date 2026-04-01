#include "net/http/http.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include <curl/curl.h>
#include <string.h>

#define VERBOSE 0L
#define DEFAULT_USER_AGENT "achievements-tracker-obs-plugin/1.0"

/**
 * @brief Growable NUL-terminated character buffer used for HTTP response bodies.
 */
struct http_buffer {
    char  *ptr;
    size_t len;
};

/**
 * @brief Growable byte buffer used for binary downloads.
 */
struct image_buffer {
    uint8_t *data;
    size_t   size;
    size_t   capacity;
};

/**
 * @brief libcurl write callback that appends received bytes into a growable buffer.
 *
 * The callback assumes @p userp points to a struct with a `ptr` member that is
 * managed via OBS' allocators (brealloc/bfree). The buffer is always kept
 * NUL-terminated so it can be treated as a C string.
 *
 * @return Number of bytes taken. Returning 0 signals an out-of-memory condition
 *         to libcurl.
 */
static size_t curl_write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t              realsize = size * nmemb;
    struct http_buffer *mem      = userp;

    char *p = brealloc(mem->ptr, mem->len + realsize + 1);

    if (!p)
        return 0;

    mem->ptr = p;
    memcpy(&(mem->ptr[mem->len]), contents, realsize);
    mem->len += realsize;
    mem->ptr[mem->len] = 0;
    return realsize;
}

/**
 * @brief libcurl write callback for binary data (image downloads).
 *
 * Appends received bytes into a growable image_buffer. Unlike curl_write_cb,
 * this does not NUL-terminate because it handles raw binary data.
 *
 * @return Number of bytes taken. Returning 0 signals an out-of-memory condition
 *         to libcurl.
 */
static size_t curl_write_image_cb(void *contents, size_t size, size_t nmemb, void *userp) {

    size_t               realsize = size * nmemb;
    struct image_buffer *buf      = userp;

    /* Ensure capacity */
    if (buf->size + realsize > buf->capacity) {
        size_t new_capacity = buf->capacity == 0 ? 4096 : buf->capacity * 2;
        while (new_capacity < buf->size + realsize) {
            new_capacity *= 2;
        }

        uint8_t *new_data = brealloc(buf->data, new_capacity);
        if (!new_data) {
            return 0; /* Out of memory */
        }

        buf->data     = new_data;
        buf->capacity = new_capacity;
    }

    memcpy(buf->data + buf->size, contents, realsize);
    buf->size += realsize;

    return realsize;
}

char *http_post_form(const char *url, const char *post_fields, long *out_http_code) {
    if (out_http_code)
        *out_http_code = 0;

    CURL *curl = curl_easy_init();

    if (!curl)
        return NULL;

    struct http_buffer chunk = {0};
    chunk.ptr                = bzalloc(1);
    chunk.len                = 0;

    struct curl_slist *headers = NULL;
    headers                    = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, DEFAULT_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        obs_log(LOG_WARNING, "curl POST form failed: %s", curl_easy_strerror(res));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        bfree(chunk.ptr);
        return NULL;
    }

    long http_code = 0;

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (out_http_code)
        *out_http_code = http_code;

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return chunk.ptr;
}

char *http_post(const char *url, const char *body, const char *extra_headers, long *out_http_code) {
    if (out_http_code)
        *out_http_code = 0;

    CURL *curl = curl_easy_init();

    if (!curl)
        return NULL;

    struct http_buffer chunk = {0};
    chunk.ptr                = bzalloc(1);
    chunk.len                = 0;

    struct curl_slist *headers = NULL;

    /* Avoid 100-continue edge cases that can hide error bodies on some proxies
     */
    headers = curl_slist_append(headers, "Expect:");

    if (extra_headers && *extra_headers) {
        /* extra_headers contains one header per line (CRLF or LF). */
        char *dup = bstrdup(extra_headers);

        for (char *line = dup; line && *line;) {
            char *next = strpbrk(line, "\r\n");

            if (next) {
                *next   = '\0';
                /* Skip consecutive line breaks */
                char *p = next + 1;
                while (*p == '\r' || *p == '\n')
                    p++;
                next = p;
            } else {
                next = NULL;
            }

            if (*line)
                headers = curl_slist_append(headers, line);
            line = next;
        }
        bfree(dup);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, DEFAULT_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        obs_log(LOG_WARNING, "curl POST failed: %s", curl_easy_strerror(res));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        bfree(chunk.ptr);
        return NULL;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (out_http_code)
        *out_http_code = http_code;

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return chunk.ptr;
}

char *http_post_json(const char *url, const char *json_body, const char *extra_headers, long *out_http_code) {
    if (out_http_code)
        *out_http_code = 0;

    CURL *curl = curl_easy_init();

    if (!curl)
        return NULL;

    struct http_buffer chunk = {0};
    chunk.ptr                = bzalloc(1);
    chunk.len                = 0;

    struct curl_slist *headers = NULL;
    headers                    = curl_slist_append(headers, "Content-Type: application/json");

    /* Avoid 100-continue edge cases that can hide error bodies on some proxies
     */
    headers = curl_slist_append(headers, "Expect:");

    if (extra_headers && *extra_headers) {
        /* extra_headers contains one header per line (CRLF or LF). */
        char *dup = bstrdup(extra_headers);

        for (char *line = dup; line && *line;) {
            char *next = strpbrk(line, "\r\n");

            if (next) {
                *next   = '\0';
                /* Skip consecutive line breaks */
                char *p = next + 1;
                while (*p == '\r' || *p == '\n')
                    p++;
                next = p;
            } else {
                next = NULL;
            }

            if (*line)
                headers = curl_slist_append(headers, line);
            line = next;
        }
        bfree(dup);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, DEFAULT_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        obs_log(LOG_WARNING, "curl POST json failed: %s", curl_easy_strerror(res));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        bfree(chunk.ptr);
        return NULL;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (out_http_code)
        *out_http_code = http_code;

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return chunk.ptr;
}

char *http_get(const char *url, const char *extra_headers, const char *post_fields, long *out_http_code) {

    if (out_http_code)
        *out_http_code = 0;

    CURL *curl = curl_easy_init();

    if (!curl)
        return NULL;

    char *ptr = NULL;

    struct http_buffer chunk = {0};
    chunk.ptr                = bzalloc(1);
    chunk.len                = 0;

    struct curl_slist *headers = NULL;

    /* Avoid 100-continue edge cases that can hide error bodies on some proxies
     */
    headers = curl_slist_append(headers, "Expect:");

    if (extra_headers && *extra_headers) {
        /* extra_headers contains one header per line (CRLF or LF). */
        char *dup = bstrdup(extra_headers);

        for (char *line = dup; line && *line;) {

            char *next = strpbrk(line, "\r\n");

            if (next) {
                *next   = '\0';
                /* Skip consecutive line breaks */
                char *p = next + 1;
                while (*p == '\r' || *p == '\n')
                    p++;
                next = p;
            } else {
                next = NULL;
            }

            if (*line)
                headers = curl_slist_append(headers, line);
            line = next;
        }
        bfree(dup);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, DEFAULT_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);

    if (post_fields)
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        obs_log(LOG_WARNING, "curl GET failed: %s", curl_easy_strerror(res));
        bfree(chunk.ptr);
        chunk.ptr = NULL;
        goto cleanup;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (out_http_code)
        *out_http_code = http_code;

    ptr = chunk.ptr;

cleanup:
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return ptr;
}

char *http_urlencode(const char *in) {

    if (!in)
        return NULL;

    CURL *curl = curl_easy_init();

    if (!curl)
        return NULL;

    char *tmp = curl_easy_escape(curl, in, 0);
    char *out = tmp ? bstrdup(tmp) : NULL;

    if (tmp)
        curl_free(tmp);

    curl_easy_cleanup(curl);

    return out;
}

static bool http_is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool http_is_unreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
           c == '_' || c == '~';
}

static void http_append_pct(char *out, size_t *pos, unsigned char c) {
    static const char hex[] = "0123456789ABCDEF";
    out[(*pos)++]           = '%';
    out[(*pos)++]           = hex[(c >> 4) & 0x0F];
    out[(*pos)++]           = hex[c & 0x0F];
}

char *http_encode_url(const char *url) {

    if (!url)
        return NULL;

    const char *scheme_end = strstr(url, "://");
    if (!scheme_end)
        return bstrdup(url);

    const char *host_start     = scheme_end + 3;
    const char *path_start     = strchr(host_start, '/');
    const char *query_start    = strchr(host_start, '?');
    const char *fragment_start = strchr(host_start, '#');
    const char *first_delim    = NULL;

    if (path_start)
        first_delim = path_start;
    if (query_start && (!first_delim || query_start < first_delim))
        first_delim = query_start;
    if (fragment_start && (!first_delim || fragment_start < first_delim))
        first_delim = fragment_start;

    if (!first_delim)
        return bstrdup(url);

    size_t prefix_len = (size_t)(first_delim - url);

    CURL *curl = curl_easy_init();
    if (!curl)
        return NULL;

    /* Worst case: every remaining byte is percent-encoded. */
    size_t suffix_len = strlen(first_delim);
    size_t buf_size   = prefix_len + suffix_len * 3 + 1;
    char  *out        = bzalloc(buf_size);
    if (!out) {
        curl_easy_cleanup(curl);
        return NULL;
    }

    /* Copy scheme + authority verbatim. */
    memcpy(out, url, prefix_len);
    size_t pos = prefix_len;

    const char *p = first_delim;

    /* Path: preserve '/', preserve existing %XX escapes, encode the rest of
     * unsafe bytes. Stop at '?' or '#'. */
    while (*p && *p != '?' && *p != '#') {
        unsigned char c = (unsigned char)*p;

        if (c == '/') {
            out[pos++] = '/';
            p++;
        } else if (c == '%' && http_is_hex_digit(p[1]) && http_is_hex_digit(p[2])) {
            out[pos++] = p[0];
            out[pos++] = p[1];
            out[pos++] = p[2];
            p += 3;
        } else if (http_is_unreserved(c)) {
            out[pos++] = (char)c;
            p++;
        } else {
            http_append_pct(out, &pos, c);
            p++;
        }
    }

    /* Query: preserve '?', '&', '=', and existing %XX escapes. Encode only
     * unsafe bytes like spaces. Stop at '#'. */
    if (*p == '?') {
        out[pos++] = *p++;
        while (*p && *p != '#') {
            unsigned char c = (unsigned char)*p;

            if (c == '%' && http_is_hex_digit(p[1]) && http_is_hex_digit(p[2])) {
                out[pos++] = p[0];
                out[pos++] = p[1];
                out[pos++] = p[2];
                p += 3;
            } else if (http_is_unreserved(c) || c == '&' || c == '=' || c == ';' || c == ':' || c == ',' || c == '+' ||
                       c == '/' || c == '@' || c == '?' || c == '-') {
                out[pos++] = (char)c;
                p++;
            } else {
                http_append_pct(out, &pos, c);
                p++;
            }
        }
    }

    /* Fragment: preserve '#' and existing %XX escapes; encode only unsafe
     * bytes. */
    if (*p == '#') {
        out[pos++] = *p++;
        while (*p) {
            unsigned char c = (unsigned char)*p;

            if (c == '%' && http_is_hex_digit(p[1]) && http_is_hex_digit(p[2])) {
                out[pos++] = p[0];
                out[pos++] = p[1];
                out[pos++] = p[2];
                p += 3;
            } else if (http_is_unreserved(c) || c == '/' || c == '?' || c == '&' || c == '=' || c == '-' || c == '.') {
                out[pos++] = (char)c;
                p++;
            } else {
                http_append_pct(out, &pos, c);
                p++;
            }
        }
    }

    out[pos] = '\0';
    curl_easy_cleanup(curl);

    return out;
}

bool http_download(const char *url, uint8_t **out_data, size_t *out_size) {

    if (!url || !out_data || !out_size)
        return false;

    *out_data = NULL;
    *out_size = 0;

    CURL *curl = curl_easy_init();

    if (!curl) {
        obs_log(LOG_ERROR, "Failed to init curl for download");
        return false;
    }

    struct image_buffer buf = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_image_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, DEFAULT_USER_AGENT);

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        obs_log(LOG_ERROR, "Download failed: %s", curl_easy_strerror(res));
        bfree(buf.data);
        return false;
    }

    if (http_code < 200 || http_code >= 300) {
        obs_log(LOG_ERROR, "Download failed: server returned HTTP %ld for '%s'", http_code, url);
        bfree(buf.data);
        return false;
    }

    *out_data = buf.data;
    *out_size = buf.size;

    return true;
}
