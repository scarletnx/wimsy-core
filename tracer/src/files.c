/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "files.h"
#include <archive.h>
#include <archive_entry.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>

#include "probe.h"

/* the bytes a zip's entries are unpacked to at most in looking for the one that holds a run of edenprobe */
#define ZIP_SCAN (TR_LOG_MAX * 4)

static int ends(const char* name, const char* ext) {
    size_t n = strlen(name), e = strlen(ext);
    return n > e && !strcasecmp(name + n - e, ext);
}
/* a zip's entry under __MACOSX/: the resource fork a Mac's zip keeps beside each file ("__MACOSX/._shot.png"), none
   of the player's files */
static int mac_fork(const char* path) { return !strncmp(path, "__MACOSX/", 9); }

/* a file's kind by its name alone, TR_OTHER for a name tr_kind knows no extension of */
static TrKind named(const char* name) {
    static const char* const logs[] = {".log", ".txt", ".jsonl", ".gz", ".zip"};
    static const char* const images[] = {".png", ".jpg", ".jpeg", ".webp", ".gif", ".ppm"};
    static const char* const videos[] = {".mp4", ".m4v", ".webm"};
    size_t i;
    for (i = 0; i < sizeof logs / sizeof *logs; ++i)
        if (ends(name, logs[i])) return TR_LOG;
    for (i = 0; i < sizeof images / sizeof *images; ++i)
        if (ends(name, images[i])) return TR_IMAGE;
    for (i = 0; i < sizeof videos / sizeof *videos; ++i)
        if (ends(name, videos[i])) return TR_VIDEO;
    return TR_OTHER;
}

