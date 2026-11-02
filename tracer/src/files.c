/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "files.h"
#include <archive.h>
#include <archive_entry.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int ends(const char* name, const char* ext) {
    size_t n = strlen(name), e = strlen(ext);
    return n > e && !strcasecmp(name + n - e, ext);
}

TrKind tr_kind(const char* name, const char* type) {
    static const char* const logs[] = {".log", ".txt", ".gz", ".zip"};
    static const char* const images[] = {".png", ".jpg", ".jpeg", ".webp", ".gif"};
    size_t i;
    for (i = 0; i < sizeof logs / sizeof *logs; ++i)
        if (ends(name, logs[i])) return TR_LOG;
    for (i = 0; i < sizeof images / sizeof *images; ++i)
        if (ends(name, images[i])) return TR_IMAGE;
    if (!strncmp(type, "image/", 6)) return TR_IMAGE;
    if (!strncmp(type, "text/plain", 10)) return TR_LOG;
    return TR_OTHER;
}

static unsigned long be32(const unsigned char* p) {
    return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}
static unsigned long le32(const unsigned char* p) {
    return (unsigned long)p[3] << 24 | (unsigned long)p[2] << 16 | (unsigned long)p[1] << 8 | p[0];
}

const char* tr_image_ext(const char* data, size_t n) {
    const unsigned char* d = (const unsigned char*)data;
    if (n >= 8 && !memcmp(d, "\x89PNG\r\n\x1a\n", 8)) return "png";
    if (n >= 3 && d[0] == 0xff && d[1] == 0xd8 && d[2] == 0xff) return "jpg";
    if (n >= 6 && (!memcmp(d, "GIF87a", 6) || !memcmp(d, "GIF89a", 6))) return "gif";
    if (n >= 12 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WEBP", 4)) return "webp";
    return NULL;
}

static size_t strip_png(unsigned char* d, size_t n) {
    static const char* const dropped[] = {"tEXt", "zTXt", "iTXt", "eXIf", "tIME"};
    size_t i = 8, out = 8, k;
    for (;;) {
        size_t len, total;
        int drop = 0, end;
        if (n - i < 12) return 0;
        len = be32(d + i);
        if (len > n - i - 12) return 0;
        total = 12 + len;
        for (k = 0; k < sizeof dropped / sizeof *dropped; ++k) drop |= !memcmp(d + i + 4, dropped[k], 4);
        end = !memcmp(d + i + 4, "IEND", 4);
        if (!drop) {
            memmove(d + out, d + i, total);
            out += total;
        }
        i += total;
        if (end) return out;
    }
}

static size_t strip_jpeg(unsigned char* d, size_t n) {
    size_t i = 2, out = 2;
    for (;;) {
        unsigned m;
        size_t len;
        if (n - i < 4 || d[i] != 0xff) return 0;
        m = d[i + 1];
        if (m == 0xff) { /* a fill byte */
            ++i;
            continue;
        }
        if (m == 0xda) { /* the scan, the image itself, to the end */
            memmove(d + out, d + i, n - i);
            return out + (n - i);
        }
        if (m == 0xd9) return 0; /* the image's end before its scan */
        if ((m >= 0xd0 && m <= 0xd7) || m == 0x01) {
            memmove(d + out, d + i, 2);
            out += 2;
            i += 2;
            continue;
        }
        len = (size_t)d[i + 2] << 8 | d[i + 3];
        if (len < 2 || len > n - i - 2) return 0;
        if (!((m >= 0xe1 && m <= 0xef && m != 0xe2 && m != 0xee) || m == 0xfe)) {
            memmove(d + out, d + i, 2 + len);
            out += 2 + len;
        }
        i += 2 + len;
    }
}

static size_t strip_webp(unsigned char* d, size_t n) {
    size_t end = 8 + (size_t)le32(d + 4), i = 12, out = 12, vp8x = 0;
    if (end > n || end < 12) return 0;
    while (i < end) {
        size_t size, padded;
        if (end - i < 8) return 0;
        size = le32(d + i + 4);
        padded = size + (size & 1);
        if (padded > end - i - 8) return 0;
        if (memcmp(d + i, "EXIF", 4) && memcmp(d + i, "XMP ", 4)) {
            memmove(d + out, d + i, 8 + padded);
            if (!memcmp(d + out, "VP8X", 4) && size) vp8x = out + 8;
            out += 8 + padded;
        }
        i += 8 + padded;
    }
    if (vp8x) d[vp8x] &= (unsigned char)~(0x08 | 0x04); /* its flags: no EXIF, no XMP */
    d[4] = (unsigned char)(out - 8);
    d[5] = (unsigned char)((out - 8) >> 8);
    d[6] = (unsigned char)((out - 8) >> 16);
    d[7] = (unsigned char)((out - 8) >> 24);
    return out;
}

size_t tr_strip_image(char* data, size_t n) {
    const char* ext = tr_image_ext(data, n);
    unsigned char* d = (unsigned char*)data;
    if (!ext) return 0;
    if (!strcmp(ext, "png")) return strip_png(d, n);
    if (!strcmp(ext, "jpg")) return strip_jpeg(d, n);
    if (!strcmp(ext, "webp")) return strip_webp(d, n);
    return n;
}

/* an archive of data read from memory, as a .zip or as a .gz of one file */
static struct archive* opened(int zip, const char* data, size_t n) {
    struct archive* a = archive_read_new();
    if (!a) return NULL;
    if (zip)
        archive_read_support_format_zip(a);
    else {
        archive_read_support_filter_gzip(a);
        archive_read_support_format_raw(a);
    }
    if (archive_read_open_memory(a, data, n) != ARCHIVE_OK) {
        archive_read_free(a);
        return NULL;
    }
    return a;
}

int tr_unpack(const char* name, const char* data, size_t n, size_t max, Buf* out, char** why) {
    int zip = ends(name, ".zip"), wanted = -1, at = 0;
    struct archive* a;
    struct archive_entry* e;
    char piece[65536];
    la_ssize_t got;
    buf_free(out);
    *why = NULL;
    if (!zip && !ends(name, ".gz")) {
        if (n > max) {
            *why = rei_format("%s is over %zu MiB", name, max >> 20);
            return -1;
        }
        buf_addn(out, data, n);
        return 0;
    }
    if (zip) { /* the first .log or .txt, else the first file */
        if (!(a = opened(1, data, n))) goto broken;
        for (at = 0; archive_read_next_header(a, &e) == ARCHIVE_OK; ++at) {
            const char* inner = archive_entry_pathname(e);
            if (archive_entry_filetype(e) != AE_IFREG) continue;
            if (wanted < 0) wanted = at;
            if (inner && (ends(inner, ".log") || ends(inner, ".txt"))) {
                wanted = at;
                break;
            }
        }
        archive_read_free(a);
        if (wanted < 0) {
            *why = rei_format("%s holds no file", name);
            return -1;
        }
    }
    if (!(a = opened(zip, data, n))) goto broken;
    for (at = 0; archive_read_next_header(a, &e) == ARCHIVE_OK; ++at) {
        if (zip && at != wanted) continue;
        while ((got = archive_read_data(a, piece, sizeof piece)) > 0) {
            if ((size_t)got > max - out->len) {
                archive_read_free(a);
                buf_free(out);
                *why = rei_format("%s unpacks to over %zu MiB", name, max >> 20);
                return -1;
            }
            buf_addn(out, piece, (size_t)got);
        }
        if (got < 0) break;
        archive_read_free(a);
        return 0;
    }
    archive_read_free(a);
broken:
    buf_free(out);
    *why = rei_format("%s could not be unpacked", name);
    return -1;
}

char* tr_utf8(const char* text, size_t n) {
    const unsigned char* s = (const unsigned char*)text;
    Buf b = {0};
    size_t i = 0;
    while (i < n) {
        unsigned c = s[i];
        size_t len = 0, k;
        unsigned lo = 0x80, hi = 0xbf;
        if (!c) {
            ++i;
            continue;
        }
        if (c < 0x80)
            len = 1;
        else if (c >= 0xc2 && c <= 0xdf)
            len = 2;
        else if (c >= 0xe0 && c <= 0xef) {
            len = 3;
            if (c == 0xe0) lo = 0xa0;
            if (c == 0xed) hi = 0x9f;
        } else if (c >= 0xf0 && c <= 0xf4) {
            len = 4;
            if (c == 0xf0) lo = 0x90;
            if (c == 0xf4) hi = 0x8f;
        }
        for (k = 1; len && k < len; ++k) {
            unsigned low = k == 1 ? lo : 0x80, high = k == 1 ? hi : 0xbf;
            if (i + k >= n || s[i + k] < low || s[i + k] > high) len = 0;
        }
        if (len) {
            buf_addn(&b, text + i, len);
            i += len;
        } else {
            buf_add(&b, "\xef\xbf\xbd");
            ++i;
        }
    }
    return buf_take(&b);
}
