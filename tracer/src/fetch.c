/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "fetch.h"
#include <curl/curl.h>
#include <stdlib.h>

typedef struct {
    Buf* out;
    size_t max;
} Into;

static size_t body_of(char* data, size_t size, size_t n, void* to) {
    Into* into = to;
    if (size * n > into->max - into->out->len) return 0; /* over the limit: libcurl stops with a write error */
    buf_addn(into->out, data, size * n);
    return size * n;
}

int tr_fetch(void* ctx, const char* url, size_t max, Buf* out, char** why) {
    CURL* curl = curl_easy_init();
    char error[CURL_ERROR_SIZE] = "";
    Into into;
    CURLcode done;
    long status = 0;
    (void)ctx;
    buf_free(out);
    *why = NULL;
    if (!curl) {
        *why = rei_strdup("libcurl could not start a request");
        return -1;
    }
    into.out = out;
    into.max = max;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, REI_AGENT);
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)max);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, body_of);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &into);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L); /* a timeout in a thread, without SIGALRM */
    done = curl_easy_perform(curl);
    if (done == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    if (done == CURLE_OK && status == 200) return 0;
    if (done != CURLE_OK)
        *why = rei_format("%s: %s", url, *error ? error : curl_easy_strerror(done));
    else
        *why = rei_format("%s: HTTP %ld", url, status);
    buf_free(out);
    return -1;
}
