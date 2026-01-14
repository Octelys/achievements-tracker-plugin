#include "net/http/http.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include <curl/curl.h>
#include <string.h>

#define DEFAULT_USER_AGENT "achievements-tracker-obs-plugin/1.0"

struct http_buffer {
    char  *ptr;
    size_t len;
};

struct image_buffer {
    uint8_t *data;
    size_t   size;
    size_t   capacity;
};

static size_t curl_write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t              realsize = size * nmemb;
    struct http_buffer *mem      = (struct http_buffer *)userp;

    char *p = brealloc(mem->ptr, mem->len + realsize + 1);

    if (!p)
        return 0;

    mem->ptr = p;
    memcpy(&(mem->ptr[mem->len]), contents, realsize);
    mem->len += realsize;
    mem->ptr[mem->len] = 0;
    return realsize;
}

static int curl_debug_cb(CURL *handle, curl_infotype type, char *data, size_t size, void *userptr) {
    (void)handle;
    (void)userptr;

    /* data is not guaranteed NUL-terminated */
    switch (type) {
    case CURLINFO_TEXT:
    case CURLINFO_HEADER_IN:
    case CURLINFO_HEADER_OUT:
        obs_log(LOG_DEBUG, "curl: %.*s", (int)size, data);
        break;
    default:
        /* CURLINFO_DATA_IN/OUT can be noisy; omit by default */
        break;
    }
    return 0;
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

    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, curl_debug_cb);
    curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);

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

    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, curl_debug_cb);
    curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);

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

    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, curl_debug_cb);
    curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);

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

    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, curl_debug_cb);
    curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);

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
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
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
