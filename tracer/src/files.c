/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "files.h"
#include <archive.h>
#include <archive_entry.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>

static int ends(const char* name, const char* ext) {
    size_t n = strlen(name), e = strlen(ext);
    return n > e && !strcasecmp(name + n - e, ext);
}

TrKind tr_kind(const char* name, const char* type) {
    static const char* const logs[] = {".log", ".txt", ".gz", ".zip"};
    static const char* const images[] = {".png", ".jpg", ".jpeg", ".webp", ".gif", ".ppm"};
    static const char* const videos[] = {".mp4", ".m4v", ".webm"};
    size_t i;
    for (i = 0; i < sizeof logs / sizeof *logs; ++i)
        if (ends(name, logs[i])) return TR_LOG;
    for (i = 0; i < sizeof images / sizeof *images; ++i)
        if (ends(name, images[i])) return TR_IMAGE;
    for (i = 0; i < sizeof videos / sizeof *videos; ++i)
        if (ends(name, videos[i])) return TR_VIDEO;
    if (!strncmp(type, "image/", 6)) return TR_IMAGE;
    if (!strncmp(type, "video/", 6)) return TR_VIDEO;
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
    if (n >= 3 && d[0] == 'P' && d[1] == '6' && (d[2] == ' ' || d[2] == '\t' || d[2] == '\n' || d[2] == '\r'))
        return "ppm";
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

const char* tr_video_ext(const char* data, size_t n) {
    const unsigned char* d = (const unsigned char*)data;
    size_t i;
    if (n >= 12 && !memcmp(d + 4, "ftyp", 4)) return "mp4";
    if (n >= 4 && d[0] == 0x1a && d[1] == 0x45 && d[2] == 0xdf && d[3] == 0xa3) /* EBML: its doctype near its start */
        for (i = 4; i + 4 <= n && i < 64; ++i)
            if (!memcmp(d + i, "webm", 4)) return "webm";
    return NULL;
}

/* the boxes of an MP4 from at to end: the ones that say where or with what it was filmed made free boxes of zeros, the
   ones that hold others walked into. 0, or -1 for a box that runs past its parent */
static int mp4_boxes(unsigned char* d, size_t at, size_t end, int depth) {
    static const char* const containers[] = {"moov", "trak", "mdia", "minf", "udta", "meta", "ilst", "edts"};
    static const char* const hidden[] = {"\xa9xyz", "loci", "\xa9mak", "\xa9mod"};
    while (at < end) {
        size_t size, head = 8, k;
        if (end - at < 8) return -1;
        size = be32(d + at);
        if (size == 1) { /* a 64-bit size after the type */
            unsigned long long big;
            if (end - at < 16) return -1;
            big = (unsigned long long)be32(d + at + 8) << 32 | be32(d + at + 12);
            if (big > end - at) return -1;
            size = (size_t)big;
            head = 16;
        } else if (size == 0) /* to the end */
            size = end - at;
        if (size < head || size > end - at) return -1;
        for (k = 0; k < sizeof hidden / sizeof *hidden; ++k)
            if (!memcmp(d + at + 4, hidden[k], 4)) {
                memcpy(d + at + 4, "free", 4);
                memset(d + at + head, 0, size - head);
            }
        for (k = 0; depth < 8 && k < sizeof containers / sizeof *containers; ++k)
            if (!memcmp(d + at + 4, containers[k], 4)) {
                size_t inner = at + head + (!memcmp(containers[k], "meta", 4) ? 4 : 0); /* meta's version and flags */
                if (inner <= at + size && mp4_boxes(d, inner, at + size, depth + 1)) return -1;
            }
        at += size;
    }
    return 0;
}

size_t tr_strip_video(char* data, size_t n) {
    const char* ext = tr_video_ext(data, n);
    if (!ext) return 0;
    if (!strcmp(ext, "mp4")) return mp4_boxes((unsigned char*)data, 0, n, 0) ? 0 : n;
    return n;
}

/* a PPM header's next number, after its whitespace and comments; -1 for none */
static long ppm_number(const unsigned char* d, size_t n, size_t* at) {
    long v = 0;
    size_t digits = 0;
    for (;;) {
        while (*at < n && (d[*at] == ' ' || d[*at] == '\t' || d[*at] == '\n' || d[*at] == '\r')) ++*at;
        if (*at < n && d[*at] == '#')
            while (*at < n && d[*at] != '\n') ++*at;
        else
            break;
    }
    while (*at < n && d[*at] >= '0' && d[*at] <= '9' && digits < 6) v = v * 10 + (d[(*at)++] - '0'), ++digits;
    return digits && !(*at < n && d[*at] >= '0' && d[*at] <= '9') ? v : -1;
}
static void be32_at(unsigned char* p, unsigned long v) {
    p[0] = (unsigned char)(v >> 24), p[1] = (unsigned char)(v >> 16), p[2] = (unsigned char)(v >> 8),
    p[3] = (unsigned char)v;
}
/* a PNG chunk of type with n bytes of data, its CRC zlib's */
static void png_chunk(Buf* out, const char* type, const unsigned char* data, size_t n) {
    unsigned char head[8], crc[4];
    uLong c = crc32(0L, Z_NULL, 0);
    be32_at(head, (unsigned long)n);
    memcpy(head + 4, type, 4);
    c = crc32(c, head + 4, 4);
    if (n) c = crc32(c, data, (uInt)n);
    be32_at(crc, c);
    buf_addn(out, (const char*)head, 8);
    if (n) buf_addn(out, (const char*)data, n);
    buf_addn(out, (const char*)crc, 4);
}

int tr_ppm_png(const char* data, size_t n, Buf* out) {
    const unsigned char* d = (const unsigned char*)data;
    size_t at = 2, row, y, raw_n, packed_n;
    long w, h, max;
    unsigned char ihdr[13], *raw, *packed;
    uLongf got;
    buf_free(out);
    if (!tr_image_ext(data, n) || strcmp(tr_image_ext(data, n), "ppm")) return -1;
    w = ppm_number(d, n, &at);
    h = ppm_number(d, n, &at);
    max = ppm_number(d, n, &at);
    if (w < 1 || h < 1 || w > 16384 || h > 16384 || max != 255 || at >= n) return -1;
    ++at; /* the one whitespace after the header */
    row = (size_t)w * 3;
    if (n - at < row * (size_t)h) return -1;
    raw_n = (row + 1) * (size_t)h; /* each row after its filter byte, 0: none */
    raw = malloc(raw_n);
    packed_n = compressBound((uLong)raw_n);
    packed = malloc(packed_n);
    if (!raw || !packed) {
        free(raw);
        free(packed);
        return -1;
    }
    for (y = 0; y < (size_t)h; ++y) {
        raw[y * (row + 1)] = 0;
        memcpy(raw + y * (row + 1) + 1, d + at + y * row, row);
    }
    got = (uLongf)packed_n;
    if (compress2(packed, &got, raw, (uLong)raw_n, 6) != Z_OK) {
        free(raw);
        free(packed);
        return -1;
    }
    be32_at(ihdr, (unsigned long)w);
    be32_at(ihdr + 4, (unsigned long)h);
    ihdr[8] = 8, ihdr[9] = 2, ihdr[10] = 0, ihdr[11] = 0, ihdr[12] = 0; /* 8 bits a channel, RGB, no interlace */
    buf_addn(out, "\x89PNG\r\n\x1a\n", 8);
    png_chunk(out, "IHDR", ihdr, sizeof ihdr);
    png_chunk(out, "IDAT", packed, (size_t)got);
    png_chunk(out, "IEND", NULL, 0);
    free(raw);
    free(packed);
    return 0;
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

/* "[   2.25] " before a line, the time the runtime prints (its point a comma under some locales): its length, 0 without
   one. As logscan.c's stamp(), it takes one space after the bracket, so a dump's indent is what follows */
static size_t stamp(const char* s, size_t n) {
    size_t i = 1, d;
    if (!n || s[0] != '[') return 0;
    while (i < n && s[i] == ' ') ++i;
    for (d = i; i < n && s[i] >= '0' && s[i] <= '9'; ++i) continue;
    if (i == d || i - d > 9 || i >= n || (s[i] != '.' && s[i] != ',')) return 0;
    for (d = ++i; i < n && s[i] >= '0' && s[i] <= '9'; ++i) continue;
    if (i == d || i >= n || s[i] != ']') return 0;
    ++i;
    return i < n && s[i] == ' ' ? i + 1 : i;
}

/* a line of the crash report that holds the game's bytes or its registers, by logscan.c's dump(): indented, and
   starting with one of its prefixes, or a register's "x12=" or "r3 =" */
static int dump_line(const char* s, size_t n) {
    static const char* const prefixes[] = {
        "code ", "mem ", "vtable:", "-> ", "pc=", "cpsr=", "info+", "data:", "(the rest"};
    size_t i = stamp(s, n), indent, k, d;
    for (indent = i; i < n && (s[i] == ' ' || s[i] == '\t'); ++i) continue;
    if (i == indent) return 0;
    s += i;
    n -= i;
    for (k = 0; k < sizeof prefixes / sizeof *prefixes; ++k)
        if (n >= strlen(prefixes[k]) && !memcmp(s, prefixes[k], strlen(prefixes[k]))) return 1;
    if (!n || (s[0] != 'x' && s[0] != 'r')) return 0;
    for (d = 1; d < n && s[d] >= '0' && s[d] <= '9'; ++d) continue;
    if (d == 1) return 0;
    while (d < n && (s[d] == ' ' || s[d] == '\t')) ++d;
    return d < n && s[d] == '=';
}

char* tr_public_log(const char* text, size_t n, size_t* size) {
    Buf b = {0};
    size_t at = 0;
    while (at < n) {
        const char* nl = memchr(text + at, '\n', n - at);
        size_t len = nl ? (size_t)(nl - (text + at)) + 1 : n - at;
        if (!dump_line(text + at, len)) buf_addn(&b, text + at, len);
        at += len;
    }
    *size = b.len;
    return buf_take(&b);
}