TrKind tr_kind(const char* name, const char* type) {
    TrKind kind = named(name);
    if (kind != TR_OTHER) return kind;
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

/* the bytes after a scan's header, its entropy-coded data, up to the marker that ends it: its length from at. In the
   data a 0xff is followed by 0 (a stuffed byte), a restart marker, or another 0xff (fill) */
static size_t scan_data(const unsigned char* d, size_t n, size_t at) {
    size_t j = at;
    while (j + 1 < n) {
        if (d[j] != 0xff || d[j + 1] == 0x00 || (d[j + 1] >= 0xd0 && d[j + 1] <= 0xd7))
            j += d[j] == 0xff ? 2 : 1;
        else if (d[j + 1] == 0xff)
            ++j;
        else
            return j - at;
    }
    return n - at;
}

/* to the end-of-image marker, which ends what is shown: what a phone appends after it (a motion photo's video, a
   maker's trailer) goes with the metadata */
static size_t strip_jpeg(unsigned char* d, size_t n) {
    size_t i = 2, out = 2;
    int scanned = 0;
    for (;;) {
        unsigned m;
        size_t len;
        if (i >= n) return scanned ? out : 0; /* cut short after a scan: shown as far as it goes */
        if (n - i < 2 || d[i] != 0xff) return 0;
        m = d[i + 1];
        if (m == 0xff) { /* a fill byte */
            ++i;
            continue;
        }
        if (m == 0xd9) { /* the image's end, or its end before its scan */
            if (!scanned) return 0;
            memmove(d + out, d + i, 2);
            return out + 2;
        }
        if ((m >= 0xd0 && m <= 0xd7) || m == 0x01) {
            memmove(d + out, d + i, 2);
            out += 2;
            i += 2;
            continue;
        }
        if (n - i < 4) return 0;
        len = (size_t)d[i + 2] << 8 | d[i + 3];
        if (len < 2 || len > n - i - 2) return 0;
        if (m == 0xda) { /* a scan: its header, then its data */
            len += scan_data(d, n, i + 2 + len);
            scanned = 1;
        }
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

/* a GIF's data sub-blocks from at (a size byte, that many bytes, until a size of 0): the offset after them, or 0 when
   they run past n */
static size_t gif_blocks(const unsigned char* d, size_t n, size_t at) {
    while (at < n && d[at]) at += 1 + (size_t)d[at];
    return at < n ? at + 1 : 0;
}
/* a GIF's comments and its applications' extensions dropped, but the loop's (NETSCAPE2.0, ANIMEXTS1.0), and what
   follows its trailer: what an editor writes there (XMP, a comment) is not the picture. Cut short after an image, it
   is kept as far as its whole blocks go, with a trailer where there is room, as a browser shows it */
static size_t strip_gif(unsigned char* d, size_t n) {
    size_t i = 13, out, end;
    int imaged = 0;
    if (n < 13) return 0;
    if (d[10] & 0x80) i += (size_t)3 << ((d[10] & 7) + 1); /* the global colour table */
    if (i > n) return 0;
    out = i;
    for (;;) {
        int keep = 1;
        end = 0;
        if (i < n && d[i] == 0x3b) break; /* the trailer */
        if (i < n && d[i] == 0x2c) {      /* an image: its descriptor, its local colour table, its LZW size, its data */
            end = i + 10;
            if (end <= n && (d[i + 9] & 0x80)) end += (size_t)3 << ((d[i + 9] & 7) + 1);
            end = end < n ? gif_blocks(d, n, end + 1) : 0;
            imaged |= end != 0;
        } else if (i < n && d[i] == 0x21) { /* an extension: its label, its blocks */
            end = n - i >= 2 ? gif_blocks(d, n, i + 2) : 0;
            if (end && d[i + 1] == 0xfe)
                keep = 0;
            else if (end && d[i + 1] == 0xff)
                keep = end - i >= 14 && d[i + 2] == 11 &&
                       (!memcmp(d + i + 3, "NETSCAPE2.0", 11) || !memcmp(d + i + 3, "ANIMEXTS1.0", 11));
        }
        if (!end) { /* cut short, or no block of a GIF's */
            if (!imaged) return 0;
            break;
        }
        if (keep) {
            memmove(d + out, d + i, end - i);
            out += end - i;
        }
        i = end;
    }
    if (out < n) d[out++] = 0x3b; /* the trailer's place, or what was dropped's */
    return out;
}

size_t tr_strip_image(char* data, size_t n) {
    const char* ext = tr_image_ext(data, n);
    unsigned char* d = (unsigned char*)data;
    if (!ext) return 0;
    if (!strcmp(ext, "png")) return strip_png(d, n);
    if (!strcmp(ext, "jpg")) return strip_jpeg(d, n);
    if (!strcmp(ext, "webp")) return strip_webp(d, n);
    return strip_gif(d, n);
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

/* the boxes of an MP4 from at to end: the ones that say where, when or with what it was filmed made free boxes of
   zeros, the ones that hold the media's boxes walked into. udta and meta go whole, as their keys are the maker's own
   (a phone's place can be meta's com.apple.quicktime.location.ISO6709, an index in ilst, never a ©xyz), and so does
   XMP's uuid box. 0, or -1 for a box that runs past its parent */
static int mp4_boxes(unsigned char* d, size_t at, size_t end, int depth) {
    static const char* const containers[] = {"moov", "trak", "mdia", "minf", "edts"};
    static const char* const hidden[] = {"\xa9xyz", "loci", "\xa9mak", "\xa9mod", "udta", "meta"};
    static const unsigned char xmp[16] = {0xbe, 0x7a, 0xcf, 0xcb, 0x97, 0xa9, 0x42, 0xe8,
                                          0x9c, 0x71, 0x99, 0x94, 0x91, 0xe3, 0xaf, 0xac};
    while (at < end) {
        size_t size, head = 8, k;
        int blank;
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
        blank = !memcmp(d + at + 4, "uuid", 4) && size - head >= 16 && !memcmp(d + at + head, xmp, 16);
        for (k = 0; k < sizeof hidden / sizeof *hidden; ++k) blank |= !memcmp(d + at + 4, hidden[k], 4);
        if (blank) {
            memcpy(d + at + 4, "free", 4);
            memset(d + at + head, 0, size - head);
        }
        for (k = 0; depth < 8 && k < sizeof containers / sizeof *containers; ++k)
            if (!memcmp(d + at + 4, containers[k], 4) && mp4_boxes(d, at + head, at + size, depth + 1)) return -1;
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

/* the UTF-8 locale a zip's names are read in, made once and kept for the process's life; 0 where there is none */
static locale_t utf8_locale(void) {
    static locale_t utf8;
    static int tried;
    if (!tried) {
        tried = 1;
        utf8 = newlocale(LC_CTYPE_MASK, "C.UTF-8", (locale_t)0);
        if (!utf8) utf8 = newlocale(LC_CTYPE_MASK, "en_US.UTF-8", (locale_t)0);
    }
    return utf8;
}
int tr_names_ready(void) { return utf8_locale() ? 0 : -1; }
/* a zip's names read as UTF-8 by the calling thread, whatever the process's locale: in a worker's, C, libarchive gives
   no name at all for an entry its zip flags as UTF-8 (miniz flags each one ScarletNX's Share Log writes) when the name
   is no ASCII (a game's folder named with an accent), and says ARCHIVE_WARN. The locale before, for names_back; 0
   where the system has no UTF-8 locale, the names then as libarchive gives them */
static locale_t names_utf8(void) {
    locale_t utf8 = utf8_locale();
    return utf8 ? uselocale(utf8) : (locale_t)0;
}
static void names_back(locale_t was) {
    if (was) uselocale(was);
}
/* whether archive_read_next_header gave an entry: ARCHIVE_WARN is one too, a name it could not convert among them */
static int entry(int got) { return got == ARCHIVE_OK || got == ARCHIVE_WARN; }

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

static int unpacked(const char* name, const char* data, size_t n, size_t max, Buf* out, char** why);
int tr_unpack(const char* name, const char* data, size_t n, size_t max, Buf* out, char** why) {
    locale_t was = names_utf8();
    int rc = unpacked(name, data, n, max, out, why);
    names_back(was);
    return rc;
}
static int unpacked(const char* name, const char* data, size_t n, size_t max, Buf* out, char** why) {
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
    if (zip) { /* an entry holding a whole run of edenprobe, else one a run begins in, else the first .log, .txt or
                  .jsonl, else the first file that is no picture or video: a Share Log zip holds many logs, one of
                  which may be the run, and the screenshots a player puts beside them */
        int text = -1, started = -1, files = 0;
        size_t read = 0;
        if (!(a = opened(1, data, n))) goto broken;
        for (at = 0; entry(archive_read_next_header(a, &e)); ++at) {
            const char* inner = archive_entry_pathname(e);
            int marks, whole = 1;
            if (archive_entry_filetype(e) != AE_IFREG || (inner && mac_fork(inner))) continue;
            files = 1;
            if (inner && (named(inner) == TR_IMAGE || named(inner) == TR_VIDEO)) continue;
            if (wanted < 0) wanted = at;
            if (!inner || (!ends(inner, ".log") && !ends(inner, ".txt") && !ends(inner, ".jsonl"))) continue;
            if (text < 0) text = at;
            if (read >= ZIP_SCAN) continue; /* past what is read in looking for a run: the order above holds */
            buf_free(out);
            while ((got = archive_read_data(a, piece, sizeof piece)) > 0 && whole) {
                read += (size_t)got;
                whole = (size_t)got <= max - out->len;
                if (whole) buf_addn(out, piece, (size_t)got);
            }
            if (got < 0 || !whole) continue;
            marks = probe_marks(out->text ? out->text : "", out->len);
            if (marks == 2) { /* this one: read whole already */
                archive_read_free(a);
                return 0;
            }
            if (marks == 1 && started < 0) started = at;
        }
        archive_read_free(a);
        buf_free(out);
        if (wanted < 0) {
            *why = rei_format(files ? "%s holds no log, only pictures or videos" : "%s holds no file", name);
            return -1;
        }
        wanted = started >= 0 ? started : text >= 0 ? text : wanted;
    }
    if (!(a = opened(zip, data, n))) goto broken;
    for (at = 0; entry(archive_read_next_header(a, &e)); ++at) {
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

int tr_zip_images(TrZipImages* z, const char* data, size_t n) {
    memset(z, 0, sizeof *z);
    z->size = -1;
    return (z->a = opened(1, data, n)) ? 0 : -1;
}

int tr_zip_next(TrZipImages* z) {
    struct archive_entry* e;
    locale_t was = names_utf8();
    int got;
    free(z->name);
    z->name = NULL;
    while (entry(got = archive_read_next_header(z->a, &e))) {
        const char *path = archive_entry_pathname(e), *leaf;
        if (archive_entry_filetype(e) != AE_IFREG) continue;
        if (!path) { /* a file whose name the zip does not give: none can say it is a picture */
            ++z->unnamed;
            continue;
        }
        if (mac_fork(path) || named(path) != TR_IMAGE) continue;
        for (leaf = path + strlen(path); leaf > path && leaf[-1] != '/' && leaf[-1] != '\\'; --leaf) continue;
        z->name = tr_utf8(leaf, strlen(leaf));
        z->size = archive_entry_size_is_set(e) ? (double)archive_entry_size(e) : -1;
        names_back(was);
        return 1;
    }
    z->broken = got != ARCHIVE_EOF;
    names_back(was);
    return 0;
}

int tr_zip_read(TrZipImages* z, size_t max, Buf* out) {
    char piece[65536];
    la_ssize_t got;
    buf_free(out);
    while ((got = archive_read_data(z->a, piece, sizeof piece)) > 0) {
        if ((size_t)got > max - out->len) {
            buf_free(out);
            return 1;
        }
        buf_addn(out, piece, (size_t)got);
    }
    if (got < 0) {
        buf_free(out);
        return -1;
    }
    return 0;
}

void tr_zip_done(TrZipImages* z) {
    if (z->a) archive_read_free(z->a);
    free(z->name);
    memset(z, 0, sizeof *z);
}

char* tr_unpacked_name(const char* name) {
    size_t n = strlen(name);
    if (ends(name, ".zip")) return rei_format("%.*s.log", (int)(n - 4), name);
    if (ends(name, ".gz")) {
        char* rest = rei_format("%.*s", (int)(n - 3), name);
        char* named;
        if (ends(rest, ".log") || ends(rest, ".txt") || ends(rest, ".jsonl")) return rest;
        named = rei_format("%s.log", rest);
        free(rest);
        return named;
    }
    return rei_strdup(name);
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

size_t tr_stamp(const char* s, size_t n) {
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
    size_t i = tr_stamp(s, n), indent, k, d;
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

static int digit(char c) { return c >= '0' && c <= '9'; }
static int letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

/* the length of an IPv4 address at s (four numbers to 255 joined by dots, no digit after it, no fifth number), its
   first number in *first; 0 for none */
static size_t ipv4(const char* s, size_t n, unsigned* first) {
    size_t i = 0, start;
    int part;
    for (part = 0; part < 4; ++part) {
        unsigned v = 0;
        if (part) {
            if (i >= n || s[i] != '.') return 0;
            ++i;
        }
        for (start = i; i < n && digit(s[i]) && i - start < 3; ++i) v = v * 10 + (unsigned)(s[i] - '0');
        if (i == start || v > 255 || (i < n && digit(s[i]))) return 0;
        if (!part) *first = v;
    }
    if (i + 1 < n && s[i] == '.' && digit(s[i + 1])) return 0; /* a fifth number: a version */
    return i;
}
static int name_char(char c) {
    return letter(c) || digit(c) || c == '.' || c == '_' || c == '%' || c == '+' || c == '-';
}
static int host_char(char c) { return letter(c) || digit(c) || c == '.' || c == '-'; }
/* the length of an e-mail address around the '@' at s + at (name@host.tld, its last label two letters or more), *from
   where it starts; 0 for none */
static size_t email(const char* s, size_t n, size_t at, size_t* from) {
    size_t a = at, z = at + 1, dot, i;
    while (a && name_char(s[a - 1])) --a;
    while (z < n && host_char(s[z])) ++z;
    while (z > at + 1 && s[z - 1] == '.') --z; /* a sentence's full stop after it */
    for (dot = z; dot > at + 1 && s[dot - 1] != '.'; --dot) continue;
    if (a == at || dot <= at + 2 || z - dot < 2) return 0; /* dot: the last label's start */
    for (i = dot; i < z; ++i)
        if (!letter(s[i])) return 0;
    *from = a;
    return z - a;
}
/* a line of the log as the site shows it, into b: each IPv4 address but the loopback's, 0.0.0.0 and a mask (255.) as
   [ip], each e-mail address as [email]. A player's address and the other players' on a network game are theirs */
static void scrubbed(Buf* b, const char* s, size_t n) {
    size_t i = 0, done = 0;
    while (i < n) {
        size_t len, from;
        unsigned first = 0;
        if (digit(s[i]) && (!i || (!digit(s[i - 1]) && s[i - 1] != '.')) && (len = ipv4(s + i, n - i, &first))) {
            if (first != 127 && first != 255 && (first || strncmp(s + i, "0.0.0.0", len))) {
                buf_addn(b, s + done, i - done);
                buf_add(b, "[ip]");
                done = i + len;
            }
            i += len;
        } else if (s[i] == '@' && (len = email(s, n, i, &from)) && from >= done) {
            buf_addn(b, s + done, from - done);
            buf_add(b, "[email]");
            done = i = from + len;
        } else
            ++i;
    }
    buf_addn(b, s + done, n - done);
}

char* tr_public_log(const char* text, size_t n, size_t* size) {
    Buf b = {0};
    size_t at = 0;
    while (at < n) {
        const char* nl = memchr(text + at, '\n', n - at);
        size_t len = nl ? (size_t)(nl - (text + at)) + 1 : n - at;
        if (!dump_line(text + at, len)) scrubbed(&b, text + at, len);
        at += len;
    }
    *size = b.len;
    return buf_take(&b);
}
