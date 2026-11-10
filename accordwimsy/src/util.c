/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "aw.h"
#include <openssl/evp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void grow(Buf* b, size_t more) {
    size_t cap = b->cap ? b->cap : 64;
    char* text;
    while (cap < b->len + more + 1) cap *= 2;
    if (cap == b->cap) return;
    text = realloc(b->text, cap);
    if (!text) {
        fprintf(stderr, "accordwimsy: out of memory for %zu bytes\n", cap);
        exit(1);
    }
    b->text = text;
    b->cap = cap;
}
void buf_addn(Buf* b, const char* s, size_t n) {
    grow(b, n);
    memcpy(b->text + b->len, s, n);
    b->len += n;
    b->text[b->len] = 0;
}
void buf_add(Buf* b, const char* s) { buf_addn(b, s, strlen(s)); }
void buf_printf(Buf* b, const char* fmt, ...) {
    va_list a;
    int n;
    va_start(a, fmt);
    n = vsnprintf(NULL, 0, fmt, a);
    va_end(a);
    if (n < 0) return;
    grow(b, (size_t)n);
    va_start(a, fmt);
    vsnprintf(b->text + b->len, (size_t)n + 1, fmt, a);
    va_end(a);
    b->len += (size_t)n;
}
void buf_free(Buf* b) {
    free(b->text);
    b->text = NULL;
    b->len = b->cap = 0;
}
char* buf_take(Buf* b) {
    char* text = b->text ? b->text : aw_strdup("");
    b->text = NULL;
    b->len = b->cap = 0;
    return text;
}
char* aw_strdup(const char* s) {
    Buf b = {0};
    buf_add(&b, s ? s : "");
    return buf_take(&b);
}
char* aw_format(const char* fmt, ...) {
    va_list a;
    Buf b = {0};
    int n;
    va_start(a, fmt);
    n = vsnprintf(NULL, 0, fmt, a);
    va_end(a);
    if (n < 0) return aw_strdup("");
    grow(&b, (size_t)n);
    va_start(a, fmt);
    vsnprintf(b.text, (size_t)n + 1, fmt, a);
    va_end(a);
    b.len = (size_t)n;
    return buf_take(&b);
}
char* aw_base64(const char* s) {
    static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const unsigned char* p = (const unsigned char*)s;
    size_t n = strlen(s), i;
    Buf b = {0};
    for (i = 0; i + 2 < n; i += 3) {
        char four[4] = {digits[p[i] >> 2], digits[((p[i] & 3) << 4) | (p[i + 1] >> 4)],
                        digits[((p[i + 1] & 15) << 2) | (p[i + 2] >> 6)], digits[p[i + 2] & 63]};
        buf_addn(&b, four, 4);
    }
    if (n - i == 1) {
        char four[4] = {digits[p[i] >> 2], digits[(p[i] & 3) << 4], '=', '='};
        buf_addn(&b, four, 4);
    } else if (n - i == 2) {
        char four[4] = {digits[p[i] >> 2], digits[((p[i] & 3) << 4) | (p[i + 1] >> 4)], digits[(p[i + 1] & 15) << 2],
                        '='};
        buf_addn(&b, four, 4);
    }
    return buf_take(&b);
}
const char* jstr(const cJSON* o, const char* key) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}
double jnum(const cJSON* o, const char* key) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : 0;
}
int jbool(const cJSON* o, const char* key) { return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, key)); }
static void say(FILE* to, const char* fmt, va_list a) {
    fputs("accordwimsy: ", to);
    vfprintf(to, fmt, a);
    fputc('\n', to);
    fflush(to);
}
void aw_log(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    say(stdout, fmt, a);
    va_end(a);
}
void aw_warn(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    say(stderr, fmt, a);
    va_end(a);
}
void aw_answer_free(AwAnswer* a) {
    free(a->body);
    free(a->error);
    a->body = a->error = NULL;
}
int aw_sha256_file(const char* path, char hex[65]) {
    static const char digits[] = "0123456789abcdef";
    FILE* f = fopen(path, "rb");
    EVP_MD_CTX* md = f ? EVP_MD_CTX_new() : NULL;
    unsigned char chunk[65536], digest[EVP_MAX_MD_SIZE];
    unsigned int n = 0, i;
    size_t got;
    int ok = md && EVP_DigestInit_ex(md, EVP_sha256(), NULL);
    while (ok && (got = fread(chunk, 1, sizeof chunk, f)) > 0) ok = EVP_DigestUpdate(md, chunk, got);
    ok = ok && !ferror(f) && EVP_DigestFinal_ex(md, digest, &n) && n == 32;
    for (i = 0; ok && i < n; ++i) {
        hex[2 * i] = digits[digest[i] >> 4];
        hex[2 * i + 1] = digits[digest[i] & 15];
    }
    if (ok) hex[64] = 0;
    EVP_MD_CTX_free(md);
    if (f) fclose(f);
    return ok ? 0 : -1;
}
