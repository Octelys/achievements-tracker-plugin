#include "net/http/http.h"

#include <obs-module.h>
#include <diagnostics/log.h>

#include <curl/curl.h>
#include <string.h>

struct http_buf {
	char *ptr;
	size_t len;
};

static size_t curl_write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t realsize = size * nmemb;
	struct http_buf *mem = (struct http_buf *)userp;

	char *p = brealloc(mem->ptr, mem->len + realsize + 1);
	if (!p)
		return 0;
	mem->ptr = p;
	memcpy(&(mem->ptr[mem->len]), contents, realsize);
	mem->len += realsize;
	mem->ptr[mem->len] = 0;
	return realsize;
}

char *http_post_form(const char *url, const char *postfields, long *out_http_code) {
	if (out_http_code)
		*out_http_code = 0;

	CURL *curl = curl_easy_init();

	if (!curl)
		return NULL;

	struct http_buf chunk = {0};
	chunk.ptr = bzalloc(1);
	chunk.len = 0;

	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postfields);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "achievements-tracker-obs-plugin/1.0");
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

char *http_post_json(const char *url, const char *json_body, const char *extra_header, long *out_http_code) {
	if (out_http_code)
		*out_http_code = 0;

	CURL *curl = curl_easy_init();

	if (!curl)
		return NULL;

	struct http_buf chunk = {0};
	chunk.ptr = bzalloc(1);
	chunk.len = 0;

	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/json");

	/* Avoid 100-continue edge cases that can hide error bodies on some proxies */
	headers = curl_slist_append(headers, "Expect:");

	if (extra_header && *extra_header)
		headers = curl_slist_append(headers, extra_header);

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "achievements-tracker-obs-plugin/1.0");
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

char *http_get(const char *url, const char *extra_headers, const char *postfields, long *out_http_code) {
	if (out_http_code)
		*out_http_code = 0;

	CURL *curl = curl_easy_init();

	if (!curl)
		return NULL;

	struct http_buf chunk = {0};
	chunk.ptr = bzalloc(1);
	chunk.len = 0;

	struct curl_slist *headers = NULL;

	/* Avoid 100-continue edge cases that can hide error bodies on some proxies */
	headers = curl_slist_append(headers, "Expect:");

	if (extra_headers && *extra_headers) {
		/* extra_headers contains one header per line (CRLF or LF). */
		char *dup = bstrdup(extra_headers);

		for (char *line = dup; line && *line;) {

			char *next = strpbrk(line, "\r\n");

			if (next) {
				*next = '\0';
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
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "achievements-tracker-obs-plugin/1.0");
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);

	if (postfields)
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postfields);

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
