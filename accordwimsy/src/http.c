/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "http.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long timeout = 60;
void aw_http_timeout(long seconds) { timeout = seconds; }
static size_t body_of(char* data, size_t size, size_t n, void* to) {
    buf_addn(to, data, size * n);
    return size * n;
}
void aw_http(void* ctx, const char* method, const char* url, const char* auth, const char* body, AwAnswer* out) {
    CURL* curl = curl_easy_init();
    struct curl_slist* headers = NULL;
    Buf got = {0};
    char *line, error[CURL_ERROR_SIZE] = "";
    CURLcode done;
    long status = 0;
    (void)ctx;
    memset(out, 0, sizeof *out);
    if (!curl) {
        out->error = aw_strdup("libcurl could not start a request");
        return;
    }
    if (auth) {
        line = aw_format("Authorization: %s", auth);
        headers = curl_slist_append(headers, line);
        free(line);
    }
    headers = curl_slist_append(headers, "Accept: application/json");
    if (body) headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    if (body) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, AW_AGENT);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, body_of);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &got);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    done = curl_easy_perform(curl);
    if (done == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        out->status = (int)status;
        out->body = buf_take(&got);
    } else {
        out->error = aw_format("%s %s: %s", method, url, *error ? error : curl_easy_strerror(done));
        buf_free(&got);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}
/* a transfer's end into out: its status, or why none came (a status past 299 counts as one, the transfer said) */
static void ended(CURL* curl, CURLcode done, const char* what, const char* error, AwAnswer* out) {
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    out->status = (int)status;
    if (done != CURLE_OK)
        out->error = aw_format("%s: %s", what, *error ? error : curl_easy_strerror(done));
    else if (status >= 300)
        out->error = aw_format("%s: HTTP %ld", what, status);
}
/* what both transfers set: the address, Authorization, the User-Agent, the error buffer, and the floor of 1 KB a second
   over two minutes in place of a time limit */
static struct curl_slist* transfer(CURL* curl, const char* url, const char* auth, const char* accept, char* error) {
    struct curl_slist* headers = NULL;
    char* line;
    if (auth) {
        line = aw_format("Authorization: %s", auth);
        headers = curl_slist_append(headers, line);
        free(line);
    }
    line = aw_format("Accept: %s", accept);
    headers = curl_slist_append(headers, line);
    free(line);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, AW_AGENT);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 120L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    return headers;
}
void aw_http_download(void* ctx, const char* url, const char* auth, const char* path, AwAnswer* out) {
    CURL* curl = curl_easy_init();
    FILE* f = fopen(path, "wb");
    struct curl_slist* headers;
    char error[CURL_ERROR_SIZE] = "", *what = aw_format("GET %s", url);
    CURLcode done;
    (void)ctx;
    memset(out, 0, sizeof *out);
    if (!curl || !f) {
        out->error = aw_format("%s: %s", what, !f ? "its file cannot be written" : "libcurl could not start");
        if (f) fclose(f);
        if (curl) curl_easy_cleanup(curl);
        free(what);
        return;
    }
    headers = transfer(curl, url, auth, "application/octet-stream", error);
    /* the Authorization header goes to url's host alone: libcurl keeps a custom one off a redirect to another host
       (GitHub's to its file store) */
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
    done = curl_easy_perform(curl);
    if (fclose(f) && done == CURLE_OK) done = CURLE_WRITE_ERROR;
    ended(curl, done, what, error, out);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(what);
}
void aw_http_upload(void* ctx, const char* url, const char* auth, const char* path, const char* name, AwAnswer* out) {
    CURL* curl = curl_easy_init();
    curl_mime* form;
    curl_mimepart* part;
    struct curl_slist* headers;
    Buf got = {0};
    char error[CURL_ERROR_SIZE] = "", *what = aw_format("POST %s", url);
    CURLcode done;
    (void)ctx;
    memset(out, 0, sizeof *out);
    if (!curl) {
        out->error = aw_format("%s: libcurl could not start", what);
        free(what);
        return;
    }
    headers = transfer(curl, url, auth, "application/json", error);
    form = curl_mime_init(curl);
    part = curl_mime_addpart(form);
    curl_mime_name(part, "attachment");
    curl_mime_type(part, "application/octet-stream");
    curl_mime_filename(part, name);
    done = curl_mime_filedata(part, path);
    if (done == CURLE_OK) {
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, body_of);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &got);
        done = curl_easy_perform(curl);
    }
    if (done == CURLE_OK) {
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        out->status = (int)status;
        out->body = buf_take(&got);
    } else {
        out->error = aw_format("%s: %s", what, *error ? error : curl_easy_strerror(done));
        buf_free(&got);
    }
    curl_mime_free(form);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(what);
}
