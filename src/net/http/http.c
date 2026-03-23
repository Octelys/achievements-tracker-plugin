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

char *http_encode_url(const char *url) {

    if (!url)
        return NULL;

    /* Find the start of the path component.
     * We look for "://" and then the next '/' after the host. */
    const char *scheme_end = strstr(url, "://");
    if (!scheme_end)
        return bstrdup(url);

    const char *host_start = scheme_end + 3;
    const char *path_start = strchr(host_start, '/');
    if (!path_start)
        return bstrdup(url); /* No path — nothing to encode */

    size_t prefix_len = (size_t)(path_start - url);

    CURL *curl = curl_easy_init();
    if (!curl)
        return NULL;

    /* Estimate output size: worst case every byte becomes %XX (×3).
     * Add prefix length + NUL. */
    size_t path_len = strlen(path_start);
    size_t buf_size = prefix_len + path_len * 3 + 1;
    char  *out      = bzalloc(buf_size);
    if (!out) {
        curl_easy_cleanup(curl);
        return NULL;
    }

    /* Copy scheme + host verbatim */
    memcpy(out, url, prefix_len);
    size_t pos = prefix_len;

    /* Encode path segment-by-segment, preserving '/' separators. */
    const char *p = path_start;
    while (*p) {
        if (*p == '/') {
            out[pos++] = '/';
            p++;
            continue;
        }

        /* Collect one segment (until next '/' or end) */
        const char *seg_start = p;
        while (*p && *p != '/')
            p++;

        size_t seg_len = (size_t)(p - seg_start);
        char  *segment = bzalloc(seg_len + 1);
        memcpy(segment, seg_start, seg_len);
        segment[seg_len] = '\0';

        char *encoded = curl_easy_escape(curl, segment, (int)seg_len);
        bfree(segment);

        if (encoded) {
            size_t enc_len = strlen(encoded);
            memcpy(out + pos, encoded, enc_len);
            pos += enc_len;
            curl_free(encoded);
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

    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        obs_log(LOG_ERROR, "Download failed: %s", curl_easy_strerror(res));
        bfree(buf.data);
        return false;
    }

    *out_data = buf.data;
    *out_size = buf.size;

    return true;
}
