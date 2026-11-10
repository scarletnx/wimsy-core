/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* HTTP(S) through libcurl, as aw.h's AwFetch: GitHub's API and the Forgejo's */
#ifndef AW_HTTP_H
#define AW_HTTP_H
#include "aw.h"

/* an AwFetch; ctx unused. Each request is given 60 seconds */
void aw_http(void* ctx, const char* method, const char* url, const char* auth, const char* body, AwAnswer* out);
/* an AwDownload and an AwUpload; ctx unused. Neither has a time limit: each stops when less than 1 KB a second has
   gone for two minutes */
void aw_http_download(void* ctx, const char* url, const char* auth, const char* path, AwAnswer* out);
void aw_http_upload(void* ctx, const char* url, const char* auth, const char* path, const char* name, AwAnswer* out);
#endif
