/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* Tracer without Discord or the network: what a message asks, which files are logs and screenshots, the screenshots'
   metadata taken out, the logs unpacked, the game a log names, and whole reports kept, drawn, refused and deleted
   through a fake download into a database and a site of a temporary folder */
#if defined(__FreeBSD__) /* sys/procctl.h's structures are of BSD types (u_int), which _POSIX_C_SOURCE hides */
#undef _POSIX_C_SOURCE
#else
#define _POSIX_C_SOURCE 200809L
#endif
#include <archive.h>
#include <archive_entry.h>
#include <ctype.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>
#if defined(__FreeBSD__)
#include <sys/procctl.h>
#endif

#include "common/utilities/demangle.h"
#include "common/utilities/logscan.h"
#include "channels.h"
#include "age.h"
#include "files.h"
#include "issues.h"
#include "mcp.h"
#include "message.h"
#include "probe.h"
#include "report.h"
#include "rules.h"
#include "site.h"
#include "store.h"
#include "titles.h"
#include "worker.h"

static int failures, checks;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(cond)) {                                                                                                 \
            ++failures;                                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                                                 \
        }                                                                                                              \
    } while (0)
#define SAME(a, b)                                                                                                     \
    do {                                                                                                               \
        const char *x_ = (a), *y_ = (b);                                                                               \
        ++checks;                                                                                                      \
        if (!x_ || !y_ || strcmp(x_, y_)) {                                                                            \
            ++failures;                                                                                                \
            fprintf(stderr, "%s:%d: %s\n  is: %s\n  not: %s\n", __FILE__, __LINE__, #a, x_ ? x_ : "(null)",            \
                    y_ ? y_ : "(null)");                                                                               \
        }                                                                                                              \
    } while (0)
#define HAS(text, part)   CHECK((text) && strstr((text), (part)))
#define LACKS(text, part) CHECK((text) && !strstr((text), (part)))

static double now_is = 1800000000;
static double clock_now(void) { return now_is; }

/* ---- a log in the runtime's format ---- */
static const char LOG[] =
    "[   0.00] ScarletNX - Some Game\n"
    "[   0.00] build: ScarletNX main 1234abc (clang 23.1.3, Linux x86_64)\n"
    "[   0.00] system: Some Linux 1.0 (Linux 6.1.0), Some CPU 9000, 12 threads, 32 GB RAM\n"
    "[   0.27] vulkan: Some GPU 3000, Vulkan 1.4.351, driver 99d38200, graphics queue family 0, compute 2\n"
    "[   0.30] game: 0100aaaabbbb0000, version 1.2.3\n"
    "[   1.00]   [undeclared] nn::audio::detail::IAudioDevice cmd 7 -> ok\n"
    "[   1.04] [game] loading /home/someone/save.bin <script>\n"
    "[   2.25]\n"
    "*** The game called svcBreak (reason 0, info 1300100df8, 4 bytes) ***\n"
    "[   2.25]   pc=164ab3c4 lr=163d4828 sp=1300100c30 fp=1300100c30 tls=0\n"
    "[   2.25]    #0 163d18d8 <_ZN2nn4diag6detail10VAbortImplEv+d8>\n"
    "[   2.25]    #1 1683a358 <_ZN2nn3hid18GetNpadJoyHoldTypeEv+38>\n";

/* ---- a fake network: each URL's bytes ---- */
typedef struct {
    const char* url;
    const char* data;
    size_t n;
} Served;
static Served served[32];
static int nserved, downloads;
static void serve(const char* url, const char* data, size_t n) {
    if (nserved == (int)(sizeof served / sizeof *served)) { /* room for more: a test past it wrote over what follows */
        fprintf(stderr, "%s: more than %d files served\n", url, nserved);
        exit(1);
    }
    served[nserved].url = url;
    served[nserved].data = data;
    served[nserved++].n = n;
}
static int fake_get(void* ctx, const char* url, size_t max, Buf* out, char** why) {
    int i;
    (void)ctx;
    buf_free(out);
    *why = NULL;
    ++downloads;
    for (i = 0; i < nserved; ++i)
        if (!strcmp(served[i].url, url)) {
            if (served[i].n > max) break;
            buf_addn(out, served[i].data, served[i].n);
            return 0;
        }
    *why = rei_format("%s: HTTP 404", url);
    return -1;
}

static char* read_file(const char* path, size_t* n) {
    FILE* f = fopen(path, "rb");
    Buf b = {0};
    char piece[4096];
    size_t got;
    if (!f) return NULL;
    while ((got = fread(piece, 1, sizeof piece, f))) buf_addn(&b, piece, got);
    fclose(f);
    if (n) *n = b.len;
    return buf_take(&b);
}
static int exists(const char* path) {
    struct stat st;
    return !stat(path, &st);
}

/* a .gz or a .zip of one file, written by libarchive */
static char* packed(int zip, const char* inner, const char* text, size_t* n) {
    static char out[1 << 16];
    struct archive* a = archive_write_new();
    struct archive_entry* e = archive_entry_new();
    size_t used = 0;
    if (zip)
        archive_write_set_format_zip(a);
    else {
        archive_write_add_filter_gzip(a);
        archive_write_set_format_raw(a);
    }
    archive_write_open_memory(a, out, sizeof out, &used);
    archive_entry_set_pathname(e, inner);
    archive_entry_set_size(e, (la_int64_t)strlen(text));
    archive_entry_set_filetype(e, AE_IFREG);
    archive_entry_set_perm(e, 0644);
    archive_write_header(a, e);
    archive_write_data(a, text, strlen(text));
    archive_entry_free(e);
    archive_write_close(a);
    archive_write_free(a);
    *n = used;
    return out;
}

enum { ZIP_STORED = 1, ZIP_UTF8 = 2 };
static unsigned le16_at(const char* p) { return (unsigned char)p[0] | (unsigned)(unsigned char)p[1] << 8; }
static unsigned long le32_at(const char* p) { return le16_at(p) | (unsigned long)le16_at(p + 2) << 16; }
/* a zip of n entries, entry i data[i]'s sizes[i] bytes (strlen's when sizes is NULL or it is 0): deflated or, with
   ZIP_STORED, stored as they are (so that a test can find and break one's bytes); with ZIP_UTF8 each entry flagged as
   holding a UTF-8 name (general purpose bit 11, in its central directory's record and its local header), as miniz
   writes ScarletNX's Share Log, which libarchive writes in a UTF-8 locale alone. Its length */
static size_t zip_bytes(char* out, size_t cap, const char* const* names, const char* const* data, const size_t* sizes,
                        int n, int how) {
    struct archive* a = archive_write_new();
    size_t used = 0, at;
    int i;
    archive_write_set_format_zip(a);
    if (how & ZIP_STORED) archive_write_set_format_option(a, "zip", "compression", "store");
    archive_write_open_memory(a, out, cap, &used);
    for (i = 0; i < n; ++i) {
        struct archive_entry* e = archive_entry_new();
        size_t len = sizes && sizes[i] ? sizes[i] : strlen(data[i]);
        archive_entry_set_pathname_utf8(e, names[i]);
        archive_entry_set_size(e, (la_int64_t)len);
        archive_entry_set_filetype(e, AE_IFREG);
        archive_entry_set_perm(e, 0644);
        archive_write_header(a, e);
        archive_write_data(a, data[i], len);
        archive_entry_free(e);
    }
    archive_write_close(a);
    archive_write_free(a);
    if ((how & ZIP_UTF8) && used >= 22 && !memcmp(out + used - 22, "PK\5\6", 4)) { /* no comment: the end's 22 bytes */
        at = le32_at(out + used - 22 + 16);                                        /* the central directory */
        for (i = 0; i < (int)le16_at(out + used - 22 + 10) && at + 46 <= used && !memcmp(out + at, "PK\1\2", 4); ++i) {
            size_t local = le32_at(out + at + 42);
            out[at + 9] |= 0x08;
            if (local + 30 <= used && !memcmp(out + local, "PK\3\4", 4)) out[local + 7] |= 0x08;
            at += 46 + le16_at(out + at + 28) + le16_at(out + at + 30) + le16_at(out + at + 32);
        }
    }
    return used;
}
static void test_message(void) {
    TrMessage m;
    const char* mention =
        "{\"id\":\"11\",\"channel_id\":\"22\",\"guild_id\":\"33\",\"author\":{\"id\":\"44\"},\"content\":\"<@99> "
        "crashes in the menu\",\"mentions\":[{\"id\":\"99\"}],\"attachments\":[{\"filename\":\"a.log\",\"url\":"
        "\"https://cdn.discordapp.com/a.log\",\"size\":10,\"content_type\":\"text/plain; charset=utf-8\"},{\"url\":"
        "\"https://cdn.discordapp.com/nameless\"}],\"member\":{\"roles\":[\"5\",\"6\"]}}";
    CHECK(tr_message(mention, strlen(mention), 99, &m));
    CHECK(m.id == 11 && m.channel == 22 && m.guild == 33 && m.author == 44 && m.mentioned);
    CHECK(m.nfiles == 1);
    CHECK(m.nroles == 2 && m.roles[0] == 5 && m.roles[1] == 6);
    CHECK(!m.reply_to);
    SAME(m.files[0].name, "a.log");
    CHECK(m.files[0].size == 10);
    tr_message_free(&m);
    {
        const char* reply = "{\"id\":\"12\",\"channel_id\":\"22\",\"guild_id\":\"33\",\"author\":{\"id\":\"44\"},"
                            "\"content\":\"<@99>\",\"mentions\":[{\"id\":\"99\"}],\"message_reference\":{"
                            "\"message_id\":\"555\",\"channel_id\":\"22\"}}";
        const char* answer = "{\"id\":\"13\",\"channel_id\":\"22\",\"guild_id\":\"33\",\"author\":{\"id\":\"44\"},"
                             "\"content\":\"<@99> more\",\"mentions\":[{\"id\":\"99\"}],\"message_reference\":{"
                             "\"message_id\":\"556\"},\"referenced_message\":{\"id\":\"556\",\"author\":{\"id\":"
                             "\"99\",\"bot\":true},\"embeds\":[{\"title\":\"Report #2\",\"url\":"
                             "\"https://reports.example/r/2/\"}]}}";
        CHECK(tr_message(reply, strlen(reply), 99, &m) && m.reply_to == 555 && !m.reply_report);
        CHECK(m.reply_channel == 22 && !m.reply_guild && !m.bot);
        tr_message_free(&m);
        CHECK(tr_message(answer, strlen(answer), 99, &m) && m.reply_to == 556 && m.reply_report == 2);
        tr_message_free(&m);
        CHECK(tr_message(answer, strlen(answer), 98, &m) && !m.reply_report); /* another bot's message */
    }
    tr_message_free(&m);
    CHECK(tr_message(mention, strlen(mention), 98, &m) && !m.mentioned);
    tr_message_free(&m);
    CHECK(!tr_message("{\"id\":\"1\",\"channel_id\":\"2\",\"author\":{\"id\":\"3\",\"bot\":true}}", 60, 99, &m));
    CHECK(
        !tr_message("{\"id\":\"1\",\"channel_id\":\"2\",\"webhook_id\":\"5\",\"author\":{\"id\":\"3\"}}", 64, 99, &m));
    CHECK(!tr_message("not json", 8, 99, &m));
    { /* a message as the REST API gives it: a bot's too, said */
        const char* got =
            "{\"id\":\"1\",\"channel_id\":\"2\",\"author\":{\"id\":\"3\",\"bot\":true},\"content\":"
            "\"<@99> hi\",\"attachments\":[{\"filename\":\"a.log\",\"url\":\"https://cdn.discordapp.com/a\"}]}";
        CHECK(tr_message_of(got, strlen(got), &m) && m.bot && m.nfiles == 1 && m.author == 3 && !m.guild);
        tr_message_free(&m);
        CHECK(tr_message_of("{\"id\":\"1\",\"channel_id\":\"2\",\"webhook_id\":\"5\",\"author\":{\"id\":\"3\"}}", 64,
                            &m) &&
              m.bot);
        tr_message_free(&m);
        CHECK(!tr_message_of("[]", 2, &m));
    }
    CHECK(tr_deleted("{\"id\":\"77\",\"channel_id\":\"2\"}", 29) == 77);
    {
        char* name;
        CHECK(tr_ready("{\"user\":{\"id\":\"99\",\"username\":\"Tracer\"}}", 41, &name) == 99);
        SAME(name, "Tracer");
        free(name);
    }
}

static void test_command(void) {
    char* note;
    long n;
    char longer[400];
    CHECK(tr_command("<@99> help", &note, &n) == TR_HELP && !note);
    CHECK(tr_command("<@!99>   HELP  ", &note, &n) == TR_HELP);
    CHECK(tr_command("<@99> delete 12", &note, &n) == TR_DELETE && n == 12);
    CHECK(tr_command("<@99> delete #7", &note, &n) == TR_DELETE && n == 7);
    CHECK(tr_command("<@99> delete", &note, &n) == TR_DELETE && n == 0 && !note);
    CHECK(tr_command("<@99> add #12 it crashes after the boss", &note, &n) == TR_ADD && n == 12);
    SAME(note, "it crashes after the boss");
    free(note);
    CHECK(tr_command("<@99> add 3", &note, &n) == TR_ADD && n == 3);
    SAME(note, "");
    free(note);
    CHECK(tr_command("<@99> add more", &note, &n) == TR_REPORT);
    free(note);
    CHECK(tr_command("<@99> delete it", &note, &n) == TR_REPORT);
    SAME(note, "delete it");
    free(note);
    CHECK(tr_command("<@99>\n  Some   Game\n<@&5> crashes <#6>", &note, &n) == TR_REPORT);
    SAME(note, "Some Game crashes");
    free(note);
    CHECK(tr_command("<@99>", &note, &n) == TR_REPORT);
    SAME(note, "");
    free(note);
    note = tr_words("<@99>\n  Some   Game <#6>");
    SAME(note, "Some Game");
    free(note);
    memset(longer, 'a', sizeof longer);
    memcpy(longer + TR_NOTE - 1, "\xc3\xa9", 2); /* an é across the cut */
    longer[sizeof longer - 1] = 0;
    tr_command(longer, &note, &n);
    CHECK(strlen(note) == TR_NOTE - 1);
    free(note);
}

static void test_kind_and_text(void) {
    char* t;
    CHECK(tr_kind("game.LOG", "") == TR_LOG);
    CHECK(tr_kind("game.log.gz", "") == TR_LOG);
    CHECK(tr_kind("logs.zip", "application/zip") == TR_LOG);
    CHECK(tr_kind("shot.JPEG", "") == TR_IMAGE);
    CHECK(tr_kind("message", "text/plain; charset=utf-8") == TR_LOG);
    CHECK(tr_kind("shot", "image/png") == TR_IMAGE);
    CHECK(tr_kind("frame-0-01.PPM", "") == TR_IMAGE);
    CHECK(tr_kind("clip.MP4", "") == TR_VIDEO && tr_kind("clip.webm", "") == TR_VIDEO);
    CHECK(tr_kind("clip", "video/quicktime") == TR_VIDEO);
    CHECK(tr_kind("game.exe", "application/octet-stream") == TR_OTHER);
    t = tr_utf8("a\xf7"
                "b\xc3\xa9"
                "c\0d\xe0\x80\x80"
                "e\xf0\x9f\x98\x80",
                16);
    SAME(t, "a\xef\xbf\xbd"
            "b\xc3\xa9"
            "cd\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd"
            "e\xf0\x9f\x98\x80");
    free(t);
    t = tr_utf8("cut \xe2\x82", 6);
    SAME(t, "cut \xef\xbf\xbd\xef\xbf\xbd");
    free(t);
}

/* whether part is in the n bytes at data */
static int found(const char* data, size_t n, const char* part) {
    size_t k = strlen(part), i;
    for (i = 0; i + k <= n; ++i)
        if (!memcmp(data + i, part, k)) return 1;
    return 0;
}
/* a PNG's chunk: its length, type and data (the CRC is not checked here) */
static size_t chunk(char* to, const char* type, const char* data, size_t n) {
    to[0] = 0, to[1] = 0, to[2] = 0, to[3] = (char)n;
    memcpy(to + 4, type, 4);
    memcpy(to + 8, data, n);
    memset(to + 8 + n, 0, 4);
    return 12 + n;
}
/* an MP4 box's header at to: its size (n, its header's 8 bytes with it) and type */
static void box_head(char* to, const char* type, size_t n) {
    to[0] = (char)(n >> 24), to[1] = (char)(n >> 16), to[2] = (char)(n >> 8), to[3] = (char)n;
    memcpy(to + 4, type, 4);
}
/* an MP4 box of n bytes of data at to: its length */
static size_t mp4_box(char* to, const char* type, const char* data, size_t n) {
    box_head(to, type, 8 + n);
    memcpy(to + 8, data, n);
    return 8 + n;
}
/* an MP4 of a phone: its ftyp, a moov with udta's place (©xyz) and meta's make (©mak), then its media */
static size_t mp4(char* to, size_t room) {
    static const unsigned char box[] = {
        0,   0,   0,   16,  'f',  't', 'y', 'p', 'i', 's', 'o', 'm', 0,   0,   2,   0,         /* ftyp, 16 */
        0,   0,   0,   74,  'm',  'o', 'o', 'v',                                               /* moov, 8 + 30 + 36 */
        0,   0,   0,   30,  'u',  'd', 't', 'a',                                               /* udta, 8 + 22 */
        0,   0,   0,   22,  0xa9, 'x', 'y', 'z', '+', '4', '0', '.', '7', '1', '2', '8',       /* ©xyz, 8 + 14 */
        '-', '0', '7', '4', '.',  '0', 0,   0,   0,   36,  'm', 'e', 't', 'a', 0,   0,   0, 0, /* meta, 12 + 24 */
        0,   0,   0,   24,  'i',  'l', 's', 't',                                               /* ilst, 8 + 16 */
        0,   0,   0,   16,  0xa9, 'm', 'a', 'k', 'S', 'o', 'm', 'e', 'P', 'h', 'o', 'n',       /* ©mak, 8 + 8 */
        0,   0,   0,   16,  'm',  'd', 'a', 't', 'f', 'r', 'a', 'm', 'e', 's', '!', '!'};      /* mdat, 16 */
    if (room < sizeof box) return 0;
    memcpy(to, box, sizeof box);
    return sizeof box;
}

static void test_images(void) {
    char png[256] = {0}, jpg[64] = {0}, webp[128] = {0};
    size_t n = 8, m;
    /* PNG: its text and time go, its header and data stay */
    memcpy(png, "\x89PNG\r\n\x1a\n", 8);
    n += chunk(png + n, "IHDR", "0123456789abc", 13);
    n += chunk(png + n, "tEXt", "Author\0someone", 14);
    n += chunk(png + n, "eXIf", "MM\0*gps", 7);
    n += chunk(png + n, "IDAT", "pixels", 6);
    n += chunk(png + n, "IEND", "", 0);
    SAME(tr_image_ext(png, n), "png");
    m = tr_strip_image(png, n);
    CHECK(m == n - 26 - 19);
    CHECK(!found(png, m, "someone") && !found(png, m, "gps") && found(png, m, "pixels"));
    CHECK(!tr_strip_image(png, 20)); /* cut short */
    /* JPEG: APP1 (EXIF) and a comment go, JFIF's APP0 and the scan stay */
    memcpy(jpg,
           "\xff\xd8"
           "\xff\xe0\x00\x04JF"
           "\xff\xe1\x00\x07"
           "Exif!"
           "\xff\xfe\x00\x04hi"
           "\xff\xda\x00\x02scan\xff\xd9",
           33);
    m = tr_strip_image(jpg, 33);
    CHECK(m == 33 - 9 - 6);
    CHECK(!found(jpg, m, "Exif") && found(jpg, m, "JF") && found(jpg, m, "scan"));
    CHECK(!tr_strip_image("\xff\xd8\xff\xd9", 4)); /* no scan */
    { /* what follows its end goes (a motion photo's video); stuffed bytes, restarts and tables stay, APP1 goes */
        static const char shown[] = "\xff\xd8"
                                    "\xff\xda\x00\x02"
                                    "ab\xff\x00"
                                    "c\xff\xd0"
                                    "d\xff\xff\xc4\x00\x03h"
                                    "\xff\xda\x00\x02"
                                    "ef"
                                    "\xff\xd9";
        static const char two[] = "\xff\xd8\xff\xda\x00\x02"
                                  "ab"
                                  "\xff\xe1\x00\x05"
                                  "gps" /* an APP1 between two scans */
                                  "\xff\xda\x00\x02"
                                  "ef"
                                  "\xff\xd9";
        char trailed[96];
        size_t k = sizeof shown - 1;
        memcpy(trailed, shown, k);
        memcpy(trailed + k, "ftypmp42 moov gps 52.1N", 23);
        m = tr_strip_image(trailed, k + 23);
        CHECK(m == k && !memcmp(trailed, shown, k));
        CHECK(!found(trailed, m, "gps"));
        memcpy(trailed, two, sizeof two - 1);
        m = tr_strip_image(trailed, sizeof two - 1);
        CHECK(m == 16 && !found(trailed, m, "gps") && found(trailed, m, "ef\xff\xd9"));
        memcpy(trailed, two, sizeof two - 1);
        CHECK(tr_strip_image(trailed, 20) == 13); /* cut in its second scan: as far as it goes, its APP1 out */
    }
    /* WebP: EXIF and XMP go, VP8X's flags for them are cleared, RIFF's size follows */
    memcpy(webp, "RIFF\x2c\x00\x00\x00WEBP", 12);
    memcpy(webp + 12, "VP8X\x02\x00\x00\x00\x0c\x00", 10);
    memcpy(webp + 22, "VP8L\x03\x00\x00\x00px!\x00", 12);
    memcpy(webp + 34, "EXIF\x02\x00\x00\x00gp", 10);
    m = tr_strip_image(webp, 44);
    CHECK(!m); /* RIFF says 0x2c + 8 = 52 bytes, and the file has 44 */
    webp[4] = 0x24;
    m = tr_strip_image(webp, 44);
    CHECK(m == 34 && webp[4] == 26 && !webp[20]);
    CHECK(!found(webp, m, "EXIF") && found(webp, m, "px!"));
    SAME(tr_image_ext("GIF89a...", 9), "gif");
    SAME(tr_image_ext("P6\n2 1\n255\n", 11), "ppm");
    {
        /* an MP4's place and make blanked where they are, its length and media as they were */
        char clip[256];
        size_t len = mp4(clip, sizeof clip);
        SAME(tr_video_ext(clip, len), "mp4");
        CHECK(tr_strip_video(clip, len) == len);
        CHECK(!found(clip, len, "\xa9xyz") && !found(clip, len, "+40.71") && !found(clip, len, "SomePhon"));
        CHECK(found(clip, len, "frames!!") && !found(clip, len, "udta") && !found(clip, len, "ilst"));
        { /* a phone's place as its maker keeps it: meta's keys and ilst, and XMP's uuid box; its media as it was */
            static const char xmp_id[] = "\xbe\x7a\xcf\xcb\x97\xa9\x42\xe8\x9c\x71\x99\x94\x91\xe3\xaf\xac";
            char phone[512];
            size_t at = 0, moov, meta, trak;
            at += mp4_box(phone + at, "ftyp", "qt  \0\0\0\0", 8);
            moov = at;
            at += 8;
            meta = at;
            at += 8;
            at += mp4_box(phone + at, "hdlr", "\0\0\0\0\0\0\0\0mdta", 12);
            at += mp4_box(phone + at, "keys",
                          "\0\0\0\0\0\0\0\1\0\0\0\x2d"
                          "mdtacom.apple.quicktime.location.ISO6709",
                          8 + 45);
            at += mp4_box(phone + at, "ilst",
                          "\0\0\0\x1a\0\0\0\1\0\0\0\x12"
                          "data+52.1000+004.3000/",
                          26);
            box_head(phone + meta, "meta", at - meta);
            trak = at;
            at += 8;
            at += mp4_box(phone + at, "udta", "\0\0\0\x0e\xa9xyz+52.1/", 14);
            box_head(phone + trak, "trak", at - trak);
            memcpy(phone + at + 8, xmp_id, 16);
            memcpy(phone + at + 24, "<exif:GPSLatitude>52,6</exif:GPSLatitude>", 41);
            box_head(phone + at, "uuid", 8 + 16 + 41);
            at += 8 + 16 + 41;
            box_head(phone + moov, "moov", at - moov);
            at += mp4_box(phone + at, "mdat", "frames!!", 8);
            CHECK(tr_strip_video(phone, at) == at);
            CHECK(!found(phone, at, "ISO6709") && !found(phone, at, "+52.1") && !found(phone, at, "GPSLatitude"));
            CHECK(found(phone, at, "frames!!") && found(phone, at, "moov") && found(phone, at, "trak"));
        }
        len = mp4(clip, sizeof clip);
        clip[19] = 100; /* moov's size past the file */
        CHECK(!tr_strip_video(clip, len));
        SAME(tr_video_ext("\x1a\x45\xdf\xa3\x9f\x42\x82\x84webm", 12), "webm");
        CHECK(!tr_video_ext("\x1a\x45\xdf\xa3\x9f\x42\x82\x88matroska", 16)); /* Matroska, not WebM */
        CHECK(!tr_video_ext("RIFF....AVI ", 12));
    }
    {
        /* a PPM with a comment, as a PNG: its header, and its pixels after each row's filter byte */
        static const char ppm[] = "P6\n# a frame\n2 1\n255\nRGBrgb";
        unsigned char rows[16];
        uLongf rows_n = sizeof rows;
        Buf out = {0};
        const unsigned char* p;
        CHECK(!tr_ppm_png(ppm, sizeof ppm - 1, &out));
        p = (const unsigned char*)out.text;
        CHECK(out.len > 45 && !memcmp(p, "\x89PNG\r\n\x1a\n", 8) && !memcmp(p + 12, "IHDR", 4));
        CHECK(p[19] == 2 && p[23] == 1 && p[24] == 8 && p[25] == 2); /* 2x1, 8 bits, RGB */
        CHECK(crc32(0, p + 12, 17) == ((uLong)p[29] << 24 | (uLong)p[30] << 16 | (uLong)p[31] << 8 | p[32]));
        CHECK(!memcmp(p + 37, "IDAT", 4));
        CHECK(uncompress(rows, &rows_n, p + 41, (uLong)(p[33] << 24 | p[34] << 16 | p[35] << 8 | p[36])) == Z_OK);
        CHECK(rows_n == 7 && !memcmp(rows, "\0RGBrgb", 7));
        CHECK(!memcmp(p + out.len - 8, "IEND", 4));
        CHECK(tr_ppm_png("P6\n2 1\n255\nRGB", 15, &out));      /* not whole */
        CHECK(tr_ppm_png("P6\n1 1\n65535\n123456", 20, &out)); /* 16 bits a channel */
        CHECK(tr_ppm_png("GIF89a...", 9, &out));
        buf_free(&out);
    }
    { /* a GIF keeps its picture and its loop, not a comment, an editor's XMP or what follows its end */
        static const char head[] = "GIF89a\1\0\1\0\x80\0\0" /* 1x1, a global table of 2 colours */
                                   "\0\0\0\xff\xff\xff";
        static const char loop[] = "\x21\xff\x0bNETSCAPE2.0\x03\x01\0\0\0";
        static const char note[] = "\x21\xfe\x05where\0";
        static const char xmp[] = "\x21\xff\x0bXMP DataXMP\x04GPS!\0";
        static const char frame[] = "\x21\xf9\x04\0\0\0\0\0"                    /* its control */
                                    "\x2c\0\0\0\0\1\0\1\0\0\x02\x02\x44\x01\0"; /* the image */
        char gif[256], want[256];
        size_t gn = 0, gw = 0;
#define PUT(to, at, part)                                                                                              \
    do {                                                                                                               \
        memcpy(to + at, part, sizeof part - 1);                                                                        \
        at += sizeof part - 1;                                                                                         \
    } while (0)
        PUT(gif, gn, head);
        PUT(gif, gn, loop);
        PUT(gif, gn, note);
        PUT(gif, gn, xmp);
        PUT(gif, gn, frame);
        PUT(gif, gn,
            "\x3b"
            "MOTION");
        PUT(want, gw, head);
        PUT(want, gw, loop);
        PUT(want, gw, frame);
        PUT(want, gw, "\x3b");
#undef PUT
        CHECK(tr_strip_image(gif, gn) == gw && !memcmp(gif, want, gw));
        CHECK(!found(gif, gw, "where") && !found(gif, gw, "GPS") && !found(gif, gw, "MOTION"));
        CHECK(!tr_strip_image("GIF89a...", 9)); /* its screen cut short */
        memcpy(gif, want, gw);
        CHECK(tr_strip_image(gif, gw - 1) == gw - 1 && !memcmp(gif, want, gw - 1)); /* no trailer: as it is */
        memcpy(gif, want, gw - 1);
        memcpy(gif + gw - 1, "\x21\xfe\x05wh", 5); /* a comment cut short after the image: what came before */
        CHECK(tr_strip_image(gif, gw + 4) == gw && !memcmp(gif, want, gw));
        memcpy(gif, want, gw);
        CHECK(!tr_strip_image(gif, gw - 3)); /* its only image cut short */
        memcpy(gif, want, gw);
        CHECK(!tr_strip_image(gif, sizeof head - 1 + sizeof loop - 3)); /* a block cut short before any image */
        memcpy(gif, want, gw);
        gif[sizeof head - 1] = 0x42; /* no block of a GIF's */
        CHECK(!tr_strip_image(gif, gw));
    }
    CHECK(!tr_image_ext("<svg>", 5));
}

static void test_unpack(void) {
    Buf out = {0};
    char *why, *data;
    size_t n;
    data = packed(0, "data", LOG, &n);
    CHECK(!tr_unpack("game.log.gz", data, n, TR_LOG_MAX, &out, &why));
    CHECK(out.len == strlen(LOG) && !memcmp(out.text, LOG, out.len));
    CHECK(tr_unpack("game.log.gz", data, n, 100, &out, &why) && strstr(why, "over"));
    free(why);
    data = packed(1, "logs/game.log", LOG, &n);
    CHECK(!tr_unpack("logs.ZIP", data, n, TR_LOG_MAX, &out, &why));
    CHECK(out.len == strlen(LOG));
    CHECK(tr_unpack("bad.zip", "PK\3\4 nothing", 13, TR_LOG_MAX, &out, &why));
    free(why);
    CHECK(!tr_unpack("plain.txt", "text", 4, TR_LOG_MAX, &out, &why) && out.len == 4);
    CHECK(tr_unpack("plain.txt", "text", 4, 3, &out, &why));
    free(why);
    { /* a Share Log zip as ScarletNX writes it, each name flagged UTF-8, a game's folder named with an accent before its
         log: read through, in the C locale this test runs in as a worker does */
        static char zip[1 << 16];
        const char* names[] = {"games/Some G\xc3\xa1me [0100AAAABBBB0000]/old.dat",
                               "games/Some G\xc3\xa1me [0100AAAABBBB0000]/2026-11-07_21-45-11.log"};
        const char* texts[] = {"older bytes\n", LOG};
        size_t k;
        n = zip_bytes(zip, sizeof zip, names, texts, NULL, 2, ZIP_UTF8);
        CHECK(!tr_unpack("scarletnx-logs-2026-11-07_21-46-12.zip", zip, n, TR_LOG_MAX, &out, &why));
        CHECK(out.len == strlen(LOG) && !memcmp(out.text, LOG, out.len));
        /* and one flagged UTF-8 whose name is none (a byte 0xff), which libarchive gives with ARCHIVE_WARN: passed by */
        names[0] = "games/oldXname.dat";
        n = zip_bytes(zip, sizeof zip, names, texts, NULL, 2, ZIP_UTF8);
        for (k = 0; k + 18 <= n; ++k)
            if (!memcmp(zip + k, "games/oldXname.dat", 18)) zip[k + 9] = (char)0xff;
        CHECK(!tr_unpack("logs.zip", zip, n, TR_LOG_MAX, &out, &why));
        CHECK(out.len == strlen(LOG) && !memcmp(out.text, LOG, out.len));
    }
    buf_free(&out);
    /* the name the unpacked text is saved as: never an archive's, as it is no archive any more */
    {
        static const char* const names[][2] = {
            {"logs.zip", "logs.log"},
            {"scarletnx-logs-2026-11-07.ZIP", "scarletnx-logs-2026-11-07.log"},
            {"game.log.gz", "game.log"},
            {"notes.TXT.gz", "notes.TXT"},
            {"run.gz", "run.log"},
            {"backup.tar.gz", "backup.tar.log"},
            {"scarletnx.log", "scarletnx.log"},
            {"plain.txt", "plain.txt"},
            {".zip", ".zip"} /* no name before it: tr_unpack takes it as text, not a zip */};
        size_t i;
        for (i = 0; i < sizeof names / sizeof *names; ++i) {
            char* got = tr_unpacked_name(names[i][0]);
            SAME(got, names[i][1]);
            free(got);
        }
    }
}

/* the log's summary as the bot makes it, owned */
static char* summarized(const char* text, size_t n) {
    SnxLogScan* s = calloc(1, sizeof *s);
    char* json = NULL;
    if (s && !snx_logscan(s, text, n)) json = snx_logscan_json(s, snx_demangle);
    snx_logscan_free(s);
    free(s);
    return json;
}

/* the crash report's code and memory dumps and registers stay off the site, as they stay out of the summary */
static void test_public_log(void) {
    static const char crash[] = "[   0.00] ScarletNX - Some Game\n"
                                "[   2.25]\n"
                                "*** The game crashed: signal 11 at 1300100df8 ***\n"
                                "[   2.25]   pc=164ab3c4 lr=163d4828 sp=1300100c30\n"
                                "[   2.25]   x0= 0  x1= 1300100c30  x2= 5  x3= 0\n"
                                "[   2.25]   r3 = 12\n"
                                "[   2.25]   code 164ab380: d10083ff  a9017bfd [b9400000] d65f03c0\n"
                                "[   2,25]   mem x1 1300100c30: 00000000 0000002a\n"
                                "  code at lr 163d4800: 94000010 f9400260\n"
                                "[   2.25]   info+00: 0000000000000001\n"
                                "[   2.25]    #0 163d18d8 <_ZN2nn4diag6detail10VAbortImplEv+d8>\n"
                                "[   2.25]   pc <main+10>  lr <main+4>\n"
                                "[   2.25] code that the game wrote\n"
                                "[   2.25] xenon=1 is a setting\n"
                                "the last line, no newline";
    size_t n, i;
    char *shown = tr_public_log(crash, sizeof crash - 1, &n), *before, *after, *source, *where;
    CHECK(n == strlen(shown));
    LACKS(shown, "pc=164ab3c4");
    LACKS(shown, "x1= 1300100c30");
    LACKS(shown, "r3 = 12");
    LACKS(shown, "d10083ff");
    LACKS(shown, "0000002a");
    LACKS(shown, "94000010");
    LACKS(shown, "info+00");
    HAS(shown, "*** The game crashed: signal 11 at 1300100df8 ***\n");
    HAS(shown, "#0 163d18d8 <_ZN2nn4diag6detail10VAbortImplEv+d8>\n");
    HAS(shown, "[   2.25]   pc <main+10>  lr <main+4>\n"); /* the symbols' line is no dump */
    HAS(shown, "[   2.25] code that the game wrote\n");    /* not indented: the game's own words */
    HAS(shown, "[   2.25] xenon=1 is a setting\n");
    HAS(shown, "the last line, no newline");
    /* the summary reads the log shown as it read the whole one */
    before = summarized(crash, sizeof crash - 1);
    after = summarized(shown, n);
    SAME(after, before);
    free(before);
    free(after);
    free(shown);
    shown = tr_public_log("", 0, &n);
    CHECK(shown && !*shown && !n);
    free(shown);
    { /* the players' addresses: IPv4 but the loopback's, 0.0.0.0 and masks, and e-mail; versions stay */
        static const char net[] = "ldn: this console is 192.168.1.23/255.255.255.0, the game's sockets bind there\n"
                                  "ldn: Bob joined as node 1 from 203.0.113.5, 2 of 8 nodes\n"
                                  "room: joined relay.example.org:24872 as JohnS, at 10.13.0.2:5000, 3 members\n"
                                  "net: 127.0.0.2 and 0.0.0.0 stay\n"
                                  "vulkan: 1.3.250.0.1, Windows 10.0.26100.1 and 300.1.1.1 stay\n"
                                  "room: Alice says: write to alice.b+x@mail.example.com.\n"
                                  "memcpy@GLIBC_2.14 and vkGet@1.3 stay";
        shown = tr_public_log(net, sizeof net - 1, &n);
        SAME(shown, "ldn: this console is [ip]/255.255.255.0, the game's sockets bind there\n"
                    "ldn: Bob joined as node 1 from [ip], 2 of 8 nodes\n"
                    "room: joined relay.example.org:24872 as JohnS, at [ip]:5000, 3 members\n"
                    "net: 127.0.0.2 and 0.0.0.0 stay\n"
                    "vulkan: 1.3.250.0.1, Windows 10.0.26100.1 and 300.1.1.1 stay\n"
                    "room: Alice says: write to [email].\n"
                    "memcpy@GLIBC_2.14 and vkGet@1.3 stay");
        CHECK(n == strlen(shown));
        free(shown);
    }
    { /* a home folder's name goes whole, spaces and all, as take_log puts ~ in a log for the site: with the summary of
         before ScarletNX #986 (7bde830fb), C:\Users\John Smith\x reached a public report as ~ Smith\x */
        static const char homes[] = "[   1.04] saved in C:\\Users\\John Smith\\AppData\\x.sav\n"
                                    "[   1.05] loading /home/Ana Maria/games/x.nsp\n"
                                    "[   1.06] saved in C:\\Users\\John Smith\n";
        char* tilded = snx_logscan_tilde(homes, sizeof homes - 1, &n);
        SAME(tilded, "[   1.04] saved in ~\\AppData\\x.sav\n"
                     "[   1.05] loading ~/games/x.nsp\n"
                     "[   1.06] saved in ~\n");
        LACKS(tilded, "Smith");
        LACKS(tilded, "Maria");
        CHECK(tilded && n == strlen(tilded));
        free(tilded);
    }
    /* each prefix the log summary's dump() leaves out is left out here: a prefix added there fails this until it is */
    source = read_file("third_party/logscan/common/utilities/logscan.c", NULL);
    where = source ? strstr(source, "static int dump(Span line)") : NULL;
    where = where ? strstr(where, "prefixes[] = {") : NULL;
    CHECK(where != NULL);
    for (i = 0; where && *where != '}'; ++where)
        if (*where == '"') {
            size_t len = strcspn(where + 1, "\"");
            char* line = rei_format("[   2.25]   %.*s 1234: 00\n", (int)len, where + 1);
            char* left = tr_public_log(line, strlen(line), &n);
            CHECK(n == 0);
            if (n) fprintf(stderr, "  a dump line is shown: %s", line);
            free(left);
            free(line);
            where += len + 1;
            ++i;
        }
    CHECK(i == 9);
    free(source);
}

static void game(const char* summary, const char* note, int ok, const char* key, const char* title, const char* tid,
                 int named) {
    cJSON* s = cJSON_Parse(summary);
    char *k, *t, *id;
    int n;
    CHECK(tr_game(s, note, &k, &t, &id, &n) == (ok ? 0 : -1));
    if (ok) {
        SAME(k, key);
        SAME(t, title);
        CHECK(tid ? id && !strcmp(id, tid) : !id);
        CHECK(n == named);
    }
    free(k);
    free(t);
    free(id);
    cJSON_Delete(s);
}
static void test_game(void) {
    game("{\"title\":\"Some Game\",\"title_id\":\"0100aaaabbbb0000\"}", "", 1, "0100AAAABBBB0000", "Some Game",
         "0100AAAABBBB0000", 1);
    game("{\"title\":\"SUPER GAME (0100ABAB00010000/0100ABAB00010000)\",\"title_id\":null}", "", 1, "0100ABAB00010000",
         "SUPER GAME", "0100ABAB00010000", 1);
    game("{\"title\":\"0100F00D12340000\"}", "it stutters", 1, "0100F00D12340000", "0100F00D12340000",
         "0100F00D12340000", 0);
    game("{\"title\":\"E\xef\xbf\xbdJ\x01G\",\"title_id\":\"0100BEEF0000A000\"}", "", 1, "0100BEEF0000A000",
         "0100BEEF0000A000", "0100BEEF0000A000", 0);
    game("{\"title\":\"0100F00D12340000\"}", "", 1, "0100F00D12340000", "0100F00D12340000", "0100F00D12340000", 0);
    game("{\"title\":\"Tale: Of a Made Up Game\"}", "", 1, "n-tale-of-a-made-up-game", "Tale: Of a Made Up Game", NULL,
         1);
    game("{\"build\":\"ScarletNX main 1\"}", "My Game!", 1, "n-my-game", "My Game!", NULL, 0);
    game("{\"build\":\"ScarletNX main 1\"}", "\xe3\x82\xb2\xe3\x83\xbc\xe3\x83\xa0", 1, "n-0adc07b2",
         "\xe3\x82\xb2\xe3\x83\xbc\xe3\x83\xa0", NULL, 0);
    game("{\"build\":\"ScarletNX main 1\"}", "", 0, NULL, NULL, NULL, 0);
}

/* ---- whole reports ---- */
static char site[64];
static Tracer tracer;
/* user 88 administers guild 33 through role 5, and guild 34 through role 6 */
static int fake_administers(void* ctx, u64 guild, u64 user, const u64* roles, int nroles) {
    (void)ctx;
    (void)user;
    return nroles == 1 && ((guild == 33 && roles[0] == 5) || (guild == 34 && roles[0] == 6));
}

static TrMessage message(u64 id, u64 author, const char* content) {
    TrMessage m;
    memset(&m, 0, sizeof m);
    m.id = id;
    m.channel = 22;
    m.guild = 33;
    m.author = author;
    m.mentioned = 1;
    m.content = rei_strdup(content);
    return m;
}
static void attach(TrMessage* m, const char* name, const char* url, double size) {
    TrAttachment* a = &m->files[m->nfiles++];
    a->name = rei_strdup(name);
    a->url = rei_strdup(url);
    a->type = rei_strdup("");
    a->size = size;
}
/* the bot's answer to m, its reply linked to its report as main.c does; the reply's id in last_reply */
static u64 last_reply = 900000;
static cJSON* respond(const TrMessage* m) {
    long report;
    cJSON* e = tr_answer(&tracer, m, &report);
    if (e) tr_replied(&tracer, report, ++last_reply);
    return e;
}
static char* at(const char* rel) { return rei_format("%s/%s", site, rel); }
static char* page(const char* rel) {
    char *path = at(rel), *text = read_file(path, NULL);
    free(path);
    return text;
}
static char* answer_text(cJSON* e) {
    char* t = cJSON_PrintUnformatted(e);
    cJSON_Delete(e);
    return t;
}

static void test_reports(void) {
    static char png[128];
    size_t npng = 8;
    TrMessage m;
    char *t, *p, *path;
    StReport r;
    StFile* files;
    int nfiles, i;
    memcpy(png, "\x89PNG\r\n\x1a\n", 8);
    npng += chunk(png + npng, "IHDR", "0123456789abc", 13);
    npng += chunk(png + npng, "tEXt", "GPS here", 8);
    npng += chunk(png + npng, "IEND", "", 0);
    serve("https://cdn.discordapp.com/1/game.log", LOG, strlen(LOG));
    serve("https://cdn.discordapp.com/1/shot.png", png, npng);
    serve("https://cdn.discordapp.com/1/notes.txt", "just some notes\n", 16);

    /* a report: the log and the screenshot kept, the other file named */
    m = message(1001, 44, "<@99> crashes at the <b>title</b> screen");
    attach(&m, "game.log", "https://cdn.discordapp.com/1/game.log", (double)strlen(LOG));
    attach(&m, "shot.png", "https://cdn.discordapp.com/1/shot.png", (double)npng);
    attach(&m, "setup.exe", "https://cdn.discordapp.com/1/setup.exe", 5);
    t = answer_text(respond(&m));
    HAS(t, "Report #1: Some Game");
    HAS(t, "https://reports.example/r/1/");
    HAS(t, "svcBreak");
    HAS(t, "1 log, 1 screenshot");
    HAS(t, "Missed: 1 service command"); /* the log's undeclared command */
    HAS(t, "setup.exe: not a log or a screenshot");
    HAS(t, "@Tracer delete 1");
    free(t);
    CHECK(downloads == 2);
    CHECK(st_get(tracer.store, 1, &r, &files, &nfiles) == 1);
    SAME(r.key, "0100AAAABBBB0000");
    SAME(r.outcome, "svcBreak");
    SAME(r.commit, "1234abc");
    SAME(r.note, "crashes at the <b>title</b> screen");
    CHECK(r.poster == 44 && r.message == 1001 && r.named);
    CHECK(nfiles == 2);
    SAME(files[0].path, "1.log");
    SAME(files[1].path, "2.png");
    HAS(files[0].summary, "\"outcome\"");
    st_report_free(&r);
    st_files_free(files, nfiles);
    /* the log keeps no home folder, the screenshot no text */
    path = at("r/1/1.log");
    p = read_file(path, NULL);
    HAS(p, "loading ~/save.bin");
    LACKS(p, "someone");
    LACKS(p, "pc=164ab3c4"); /* the registers' line stays off the site */
    HAS(p, "#1 1683a358 <_ZN2nn3hid18GetNpadJoyHoldTypeEv+38>");
    free(p);
    free(path);
    path = at("r/1/2.png");
    p = read_file(path, NULL);
    CHECK(p && !strstr(p + 8, "GPS"));
    free(p);
    free(path);
    /* the pages: everything the log and the message said escaped */
    p = page("r/1/index.html");
    HAS(p, "<h1>Report #1</h1>");
    /* the theme picker on every page, its links from the site's root however deep the page: the nginx answers them */
    HAS(p, "<body>\n<nav class=\"sws\" aria-label=\"Theme\">");
    HAS(p,
        "<a href=\"/sws/fuddles\" referrerpolicy=\"same-origin\"><img src=\"/sws/logo-fuddles.svg\" alt=\"\">Fuddles</a>");
    HAS(p, "<a href=\"/sws/dark\" referrerpolicy=\"same-origin\">Dark</a></nav>\n<header class=\"top\">");
    HAS(p, "<span class=\"badge missed\" title=\"Missed: 1 service command\">something missed</span>");
    HAS(p, "<dt>Missed</dt><dd>1 service command</dd>");
    HAS(p, "crashes at the &lt;b&gt;title&lt;/b&gt; screen");
    LACKS(p, "<b>title</b>");
    HAS(p, "~/save.bin &lt;script&gt;");
    HAS(p, "nn::hid::GetNpadJoyHoldType()");
    HAS(p, "https://github.com/scarletnx/ScarletNX/commit/1234abc");
    HAS(p, "<img src=\"2.png\"");
    HAS(p, "href=\"1.log\" download=\"game.log\"");
    HAS(p, "href=\"../../g/0100AAAABBBB0000.html\"");
    free(p);
    p = page("g/0100AAAABBBB0000.html");
    HAS(p, "<h1>Some Game</h1>");
    HAS(p, "href=\"../r/1/\"");
    free(p);
    p = page("index.html");
    HAS(p, "href=\"g/0100AAAABBBB0000.html\"");
    HAS(p, "<b>1</b> game, <b>1</b> report");
    free(p);
    p = page("all.html");
    HAS(p, "href=\"r/1/\"");
    HAS(p, "data-missed=\"something missed\"");
    HAS(p, "<select data-key=\"missed\"");
    HAS(p,
        "<select data-key=\"console\" data-fixed aria-label=\"Console\"><option value=\"\">Console: all</option><option "
        "value=\"Switch\">Switch</option><option value=\"3DS\">3DS</option>"); /* every console, never hidden */
    free(p);
    /* the JSON: the report, its files and summaries, and no Discord id */
    p = page("api/r/1.json");
    HAS(p, "\"game\":\"0100AAAABBBB0000\"");
    HAS(p, "\"path\":\"r/1/1.log\"");
    HAS(p, "\"missed\":\"1 service command\"");
    HAS(p, "\"summary\":{");
    HAS(p, "\"note\":\"crashes at the <b>title</b> screen\"");
    LACKS(p, "1001");
    LACKS(p, "\"44\"");
    free(p);
    p = page("api/games.json");
    HAS(p, "\"reports\":1");
    free(p);
    p = page("api/reports.json");
    HAS(p, "\"id\":1,");
    free(p);

    /* the same message again (the gateway after a resume): no reply, no second report */
    CHECK(!respond(&m));
    tr_message_free(&m);

    /* no log, a file that is no ScarletNX log, a broken download, a log without a game */
    m = message(1002, 44, "<@99> look");
    attach(&m, "shot.png", "https://cdn.discordapp.com/1/shot.png", (double)npng);
    t = answer_text(respond(&m));
    HAS(t, "Attach the game's ScarletNX log");
    free(t);
    tr_message_free(&m);
    m = message(1003, 44, "<@99>");
    attach(&m, "notes.txt", "https://cdn.discordapp.com/1/notes.txt", 16);
    attach(&m, "gone.log", "https://cdn.discordapp.com/1/gone.log", 16);
    attach(&m, "elsewhere.log", "https://example.com/x.log", 16);
    attach(&m, "huge.log", "https://cdn.discordapp.com/1/huge.log", 1e9);
    t = answer_text(respond(&m));
    HAS(t, "No ScarletNX log here");
    HAS(t, "notes.txt: not a ScarletNX log");
    HAS(t, "gone.log: the download failed");
    HAS(t, "elsewhere.log: not a file on Discord");
    HAS(t, "huge.log: over 64 MiB");
    free(t);
    tr_message_free(&m);

    /* deletion: someone else's refused, an admin's or the poster's done, a message deleted on Discord */
    m = message(1004, 55, "<@99> delete 1");
    t = answer_text(respond(&m));
    HAS(t, "is not yours");
    free(t);
    tr_message_free(&m);
    m = message(1010, 88, "<@99> delete 1");
    m.roles[m.nroles++] = 6;
    t = answer_text(respond(&m));
    HAS(t, "is not yours");
    free(t);
    tr_message_free(&m);
    path = at("r/1/1.log");
    CHECK(exists(path));
    m = message(1005, 88, "<@99> delete #1");
    m.roles[m.nroles++] = 5; /* an administrator */
    t = answer_text(respond(&m));
    HAS(t, "Report #1 is deleted");
    free(t);
    tr_message_free(&m);
    CHECK(!exists(path));
    free(path);
    path = at("api/r/1.json");
    CHECK(!exists(path));
    free(path);
    path = at("g/0100AAAABBBB0000.html");
    CHECK(!exists(path));
    free(path);
    p = page("index.html");
    HAS(p, "No reports yet");
    free(p);
    m = message(1006, 44, "<@99> delete 1");
    t = answer_text(respond(&m));
    HAS(t, "There is no report #1");
    free(t);
    tr_message_free(&m);

    /* a log that names its game but not its title ID goes with the reports that know the ID */
    {
        static char named[sizeof LOG];
        char* cut;
        strcpy(named, LOG);
        cut = strstr(named, "[   0.30] game:");
        memmove(cut, strchr(cut, '\n') + 1, strlen(strchr(cut, '\n') + 1) + 1);
        serve("https://cdn.discordapp.com/2/named.log", named, strlen(named));
    }
    m = message(1007, 66, "<@99>");
    attach(&m, "game.log", "https://cdn.discordapp.com/1/game.log", 10);
    free(answer_text(respond(&m)));
    tr_message_free(&m);
    m = message(1008, 66, "<@99>");
    attach(&m, "named.log", "https://cdn.discordapp.com/2/named.log", 10);
    free(answer_text(respond(&m)));
    tr_message_free(&m);
    CHECK(st_get(tracer.store, 3, &r, NULL, NULL) == 1);
    SAME(r.key, "0100AAAABBBB0000");
    CHECK(!r.title_id);
    st_report_free(&r);
    CHECK(tr_forget(&tracer, 1008));
    CHECK(!tr_forget(&tracer, 1008));
    CHECK(st_get(tracer.store, 3, &r, NULL, NULL) == 0);
    /* a log with the title ID alone takes the name other reports gave the game */
    {
        static char bare[sizeof LOG + 16];
        char* name = strstr(strcpy(bare, LOG), "Some Game");
        memmove(name + 16, name + 9, strlen(name + 9) + 1);
        memcpy(name, "0100AAAABBBB0000", 16);
        serve("https://cdn.discordapp.com/2/bare.log", bare, strlen(bare));
    }
    m = message(1009, 66, "<@99> it stutters");
    attach(&m, "bare.log", "https://cdn.discordapp.com/2/bare.log", 10);
    t = answer_text(respond(&m));
    HAS(t, "Report #4: Some Game");
    free(t);
    tr_message_free(&m);
    CHECK(st_get(tracer.store, 4, &r, NULL, NULL) == 1);
    CHECK(!r.named);
    SAME(r.note, "it stutters");
    st_report_free(&r);
    CHECK(tr_forget(&tracer, 1009));

    /* ten reports in an hour, then a wait */
    for (i = 0; i < TR_HOURLY; ++i) {
        m = message(2000 + (u64)i, 77, "<@99>");
        attach(&m, "game.log", "https://cdn.discordapp.com/1/game.log", 10);
        t = answer_text(respond(&m));
        if (i == TR_HOURLY - 1) HAS(t, "Report #");
        free(t);
        tr_message_free(&m);
    }
    m = message(2100, 77, "<@99>");
    attach(&m, "game.log", "https://cdn.discordapp.com/1/game.log", 10);
    t = answer_text(respond(&m));
    HAS(t, "in the last hour");
    free(t);
    now_is += 3601;
    t = answer_text(respond(&m));
    HAS(t, "Report #");
    free(t);
    tr_message_free(&m);

    /* updates: by a reply or "add N", for the sender and administrators, within the limits, taken back with their
       message; a log that names a game the report did not */
    {
        static char other[sizeof LOG + 16], other_named[sizeof LOG + 16];
        char *name, *id;
        u64 answer;
        long report;
        strcpy(other, LOG);
        name = strstr(other, "Some Game");
        memmove(name + 16, name + 9, strlen(name + 9) + 1);
        memcpy(name, "0100CCCCDDDD0000", 16);
        id = strstr(other, "0100aaaabbbb0000");
        memcpy(id, "0100ccccdddd0000", 16);
        strcpy(other_named, LOG);
        memcpy(strstr(other_named, "Some Game"), "Next Game", 9);
        memcpy(strstr(other_named, "0100aaaabbbb0000"), "0100ccccdddd0000", 16);
        serve("https://cdn.discordapp.com/3/other.log", other, strlen(other));
        serve("https://cdn.discordapp.com/3/named.log", other_named, strlen(other_named));
        m = message(4000, 101, "<@99>");
        attach(&m, "other.log", "https://cdn.discordapp.com/3/other.log", 10);
        t = answer_text(respond(&m));
        HAS(t, "Report #16: 0100CCCCDDDD0000");
        free(t);
        tr_message_free(&m);
        answer = last_reply;
        /* a reply to an answer the bot did not keep (posted before it kept them): its embed names the report */
        m = message(3999, 101, "<@99> from an old answer");
        m.reply_to = 777777;
        m.reply_report = 16;
        t = answer_text(respond(&m));
        HAS(t, "0 logs, 0 screenshots, a note");
        free(t);
        tr_message_free(&m);
        CHECK(st_report_of(tracer.store, 777777) == 16);
        /* someone else's reply is refused */
        m = message(4001, 102, "<@99> same here");
        m.reply_to = answer;
        attach(&m, "shot.png", "https://cdn.discordapp.com/1/shot.png", (double)npng);
        t = answer_text(respond(&m));
        HAS(t, "Report #16 is not yours");
        free(t);
        tr_message_free(&m);
        /* the sender's reply to the answer adds a screenshot and a note */
        m = message(4002, 101, "<@99> after the boss <i>");
        m.reply_to = answer;
        attach(&m, "shot.png", "https://cdn.discordapp.com/1/shot.png", (double)npng);
        t = answer_text(respond(&m));
        HAS(t, "Report #16: 0100CCCCDDDD0000");
        HAS(t, "0 logs, 1 screenshot, a note");
        free(t);
        CHECK(!respond(&m)); /* the gateway's event again */
        tr_message_free(&m);
        p = page("r/16/index.html");
        HAS(p, "after the boss &lt;i&gt;");
        HAS(p, "<img src=\"2.png\"");
        free(p);
        /* an administrator's "add 16", a note alone; nothing at all is refused */
        m = message(4003, 88, "<@99> add 16 also in the menu");
        m.roles[m.nroles++] = 5;
        t = answer_text(respond(&m));
        HAS(t, "0 logs, 0 screenshots, a note");
        free(t);
        tr_message_free(&m);
        m = message(4004, 101, "<@99> add 16");
        t = answer_text(respond(&m));
        HAS(t, "Nothing to add");
        free(t);
        tr_message_free(&m);
        /* three logs at most: the report has one, two more are kept; the second names the game */
        m = message(4005, 101, "<@99>");
        m.reply_to = 4000; /* the report's own message */
        attach(&m, "named.log", "https://cdn.discordapp.com/3/named.log", 10);
        attach(&m, "game.log", "https://cdn.discordapp.com/1/game.log", 10);
        attach(&m, "third.log", "https://cdn.discordapp.com/1/game.log", 10);
        t = answer_text(respond(&m));
        HAS(t, "Report #16: Next Game");
        HAS(t, "named by its new log");
        HAS(t, "2 logs, 0 screenshots");
        HAS(t, "third.log: a report keeps 3 logs");
        free(t);
        tr_message_free(&m);
        CHECK(st_get(tracer.store, 16, &r, &files, &nfiles) == 1);
        SAME(r.title, "Next Game");
        SAME(r.key, "0100CCCCDDDD0000");
        CHECK(r.named && nfiles == 4);
        SAME(files[2].path, "3.log");
        SAME(files[3].path, "4.log");
        st_report_free(&r);
        st_files_free(files, nfiles);
        p = page("g/0100CCCCDDDD0000.html");
        HAS(p, "<h1>Next Game</h1>");
        free(p);
        p = page("api/r/16.json");
        HAS(p, "\"notes_added\":[{");
        HAS(p, "also in the menu");
        free(p);
        /* a reply to the update's answer reaches the report too; a PPM frame is kept as a PNG */
        {
            static const char frame[] = "P6\n2 2\n255\nRGBrgbRGBrgb";
            serve("https://cdn.discordapp.com/1/frame.ppm", frame, sizeof frame - 1);
        }
        m = message(4006, 101, "<@99> add #16");
        attach(&m, "shot.png", "https://cdn.discordapp.com/1/shot.png", (double)npng);
        attach(&m, "frame-0-01.ppm", "https://cdn.discordapp.com/1/frame.ppm", 22);
        {
            static char clip[256];
            serve("https://cdn.discordapp.com/1/clip.mp4", clip, mp4(clip, sizeof clip));
        }
        attach(&m, "clip.mp4", "https://cdn.discordapp.com/1/clip.mp4", 90);
        attach(&m, "again.mp4", "https://cdn.discordapp.com/1/clip.mp4", 90);
        attach(&m, "third.mp4", "https://cdn.discordapp.com/1/clip.mp4", 90);
        t = answer_text(respond(&m));
        HAS(t, "0 logs, 2 screenshots, 2 videos");
        HAS(t, "third.mp4: a report keeps 2 videos");
        free(t);
        tr_message_free(&m);
        p = page("r/16/index.html");
        HAS(p, "<img src=\"6.png\" alt=\"frame-0-01.ppm\"");
        HAS(p, "<video controls preload=\"metadata\" src=\"7.mp4\"></video>");
        free(p);
        path = at("r/16/6.png");
        p = read_file(path, NULL);
        CHECK(p && !memcmp(p, "\x89PNG", 4));
        free(p);
        free(path);
        m = message(4007, 101, "<@99> delete");
        t = answer_text(respond(&m));
        HAS(t, "Which report?");
        free(t);
        tr_message_free(&m);
        /* the update's message deleted: its screenshot and its note go, the report stays */
        CHECK(tr_forget(&tracer, 4002));
        path = at("r/16/2.png");
        CHECK(!exists(path));
        free(path);
        p = page("r/16/index.html");
        LACKS(p, "after the boss");
        HAS(p, "also in the menu");
        free(p);
        CHECK(!tr_forget(&tracer, 4002));
        /* a reply with "delete" deletes the report it answers */
        m = message(4008, 101, "<@99> delete");
        m.reply_to = answer;
        t = answer_text(respond(&m));
        HAS(t, "Report #16 is deleted");
        free(t);
        tr_message_free(&m);
        CHECK(st_get(tracer.store, 16, &r, NULL, NULL) == 0);
        CHECK(st_report_of(tracer.store, answer) == 0); /* its messages went with it */
        path = at("r/16/5.png");
        CHECK(!exists(path));
        free(path);
        (void)report;
    }

    /* the help */
    m = message(3000, 44, "<@99> help");
    t = answer_text(respond(&m));
    HAS(t, "delete 12");
    free(t);
    tr_message_free(&m);

    /* every page drawn again, as after an upgrade */
    CHECK(!site_rebuild(tracer.store, site));
    p = page("index.html");
    HAS(p, "<b>1</b> game, <b>12</b> reports");
    free(p);
}

/* one JSON-RPC message to the MCP server: its answer and HTTP status */
static char* ask(const Mcp* m, const char* body, int* status) { return mcp_handle(m, body, strlen(body), status); }
static char* call(const Mcp* m, const char* tool, const char* args) {
    int status;
    char *body = rei_format("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\",\"params\":{\"name\":\"%s\","
                            "\"arguments\":%s}}",
                            tool, args),
         *text = ask(m, body, &status);
    CHECK(status == 200);
    free(body);
    return text;
}

/* the MCP server over the site test_reports left: report 2 and reports 5 to 15 of one game, all svcBreak */
static void test_mcp(void) {
    Mcp m;
    int status;
    char* t;
    cJSON* o;
    TrMessage add = message(5000, 66, "<@99> add 2");
    attach(&add, "shot.png", "https://cdn.discordapp.com/1/shot.png", 10);
    free(answer_text(respond(&add))); /* report 2 message_gets a screenshot, 2.png */
    tr_message_free(&add);
    m.root = site;
    m.url = "https://reports.example";
    t = ask(&m,
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":"
            "\"2025-03-26\",\"capabilities\":{},\"clientInfo\":{\"name\":\"t\",\"version\":\"1\"}}}",
            &status);
    CHECK(status == 200);
    HAS(t, "\"protocolVersion\":\"2025-03-26\"");
    HAS(t, "\"name\":\"tracer\"");
    HAS(t, "\"tools\":{\"listChanged\":false}");
    HAS(t, "\"id\":1");
    free(t);
    t = ask(&m,
            "{\"jsonrpc\":\"2.0\",\"id\":\"a\",\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"1999\"}}",
            &status);
    HAS(t, "\"protocolVersion\":\"2025-06-18\"");
    HAS(t, "\"id\":\"a\"");
    free(t);
    CHECK(!ask(&m, "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}", &status) && status == 202);
    t = ask(&m, "{nope", &status);
    CHECK(status == 400);
    HAS(t, "-32700");
    free(t);
    t = ask(&m, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"resources/list\"}", &status);
    HAS(t, "-32601");
    free(t);
    t = ask(&m, "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/list\"}", &status);
    o = cJSON_Parse(t);
    CHECK(cJSON_GetArraySize(jpath(o, "result.tools")) == 9); /* the reports' six, the runs' three */
    cJSON_Delete(o);
    HAS(t, "\"read_log\"");
    free(t);

    t = call(&m, "list_games", "{\"query\":\"some GAME\"}");
    HAS(t, "\\\"count\\\":1");
    HAS(t, "https://reports.example/g/0100AAAABBBB0000.html");
    free(t);
    t = call(&m, "list_games", "{}");
    HAS(t, "\\\"count\\\":1");
    free(t);
    t = call(&m, "list_games", "{\"console\":\"switch\"}");
    HAS(t, "\\\"count\\\":1");
    free(t);
    t = call(&m, "search_reports", "{\"console\":\"3DS\"}");
    HAS(t, "\\\"matched\\\":0");
    free(t);
    t = call(&m, "list_games", "{\"query\":\"made up\"}");
    HAS(t, "\\\"count\\\":0");
    free(t);
    t = call(&m, "search_reports", "{\"game\":\"0100aaaabbbb0000\",\"outcome\":\"SVCBREAK\",\"limit\":2}");
    HAS(t, "\\\"matched\\\":12");
    HAS(t, "\\\"shown\\\":2");
    HAS(t, "https://reports.example/r/15/");
    LACKS(t, "r/13/");
    free(t);
    t = call(&m, "search_reports", "{\"signature\":\"GetNpadJoyHoldType\",\"gpu\":\"some gpu\",\"query\":\"#2\"}");
    HAS(t, "\\\"matched\\\":1");
    free(t);
    { /* a query's words: 16 at most, each found in time linear in a report's fields */
        Buf words = {0}, args = {0};
        int i;
        for (i = 0; i < MCP_WORDS; ++i) buf_add(&words, i % 2 ? " svcbreak" : " SOME");
        buf_printf(&args, "{\"query\":\"%s\"}", words.text);
        t = call(&m, "search_reports", args.text);
        HAS(t, "\\\"matched\\\":12");
        free(t);
        t = call(&m, "list_games", args.text);
        HAS(t, "\\\"count\\\":1");
        free(t);
        buf_add(&words, " game");
        buf_free(&args);
        buf_printf(&args, "{\"query\":\"%s\"}", words.text);
        t = call(&m, "search_reports", args.text);
        HAS(t, "query has 17 words: 16 at most");
        HAS(t, "\"isError\":true");
        free(t);
        t = call(&m, "list_games", args.text);
        HAS(t, "query has 17 words: 16 at most");
        free(t);
        buf_free(&args);
        buf_free(&words);
    }
    t = call(&m, "get_report", "{\"id\":2}");
    HAS(t, "https://reports.example/r/2/1.log");
    HAS(t, "\\\"summary\\\":{");
    LACKS(t, "isError");
    HAS(t, "\\\"symbolize\\\":{\\\"title_id\\\":\\\"0100AAAABBBB0000\\\",\\\"game_version\\\":\\\"1.2.3\\\","
           "\\\"addresses\\\":[\\\"163d18d8\\\",\\\"1683a358\\\"]");
    free(t);
    t = call(&m, "get_screenshot", "{\"id\":2}");
    HAS(t, "{\"type\":\"image\",\"data\":\"iVBORw0KGgo"); /* a PNG's first bytes, in base64 */
    HAS(t, "\"mimeType\":\"image/png\"");
    HAS(t, "screenshot 1 of 1: shot.png, https://reports.example/r/2/2.png");
    LACKS(t, "isError");
    free(t);
    t = call(&m, "get_screenshot", "{\"id\":2,\"n\":2}");
    HAS(t, "has 1 screenshot, not a screenshot 2");
    HAS(t, "\"isError\":true");
    free(t);
    t = call(&m, "get_report", "{\"id\":999}");
    HAS(t, "there is no report #999");
    HAS(t, "\"isError\":true");
    free(t);
    t = call(&m, "get_report", "{}");
    HAS(t, "\"isError\":true");
    free(t);
    t = call(&m, "read_log", "{\"id\":2,\"grep\":\"SVCBREAK\"}");
    HAS(t, "11 lines; 1 from line 1 on hold"); /* LOG's 12 without its registers' line */
    HAS(t, "9: *** The game called svcBreak");
    free(t);
    t = call(&m, "read_log", "{\"id\":2,\"start\":3,\"lines\":2}");
    HAS(t, "lines 3 to 4 shown; go on with start=5");
    HAS(t, "3: [   0.00] system:");
    LACKS(t, "5: ");
    free(t);
    t = call(&m, "read_log", "{\"id\":2,\"log\":2}");
    HAS(t, "has 1 log, not a log 2");
    free(t);
    t = call(&m, "signatures", "{}");
    HAS(t, "\\\"signature\\\":\\\"svcBreak in _ZN2nn3hid18GetNpadJoyHoldTypeEv\\\"");
    HAS(t, "\\\"reports\\\":12");
    HAS(t, "\\\"newest\\\":[15,14,13,12,11]");
    free(t);
    t = ask(&m, "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"tools/call\",\"params\":{\"name\":\"rm\"}}", &status);
    HAS(t, "-32602");
    free(t);
}

/* a database of the first schema (no user_version) brought to the last: each report's message leads to it, each
   file knows its message, the notes' table is there */
static void test_upgrade(void) {
    char* path = rei_format("%s/old.db", site);
    sqlite3* db;
    Store* s;
    StFile* files;
    int nfiles;
    CHECK(sqlite3_open(path, &db) == SQLITE_OK);
    CHECK(sqlite3_exec(db,
                       "CREATE TABLE reports(id INTEGER PRIMARY KEY AUTOINCREMENT, created REAL NOT NULL, guild INTEGER"
                       " NOT NULL, channel INTEGER NOT NULL, message INTEGER NOT NULL UNIQUE, poster INTEGER NOT NULL,"
                       " game TEXT NOT NULL, title TEXT NOT NULL, named INTEGER NOT NULL, title_id TEXT, note TEXT,"
                       " version TEXT, build TEXT, commit_id TEXT, branch TEXT, system TEXT, gpu TEXT, driver TEXT,"
                       " outcome TEXT, signature TEXT);"
                       "CREATE TABLE files(report INTEGER NOT NULL REFERENCES reports(id) ON DELETE CASCADE, n INTEGER"
                       " NOT NULL, kind TEXT NOT NULL, name TEXT NOT NULL, path TEXT NOT NULL, bytes REAL NOT NULL,"
                       " summary TEXT, PRIMARY KEY(report, n));"
                       "INSERT INTO reports(created, guild, channel, message, poster, game, title, named) VALUES(1, 1,"
                       " 2, 7777, 3, 'K', 'T', 1);"
                       "INSERT INTO files VALUES(1, 1, 'log', 'a.log', '1.log', 10, NULL);",
                       NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(db);
    s = st_open(path);
    CHECK(s != NULL);
    if (s) {
        CHECK(st_report_of(s, 7777) == 1);
        CHECK(st_counts(s, 1, &nfiles, &nfiles, &nfiles) == 1);
        CHECK(!st_update(s, 1, 7778, 2, NULL, 0, "a note"));
        CHECK(st_take_back(s, 7778, &files, &nfiles) == 1 && nfiles == 0);
        st_files_free(files, nfiles);
        CHECK(st_take_back(s, 7777, &files, &nfiles) == 0); /* the report's own message is not an update */
        st_close(s);
        s = st_open(path); /* opened again: nothing left to upgrade */
        CHECK(s != NULL);
        st_close(s);
    }
    free(path);
}

/* the reports summarized again when the log summary changed, once: report 2's facts as its log says them */
static void test_rescan(void) {
    StReport r;
    CHECK(st_set_facts(tracer.store, &(StReport){.id = 2, .outcome = "stale"}) == 0);
    CHECK(st_get(tracer.store, 2, &r, NULL, NULL) == 1);
    SAME(r.outcome, "stale");
    st_report_free(&r);
    CHECK(tr_rescan(&tracer, "a") == 12);
    CHECK(st_get(tracer.store, 2, &r, NULL, NULL) == 1);
    SAME(r.outcome, "svcBreak");
    SAME(r.signature, "svcBreak in _ZN2nn3hid18GetNpadJoyHoldTypeEv");
    SAME(r.title, "Some Game"); /* the game stays as it was */
    SAME(r.missed, "1 service command");
    SAME(r.console, "Switch");
    st_report_free(&r);
    CHECK(tr_rescan(&tracer, "a") == 0); /* the same summary: nothing to do */
    /* a report filed under another game than its log names (an older summary's mistake) is filed again */
    CHECK(!st_set_game(tracer.store, 2, "0100DDDD00000000", "Wrong Game", "0100DDDD00000000", 1));
    CHECK(!site_update(tracer.store, site, 2, "0100DDDD00000000"));
    {
        char* path = at("g/0100DDDD00000000.html");
        CHECK(exists(path));
        free(path);
    }
    CHECK(tr_rescan(&tracer, "c") == 12);
    CHECK(st_get(tracer.store, 2, &r, NULL, NULL) == 1);
    SAME(r.key, "0100AAAABBBB0000");
    SAME(r.title, "Some Game");
    st_report_free(&r);
    {
        char* path = at("g/0100DDDD00000000.html");
        CHECK(!exists(path)); /* the wrong game's page went with its last report */
        free(path);
    }
    CHECK(tr_rescan(&tracer, "b") == 12);
    { /* a log kept before the site left its dumps out: written again without them, its size with it */
        char *path = at("r/2/1.log"), *text = read_file(path, NULL), *dumped;
        StFile* files;
        int nfiles;
        size_t n = 0;
        CHECK(text != NULL);
        dumped = rei_format("%s[   2.25]   code 164ab380: d10083ff  a9017bfd\n", text ? text : "");
        CHECK(!site_write(site, "r/2/1.log", dumped, strlen(dumped)));
        CHECK(tr_rescan(&tracer, "e") == 12);
        free(text);
        text = read_file(path, &n);
        LACKS(text, "d10083ff");
        HAS(text, "GetNpadJoyHoldType");
        CHECK(st_get(tracer.store, 2, &r, &files, &nfiles) == 1);
        CHECK(nfiles >= 1 && files[0].bytes == (double)n);
        st_report_free(&r);
        st_files_free(files, nfiles);
        free(dumped);
        free(text);
        free(path);
    }
}

/* the games' names from the list: an update's and an add-on's id find their game, a report its logs don't name is named
   by it at once, one made before the list at the next start, and a log's own name stays first */
/* what the runtime missed, counted: shaders by the shaders (compute ones too), the rest by the different ones */
/* names that are not ASCII: a log's own UTF-8 name kept through to the reply, the pages and the JSON; a name that
   ScarletNX's launcher's Share Log wrote as '?' a byte taken from the list instead, and one kept so repaired */
static char* named_log(const char* name, const char* id) {
    return rei_format("[   0.00] ScarletNX - %s\n[   0.00] build: ScarletNX main 1234abc (clang 23.1.3, Windows "
                      "x86_64)\n[   0.30] game: %s, version 1.0.0\n",
                      name, id);
}
static void test_names(void) {
    static const char list_text[] = "0100CAFE00000000\tCaf\xc3\xa9 Racer\n"
                                    "0100F00D00000000\t\xe6\x9c\x88\xe3\x81\xae\xe6\x97\x85 the 1st\n";
    char *good = named_log("Caf\xc3\xa9 Racer Deluxe", "0100c0de00000000"),
         *latin = named_log("Caf?? Racer", "0100cafe00000000"),
         *japanese = named_log("????????? the 1st", "0100f00d00000000");
    const Titles* was = tracer.titles;
    Titles list;
    char *path = at("names.tsv"), *t, *p, *key, *title, *tid;
    long a, b, c;
    int named;
    StReport r;
    TrMessage m;
    cJSON* summary = cJSON_Parse("{\"title\":\"Caf?? Racer\",\"title_id\":\"0100CAFE00000000\"}");
    CHECK(!tr_game(summary, "", &key, &title, &tid, &named) && !named); /* the launcher's '?', not a name */
    SAME(title, "Caf?? Racer");
    free(key);
    free(title);
    free(tid);
    cJSON_Delete(summary);
    serve("https://cdn.discordapp.com/9/good.log", good, strlen(good));
    serve("https://cdn.discordapp.com/9/latin.log", latin, strlen(latin));
    serve("https://cdn.discordapp.com/9/japanese.log", japanese, strlen(japanese));
    CHECK(!site_write(site, "names.tsv", list_text, sizeof list_text - 1));
    CHECK(!tr_titles_load(&list, path));
    /* the log's own name, é and all, to the card, the pages and the JSON */
    tracer.titles = &list;
    m = message(6500, 140, "<@99>");
    attach(&m, "good.log", "https://cdn.discordapp.com/9/good.log", 10);
    t = answer_text(respond(&m));
    c = st_by_message(tracer.store, 6500);
    HAS(t, ": Caf\xc3\xa9 Racer Deluxe\"");
    free(t);
    tr_message_free(&m);
    /* a Japanese name the launcher wrote as '?': the list's */
    m = message(6501, 140, "<@99>");
    attach(&m, "japanese.log", "https://cdn.discordapp.com/9/japanese.log", 10);
    t = answer_text(respond(&m));
    b = st_by_message(tracer.store, 6501);
    HAS(t, ": \xe6\x9c\x88\xe3\x81\xae\xe6\x97\x85 the 1st\"");
    LACKS(t, "??");
    free(t);
    tr_message_free(&m);
    {
        char *rel = rei_format("r/%ld/index.html", b), *json = rei_format("api/r/%ld.json", b);
        p = page(rel);
        HAS(p, "\xe6\x9c\x88\xe3\x81\xae\xe6\x97\x85 the 1st");
        free(p);
        p = page(json);
        HAS(p, "\"title\":\"\xe6\x9c\x88\xe3\x81\xae\xe6\x97\x85 the 1st\"");
        free(p);
        free(rel);
        free(json);
        rel = rei_format("r/%ld/index.html", c);
        p = page(rel);
        HAS(p, "Caf\xc3\xa9 Racer Deluxe");
        free(p);
        free(rel);
        p = page("index.html");
        HAS(p, "Caf\xc3\xa9 Racer Deluxe");
        free(p);
    }
    /* a report kept before this, named by the launcher's '?': named again from the list at the next start */
    tracer.titles = NULL;
    m = message(6502, 140, "<@99>");
    attach(&m, "latin.log", "https://cdn.discordapp.com/9/latin.log", 10);
    free(answer_text(respond(&m)));
    tr_message_free(&m);
    a = st_by_message(tracer.store, 6502);
    CHECK(st_get(tracer.store, a, &r, NULL, NULL) == 1);
    SAME(r.title, "Caf?? Racer"); /* no list: what the log said, as a name of no weight */
    CHECK(!r.named);
    CHECK(!st_set_game(tracer.store, a, r.key, r.title, r.title_id, 1)); /* as the reports before this kept it */
    st_report_free(&r);
    tracer.titles = &list;
    CHECK(tr_rescan(&tracer, "names again") > 0);
    CHECK(st_get(tracer.store, a, &r, NULL, NULL) == 1);
    SAME(r.title, "Caf\xc3\xa9 Racer");
    st_report_free(&r);
    CHECK(!site_rebuild(tracer.store, site));
    p = page("index.html");
    HAS(p, ">Caf\xc3\xa9 Racer<");
    LACKS(p, "Caf??");
    free(p);
    tracer.titles = was;
    tr_titles_free(&list);
    free(path);
    free(good);
    free(latin);
    free(japanese);
}

static void test_missed(void) {
    cJSON* s = cJSON_Parse(
        "{\"missed\":[{\"kind\":\"shader\",\"what\":\"a jump table\",\"count\":763},{\"kind\":\"shader\","
        "\"what\":\"unknown opcode\",\"count\":352},{\"kind\":\"compute shader\",\"what\":\"x\",\"count\":1},"
        "{\"kind\":\"texture format\",\"what\":\"0\",\"count\":3},{\"kind\":\"macro\",\"what\":\"a\",\"count\":1},"
        "{\"kind\":\"macro compile\",\"what\":\"b\",\"count\":9}],\"ipc\":[{\"kind\":\"undeclared\"},"
        "{\"kind\":\"unknown\"}]}");
    char* t = tr_missed(s);
    SAME(t, "1116 shaders, 1 texture format, 2 macros, 2 service commands");
    free(t);
    cJSON_Delete(s);
    s = cJSON_Parse("{\"missed\":[],\"ipc\":[]}");
    CHECK(!tr_missed(s));
    cJSON_Delete(s);
    CHECK(!tr_missed(NULL));
}

static void test_titles(void) {
    static const char list_text[] =
        "# title ID, name\n0100EEEE00000000\tListed Game\nnot a line\n"
        "0100aaaabbbb0000\tSome Game From The List\nAGB-ZQXE\tMade Up Quest - Green Version\n";
    static char bare[sizeof LOG + 16];
    Titles list;
    char *path = at("titles.tsv"), *t, *name;
    StReport r;
    TrMessage m;
    long first, second;
    CHECK(!site_write(site, "titles.tsv", list_text, sizeof list_text - 1));
    CHECK(!tr_titles_load(&list, path));
    CHECK(list.n == 3 && strlen(list.version) == 16);
    SAME(tr_titles_name(&list, "GBA", "zqxe"), "Made Up Quest - Green Version");
    SAME(tr_titles_name(&list, "Switch", "0100EEEE00000000"), "Listed Game");
    CHECK(!tr_titles_name(&list, "3DS", "0100EEEE00000000")); /* the list has the Switch's and the GBA's */
    CHECK(!tr_titles_name(&list, "GBA", "BPEJ"));
    SAME(tr_titles_name(&list, NULL, "0100EEEE00000000"), "Listed Game");
    SAME(tr_titles_name(&list, NULL, "0100eeee00000800"), "Listed Game");
    SAME(tr_titles_name(&list, NULL, "0100EEEE00001001"), "Listed Game");
    CHECK(!tr_titles_name(&list, NULL, "0100FFFF00000000"));
    CHECK(!tr_titles_name(NULL, NULL, "0100EEEE00000000"));
    name = strstr(strcpy(bare, LOG), "Some Game");
    memmove(name + 16, name + 9, strlen(name + 9) + 1);
    memcpy(name, "0100EEEE00000000", 16);
    memcpy(strstr(bare, "0100aaaabbbb0000"), "0100eeee00000000", 16);
    serve("https://cdn.discordapp.com/6/bare.log", bare, strlen(bare));
    m = message(6000, 120, "<@99>");
    attach(&m, "bare.log", "https://cdn.discordapp.com/6/bare.log", 10);
    t = answer_text(respond(&m));
    HAS(t, ": 0100EEEE00000000\""); /* no list yet: its title ID */
    free(t);
    tr_message_free(&m);
    first = st_by_message(tracer.store, 6000);
    tracer.titles = &list;
    CHECK(tr_rescan(&tracer, "with the list") > 0);
    CHECK(st_get(tracer.store, first, &r, NULL, NULL) == 1);
    SAME(r.title, "Listed Game");
    CHECK(!r.named);
    st_report_free(&r);
    m = message(6001, 120, "<@99>");
    attach(&m, "bare.log", "https://cdn.discordapp.com/6/bare.log", 10);
    t = answer_text(respond(&m));
    HAS(t, ": Listed Game\"");
    free(t);
    tr_message_free(&m);
    second = st_by_message(tracer.store, 6001);
    CHECK(st_get(tracer.store, 2, &r, NULL, NULL) == 1);
    SAME(r.title, "Some Game"); /* its log named it */
    st_report_free(&r);
    {
        char* p = page("index.html");
        HAS(p, ">Listed Game<");
        free(p);
    }
    /* the other consoles: a 3DS game by its title ID, a Vita game by its name, a GBA game by its code and the list */
    {
        static const char three[] = "Game: Some 3DS Game (0004000000ABC400) - ScarletNX main d22bbab (clang 23.1.4, "
                                    "Linux x86_64)\nNo Vulkan device: no frames will be drawn, shown or saved\n";
        static const char vita[] = "Game: Some Vita Game - ScarletNX main 1234abc (clang 23.1.3, Linux x86_64)\n";
        static const char gba[] = "ScarletNX - MADEUP QUEST (GBA)\nbuild: ScarletNX main 1234abc (clang 23.1.3, Linux "
                                  "x86_64)\ncartridge: ZQXE\n";
        static const struct {
            const char *text, *url, *key, *title, *console, *reply;
        } cases[] = {{three, "https://cdn.discordapp.com/7/3ds.log", "0004000000ABC400", "Some 3DS Game", "3DS",
                      ": Some 3DS Game (3DS)\""},
                     {vita, "https://cdn.discordapp.com/7/vita.log", "n-vita-some-vita-game", "Some Vita Game", "Vita",
                      ": Some Vita Game (Vita)\""},
                     {gba, "https://cdn.discordapp.com/7/gba.log", "gba-ZQXE", "Made Up Quest - Green Version", "GBA",
                      ": Made Up Quest - Green Version (GBA)\""}};
        size_t c;
        for (c = 0; c < sizeof cases / sizeof *cases; ++c) {
            long id;
            serve(cases[c].url, cases[c].text, strlen(cases[c].text));
            m = message(6100 + (u64)c, 121, "<@99>");
            attach(&m, "last-run.log.txt", cases[c].url, 10);
            t = answer_text(respond(&m));
            HAS(t, cases[c].reply);
            free(t);
            tr_message_free(&m);
            id = st_by_message(tracer.store, 6100 + (u64)c);
            CHECK(st_get(tracer.store, id, &r, NULL, NULL) == 1);
            SAME(r.key, cases[c].key);
            SAME(r.title, cases[c].title);
            SAME(r.console, cases[c].console);
            st_report_free(&r);
        }
        {
            char* p = page("index.html");
            HAS(p, "data-console=\"GBA\"");
            HAS(p, "GBA · ZQXE");
            free(p);
        }
        for (c = 0; c < sizeof cases / sizeof *cases; ++c) CHECK(tr_forget(&tracer, 6100 + (u64)c));
    }
    CHECK(tr_forget(&tracer, 6000) && tr_forget(&tracer, 6001) && first && second);
    tracer.titles = NULL;
    tr_titles_free(&list);
    free(path);
}

/* the hourly limit counts what was asked: reports deleted since, files that were no log, and updates */
/* the workers a message got: tr_take here, counted */
static int takes;
static int counted_take(void* ctx, Tracer* t, const TrMessage* m, Taken* k, char** why) {
    (void)ctx;
    *why = NULL;
    ++takes;
    tr_take(t, k, m);
    return 0;
}
static void test_limits(void) {
    TrMessage m;
    char* t;
    int i;
    now_is += 3601;
    for (i = 0; i < TR_HOURLY; ++i) {
        m = message(2200 + (u64)i, 78, "<@99>");
        attach(&m, "game.log", "https://cdn.discordapp.com/1/game.log", 10);
        t = answer_text(respond(&m));
        HAS(t, "Report #");
        free(t);
        tr_message_free(&m);
        CHECK(tr_forget(&tracer, 2200 + (u64)i));
    }
    m = message(2300, 78, "<@99>");
    attach(&m, "game.log", "https://cdn.discordapp.com/1/game.log", 10);
    t = answer_text(respond(&m));
    HAS(t, "You sent 10 reports and updates in the last hour");
    free(t);
    tr_message_free(&m);
    downloads = 0;
    for (i = 0; i <= TR_HOURLY; ++i) {
        m = message(2400 + (u64)i, 79, "<@99>");
        attach(&m, "notes.txt", "https://cdn.discordapp.com/1/notes.txt", 10);
        t = answer_text(respond(&m));
        HAS(t, i < TR_HOURLY ? "No ScarletNX log here" : "in the last hour");
        free(t);
        tr_message_free(&m);
    }
    CHECK(downloads == TR_HOURLY);
    m = message(2500, 80, "<@99>"); /* no file: nothing to download, nothing counted, no worker */
    tracer.take = counted_take;
    takes = 0;
    t = answer_text(respond(&m));
    HAS(t, "Attach the game's ScarletNX log");
    free(t);
    tr_message_free(&m);
    CHECK(takes == 0);
    tracer.take = NULL;
    {
        long made;
        m = message(2501, 80, "<@99>");
        attach(&m, "game.log", "https://cdn.discordapp.com/1/game.log", 10);
        free(answer_text(respond(&m)));
        tr_message_free(&m);
        made = st_report_of(tracer.store, 2501);
        CHECK(made > 0);
        for (i = 1; i <= TR_HOURLY; ++i) {
            char* add = rei_format("<@99> add %ld a note", made);
            m = message(2501 + (u64)i, 80, add);
            tracer.take = counted_take;
            takes = 0;
            t = answer_text(respond(&m));
            CHECK(takes == 0); /* a note alone: no worker either */
            tracer.take = NULL;
            HAS(t, i < TR_HOURLY ? "a note" : "in the last hour");
            free(t);
            tr_message_free(&m);
            free(add);
        }
        CHECK(tr_forget(&tracer, 2501));
    }
}

/* ---- a reply that asks for a report of the message it replies to ---- */
typedef struct {
    u64 id;
    int status;
    const char* json;
} Held;
static Held held[16];
static int nheld, intent = 1, message_gets;
static char* fake_message(void* ctx, u64 channel, u64 id, int* status, char** why) {
    int i;
    (void)ctx;
    ++message_gets;
    *why = NULL;
    for (i = 0; i < nheld; ++i)
        if (held[i].id == id) {
            *status = held[i].status;
            if (!held[i].status) {
                *why = rei_strdup("Could not resolve host: discord.com");
                return NULL;
            }
            if (held[i].status != 200) *why = rei_strdup("{\"message\": \"Missing Access\", \"code\": 50001}");
            return rei_strdup(held[i].json);
        }
    *status = 404;
    *why = rei_format("{\"message\": \"Unknown Message\", \"channel\": %llu}", (unsigned long long)channel);
    return rei_strdup("{\"message\": \"Unknown Message\", \"code\": 10008}");
}
static int fake_intent(void* ctx, char** why) {
    (void)ctx;
    *why = intent < 0 ? rei_strdup("GET applications/@me answered 401: 401: Unauthorized") : NULL;
    return intent;
}
static void hold(u64 id, int status, const char* json) {
    held[nheld].id = id;
    held[nheld].status = status;
    held[nheld++].json = json;
}
static int asks_of(u64 poster) {
    sqlite3* db;
    sqlite3_stmt* q;
    char* path = rei_format("%s/tracer.db", site);
    int n = -1;
    if (sqlite3_open(path, &db) == SQLITE_OK &&
        sqlite3_prepare_v2(db, "SELECT count(*) FROM asks WHERE poster = ?", -1, &q, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(q, 1, (sqlite3_int64)poster);
        if (sqlite3_step(q) == SQLITE_ROW) n = sqlite3_column_int(q, 0);
        sqlite3_finalize(q);
    }
    sqlite3_close(db);
    free(path);
    return n;
}
/* a reply to message to, by author, saying content */
static TrMessage reply_to(u64 id, u64 author, const char* content, u64 to) {
    TrMessage m = message(id, author, content);
    m.reply_to = to;
    m.reply_channel = 22;
    m.reply_guild = 33;
    return m;
}
static void test_replies(void) {
    TrMessage m;
    char* t;
    long made, other;
    StReport r;
    int reports_before, n;
    StReport* all;
    tracer.message = fake_message;
    tracer.content_intent = fake_intent;
    now_is += 3601;
    /* someone's message with a log, sent without a mention; a reply that mentions Tracer and says nothing else makes it
       a report: its files, its author and its words */
    hold(
        7101, 200,
        "{\"id\":\"7101\",\"channel_id\":\"22\",\"author\":{\"id\":\"145\"},\"content\":\"crashes when I open the map\","
        "\"attachments\":[{\"filename\":\"game.log\",\"url\":\"https://cdn.discordapp.com/1/game.log\",\"size\":10},"
        "{\"filename\":\"shot.png\",\"url\":\"https://cdn.discordapp.com/1/shot.png\",\"size\":10}]}");
    downloads = 0;
    m = reply_to(7102, 146, "<@99>", 7101);
    t = answer_text(tr_answer(&tracer, &m, &made));
    HAS(t, "Report #");
    HAS(t, "1 log, 1 screenshot");
    free(t);
    CHECK(made > 0 && downloads == 2);
    CHECK(st_get(tracer.store, made, &r, NULL, NULL) == 1);
    CHECK(r.poster == 145 && r.message == 7101 && r.channel == 22 && r.guild == 33);
    SAME(r.note, "crashes when I open the map");
    st_report_free(&r);
    CHECK(st_report_of(tracer.store, 7102) == made); /* the reply leads to it too */
    CHECK(asks_of(146) == 1 && asks_of(145) == 0);   /* the hourly limit counts the one who asked */
    {                                                /* the gateway's event again: nothing, no second report */
        long again;
        CHECK(!tr_answer(&tracer, &m, &again) && !again);
    }
    tr_message_free(&m);
    /* a second reply to the same message: the report's link, no other report */
    CHECK(!st_reports(tracer.store, NULL, &all, &reports_before));
    st_reports_free(all, reports_before);
    message_gets = 0;
    m = reply_to(7103, 147, "<@99>", 7101);
    {
        char* want = rei_format("The message you replied to is report #%ld's already", made);
        char* link = rei_format("https://reports.example/r/%ld/", made);
        t = answer_text(tr_answer(&tracer, &m, &other));
        HAS(t, want);
        HAS(t, link);
        free(want);
        free(link);
        free(t);
    }
    CHECK(!other && !message_gets);
    tr_message_free(&m);
    CHECK(!st_reports(tracer.store, NULL, &all, &n));
    st_reports_free(all, n);
    CHECK(n == reports_before);
    /* deleting the reply leaves the report; deleting the message it made a report of deletes it */
    CHECK(tr_forget(&tracer, 7102));
    CHECK(st_get(tracer.store, made, &r, NULL, NULL) == 1);
    st_report_free(&r);
    CHECK(tr_forget(&tracer, 7101));
    CHECK(st_get(tracer.store, made, &r, NULL, NULL) == 0);
    /* the reply's own files win: the report is the reply's */
    hold(7104, 200,
         "{\"id\":\"7104\",\"channel_id\":\"22\",\"author\":{\"id\":\"145\"},\"content\":\"\",\"attachments\":["
         "{\"filename\":\"game.log\",\"url\":\"https://cdn.discordapp.com/1/game.log\",\"size\":10}]}");
    message_gets = 0;
    m = reply_to(7105, 146, "<@99> mine instead", 7104);
    attach(&m, "game.log", "https://cdn.discordapp.com/1/game.log", 10);
    t = answer_text(tr_answer(&tracer, &m, &made));
    HAS(t, "Report #");
    free(t);
    CHECK(!message_gets && st_get(tracer.store, made, &r, NULL, NULL) == 1);
    CHECK(r.poster == 146 && r.message == 7105);
    SAME(r.note, "mine instead");
    st_report_free(&r);
    tr_message_free(&m);
    /* a message without words: the reply's are the note */
    m = reply_to(7106, 146, "<@99> Some Game, at the boss", 7104);
    t = answer_text(tr_answer(&tracer, &m, &made));
    free(t);
    CHECK(st_get(tracer.store, made, &r, NULL, NULL) == 1);
    CHECK(r.poster == 145 && r.message == 7104);
    SAME(r.note, "Some Game, at the boss");
    st_report_free(&r);
    tr_message_free(&m);
    /* a message whose files are no log */
    hold(7107, 200,
         "{\"id\":\"7107\",\"channel_id\":\"22\",\"author\":{\"id\":\"145\"},\"content\":\"\",\"attachments\":["
         "{\"filename\":\"setup.exe\",\"url\":\"https://cdn.discordapp.com/1/setup.exe\",\"size\":10}]}");
    m = reply_to(7108, 146, "<@99>", 7107);
    t = answer_text(tr_answer(&tracer, &m, &made));
    HAS(t, "The message you replied to has no ScarletNX log, so no report:\\nsetup.exe: not a log or a screenshot");
    free(t);
    CHECK(!made);
    tr_message_free(&m);
    /* a message without files: said as Discord shows it, the Message Content intent on, off, or not said */
    hold(7109, 200,
         "{\"id\":\"7109\",\"channel_id\":\"22\",\"author\":{\"id\":\"145\"},\"content\":\"\",\"attachments\":[]}");
    m = reply_to(7110, 146, "<@99>", 7109);
    intent = 1;
    t = answer_text(tr_answer(&tracer, &m, &made));
    HAS(t, "The message you replied to has no file, so it has no ScarletNX log");
    free(t);
    intent = 0;
    t = answer_text(tr_answer(&tracer, &m, &made));
    HAS(t, "I can't see the files of the message you replied to");
    HAS(t, "Message Content intent");
    free(t);
    intent = -1;
    t = answer_text(tr_answer(&tracer, &m, &made));
    HAS(t, "did not say whether it would show me one (GET applications/@me answered 401: 401: Unauthorized)");
    free(t);
    intent = 1;
    tr_message_free(&m);
    /* a bot's message, a message gone, one Discord keeps from the bot, no answer, an answer that is no message */
    hold(
        7111, 200,
        "{\"id\":\"7111\",\"channel_id\":\"22\",\"author\":{\"id\":\"5\",\"bot\":true},\"content\":\"\",\"attachments\":["
        "{\"filename\":\"game.log\",\"url\":\"https://cdn.discordapp.com/1/game.log\",\"size\":10}]}");
    hold(7112, 403, "{\"message\": \"Missing Access\", \"code\": 50001}");
    hold(7113, 0, NULL);
    hold(7114, 200, "[1, 2]");
    {
        static const struct {
            u64 to;
            const char* said;
        } refused[] = {
            {7111, "is a bot's or a webhook's, not a report"},
            {7199, "The message you replied to is gone (Discord answered 404), so no report."},
            {7112,
             "(it answered 403: {\\\"message\\\": \\\"Missing Access\\\", \\\"code\\\": 50001}): I need the Read Message "
             "History permission"},
            {7113, "Discord did not answer when I asked for the message you replied to (Could not resolve host: "
                   "discord.com)"},
            {7114, "is no message (its answer is no message: [1, 2])"},
        };
        size_t i;
        downloads = 0;
        for (i = 0; i < sizeof refused / sizeof *refused; ++i) {
            m = reply_to(7120 + i, 146, "<@99>", refused[i].to);
            t = answer_text(tr_answer(&tracer, &m, &made));
            HAS(t, refused[i].said);
            CHECK(!made);
            free(t);
            tr_message_free(&m);
        }
        CHECK(!downloads);
    }
    /* a reference into another channel or server is refused, and Discord is not asked */
    message_gets = 0;
    m = reply_to(7130, 146, "<@99>", 7109);
    m.reply_channel = 23;
    t = answer_text(tr_answer(&tracer, &m, &made));
    HAS(t, "The message you replied to is in <#23>, not this channel");
    free(t);
    m.reply_channel = 22;
    m.reply_guild = 34;
    t = answer_text(tr_answer(&tracer, &m, &made));
    HAS(t, "The message you replied to is in another server (34)");
    free(t);
    CHECK(!message_gets && !made);
    tr_message_free(&m);
    /* the hourly limit of the one who asks: refused before anything is downloaded */
    {
        int i;
        for (i = asks_of(146); i < TR_HOURLY; ++i) {
            m = message(7140 + (u64)i, 146, "<@99>");
            attach(&m, "notes.txt", "https://cdn.discordapp.com/1/notes.txt", 10);
            free(answer_text(respond(&m)));
            tr_message_free(&m);
        }
        hold(7160, 200,
             "{\"id\":\"7160\",\"channel_id\":\"22\",\"author\":{\"id\":\"148\"},\"content\":\"\",\"attachments\":["
             "{\"filename\":\"game.log\",\"url\":\"https://cdn.discordapp.com/1/game.log\",\"size\":10}]}");
        downloads = 0;
        m = reply_to(7161, 146, "<@99>", 7160);
        t = answer_text(tr_answer(&tracer, &m, &made));
        HAS(t, "You sent 10 reports and updates in the last hour");
        free(t);
        CHECK(!made && !downloads);
        tr_message_free(&m);
    }
    /* without the means to ask Discord, a reply alone is asked for a log, as before */
    tracer.message = NULL;
    m = reply_to(7170, 149, "<@99>", 7160);
    t = answer_text(tr_answer(&tracer, &m, &made));
    HAS(t, "Attach the game's ScarletNX log");
    free(t);
    tr_message_free(&m);
    tracer.content_intent = NULL;
}

/* another server's administrator neither deletes nor adds to a report of this one */
static void test_other_guild(void) {
    TrMessage m;
    char *t, *text;
    long id;
    m = message(2600, 81, "<@99>");
    attach(&m, "game.log", "https://cdn.discordapp.com/1/game.log", 10);
    free(answer_text(respond(&m)));
    tr_message_free(&m);
    id = st_report_of(tracer.store, 2600);
    CHECK(id > 0);
    text = rei_format("<@99> delete %ld", id);
    m = message(2601, 88, text);
    m.guild = 34;
    m.roles[m.nroles++] = 6;
    t = answer_text(respond(&m));
    HAS(t, "is not yours");
    free(t);
    tr_message_free(&m);
    free(text);
    text = rei_format("<@99> add %ld from another server", id);
    m = message(2602, 88, text);
    m.guild = 34;
    m.roles[m.nroles++] = 6;
    t = answer_text(respond(&m));
    HAS(t, "is not yours");
    free(t);
    tr_message_free(&m);
    free(text);
    text = rei_format("<@99> delete %ld", id); /* this server's administrator does */
    m = message(2603, 88, text);
    m.roles[m.nroles++] = 5;
    t = answer_text(respond(&m));
    HAS(t, "is deleted");
    free(t);
    tr_message_free(&m);
    free(text);
}

/* read_log's grep: in any case, overlapping starts, and in time linear in the line: a 2 MiB line and a 16 KiB grep that
   matches all but its last byte at each place took over a minute, its length times the line's */
/* tracer-mcp takes the bot's env file: each of the bot's keys, its own, and no other TRACER_ key */
static void test_mcp_keys(void) {
    static const char* const bot[] = {TR_KEYS};
    char one[64], *alone[] = {one, NULL};
    char* own[] = {"TRACER_URL=https://t", "TRACER_SITE=/s", "TRACER_MCP_LISTEN=127.0.0.1:8081", "PATH=/bin", NULL};
    char* other[] = {"TRACER_URL=https://t", "TRACER_URLS=x", NULL};
    char* upstream[] = {"TRACER_UPSTREAM_REPO=scarletnx/ScarletNX", NULL};
    size_t i;
    for (i = 0; i < sizeof bot / sizeof *bot; ++i) {
        snprintf(one, sizeof one, "%s=x", bot[i]);
        CHECK(!mcp_unknown_key(alone));
    }
    CHECK(!mcp_unknown_key(upstream));
    CHECK(!mcp_unknown_key(own));
    SAME(mcp_unknown_key(other), "TRACER_URLS=x");
    CHECK(!mcp_unknown_key(NULL));
}

static void test_mcp_grep(void) {
    static const char lines[] = "first aaab line\nthe abaabab line\nAbAb\nnothing here\n";
    size_t line = (size_t)2 << 20, glen = 16 << 10;
    char *log = malloc(line + 2), *grep = malloc(glen + 1), *t, *args;
    Mcp m;
    clock_t start;
    m.root = site;
    m.url = "https://reports.example";
    CHECK(!site_write(site, "api/r/990.json",
                      "{\"id\":990,\"title\":\"x\",\"files\":[{\"kind\":\"log\",\"name\":\"a.log\","
                      "\"path\":\"r/990/1.log\"},{\"kind\":\"log\",\"name\":\"b.log\",\"path\":\"r/990/2.log\"}]}",
                      strlen("{\"id\":990,\"title\":\"x\",\"files\":[{\"kind\":\"log\",\"name\":\"a.log\","
                             "\"path\":\"r/990/1.log\"},{\"kind\":\"log\",\"name\":\"b.log\",\"path\":"
                             "\"r/990/2.log\"}]}")));
    CHECK(!site_write(site, "r/990/1.log", lines, strlen(lines)));
    t = call(&m, "read_log", "{\"id\":990,\"grep\":\"aab\"}");
    HAS(t, "2 from line 1 on hold");
    HAS(t, "1: first aaab line");
    HAS(t, "2: the abaabab line");
    free(t);
    t = call(&m, "read_log", "{\"id\":990,\"grep\":\"ABAB\"}");
    HAS(t, "2 from line 1 on hold");
    HAS(t, "2: the abaabab line");
    HAS(t, "3: AbAb");
    free(t);
    t = call(&m, "read_log", "{\"id\":990,\"grep\":\"abac\"}");
    HAS(t, "0 from line 1 on hold");
    free(t);
    memset(log, 'a', line);
    log[line] = '\n';
    memset(grep, 'a', glen - 1);
    grep[glen - 1] = 'b';
    grep[glen] = 0;
    CHECK(!site_write(site, "r/990/2.log", log, line + 1));
    args = rei_format("{\"id\":990,\"log\":2,\"grep\":\"%s\"}", grep);
    start = clock();
    t = call(&m, "read_log", args);
    CHECK((double)(clock() - start) / CLOCKS_PER_SEC < 2);
    HAS(t, "1 lines; 0 from line 1 on hold");
    free(t);
    free(args);
    free(log);
    free(grep);
}

/* the channels: none without a list, a listed one, a thread of one asked once */
static int parents_asked;
static u64 fake_parent(void* ctx, u64 channel) {
    (void)ctx;
    ++parents_asked;
    return channel == 701 ? 70 : 0; /* thread 701 is channel 70's */
}
static void test_channels(void) {
    static const u64 listed[2] = {70, 71};
    TrParents cache;
    char* said;
    memset(&cache, 0, sizeof cache);
    CHECK(!tr_answers_in(listed, 0, &cache, 70, fake_parent, NULL) && !parents_asked); /* no list: no channel */
    said = tr_channels_said(listed, 0);
    SAME(said, "none, as TRACER_CHANNELS is not set: no report is taken");
    free(said);
    said = tr_channels_said(listed, 2);
    SAME(said, "70,71");
    free(said);
    CHECK(tr_answers_in(listed, 2, &cache, 71, fake_parent, NULL) && !parents_asked);
    CHECK(tr_answers_in(listed, 2, &cache, 701, fake_parent, NULL) && parents_asked == 1);
    CHECK(tr_answers_in(listed, 2, &cache, 701, fake_parent, NULL) && parents_asked == 1);
    CHECK(!tr_answers_in(listed, 2, &cache, 702, fake_parent, NULL) && parents_asked == 2);
    /* all: every channel and thread, no parent asked; ids; nothing; anything else refused naming it */
    CHECK(tr_answers_in(listed, TR_CHANNELS_ALL, &cache, 999, fake_parent, NULL) && parents_asked == 2);
    said = tr_channels_said(listed, TR_CHANNELS_ALL);
    SAME(said, "all, as TRACER_CHANNELS=all: every channel and thread it can read");
    free(said);
    {
        u64 got[3];
        char* why;
        CHECK(tr_channels_of("all", got, 3, &why) == TR_CHANNELS_ALL && !why);
        CHECK(tr_channels_of(NULL, got, 3, &why) == 0 && tr_channels_of("", got, 3, &why) == 0 && !why);
        CHECK(tr_channels_of("70,,71", got, 3, &why) == 2 && got[0] == 70 && got[1] == 71);
        CHECK(tr_channels_of("All", got, 3, &why) == -2);
        SAME(why, "TRACER_CHANNELS=All: All is neither all nor a channel's id (Developer Mode, Copy Channel ID: its "
                  "digits)");
        free(why);
        CHECK(tr_channels_of("70,all", got, 3, &why) == -2);
        SAME(why, "TRACER_CHANNELS=70,all: all is neither all nor a channel's id (Developer Mode, Copy Channel ID: its "
                  "digits)");
        free(why);
        CHECK(tr_channels_of("1,2,3,4", got, 3, &why) == -2);
        SAME(why, "TRACER_CHANNELS=1,2,3,4 names more than 3 channels");
        free(why);
    }
}

/* ---- the worker ---- */
/* a download in a worker that the tests run: url's bytes from the file the test wrote for it in WORKER_TEST_SERVED (no
   TRACER_ name, which a worker never message_gets) */
static char* served_path(const char* url) {
    unsigned long h = 2166136261UL;
    const char* dir = getenv("WORKER_TEST_SERVED");
    for (; *url; ++url) h = ((h ^ (unsigned char)*url) * 16777619UL) & 0xffffffffUL;
    return rei_format("%s/%08lx", dir ? dir : ".", h);
}
static int served_get(void* ctx, const char* url, size_t max, Buf* out, char** why) {
    char* path = served_path(url);
    size_t n;
    char* data = read_file(path, &n);
    (void)ctx;
    buf_free(out);
    *why = NULL;
    free(path);
    if (!data || n > max) {
        free(data);
        *why = rei_format("%s: HTTP 404", url);
        return -1;
    }
    buf_addn(out, data, n);
    free(data);
    return 0;
}
static void serve_file(const char* url, const char* data, size_t n) {
    char* path = served_path(url);
    FILE* f = fopen(path, "wb");
    CHECK(f && fwrite(data, 1, n, f) == n);
    if (f) fclose(f);
    free(path);
}
/* the test program run as a worker that answers what it is told: the files, a crash, a wait, its environment, a lie */
static int as_worker(const char* how) {
    if (!strcmp(how, "--report")) {
        int failed;
        wk_limit();
        failed = wk_serve(stdin, stdout, served_get, NULL);
        fflush(NULL); /* as main.c's worker ends: shut in, its exit handlers would open nothing they need */
        _exit(failed);
    }
    if (!strcmp(how, "--crash")) kill(getpid(), SIGKILL);
    if (!strcmp(how, "--hang")) sleep(30);
    if (!strcmp(how, "--gone")) return 0; /* gone before it reads its question */
    if (!strcmp(how, "--env"))
        printf("{\"shut_in\":\"test\",\"logs\":0,\"images\":0,\"files\":[],\"skipped\":\"TRACER_TOKEN %s, HOME %s\"}",
               getenv("TRACER_TOKEN") ? "seen" : "none", getenv("HOME") ? "seen" : "none");
#if defined(__linux__)
    if (!strcmp(how, "--peek")) { /* a worker taken over: the bot's environment, where its tokens are, read */
        char* path = rei_format("/proc/%ld/environ", (long)getppid());
        FILE* f = fopen(path, "rb");
        int c = f ? fgetc(f) : EOF;
        printf("{\"shut_in\":\"test\",\"logs\":0,\"images\":0,\"files\":[],\"skipped\":\"%s\"}",
               c != EOF ? "read" : "kept");
        if (f) fclose(f);
        free(path);
    }
#endif
    if (!strcmp(how, "--fds")) { /* the bot's files, which the bot left open at a high descriptor: closed by wk_limit */
        int was = fcntl(900, F_GETFD) != -1;
        wk_limit();
        printf("{\"shut_in\":\"test\",\"logs\":0,\"images\":0,\"files\":[],\"skipped\":\"%s, %s\"}",
               was ? "open" : "none", fcntl(900, F_GETFD) != -1 ? "open" : "closed");
    }
    if (!strcmp(how, "--unshut")) /* a worker that read the files without shutting itself in */
        printf("{\"logs\":0,\"images\":0,\"files\":[],\"skipped\":\"\"}");
    if (!strcmp(how, "--escape")) { /* a worker taken over once shut in: what it can still open */
        Buf q = {0};
        char piece[4096], *why = NULL, *site_file, *new_file, *db;
        size_t got;
        cJSON* question;
        const char *shut, *dir, *site_dir = getenv("WORKER_TEST_SITE");
        int dirfd, inside, site_kept, made_kept, db_kept, passwd_kept, fd;
        while ((got = fread(piece, 1, sizeof piece, stdin))) buf_addn(&q, piece, got);
        question = cJSON_ParseWithLength(q.text ? q.text : "", q.len);
        dir = jstr(question, "dir");
        dirfd = dir ? open(dir, O_RDONLY | O_DIRECTORY) : -1;
        site_file = rei_format("%s/index.html", site_dir ? site_dir : ".");
        new_file = rei_format("%s/planted.js", site_dir ? site_dir : ".");
        db = rei_format("%s/tracer.db", site_dir ? site_dir : ".");
        if (dirfd < 0 || !(shut = wk_shut_in(dirfd, &why))) {
            printf("{\"logs\":0,\"images\":0,\"files\":[],\"skipped\":\"not shut in: %s\"}", why ? why : "no folder");
            return 0;
        }
        fd = openat(dirfd, "inside", O_WRONLY | O_CREAT | O_EXCL, 0600);
        inside = fd >= 0 && write(fd, "x", 1) == 1;
        if (fd >= 0) close(fd);
        unlinkat(dirfd, "inside", 0);
        fd = open(site_file, O_WRONLY | O_APPEND);
        site_kept = fd < 0;
        if (fd >= 0) close(fd);
        fd = open(new_file, O_WRONLY | O_CREAT | O_EXCL, 0644);
        made_kept = fd < 0;
        if (fd >= 0) close(fd);
        fd = open(db, O_RDWR);
        db_kept = fd < 0;
        if (fd >= 0) close(fd);
        fd = open("/etc/passwd", O_RDONLY);
        passwd_kept = fd < 0;
        if (fd >= 0) close(fd);
        printf("{\"shut_in\":\"%s\",\"logs\":0,\"images\":0,\"files\":[],\"skipped\":\"inside %s, site page %s, "
               "new site file %s, database %s, /etc/passwd %s\"}",
               shut, inside ? "written" : "refused", site_kept ? "refused" : "written", made_kept ? "refused" : "made",
               db_kept ? "refused" : "opened", passwd_kept ? "refused" : "read");
        fflush(NULL); /* shut in: exit's handlers would open nothing they need */
        _exit(0);
    }
    if (!strcmp(how, "--lie")) /* a worker taken over: a file of the host as a report's */
        printf(
            "{\"shut_in\":\"test\",\"logs\":0,\"images\":1,\"files\":[{\"n\":1,\"kind\":\"image\",\"name\":\"x.png\","
            "\"path\":\"1.png\",\"taken\":\"/etc/passwd\"}]}");
    return 0;
}
static char* worker_argv[3];
static int test_worker_take(void* ctx, Tracer* t, const TrMessage* m, Taken* k, char** why) {
    (void)ctx;
    (void)t;
    return wk_take(worker_argv, 20, m, k, why);
}
/* files waiting in the work folder: how many */
static int waiting_files(void) {
    char* path = rei_format("%s/work", site);
    int n = 0;
    DIR* d = opendir(path);
    struct dirent* e;
    while (d && (e = readdir(d)))
        if (e->d_name[0] != '.') {
            ++n;
            fprintf(stderr, "  left in the work folder: %s\n", e->d_name);
        }
    if (d) closedir(d);
    free(path);
    return n;
}
static void test_worker(const char* self_path) {
    static char png[128];
    size_t npng = 8;
    TrMessage m;
    char *t, *p, *why, **env;
    Taken k;
    long id;
    char* const fake_environ[] = {"HOME=/home/x", "TRACER_TOKEN=secret", "TRACER_URL=https://a", "PATH=/bin", NULL};
    /* its environment: no setting or token of the bot's */
    env = wk_env(fake_environ);
    CHECK(env[0] && !strcmp(env[0], "HOME=/home/x") && env[1] && !strcmp(env[1], "PATH=/bin") && !env[2]);
    free(env);
    worker_argv[0] = (char*)self_path;
    worker_argv[2] = NULL;
    setenv("TRACER_TOKEN", "secret", 1);
    memset(&k, 0, sizeof k);
    k.dir = rei_format("%s/work/take.XXXXXX", site);
    CHECK(mkdtemp(k.dir) != NULL);
    m = message(7000, 130, "<@99>");
    worker_argv[1] = "--env";
    CHECK(!wk_take(worker_argv, 20, &m, &k, &why));
    SAME(k.skipped.text, "TRACER_TOKEN none, HOME seen");
    tr_taken_free(&k);
    tr_message_free(&m);
    unsetenv("TRACER_TOKEN");
    /* a report through the worker: the same as one taken here, nothing left waiting */
    memcpy(png, "\x89PNG\r\n\x1a\n", 8);
    npng += chunk(png + npng, "IHDR", "0123456789abc", 13);
    npng += chunk(png + npng, "tEXt", "GPS here", 8);
    npng += chunk(png + npng, "IEND", "", 0);
    serve_file("https://cdn.discordapp.com/8/game.log", LOG, strlen(LOG));
    serve_file("https://cdn.discordapp.com/8/shot.png", png, npng);
    tracer.take = test_worker_take;
    worker_argv[1] = "--report";
    m = message(7001, 131, "<@99> through the worker");
    attach(&m, "game.log", "https://cdn.discordapp.com/8/game.log", 10);
    attach(&m, "shot.png", "https://cdn.discordapp.com/8/shot.png", 10);
    attach(&m, "gone.log", "https://cdn.discordapp.com/8/gone.log", 10);
    t = answer_text(respond(&m));
    HAS(t, "1 log, 1 screenshot");
    HAS(t, "gone.log: the download failed");
    free(t);
    tr_message_free(&m);
    id = st_by_message(tracer.store, 7001);
    CHECK(id > 0);
    {
        char *log = rei_format("r/%ld/1.log", id), *shot = rei_format("r/%ld/2.png", id), *path = at(shot);
        size_t n = 0;
        p = page(log);
        HAS(p, "loading ~/save.bin");
        LACKS(p, "pc=164ab3c4");
        free(p);
        p = read_file(path, &n);
        CHECK(p && n == npng - 20 && !found(p, n, "GPS"));
        free(p);
        free(path);
        free(shot);
        free(log);
    }
    CHECK(waiting_files() == 0);
    { /* a Share Log zip through the worker, shut in its folder: its names flagged UTF-8 as miniz writes them, a game's
         folder named with an accent, read in the UTF-8 locale it made before it shut itself in */
        static char zip[1 << 16];
        const char* names[] = {"games/Some G\xc3\xa1me [0100AAAABBBB0000]/old.dat",
                               "games/Some G\xc3\xa1me [0100AAAABBBB0000]/2026-11-07_21-45-11.log"};
        const char* data[] = {"older bytes\n", LOG};
        size_t n = zip_bytes(zip, sizeof zip, names, data, NULL, 2, ZIP_UTF8);
        serve_file("https://cdn.discordapp.com/8/logs.zip", zip, n);
        m = message(7008, 131, "<@99> a zip through the worker");
        attach(&m, "scarletnx-logs-2026-11-07_21-46-12.zip", "https://cdn.discordapp.com/8/logs.zip", (double)n);
        t = answer_text(respond(&m));
        HAS(t, "1 log, 0 screenshots");
        free(t);
        tr_message_free(&m);
        CHECK(st_by_message(tracer.store, 7008) > 0);
        CHECK(waiting_files() == 0);
    }
    /* a worker that crashes, one that never answers, and one that names a host file: nothing kept, nothing left */
    worker_argv[1] = "--crash";
    m = message(7002, 132, "<@99>");
    attach(&m, "game.log", "https://cdn.discordapp.com/8/game.log", 10);
    t = answer_text(respond(&m));
    HAS(t, "Your files could not be read, so nothing was kept");
    free(t);
    CHECK(!st_by_message(tracer.store, 7002));
    memset(&k, 0, sizeof k);
    k.dir = rei_format("%s/work/take.XXXXXX", site);
    CHECK(mkdtemp(k.dir) != NULL);
    CHECK(wk_take(worker_argv, 20, &m, &k, &why) == -1);
    SAME(why, "the worker stopped on signal 9 (Killed)");
    free(why);
    worker_argv[1] = "--hang";
    CHECK(wk_take(worker_argv, 1, &m, &k, &why) == -1);
    SAME(why, "the worker took over 1 s, and was stopped");
    free(why);
    { /* a worker gone before it reads a question longer than a pipe holds: an error said, not the bot's end */
        TrMessage big = message(7003, 133, "<@99>");
        char name[8000];
        int f;
        memset(name, 'a', sizeof name - 5);
        memcpy(name + sizeof name - 5, ".log", 5);
        for (f = 0; f < TR_FILES; ++f) attach(&big, name, "https://cdn.discordapp.com/8/game.log", 10);
        worker_argv[1] = "--gone";
        CHECK(wk_take(worker_argv, 20, &big, &k, &why) == -1);
        SAME(why, "its answer is no JSON object");
        free(why);
        tr_message_free(&big);
    }
    { /* a descriptor of the bot's, open in the worker until wk_limit closes it */
        int keep = open("/dev/null", O_RDONLY);
        CHECK(keep >= 0 && dup2(keep, 900) == 900);
        worker_argv[1] = "--fds";
        CHECK(!wk_take(worker_argv, 20, &m, &k, &why));
        SAME(k.skipped.text, "open, closed");
        buf_free(&k.skipped);
        close(900);
        close(keep);
    }
    worker_argv[1] = "--unshut"; /* a worker that read the files with the site and the database in its reach */
    CHECK(wk_take(worker_argv, 20, &m, &k, &why) == -1);
    SAME(why, "its answer does not say it shut itself in its folder before it read the files");
    free(why);
    { /* once shut in, a worker taken over writes in its folder alone: not the site, not the database */
        char *planted = at("planted.js"), *index = at("index.html"), *db = at("tracer.db");
        CHECK(exists(index) && exists(db)); /* what it is kept from is there */
        free(index);
        free(db);
        worker_argv[1] = "--escape";
        CHECK(!wk_take(worker_argv, 20, &m, &k, &why));
        HAS(k.skipped.text, "inside written, site page refused, new site file refused, database refused, /etc/passwd "
                            "refused");
        if (why) fprintf(stderr, "  %s\n", why);
        free(why);
        why = NULL;
        CHECK(!exists(planted));
        buf_free(&k.skipped);
        free(planted);
    }
    worker_argv[1] = "--lie";
    CHECK(wk_take(worker_argv, 20, &m, &k, &why) == -1);
    SAME(why, "its file 0 is not one it may answer (image 1.png, at /etc/passwd)");
    free(why);
    CHECK(k.n == 0);
    tr_taken_free(&k);
    tr_message_free(&m);
    worker_argv[1] = "--report";
    tracer.take = NULL;
    CHECK(waiting_files() == 0);
}

/* the bot kept from its workers' debugging: in a process of its own, as nothing gives that back (and LeakSanitizer
   needs it). Linux: a worker reads its parent's /proc environ before, and not after; FreeBSD: tracing disabled */
static void test_untraceable(const char* self_path) {
#if defined(__linux__)
    int kept;
    if (!geteuid()) { /* root reads every process's */
        fputs("tracer: the bot kept from its workers: not tried as root, whom nothing keeps out\n", stderr);
        return;
    }
    for (kept = 0; kept < 2; ++kept) {
        int status = -1;
        pid_t pid;
        fflush(NULL);
        if (!(pid = fork())) {
            TrMessage m = message(7004, 134, "<@99>");
            Taken k;
            char* why = NULL;
            int read, said;
            if (kept && wk_untraceable(&why)) _exit(3);
            memset(&k, 0, sizeof k);
            k.dir = rei_format("%s/work/take.XXXXXX", site);
            if (!mkdtemp(k.dir)) _exit(4);
            worker_argv[0] = (char*)self_path;
            worker_argv[1] = "--peek";
            if (wk_take(worker_argv, 20, &m, &k, &why)) _exit(5);
            read = k.skipped.text && !strcmp(k.skipped.text, "read");
            said = read || (k.skipped.text && !strcmp(k.skipped.text, "kept"));
            tr_taken_free(&k);
            _exit(read ? 1 : said ? 0 : 6);
        }
        CHECK(pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status));
        if (WEXITSTATUS(status) != !kept)
            fprintf(stderr, "  a worker %s its bot's environment (%d)\n",
                    kept ? "read, after wk_untraceable," : "could not read, before wk_untraceable,",
                    WEXITSTATUS(status));
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == !kept);
    }
#elif defined(__FreeBSD__)
    int status = -1;
    pid_t pid;
    (void)self_path;
    fflush(NULL);
    if (!(pid = fork())) {
        char* why;
        int traced = 0;
        if (wk_untraceable(&why)) _exit(3);
        _exit(procctl(P_PID, getpid(), PROC_TRACE_STATUS, &traced) ? 4 : traced == -1 ? 0 : 1);
    }
    CHECK(pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
#else
    (void)self_path;
#endif
}

/* ---- the issues ---- */
static int gh_posts, gh_status = 201;
static char* gh_body;
static char gh_url[256];
static void fake_github(void* ctx, const char* method, const char* url, const char* body, const char* etag,
                        RgAnswer* out) {
    (void)ctx;
    (void)etag;
    memset(out, 0, sizeof *out);
    ++gh_posts;
    snprintf(gh_url, sizeof gh_url, "%s %s", method, url);
    free(gh_body);
    gh_body = rei_strdup(body ? body : "");
    out->status = gh_status;
    out->body = rei_strdup(gh_status == 201 ? "{\"number\":345,\"html_url\":\"https://github.com/o/r/issues/345\"}"
                                            : "{\"message\":\"Resource not accessible by personal access token\"}");
}
static const char* issue_settings[8][2];
static const char* issue_env(const char* name) {
    int i;
    for (i = 0; i < 8 && issue_settings[i][0]; ++i)
        if (!strcmp(issue_settings[i][0], name)) return issue_settings[i][1];
    return NULL;
}
/* setting i, and none after it */
static void issue_set(int i, const char* name, const char* value) {
    issue_settings[i][0] = name;
    issue_settings[i][1] = value;
    issue_settings[i + 1][0] = NULL;
}
/* the gateway's reaction json, self being 99 */
static int read_reaction(const char* json, TrReaction* r) { return tr_reaction(json, strlen(json), 99, r); }
static TrReaction reaction(u64 user, u64 guild, u64 message, const char* emoji) {
    TrReaction r;
    memset(&r, 0, sizeof r);
    r.user = user;
    r.channel = 22;
    r.guild = guild;
    r.message = message;
    r.emoji = rei_strdup(emoji);
    return r;
}
static void test_issues(void) {
    TrIssues is;
    TrReaction r;
    TrMessage m;
    StReport rep;
    const char* token;
    char *why, *t, *p;
    long id;
    /* the settings: off without a repository, said; the token, the emoji and the labels */
    issue_settings[0][0] = NULL;
    CHECK(!tr_issues_settings(&is, issue_env, &token, &why) && !is.repo && !token);
    t = tr_issues_said(&is);
    SAME(t, "issues: none, as TRACER_ISSUES_REPO is not set");
    free(t);
    issue_set(0, "TRACER_ISSUES_TOKEN", "github_pat_x");
    CHECK(tr_issues_settings(&is, issue_env, &token, &why) == -1);
    SAME(why, "TRACER_ISSUES_TOKEN is set, and TRACER_ISSUES_REPO, the issues' repository, is not");
    free(why);
    issue_set(0, "TRACER_ISSUES_REPO", "o/r");
    CHECK(tr_issues_settings(&is, issue_env, &token, &why) == -1);
    SAME(why, "TRACER_ISSUES_REPO=o/r needs TRACER_ISSUES_TOKEN, a fine-grained token that writes its issues");
    free(why);
    issue_set(0, "TRACER_ISSUES_REPO", "o/r/x");
    issue_set(1, "TRACER_ISSUES_TOKEN", "github_pat_x");
    CHECK(tr_issues_settings(&is, issue_env, &token, &why) == -1);
    SAME(why, "TRACER_ISSUES_REPO=o/r/x is not OWNER/NAME");
    free(why);
    issue_set(0, "TRACER_ISSUES_REPO", "o/r");
    issue_set(1, "TRACER_ISSUES_TOKEN", "github_pat_x");
    issue_set(2, "TRACER_ISSUES_LABELS", "game report, needs triage");
    CHECK(!tr_issues_settings(&is, issue_env, &token, &why));
    SAME(token, "github_pat_x");
    SAME(is.emoji, "\xe2\x9c\x85");
    CHECK(is.nlabels == 2);
    SAME(is.labels[1], "needs triage");
    t = tr_issues_said(&is);
    SAME(t, "issues: into o/r, once a server's administrator reacts to a report with \xe2\x9c\x85; labels: game "
            "report,needs triage");
    LACKS(t, "github_pat");
    free(t);
    is.fetch = fake_github;
    tracer.issues = &is;
    /* a report: then reactions to it */
    m = message(8000, 140, "<@99> @everyone #12 ```ticks```");
    attach(&m, "game.log", "https://cdn.discordapp.com/1/game.log", 10);
    free(answer_text(respond(&m)));
    tr_message_free(&m);
    id = st_report_of(tracer.store, 8000);
    CHECK(id > 0);
    gh_posts = 0;
    r = reaction(141, 33, 8000, "\xe2\x9c\x85"); /* not an administrator */
    CHECK(!tr_approved(&tracer, &r));
    tr_reaction_free(&r);
    r = reaction(88, 33, 8000, "\xf0\x9f\x91\x8d"); /* an administrator, another emoji */
    r.roles[r.nroles++] = 5;
    CHECK(!tr_approved(&tracer, &r));
    tr_reaction_free(&r);
    r = reaction(88, 34, 8000, "\xe2\x9c\x85"); /* another server's administrator */
    r.roles[r.nroles++] = 6;
    CHECK(!tr_approved(&tracer, &r));
    tr_reaction_free(&r);
    CHECK(gh_posts == 0);
    /* GitHub refuses: said, and a later approval tries again */
    gh_status = 403;
    r = reaction(88, 33, 8000, "\xe2\x9c\x85");
    r.roles[r.nroles++] = 5;
    t = answer_text(tr_approved(&tracer, &r));
    HAS(t, "could not be filed as an issue: GitHub answered 403 to POST repos/o/r/issues: Resource not accessible");
    free(t);
    CHECK(gh_posts == 1 && st_get(tracer.store, id, &rep, NULL, NULL) == 1 && rep.issue == 0);
    st_report_free(&rep);
    gh_status = 201;
    t = answer_text(tr_approved(&tracer, &r));
    HAS(t, "is issue #345 now");
    HAS(t, "https://github.com/o/r/issues/345");
    free(t);
    CHECK(gh_posts == 2);
    SAME(gh_url, "POST https://api.github.com/repos/o/r/issues");
    HAS(gh_body, "\"labels\":[\"game report\",\"needs triage\"]");
    HAS(gh_body, "approved on Discord by a server's administrator");
    { /* no "#" outside the code block, where GitHub would link an issue */
        const char* fence = strstr(gh_body, "````text");
        CHECK(fence && !memchr(gh_body, '#', (size_t)(fence - gh_body)));
    }
    HAS(gh_body, "https://reports.example/r/");
    HAS(gh_body, "````text"); /* a fence longer than the note's backticks: what it says is code, no @ or # link */
    HAS(gh_body, "@everyone #12 ```ticks```");
    HAS(gh_body, "svcBreak");
    LACKS(gh_body, "140"); /* no Discord id */
    CHECK(st_get(tracer.store, id, &rep, NULL, NULL) == 1 && rep.issue == 345);
    SAME(rep.issue_url, "https://github.com/o/r/issues/345");
    st_report_free(&rep);
    {
        char* rel = rei_format("r/%ld/index.html", id);
        p = page(rel);
        HAS(p, "<dt>Issue</dt><dd><a href=\"https://github.com/o/r/issues/345\">#345</a></dd>");
        free(p);
        free(rel);
        rel = rei_format("api/r/%ld.json", id);
        p = page(rel);
        HAS(p, "\"issue\":345");
        free(p);
        free(rel);
    }
    /* approved again, by another administrator or on the bot's answer: no second issue */
    CHECK(!tr_approved(&tracer, &r));
    tr_reaction_free(&r);
    r = reaction(88, 33, last_reply, "\xe2\x9c\x85");
    r.roles[r.nroles++] = 5;
    CHECK(!tr_approved(&tracer, &r) && gh_posts == 2);
    tr_reaction_free(&r);
    /* the gateway's reaction read: a person's, never a bot's or self's */
    CHECK(
        read_reaction("{\"user_id\":\"88\",\"channel_id\":\"22\",\"message_id\":\"8000\",\"guild_id\":\"33\","
                      "\"member\":{\"roles\":[\"5\"],\"user\":{\"id\":\"88\"}},\"emoji\":{\"id\":null,\"name\":\"x\"}}",
                      &r));
    CHECK(r.user == 88 && r.guild == 33 && r.message == 8000 && r.nroles == 1 && r.roles[0] == 5);
    SAME(r.emoji, "x");
    tr_reaction_free(&r);
    CHECK(!read_reaction("{\"user_id\":\"99\",\"channel_id\":\"22\",\"message_id\":\"1\",\"emoji\":{\"name\":\"x\"}}",
                         &r));
    CHECK(!read_reaction(
        "{\"user_id\":\"7\",\"channel_id\":\"22\",\"message_id\":\"1\",\"member\":{\"user\":{\"bot\":true}},"
        "\"emoji\":{\"name\":\"x\"}}",
        &r));
    CHECK(tr_forget(&tracer, 8000));
    tracer.issues = NULL;
    tr_issues_free(&is);
    free(gh_body);
    gh_body = NULL;
}

/* ---- the build's age ---- */
static int gql_asks, gql_status = 200;
static char *gql_answer, *gql_body;
static char gql_url[256];
static void fake_graphql(void* ctx, const char* method, const char* url, const char* body, const char* etag,
                         RgAnswer* out) {
    (void)ctx;
    (void)etag;
    memset(out, 0, sizeof *out);
    ++gql_asks;
    snprintf(gql_url, sizeof gql_url, "%s %s", method, url);
    free(gql_body);
    gql_body = rei_strdup(body ? body : "");
    out->status = gql_status;
    if (gql_status)
        out->body = rei_strdup(gql_answer);
    else
        out->error = rei_strdup("POST https://api.github.com/graphql: Could not resolve host: api.github.com");
}
/* GitHub's answer for a commit of the repository made at made, main's last at last, behind commits behind it */
static void github_says(const char* made, const char* last, long behind, const char* tag) {
    free(gql_answer);
    gql_answer =
        rei_format("{\"data\":{\"repository\":{\"object\":{\"committedDate\":\"%s\"},\"ref\":{\"target\":{"
                   "\"committedDate\":\"%s\"},\"compare\":{\"behindBy\":%ld}}},\"nightly\":{\"latestRelease\":{"
                   "\"tagName\":\"%s\"}}}}",
                   made, last, behind, tag);
}
static char age_log[2048];
static int age_served = -1;
/* a report of a log whose build is commit, sent by author as message id: the reply's text, owned; *report its id */
static char* age_report(u64 id, u64 author, const char* commit, long* report) {
    const char* at = strstr(LOG, "1234abc");
    TrMessage m;
    char* t;
    snprintf(age_log, sizeof age_log, "%.*s%s%s", (int)(at - LOG), LOG, commit, at + 7);
    if (age_served < 0) {
        age_served = nserved;
        serve("https://cdn.discordapp.com/age/game.log", age_log, 0);
    }
    served[age_served].n = strlen(age_log);
    m = message(id, author, "<@99> how old");
    attach(&m, "game.log", "https://cdn.discordapp.com/age/game.log", (double)strlen(age_log));
    t = answer_text(respond(&m));
    tr_message_free(&m);
    *report = st_report_of(tracer.store, id);
    return t;
}
static const char* age_settings[2][2];
static const char* age_env(const char* name) {
    return age_settings[0][0] && !strcmp(age_settings[0][0], name) ? age_settings[0][1] : NULL;
}
static void test_age(void) {
    static const char BEHIND[] = "Build aaaa111 (main) is 37 commits and 2 days behind main; the newest nightly is "
                                 "2026-11-04 (413fc2f): retry on it before we dig in.";
    static const char DIRTY[] =
        "Build bbbb222-dirty is not a published build (local changes): numbers from it may not match main.";
    TrAge a;
    StReport r;
    char *why, *t, *p, *rel;
    long id;
    /* the setting: scarletnx/ScarletNX by default, OWNER/NAME or refused, said with its value */
    age_settings[0][0] = NULL;
    CHECK(!tr_age_settings(&a, age_env, &why) && !why);
    SAME(a.repo, "scarletnx/ScarletNX");
    SAME(a.owner, "scarletnx");
    t = tr_age_said(&a);
    SAME(t, "build age: a -dirty build said not to be a published one, nothing asked of GitHub, as "
            "TRACER_ISSUES_TOKEN is not set");
    free(t);
    age_settings[0][0] = "TRACER_UPSTREAM_REPO";
    age_settings[0][1] = "o";
    CHECK(tr_age_settings(&a, age_env, &why) == -1);
    SAME(why, "TRACER_UPSTREAM_REPO=o is not OWNER/NAME");
    free(why);
    age_settings[0][1] = "o/r/x";
    CHECK(tr_age_settings(&a, age_env, &why) == -1);
    SAME(why, "TRACER_UPSTREAM_REPO=o/r/x is not OWNER/NAME");
    free(why);
    age_settings[0][1] = "o/r x";
    CHECK(tr_age_settings(&a, age_env, &why) == -1);
    SAME(why, "TRACER_UPSTREAM_REPO=o/r x is not OWNER/NAME");
    free(why);
    age_settings[0][1] = "o/r";
    CHECK(!tr_age_settings(&a, age_env, &why));
    SAME(a.owner, "o");
    SAME(a.name, "r");
    a.fetch = fake_graphql;
    t = tr_age_said(&a);
    SAME(t, "build age: each report's commit against o/r's main and o/nightly's newest nightly, asked of GitHub once "
            "in 3600 s a commit");
    free(t);
    tracer.age = &a;

    /* a build behind main: how far, and the newest nightly, in the reply and on the page, from one query */
    github_says("2026-11-02T23:59:59Z", "2026-11-04T00:00:01Z", 37, "nightly-2026-11-04-413fc2f");
    gql_asks = 0;
    t = age_report(9100, 300, "aaaa111", &id);
    HAS(t, BEHIND);
    free(t);
    CHECK(gql_asks == 1);
    SAME(gql_url, "POST https://api.github.com/graphql");
    HAS(gql_body, "\"owner\":\"o\"");
    HAS(gql_body, "\"name\":\"r\"");
    HAS(gql_body, "\"commit\":\"aaaa111\"");
    HAS(gql_body, "refs/heads/main");
    CHECK(st_get(tracer.store, id, &r, NULL, NULL) == 1);
    SAME(r.age, BEHIND);
    st_report_free(&r);
    rel = rei_format("r/%ld/index.html", id);
    p = page(rel);
    HAS(p, "<br>Build aaaa111 (main) is 37 commits and 2 days behind main; the newest nightly is 2026-11-04 (413fc2f)");
    HAS(p, "<a href=\"https://github.com/scarletnx/ScarletNX/commit/aaaa111\">aaaa111</a>");
    free(p);
    free(rel);
    rel = rei_format("api/r/%ld.json", id);
    p = page(rel);
    HAS(p, "\"build_age\":\"Build aaaa111 (main) is 37 commits");
    free(p);
    free(rel);
    /* the same commit within the hour: GitHub not asked again; past it, asked again */
    now_is += 600;
    t = age_report(9101, 301, "aaaa111", &id);
    HAS(t, BEHIND);
    free(t);
    CHECK(gql_asks == 1);
    now_is += TR_AGE_FRESH;
    github_says("2026-11-02T10:00:00Z", "2026-11-05T10:00:00Z", 40, "nightly-2026-11-05-5555aaa");
    t = age_report(9102, 302, "aaaa111", &id);
    HAS(t, "Build aaaa111 (main) is 40 commits and 3 days behind main; the newest nightly is 2026-11-05 (5555aaa)");
    free(t);
    CHECK(gql_asks == 2);

    /* local changes: said without asking */
    t = age_report(9103, 303, "bbbb222-dirty", &id);
    HAS(t, DIRTY);
    free(t);
    CHECK(gql_asks == 2);
    /* a commit the repository does not have: not a published build either, kept for the hour */
    free(gql_answer);
    gql_answer = rei_strdup(
        "{\"data\":{\"repository\":{\"object\":null,\"ref\":{\"target\":{\"committedDate\":\"2026-11-05T10:00:00Z\"},"
        "\"compare\":null}},\"nightly\":{\"latestRelease\":{\"tagName\":\"nightly-2026-11-05-5555aaa\"}}},\"errors\":["
        "{\"type\":\"NOT_FOUND\",\"path\":[\"repository\",\"ref\",\"compare\"],\"message\":\"Could not resolve head ref "
        "'cccc333'.\"}]}");
    t = age_report(9104, 304, "cccc333", &id);
    HAS(t, "Build cccc333 is not a published build (local changes): numbers from it may not match main.");
    free(t);
    t = age_report(9105, 305, "cccc333", &id);
    HAS(t, "Build cccc333 is not a published build");
    free(t);
    CHECK(gql_asks == 3);
    /* one commit and one day: no plural */
    github_says("2026-11-04T10:00:00Z", "2026-11-05T10:00:00Z", 1, "nightly-2026-11-05-5555aaa");
    t = age_report(9106, 306, "dddd444", &id);
    HAS(t, "Build dddd444 (main) is 1 commit and 1 day behind main; the newest nightly is 2026-11-05 (5555aaa)");
    free(t);
    /* no line for a build with every commit of main, nor for the newest nightly itself, nor without a nightly */
    github_says("2026-11-05T10:00:00Z", "2026-11-05T10:00:00Z", 0, "nightly-2026-11-05-5555aaa");
    t = age_report(9107, 307, "eeee555", &id);
    LACKS(t, "behind main");
    LACKS(t, "published build");
    free(t);
    CHECK(st_get(tracer.store, id, &r, NULL, NULL) == 1 && !r.age);
    st_report_free(&r);
    rel = rei_format("r/%ld/index.html", id);
    p = page(rel);
    LACKS(p, "behind main");
    free(p);
    free(rel);
    github_says("2026-11-05T10:00:00Z", "2026-11-05T12:00:00Z", 3, "nightly-2026-11-05-5555aaa");
    t = age_report(9108, 308, "5555aaa", &id);
    LACKS(t, "behind main");
    free(t);
    github_says("2026-11-04T10:00:00Z", "2026-11-05T12:00:00Z", 3, "v1.0.0");
    t = age_report(9109, 309, "ffff666", &id);
    LACKS(t, "behind main");
    free(t);
    /* GitHub silent or refusing: no line, and asked again at the next report */
    gql_asks = 0;
    gql_status = 0;
    t = age_report(9110, 310, "1111aaa", &id);
    LACKS(t, "behind main");
    free(t);
    gql_status = 502;
    free(gql_answer);
    gql_answer = rei_strdup("{\"message\":\"Server Error\"}");
    t = age_report(9111, 311, "1111aaa", &id);
    LACKS(t, "behind main");
    free(t);
    gql_status = 200;
    free(gql_answer);
    gql_answer = rei_strdup("{\"data\":{\"repository\":null,\"nightly\":null},\"errors\":[{\"type\":\"NOT_FOUND\","
                            "\"message\":\"Could not resolve to a Repository with the name 'o/r'.\"}]}");
    t = age_report(9112, 312, "1111aaa", &id);
    LACKS(t, "behind main");
    LACKS(t, "published build"); /* not the commit's fault */
    free(t);
    CHECK(gql_asks == 3);
    /* without a token: nothing asked, a -dirty build still said */
    a.fetch = NULL;
    t = age_report(9113, 313, "2222bbb", &id);
    LACKS(t, "behind main");
    free(t);
    t = age_report(9114, 314, "bbbb222-dirty", &id);
    HAS(t, DIRTY);
    free(t);
    CHECK(gql_asks == 3);
    /* a build without git: nothing to ask */
    a.fetch = fake_graphql;
    t = age_report(9115, 315, "unknown", &id);
    LACKS(t, "published build");
    free(t);
    CHECK(gql_asks == 3);
    tracer.age = NULL;
    free(gql_answer);
    free(gql_body);
    gql_answer = gql_body = NULL;
}

/* a log sent in a .zip or a .gz is kept as its text: the report page's Download saves that text under a name of a log,
   never the archive's, which no program opens as one */
static void test_archive_download(void) {
    static char zip[1 << 16], gz[1 << 16];
    size_t nzip, ngz;
    TrMessage m;
    StFile* files;
    StReport r;
    int nfiles;
    char *t, *p, *rel, *data;
    data = packed(1, "games/Some Game/scarletnx.log", LOG, &nzip); /* one buffer of packed's: copied before the next */
    memcpy(zip, data, nzip);
    data = packed(0, "data", LOG, &ngz);
    memcpy(gz, data, ngz);
    serve("https://cdn.discordapp.com/9/scarletnx-logs-2026-11-07.zip", zip, nzip);
    serve("https://cdn.discordapp.com/9/game.log.gz", gz, ngz);
    m = message(9500, 950, "<@99> from the launcher's Share Zip");
    attach(&m, "scarletnx-logs-2026-11-07.zip", "https://cdn.discordapp.com/9/scarletnx-logs-2026-11-07.zip",
           (double)nzip);
    attach(&m, "game.log.gz", "https://cdn.discordapp.com/9/game.log.gz", (double)ngz);
    t = answer_text(respond(&m));
    HAS(t, "2 logs, 0 screenshots");
    free(t);
    CHECK(st_get(tracer.store, st_by_message(tracer.store, 9500), &r, &files, &nfiles) == 1);
    CHECK(nfiles == 2);
    if (nfiles == 2) {
        SAME(files[0].name, "scarletnx-logs-2026-11-07.zip"); /* what was sent, as the page's heading and JSON say */
        rel = rei_format("r/%ld/index.html", r.id);
        p = page(rel);
        HAS(p, "<h2>scarletnx-logs-2026-11-07.zip <small>");
        CHECK(strstr(p, "\" download=\"scarletnx-logs-2026-11-07.log\">Download</a>") != NULL);
        CHECK(strstr(p, "\" download=\"game.log\">Download</a>") != NULL);
        LACKS(p, "download=\"scarletnx-logs-2026-11-07.zip\"");
        LACKS(p, "download=\"game.log.gz\"");
        free(p);
        free(rel);
        /* and the file the link names is the log's text, not a zip's bytes */
        rel = rei_format("r/%ld/%s", r.id, files[0].path);
        p = page(rel);
        CHECK(p && strncmp(p, "PK", 2));
        HAS(p, "ScarletNX - Some Game");
        free(p);
        free(rel);
    }
    st_report_free(&r);
    st_files_free(files, nfiles);
    tr_message_free(&m);
}

/* ---- runs of edenprobe ---- */

/* ScarletNX's log of a run: its machine, then the report's lines as [game] lines. The account case's detail names a
   player, the nifm case's an address, the net note a session; a suite edenprobe's table lacks says nothing of what it
   holds */
static const char PROBE_LOG[] =
    "[   0.00] ScarletNX - edenprobe\n"
    "[   0.00] build: ScarletNX main 1234abc, committed 2026-11-01 (clang 23.1.3, Linux x86_64)\n"
    "[   0.00] system: 'Some Linux' (Linux 6.1.0), Some CPU 9000, 12 threads, 32 GB RAM\n"
    "[   0.18] vulkan: the driver that runs: radv, Mesa 26.1.8 (driver ID 3)\n"
    "[   0.18] vulkan: Some GPU 3000 (RADV X), Vulkan 1.4.354, driver 06801008, graphics queue family 0, compute 1\n"
    "[   0.19] game: 0100000000001000, version 0.9.0\n"
    "[   0.20] [game] {\"type\":\"note\",\"text\":\"edenprobe 0.9.0 (v0.9.0) starting\"}\n"
    "[   0.20] [game] {\"type\":\"note\",\"text\":\"folder: sdmc:/edenprobe/runs/2a3ec1e_x\"}\n"
    "[   0.20] [game] {\"type\":\"note\",\"text\":\"mode: quick, from the mode file; pictures of failures only; "
    "families cpu graphics net; no timing passes; label x\"}\n"
    "[   0.21] [game] {\"type\":\"case\",\"suite\":\"clear\",\"name\":\"clear to opaque red\",\"tier\":\"exact\","
    "\"result\":\"pass\",\"detail\":\"\",\"us\":12}\n"
    "[   0.21] [game] {\"type\":\"case\",\"suite\":\"astc\",\"name\":\"a 4x4 block decodes\",\"tier\":\"exact\","
    "\"result\":\"fail\",\"detail\":\"texel 0 reads 00ff00ff, it holds ff0000ff\"}\n"
    "[   0.21] [game] {\"type\":\"case\",\"suite\":\"account\",\"name\":\"the user's nickname reads back\",\"tier\":"
    "\"exact\",\"result\":\"pass\",\"detail\":\"uid 0123456789abcdeffedcba9876543210, nickname Someone\"}\n"
    "[   0.21] [game] {\"type\":\"case\",\"suite\":\"nifm\",\"name\":\"a machine has an address\",\"tier\":\"exact\","
    "\"result\":\"pass\",\"detail\":\"got 192.168.1.20\"}\n"
    "[   0.21] [game] {\"type\":\"note\",\"text\":\"net: sdmc:/edenprobe/net; session tok-abcdef; account named\"}\n"
    "[   0.22] [game] {\"type\":\"case\",\"suite\":\"index\",\"name\":\"an indexed draw\",\"tier\":\"none\",\"result\":"
    "\"skip\",\"reason\":\"crashed\",\"detail\":\"the program ended inside it\"}\n"
    "[   0.22] [game] {\"type\":\"case\",\"suite\":\"newsuite\",\"name\":\"something new\",\"tier\":\"exact\","
    "\"result\":\"fail\",\"detail\":\"what it holds nobody said\"}\n"
    "[   0.23] [game] {\"type\":\"summary\",\"passed\":3,\"failed\":2,\"skipped\":0,\"crashed\":1}\n"
    "[   0.23] [game] EDENPROBE DONE passed=3 failed=2 skipped=0 crashed=1\n";
/* Eden's log of a run, as its yuzu_cmd writes it */
static const char EDEN_LOG[] =
    "[   0.905119] Frontend <Info> yuzu_cmd/emu_window/emu_window_sdl3_vk.cpp:91:EmuWindow_SDL3_VK: Eden Version: Eden "
    "| HEAD-4534fde3aa-HEAD (Vulkan)\n"
    "[   1.320106] Render.Vulkan <Info> video_core/renderer_vulkan/renderer_vulkan.cpp:264:Report: Driver: NVIDIA "
    "580.95.5.0\n"
    "[   1.320108] Render.Vulkan <Info> video_core/renderer_vulkan/renderer_vulkan.cpp:265:Report: Device: Some "
    "GeForce 9000\n"
    "[   1.320109] Render.Vulkan <Info> video_core/renderer_vulkan/renderer_vulkan.cpp:266:Report: Vulkan: 1.4.329\n"
    "[   1.400000] Frontend <Info> qt_common/qt_common.cpp:265:Init: Host OS: Windows 11 Version 24H2\n"
    "[   3.092326] Debug.Emulated <Info> core/hle/kernel/svc/svc_debug_string.cpp:25:OutputDebugString: "
    "{\"type\":\"note\",\"text\":\"edenprobe 0.9.0 (v0.9.0) starting\"}\n"
    "[   3.092400] Debug.Emulated <Info> core/hle/kernel/svc/svc_debug_string.cpp:25:OutputDebugString: "
    "{\"type\":\"note\",\"text\":\"mode: quick, from the mode file; pictures of failures only; families graphics\"}\n"
    "[   3.130376] Debug.Emulated <Info> core/hle/kernel/svc/svc_debug_string.cpp:25:OutputDebugString: "
    "{\"type\":\"case\",\"suite\":\"astc\",\"name\":\"a 4x4 block decodes\",\"tier\":\"exact\",\"result\":\"fail\","
    "\"detail\":\"texel 0 reads 0000ff00\"}\n"
    "[   3.130400] Debug.Emulated <Info> core/hle/kernel/svc/svc_debug_string.cpp:25:OutputDebugString: "
    "{\"type\":\"summary\",\"passed\":0,\"failed\":1,\"skipped\":0,\"crashed\":0}\n";
/* the report itself, as the card keeps it */
static const char PROBE_JSONL[] =
    "{\"type\":\"note\",\"text\":\"edenprobe 0.9.0 (v0.9.0) starting\"}\n"
    "{\"type\":\"note\",\"text\":\"mode: quick, from the mode file; pictures of failures only; families graphics\"}\n"
    "{\"type\":\"case\",\"suite\":\"astc\",\"name\":\"a 4x4 block decodes\",\"tier\":\"exact\",\"result\":\"pass\","
    "\"detail\":\"\"}\n"
    "{\"type\":\"summary\",\"passed\":1,\"failed\":0,\"skipped\":0,\"crashed\":0}\n";

static void test_probe_parts(void) {
    char text[64];
    unsigned long v = 0;
    /* the families: service, net, pad and a suite the table has not lose their detail */
    CHECK(probe_family("tex8k") == PROBE_GRAPHICS && probe_family("acc-app") == PROBE_SERVICE);
    CHECK(probe_family("friend") == PROBE_NET && probe_family("touch") == PROBE_PAD);
    CHECK(probe_family("nonesuch") == PROBE_OTHER && probe_family(NULL) == PROBE_OTHER);
    SAME(probe_family_name(PROBE_KERNEL), "kernel");
    CHECK(probe_keeps_detail("astc") && probe_keeps_detail("fs") && probe_keeps_detail("game"));
    CHECK(!probe_keeps_detail("account") && !probe_keeps_detail("ldn") && !probe_keeps_detail("nifm"));
    CHECK(!probe_keeps_detail("pad") && !probe_keeps_detail("nonesuch"));
    /* the drivers, by the ban table's names, and by their own names as Eden logs them */
    SAME(probe_driver_name(3), "radv");
    SAME(probe_driver_name(30), "apesoft");
    CHECK(!probe_driver_name(0) && !probe_driver_name(31));
    SAME(probe_driver_label(4), "NVIDIA's driver");
    CHECK(probe_driver_id("turnip") == 18 && probe_driver_id("nonesuch") == 0);
    CHECK(probe_driver_of_name("radv") == 3 && probe_driver_of_name("NVIDIA") == 4 &&
          probe_driver_of_name("NVK") == 24);
    CHECK(probe_driver_of_name("Qualcomm Technologies Inc. Adreno Vulkan Driver") == 8);
    CHECK(probe_driver_of_name("turnip Mesa driver") == 18);
    CHECK(probe_driver_of_name("Intel open-source Mesa driver") == 6 && probe_driver_of_name("Intel Corporation") == 5);
    CHECK(probe_driver_of_name("Some Driver") == 0 && probe_driver_of_name(NULL) == 0);
    /* each driver's numbering, both ways */
    probe_version_text(4, 0x9b404000UL, text, sizeof text);
    SAME(text, "621.1.00");
    probe_version_text(3, 0x06801008UL, text, sizeof text);
    SAME(text, "26.1.8");
    CHECK(probe_version_parse(5, "101.4502", &v) && v == 1659286UL);
    probe_version_text(5, v, text, sizeof text);
    SAME(text, "101.4502");
    CHECK(probe_version_parse(14, "1.2.11", &v) && v == 10211UL);
    CHECK(probe_version_parse(3, "26.1.8", &v) && v == 0x06801008UL);
    CHECK(probe_version_parse(4, "580.95.5.0", &v));
    probe_version_text(4, v, text, sizeof text);
    SAME(text, "580.95.05");
    CHECK(!probe_version_parse(3, "abc", &v) && !probe_version_parse(3, "26.", &v) &&
          !probe_version_parse(3, "1..2", &v));
    CHECK(!probe_version_parse(3, "26", &v) && !probe_version_parse(3, "2000.1.1", &v));
    /* the system's family */
    SAME(probe_os("'Gentoo Linux' (Linux 6.18.57)"), "linux");
    SAME(probe_os("Android 15 (Linux 6.1.75)"), "android");
    SAME(probe_os("Windows 11 26H2"), "windows");
    SAME(probe_os("macOS 15.1"), "macos");
    SAME(probe_os("FreeBSD 15.0-RELEASE"), "freebsd");
    SAME(probe_os("CachyOS"), "unknown");
    SAME(probe_os(NULL), "unknown");
}

static void test_probe_lines(void) {
    size_t n;
    int cases, notes, i;
    char *cut, *why, digest[17], other[17], *t;
    Probe p, q;
    cJSON* o;
    /* the cut: the details of service, net, pad and unknown suites, and the net notes; nothing else */
    cut = probe_cut(PROBE_LOG, strlen(PROBE_LOG), &n, &cases, &notes);
    CHECK(cases == 3 && notes == 1);
    LACKS(cut, "nickname Someone");
    LACKS(cut, "192.168.1.20");
    LACKS(cut, "tok-abcdef");
    LACKS(cut, "what it holds nobody said");
    HAS(cut, "\"suite\":\"account\",\"name\":\"the user's nickname reads back\",\"tier\":\"exact\",\"result\":\"pass\","
             "\"detail\":\"\"}");
    HAS(cut, "net: left out by Tracer");
    HAS(cut, "texel 0 reads 00ff00ff, it holds ff0000ff"); /* graphics keeps its detail */
    HAS(cut, "\"reason\":\"crashed\",\"detail\":\"the program ended inside it\"");
    HAS(cut, "[   0.18] vulkan: the driver that runs: radv, Mesa 26.1.8 (driver ID 3)\n");
    free(cut);
    cut = probe_cut(LOG, strlen(LOG), &n, &cases, &notes); /* a game's log: as it was */
    CHECK(cases == 0 && notes == 0 && n == strlen(LOG) && !memcmp(cut, LOG, n));
    free(cut);
    /* what a text holds of a run */
    CHECK(probe_marks(LOG, strlen(LOG)) == 0);
    CHECK(probe_marks(PROBE_LOG, strlen(PROBE_LOG)) == 2);
    CHECK(probe_marks(PROBE_LOG, (size_t)(strstr(PROBE_LOG, "[   0.23]") - PROBE_LOG)) == 1);
    CHECK(probe_marks(EDEN_LOG, strlen(EDEN_LOG)) == 2 && probe_marks(PROBE_JSONL, strlen(PROBE_JSONL)) == 2);
    /* ScarletNX's log */
    cut = probe_cut(PROBE_LOG, strlen(PROBE_LOG), &n, &cases, &notes);
    CHECK(probe_parse(cut, n, &p, &why) == 1 && !why);
    SAME(p.emulator, "ScarletNX");
    SAME(p.emulator_version, "main 1234abc");
    SAME(p.commit, "1234abc");
    CHECK(p.built == 1793491200.0);
    SAME(p.version, "0.9.0");
    SAME(p.describe, "v0.9.0");
    SAME(p.words, "cpu graphics net notime");
    SAME(p.os, "linux");
    SAME(p.gpu, "Some GPU 3000 (RADV X)");
    SAME(p.vulkan, "1.4.354");
    SAME(p.driver_name, "radv");
    SAME(p.driver_info, "Mesa 26.1.8");
    CHECK(p.driver_id == 3 && !p.inferred && p.driver_version == 0x06801008UL);
    SAME(p.driver_text, "26.1.8");
    CHECK(p.whole && p.passed == 3 && p.failed == 2 && p.skipped == 0 && p.crashed == 1 && p.ncases == 6);
    CHECK(!p.unread && !p.over);
    SAME(p.cases[1].suite, "astc");
    SAME(p.cases[1].result, "fail");
    SAME(p.cases[4].reason, "crashed");
    CHECK(p.cases[0].us == 12 && p.cases[1].us == -1);
    SAME(p.cases[2].detail, "");
    t = probe_counts(&p);
    SAME(t, "3 passed, 2 failed (astc 1, newsuite 1), 0 skipped, 1 crashed");
    free(t);
    t = probe_machine(&p);
    SAME(t, "radv, Mesa 26.1.8 (Linux)");
    free(t);
    /* through JSON, as a worker answers it: the same run, the same mark */
    o = probe_json(&p);
    CHECK(!probe_of_json(o, &q, &why) && !why);
    probe_digest(&p, digest);
    probe_digest(&q, other);
    SAME(digest, other);
    CHECK(q.ncases == 6 && q.driver_id == 3);
    SAME(q.driver_text, "26.1.8");
    probe_free(&q);
    /* an answer that is no run's, or keeps a detail the cut takes out */
    cJSON_ReplaceItemInObjectCaseSensitive(o, "emulator", cJSON_CreateString("Ryu"));
    CHECK(probe_of_json(o, &q, &why) == -1);
    HAS(why, "no emulator it may (Ryu)");
    free(why);
    cJSON_ReplaceItemInObjectCaseSensitive(o, "emulator", cJSON_CreateString("ScarletNX"));
    cJSON_AddStringToObject(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(o, "cases"), 2), "detail",
                            "nickname Someone"); /* probe_json writes no empty detail */
    CHECK(probe_of_json(o, &q, &why) == -1);
    HAS(why, "keeps a detail of a family that loses it (account)");
    free(why);
    cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(o, "cases"),
                         cJSON_Parse("{\"suite\":\"a b;c\",\"name\":\"x\","
                                     "\"result\":\"pass\"}"));
    cJSON_ReplaceItemInObjectCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(o, "cases"), 2),
                                           "detail", cJSON_CreateString(""));
    CHECK(probe_of_json(o, &q, &why) == -1);
    HAS(why, "case 7 is not one edenprobe writes");
    free(why);
    cJSON_Delete(o);
    /* another result is another mark */
    free(p.cases[0].result);
    p.cases[0].result = rei_strdup("fail");
    probe_digest(&p, other);
    CHECK(strcmp(digest, other) != 0);
    probe_free(&p);
    free(cut);
    /* Eden's log: its driver ID read from the driver's name, its version as NVIDIA numbers it */
    CHECK(probe_parse(EDEN_LOG, strlen(EDEN_LOG), &p, &why) == 1);
    SAME(p.emulator, "Eden");
    SAME(p.emulator_version, "Eden | HEAD-4534fde3aa-HEAD (Vulkan)");
    SAME(p.driver_name, "NVIDIA");
    SAME(p.driver_info, "580.95.5.0");
    CHECK(p.driver_id == 4 && p.inferred);
    SAME(p.driver_text, "580.95.05");
    SAME(p.gpu, "Some GeForce 9000");
    SAME(p.os, "windows");
    SAME(p.words, "graphics");
    CHECK(p.whole && p.ncases == 1 && !p.commit && p.built == 0);
    t = probe_machine(&p);
    SAME(t, "NVIDIA 580.95.5.0, driver ID inferred (Windows)");
    free(t);
    probe_free(&p);
    { /* rEden logs as Eden does, and says so in its version */
        char* reden = rei_strdup(EDEN_LOG);
        memcpy(strstr(reden, "Eden | HEAD"), "rEden| HEAD", 11);
        CHECK(probe_parse(reden, strlen(reden), &p, &why) == 1);
        SAME(p.emulator, "rEden");
        probe_free(&p);
        free(reden);
    }
    /* the report alone: the console's, no machine */
    CHECK(probe_parse(PROBE_JSONL, strlen(PROBE_JSONL), &p, &why) == 1);
    SAME(p.emulator, "console");
    CHECK(!p.driver_name && !p.gpu && !p.driver_id);
    SAME(p.os, "unknown");
    t = probe_machine(&p);
    SAME(t, "the console (a report.jsonl alone names no machine)");
    free(t);
    probe_free(&p);
    /* two copies of a run (the Share Log's summary.txt has the run's output and its log): the later, each case once;
       a run cut short before them is passed over for a whole one */
    {
        char* two = rei_format("%s%s%s", PROBE_JSONL, PROBE_LOG,
                               "[   9.00] [game] {\"type\":\"note\",\"text\":"
                               "\"edenprobe 0.8.0 (v0.8.0) starting\"}\n");
        CHECK(probe_parse(two, strlen(two), &p, &why) == 1);
        SAME(p.version, "0.9.0");
        SAME(p.emulator, "ScarletNX");
        CHECK(p.ncases == 6 && p.whole);
        probe_free(&p);
        free(two);
    }
    /* a run without its summary: what it has, counted from its lines */
    {
        char* part = rei_format("%.*s", (int)(strstr(PROBE_LOG, "[   0.23]") - PROBE_LOG), PROBE_LOG);
        CHECK(probe_parse(part, strlen(part), &p, &why) == 1);
        CHECK(!p.whole && p.passed == -1 && p.ncases == 6);
        t = probe_counts(&p);
        SAME(t, "3 passed, 2 failed (astc 1, newsuite 1), 0 skipped, 1 crashed");
        free(t);
        probe_free(&p);
        free(part);
    }
    /* a launch that went on with a run: lines with no start */
    {
        static const char resumed[] =
            "[   0.20] [game] {\"type\":\"note\",\"text\":\"journal: launch 2 resumes the run; the program ended "
            "inside index case \\\"an indexed draw\\\"\"}\n"
            "[   0.21] [game] {\"type\":\"case\",\"suite\":\"clear\",\"name\":\"clear to opaque red\",\"tier\":"
            "\"exact\",\"result\":\"pass\",\"detail\":\"\"}\n";
        CHECK(probe_parse(resumed, strlen(resumed), &p, &why) == -1);
        HAS(why, "a launch that went on with a run after a crash");
        free(why);
        CHECK(probe_parse(LOG, strlen(LOG), &p, &why) == 0 && !why);
    }
    /* lines that are not edenprobe's are counted, not read */
    {
        char* odd = rei_format("%s{\"type\":\"case\",\"suite\":\"clear\",\"name\":\"\",\"result\":\"pass\"}\n"
                               "{\"type\":\"wobble\"}\n",
                               PROBE_JSONL);
        CHECK(probe_parse(odd, strlen(odd), &p, &why) == 1);
        CHECK(p.unread == 2 && p.ncases == 1);
        probe_free(&p);
        free(odd);
    }
    /* a case's page name: the same for the same case */
    probe_case_key("astc", "a 4x4 block decodes", digest);
    probe_case_key("astc", "a 4x4 block decodes", other);
    SAME(digest, other);
    CHECK(strlen(digest) == 16);
    probe_case_key("astc", "a 4x4 block decode", other);
    CHECK(strcmp(digest, other) != 0);
    for (i = 0; i < 16; ++i) CHECK(isxdigit((unsigned char)digest[i]));
    /* .jsonl is a log's name now */
    CHECK(tr_kind("report.jsonl", "") == TR_LOG);
    t = tr_unpacked_name("report.jsonl.gz");
    SAME(t, "report.jsonl");
    free(t);
}

/* a zip of several files, each name and text, written by libarchive into out (64 KiB) */
static size_t zipped(char* out, size_t cap, const char* const* names, const char* const* texts, int n) {
    struct archive* a = archive_write_new();
    size_t used = 0;
    int i;
    archive_write_set_format_zip(a);
    archive_write_open_memory(a, out, cap, &used);
    for (i = 0; i < n; ++i) {
        struct archive_entry* e = archive_entry_new();
        archive_entry_set_pathname(e, names[i]);
        archive_entry_set_size(e, (la_int64_t)strlen(texts[i]));
        archive_entry_set_filetype(e, AE_IFREG);
        archive_entry_set_perm(e, 0644);
        archive_write_header(a, e);
        archive_write_data(a, texts[i], strlen(texts[i]));
        archive_entry_free(e);
    }
    archive_write_close(a);
    archive_write_free(a);
    return used;
}

/* the run in a Share Log zip: the entry that holds it, though other logs come first */
static void test_probe_zip(void) {
    static char zip[1 << 16];
    char* part = rei_format("%.*s", (int)(strstr(PROBE_LOG, "[   0.23]") - PROBE_LOG), PROBE_LOG);
    const char* names[] = {"summary.txt", "launcher/scarletnx-console-2026-11-07.log", "games/edenprobe/edenprobe.log",
                           "games/edenprobe/edenprobe-old.log"};
    const char* texts[] = {"== summary ==\nnothing here\n", LOG, PROBE_LOG, part};
    Buf out = {0};
    char* why;
    size_t n = zipped(zip, sizeof zip, names, texts, 4);
    CHECK(!tr_unpack("scarletnx-logs-2026-11-07.zip", zip, n, TR_LOG_MAX, &out, &why));
    CHECK(out.len == strlen(PROBE_LOG) && !memcmp(out.text, PROBE_LOG, out.len));
    /* a run begun and no whole one: that one, before the first .log */
    texts[2] = "no run here\n";
    n = zipped(zip, sizeof zip, names, texts, 4);
    CHECK(!tr_unpack("logs.zip", zip, n, TR_LOG_MAX, &out, &why));
    CHECK(out.len == strlen(part) && !memcmp(out.text, part, out.len));
    /* no run at all: the first .log, .txt or .jsonl as before */
    texts[3] = "nor here\n";
    n = zipped(zip, sizeof zip, names, texts, 4);
    CHECK(!tr_unpack("logs.zip", zip, n, TR_LOG_MAX, &out, &why));
    SAME(out.text, "== summary ==\nnothing here\n");
    buf_free(&out);
    free(part);
}

/* whole runs sent as reports: kept with their cases, the details that name the player left out of the database and
   the log, answered with the counts, never twice from one person, gone with their message */
static void test_probe_reports(void) {
    TrMessage m;
    char *t, *p, *rel, *why;
    long id, eden, console;
    StReport r;
    Probe run;
    StGame* games;
    int ngames, i;
    serve("https://cdn.discordapp.com/20/edenprobe.log", PROBE_LOG, strlen(PROBE_LOG));
    serve("https://cdn.discordapp.com/20/eden_log.txt", EDEN_LOG, strlen(EDEN_LOG));
    serve("https://cdn.discordapp.com/20/report.jsonl", PROBE_JSONL, strlen(PROBE_JSONL));
    m = message(9600, 960, "<@99> my run");
    attach(&m, "edenprobe.log", "https://cdn.discordapp.com/20/edenprobe.log", (double)strlen(PROBE_LOG));
    t = answer_text(respond(&m));
    HAS(t, "edenprobe 0.9.0 on radv, Mesa 26.1.8 (Linux): 3 passed, 2 failed (astc 1, newsuite 1), 0 skipped, 1 "
           "crashed");
    HAS(t, "Report #");
    HAS(t, ": edenprobe 0.9.0 on ScarletNX");
    HAS(t, "the detail of 3 cases of the service, net and pad families (or of no family edenprobe names) and 1 net "
           "note, which can name the player");
    HAS(t, "main 1234abc");
    free(t);
    tr_message_free(&m);
    id = st_by_message(tracer.store, 9600);
    CHECK(id > 0);
    CHECK(st_get(tracer.store, id, &r, NULL, NULL) == 1);
    SAME(r.key, PROBE_KEY);
    SAME(r.title, "edenprobe 0.9.0 on ScarletNX");
    SAME(r.version, "0.9.0");
    SAME(r.driver, "26.1.8");
    SAME(r.commit, "1234abc");
    st_report_free(&r);
    CHECK(st_probe_get(tracer.store, id, &run, 1) == 1);
    CHECK(run.ncases == 6 && run.driver_id == 3 && run.cut_cases == 3 && run.cut_notes == 1);
    SAME(run.words, "cpu graphics net notime");
    for (i = 0; i < run.ncases; ++i)
        if (!strcmp(run.cases[i].suite, "account") || !strcmp(run.cases[i].suite, "nifm") ||
            !strcmp(run.cases[i].suite, "newsuite"))
            SAME(run.cases[i].detail, "");
        else if (!strcmp(run.cases[i].suite, "index"))
            SAME(run.cases[i].reason, "crashed");
    probe_free(&run);
    /* the kept log says nothing of the player either */
    rel = rei_format("r/%ld/1.log", id);
    p = page(rel);
    LACKS(p, "nickname Someone");
    LACKS(p, "tok-abcdef");
    LACKS(p, "192.168");
    HAS(p, "texel 0 reads 00ff00ff");
    free(p);
    free(rel);
    /* no game's: the games and the lists leave it out */
    CHECK(!st_games(tracer.store, &games, &ngames));
    for (i = 0; i < ngames; ++i) CHECK(strcmp(games[i].key, PROBE_KEY) != 0);
    st_games_free(games, ngames);
    p = page("all.html");
    rel = rei_format("href=\"r/%ld/\"", id);
    LACKS(p, rel);
    free(p);
    p = page("api/reports.json");
    LACKS(p, "edenprobe 0.9.0 on ScarletNX");
    free(p);
    free(rel);
    /* the same run again from the same person: its link, nothing kept; from another person, a report of its own */
    m = message(9601, 960, "<@99> again");
    attach(&m, "edenprobe.log", "https://cdn.discordapp.com/20/edenprobe.log", (double)strlen(PROBE_LOG));
    t = answer_text(respond(&m));
    rel = rei_format("This run of edenprobe is report #%ld already", id);
    HAS(t, rel);
    free(rel);
    free(t);
    CHECK(!st_by_message(tracer.store, 9601));
    tr_message_free(&m);
    m = message(9602, 961, "<@99> mine too");
    attach(&m, "edenprobe.log", "https://cdn.discordapp.com/20/edenprobe.log", (double)strlen(PROBE_LOG));
    t = answer_text(respond(&m));
    HAS(t, "3 passed, 2 failed");
    free(t);
    tr_message_free(&m);
    CHECK(st_by_message(tracer.store, 9602) > id);
    /* Eden's log, and the report alone */
    m = message(9603, 962, "<@99>");
    attach(&m, "eden_log.txt", "https://cdn.discordapp.com/20/eden_log.txt", (double)strlen(EDEN_LOG));
    t = answer_text(respond(&m));
    HAS(t, "edenprobe 0.9.0 on NVIDIA 580.95.5.0, driver ID inferred (Windows): 0 passed, 1 failed (astc 1)");
    HAS(t, "Eden logs no driver ID: it is read from the driver's name.");
    free(t);
    tr_message_free(&m);
    eden = st_by_message(tracer.store, 9603);
    CHECK(st_get(tracer.store, eden, &r, NULL, NULL) == 1);
    SAME(r.build, "Eden | HEAD-4534fde3aa-HEAD (Vulkan)");
    SAME(r.gpu, "Some GeForce 9000");
    SAME(r.driver, "580.95.05");
    CHECK(!r.outcome && !r.commit);
    st_report_free(&r);
    m = message(9604, 963, "<@99> from my console");
    attach(&m, "report.jsonl", "https://cdn.discordapp.com/20/report.jsonl", (double)strlen(PROBE_JSONL));
    t = answer_text(respond(&m));
    HAS(t, "edenprobe 0.9.0 on the console (a report.jsonl alone names no machine): 1 passed, 0 failed, 0 skipped");
    HAS(t, "kept as the console's reference");
    free(t);
    tr_message_free(&m);
    console = st_by_message(tracer.store, 9604);
    CHECK(console > 0);
    /* gone with its message: its run and cases with it */
    CHECK(tr_forget(&tracer, 9604));
    CHECK(st_probe_get(tracer.store, console, &run, 1) == 0);
    {
        StProbeResult* results;
        int nresults, left = 0;
        CHECK(!st_probe_results(tracer.store, &results, &nresults));
        for (i = 0; i < nresults; ++i) left += results[i].report == console;
        CHECK(left == 0 && nresults == 6 + 6 + 1);
        free(results);
    }
    /* a launch that went on after a crash: kept as the game's log, said not to be a run */
    {
        static char resumed[sizeof LOG + 512];
        snprintf(resumed, sizeof resumed, "%s%s", LOG,
                 "[   2.30] [game] {\"type\":\"note\",\"text\":\"journal: launch 2 resumes the run\"}\n"
                 "[   2.31] [game] {\"type\":\"case\",\"suite\":\"clear\",\"name\":\"clear to opaque red\",\"tier\":"
                 "\"exact\",\"result\":\"pass\",\"detail\":\"\"}\n");
        serve("https://cdn.discordapp.com/20/resumed.log", resumed, strlen(resumed));
        m = message(9605, 964, "<@99>");
        attach(&m, "resumed.log", "https://cdn.discordapp.com/20/resumed.log", 10);
        t = answer_text(respond(&m));
        HAS(t, "Report #");
        HAS(t, "Not a run of edenprobe");
        HAS(t, "resumed.log: its edenprobe lines are a launch that went on with a run after a crash");
        free(t);
        tr_message_free(&m);
        CHECK(st_get(tracer.store, st_by_message(tracer.store, 9605), &r, NULL, NULL) == 1);
        SAME(r.key, "0100AAAABBBB0000");
        st_report_free(&r);
    }
    /* through a worker: the run in its answer, checked, the same as one read here */
    serve_file("https://cdn.discordapp.com/21/edenprobe.log", PROBE_LOG, strlen(PROBE_LOG));
    tracer.take = test_worker_take;
    worker_argv[1] = "--report";
    m = message(9606, 965, "<@99> through the worker");
    attach(&m, "edenprobe.log", "https://cdn.discordapp.com/21/edenprobe.log", 10);
    t = answer_text(respond(&m));
    HAS(t, "edenprobe 0.9.0 on radv, Mesa 26.1.8 (Linux): 3 passed, 2 failed");
    HAS(t, "the detail of 3 cases of the service, net and pad families");
    free(t);
    tr_message_free(&m);
    tracer.take = NULL;
    CHECK(st_probe_get(tracer.store, st_by_message(tracer.store, 9606), &run, 1) == 1);
    CHECK(run.ncases == 6 && run.cut_cases == 3);
    probe_free(&run);
    CHECK(waiting_files() == 0);
    (void)why;
}

/* a run of edenprobe on a machine: the driver's line (name, info, ID), the raw driverVersion, the system, the build's
   commit and day, and the astc case's result */
static char* machine_log(const char* driver, int id, const char* raw, const char* system, const char* commit,
                         const char* day, const char* result) {
    int pass = !strcmp(result, "pass");
    return rei_format(
        "[   0.00] ScarletNX - edenprobe\n"
        "[   0.00] build: ScarletNX main %s, committed %s (clang 23.1.3, x86_64)\n"
        "[   0.00] system: %s, Some CPU, 8 threads, 16 GB RAM\n"
        "[   0.18] vulkan: the driver that runs: %s (driver ID %d)\n"
        "[   0.18] vulkan: Some GPU, Vulkan 1.4.354, driver %s, graphics queue family 0\n"
        "[   0.20] [game] {\"type\":\"note\",\"text\":\"edenprobe 0.9.0 (v0.9.0) starting\"}\n"
        "[   0.20] [game] {\"type\":\"note\",\"text\":\"mode: quick, from the mode file; pictures of "
        "failures only; families graphics\"}\n"
        "[   0.21] [game] {\"type\":\"case\",\"suite\":\"astc\",\"name\":\"a 4x4 block decodes\",\"tier\":"
        "\"exact\",\"result\":\"%s\",\"detail\":\"%s\"}\n"
        "[   0.23] [game] {\"type\":\"summary\",\"passed\":%d,\"failed\":%d,\"skipped\":0,\"crashed\":0}\n",
        commit, day, system, driver, id, raw, result, pass ? "" : "texel 0 reads 0000ffff", pass, !pass);
}

/* the Probe pages and their JSON over the runs test_probe_reports left, then a ban candidate made of whole reports,
   drawn, given to the MCP server, and gone with its reports */
static void test_probe_site(void) {
    static const struct {
        const char *driver, *raw, *system, *commit, *day, *result;
        int id;
        u64 poster;
    } machines[] = {
        {"radv, Mesa 26.1.0", "06801000", "'Some Linux' (Linux 6.1)", "aaaa111", "2026-11-01", "fail", 3, 980},
        {"radv, Mesa 26.1.8", "06801008", "'Some Linux' (Linux 6.1)", "aaaa111", "2026-11-01", "fail", 3, 981},
        {"radv, Mesa 26.1.8", "06801008", "'Some Linux' (Linux 6.1)", "bbbb222", "2026-11-02", "fail", 3, 980},
        {"radv, Mesa 26.2.0", "06802000", "'Some Linux' (Linux 6.1)", "bbbb222", "2026-11-02", "pass", 3, 982},
        {"NVIDIA, 621.14", "9b404000", "Windows 11 26H2", "aaaa111", "2026-11-01", "pass", 4, 983},
        {"NVIDIA, 621.14", "9b404000", "Windows 11 26H2", "bbbb222", "2026-11-02", "pass", 4, 984},
        {"Intel open-source Mesa driver, Mesa 25.3.5", "06403005", "'Some Linux' (Linux 6.1)", "bbbb222", "2026-11-03",
         "pass", 6, 985},
    };
    static char logs[7][1200];
    char *p, *t, *rel, key[17], url[80];
    long ids[7];
    Mcp mcp;
    size_t i;
    probe_case_key("astc", "a 4x4 block decodes", key);
    mcp.root = site;
    mcp.url = "https://reports.example";
    /* the runs so far: the matrix, a case, a driver, the report's own page and JSON */
    p = page("probe/index.html");
    HAS(p, "<h1>Runs of edenprobe</h1>");
    HAS(p, "<th>ScarletNX<br>radv 26.1.8<br>Linux</th>");
    HAS(p, "<th>Eden<br>nvidia 580.95.05*<br>Windows</th>");
    rel = rei_format("<a href=\"c/%s.html\">a 4x4 block decodes</a></td><td><span class=\"badge bad\">", key);
    HAS(p, rel);
    free(rel);
    LACKS(p, "data-family=\"service\""); /* the account case is in no failing row: it passed */
    free(p);
    rel = rei_format("probe/c/%s.html", key);
    p = page(rel);
    HAS(p, "<h1>a 4x4 block decodes</h1>");
    HAS(p, "<h2>By machine</h2>");
    HAS(p, "texel 0 reads 00ff00ff, it holds ff0000ff");
    free(p);
    free(rel);
    p = page("probe/d/radv.html");
    HAS(p, "<h1>RADV</h1>");
    HAS(p, "radv · VkDriverId 3");
    free(p);
    p = page("probe/candidates.html");
    HAS(p, "<h2>Ban candidates <small>0</small></h2>");
    free(p);
    {
        long id = st_by_message(tracer.store, 9600);
        rel = rei_format("r/%ld/index.html", id);
        p = page(rel);
        HAS(p, "<h2>edenprobe <small>3 passed, 2 failed (astc 1, newsuite 1), 0 skipped, 1 crashed</small></h2>");
        HAS(p, "<a href=\"../../probe/index.html\">Probe</a> / edenprobe 0.9.0 on ScarletNX</p>");
        HAS(p, "<h3>Failed: 2</h3>");
        HAS(p, "crashed: the program ended inside it");
        LACKS(p, "nickname Someone");
        free(p);
        free(rel);
        rel = rei_format("api/r/%ld.json", id);
        p = page(rel);
        HAS(p, "\"probe\":{");
        HAS(p, "\"not_passed\":[");
        LACKS(p, "Someone"); /* the JSON names no player either */
        free(p);
        free(rel);
    }
    /* a ban candidate from whole reports: radv fails it from 26.1.0 for two people and passes on 26.2.0, and NVIDIA
       and ANV pass it */
    for (i = 0; i < sizeof machines / sizeof *machines; ++i) {
        char *log = machine_log(machines[i].driver, machines[i].id, machines[i].raw, machines[i].system,
                                machines[i].commit, machines[i].day, machines[i].result),
             name[32];
        TrMessage m;
        snprintf(logs[i], sizeof logs[i], "%s", log);
        free(log);
        snprintf(url, sizeof url, "https://cdn.discordapp.com/30/%zu.log", i);
        serve(rei_strdup(url), logs[i], strlen(logs[i]));
        m = message(9800 + (u64)i, machines[i].poster, "<@99>");
        snprintf(name, sizeof name, "run%zu.log", i);
        attach(&m, name, url, 10);
        t = answer_text(respond(&m));
        HAS(t, "edenprobe 0.9.0 on");
        free(t);
        tr_message_free(&m);
        ids[i] = st_by_message(tracer.store, 9800 + (u64)i);
        CHECK(ids[i] > 0);
    }
    p = page("probe/candidates.html");
    HAS(p, "<h2>Ban candidates <small>1</small></h2>");
    rel =
        rei_format("&lt;key&gt; | off | radv | &gt;= 26.1.0 &lt; 26.2.0 | linux | * | fails edenprobe&#39;s astc case "
                   "&quot;a 4x4 block decodes&quot; | Tracer #%ld, #%ld, #%ld; edenprobe 0.9.0 &quot;a 4x4 block "
                   "decodes&quot; | edenprobe:astc: a 4x4 block decodes | ",
                   ids[0], ids[1], ids[2]);
    HAS(p, rel);
    free(rel);
    HAS(p, "3 reports from 2 people");
    HAS(p, "3 reports of 2 other drivers");
    free(p);
    p = page("probe/d/radv.html");
    HAS(p, "<h2>Ban candidates</h2>");
    free(p);
    p = page("api/probe/candidates.json");
    HAS(p, "\"versions\":\">= 26.1.0 < 26.2.0\"");
    free(p);
    /* the MCP server's tools over it */
    t = call(&mcp, "ban_candidates", "{\"driver\":\"radv\"}");
    HAS(t, ">= 26.1.0 < 26.2.0");
    HAS(t, "probe/candidates.html");
    free(t);
    t = call(&mcp, "ban_candidates", "{\"driver\":\"nvidia\",\"kind\":\"candidates\"}");
    LACKS(t, "26.1.0");
    LACKS(t, "regressions");
    free(t);
    t = call(&mcp, "ban_candidates", "{\"kind\":\"bans\"}");
    HAS(t, "kind=bans is none of candidates, general, regressions");
    HAS(t, "\"isError\":true");
    free(t);
    t = call(&mcp, "probe_matrix", "{\"driver\":\"radv\",\"query\":\"4x4\"}");
    HAS(t, "\\\"name\\\":\\\"a 4x4 block decodes\\\"");
    HAS(t, "\\\"driver\\\":\\\"radv\\\"");
    LACKS(t, "\\\"driver\\\":\\\"nvidia\\\"");
    free(t);
    t = call(&mcp, "probe_matrix", "{\"suite\":\"clear\"}"); /* it passes everywhere: none failing */
    HAS(t, "\\\"matched\\\":0");
    free(t);
    t = call(&mcp, "probe_matrix", "{\"suite\":\"clear\",\"failing\":false}");
    HAS(t, "\\\"matched\\\":1");
    free(t);
    t = call(&mcp, "probe_case", "{\"query\":\"4x4 block\"}");
    HAS(t, "\\\"family\\\":\\\"graphics\\\"");
    HAS(t, "texel 0 reads 0000ffff");
    free(t);
    rel = rei_format("{\"case\":\"%s\"}", key);
    t = call(&mcp, "probe_case", rel);
    HAS(t, "\\\"name\\\":\\\"a 4x4 block decodes\\\"");
    free(t);
    free(rel);
    t = call(&mcp, "probe_case", "{\"case\":\"../../etc/passwd\"}");
    HAS(t, "is not a case's key");
    free(t);
    t = call(&mcp, "probe_case", "{\"query\":\"a\"}"); /* several: the list */
    HAS(t, "\\\"matched\\\":");
    free(t);
    t = call(&mcp, "probe_case", "{\"query\":\"nonesuch\"}");
    HAS(t, "no case's suite or name holds nonesuch");
    free(t);
    /* gone with their reports: the candidate, and a case no run names any more loses its page */
    for (i = 0; i < sizeof machines / sizeof *machines; ++i) CHECK(tr_forget(&tracer, 9800 + (u64)i));
    p = page("probe/candidates.html");
    HAS(p, "<h2>Ban candidates <small>0</small></h2>");
    free(p);
    {
        long left[] = {st_by_message(tracer.store, 9600), st_by_message(tracer.store, 9602),
                       st_by_message(tracer.store, 9603), st_by_message(tracer.store, 9606)};
        char* path;
        for (i = 0; i < sizeof left / sizeof *left; ++i) CHECK(st_remove(tracer.store, left[i]) == 0);
        CHECK(!site_probe(tracer.store, site));
        rel = rei_format("probe/c/%s.html", key);
        path = at(rel);
        CHECK(!exists(path)); /* no run names the case now */
        free(path);
        free(rel);
    }
}

/* a report kept before Tracer knew runs of edenprobe: the next start makes it the run's, its log cut */
static void test_probe_rescan(void) {
    StReport r;
    StFile f;
    Probe run;
    char *rel, *p;
    StReport* all;
    int n;
    memset(&r, 0, sizeof r);
    memset(&f, 0, sizeof f);
    r.when = now_is;
    r.message = 9700;
    r.poster = 970;
    r.key = "0100000000001000";
    r.title = "edenprobe";
    r.named = 1;
    f.n = 1;
    strcpy(f.kind, "log");
    f.name = "edenprobe.log";
    f.path = "1.log";
    f.bytes = (double)strlen(PROBE_LOG);
    CHECK(!st_add(tracer.store, &r, &f, 1));
    rel = rei_format("r/%ld/1.log", r.id);
    CHECK(!site_write(site, rel, PROBE_LOG, strlen(PROBE_LOG)));
    CHECK(!st_reports(tracer.store, NULL, &all, &n));
    st_reports_free(all, n);
    CHECK(tr_rescan(&tracer, "probe rescan") == n);
    CHECK(st_probe_get(tracer.store, r.id, &run, 1) == 1);
    CHECK(run.ncases == 6);
    SAME(run.emulator, "ScarletNX");
    probe_free(&run);
    {
        StReport now;
        CHECK(st_get(tracer.store, r.id, &now, NULL, NULL) == 1);
        SAME(now.key, PROBE_KEY);
        SAME(now.title, "edenprobe 0.9.0 on ScarletNX");
        SAME(now.driver, "26.1.8");
        st_report_free(&now);
    }
    p = page(rel);
    LACKS(p, "nickname Someone");
    HAS(p, "texel 0 reads 00ff00ff");
    free(p);
    free(rel);
}

/* ---- the rules over the runs ---- */

#define DAY        86400.0
#define D0         1793491200.0 /* 2026-11-01 */
#define V(a, b, c) (((unsigned long)(a) << 22) | ((unsigned long)(b) << 12) | (unsigned long)(c))
/* a run as the rules read it: its report, who sent it, the emulator, the driver, its version, the system's family, the
   build's commit and day (0 for none), edenprobe's words */
static StProbeRun rule_run(long report, u64 poster, const char* emulator, int driver, unsigned long version,
                           const char* os, const char* commit, double built, const char* words) {
    StProbeRun r;
    memset(&r, 0, sizeof r);
    r.report = report;
    r.poster = poster;
    r.when = built ? built + 3600 : D0;
    r.p.emulator = (char*)emulator;
    r.p.version = (char*)"0.9.0";
    r.p.words = (char*)words;
    r.p.os = (char*)os;
    r.p.driver_id = driver;
    r.p.inferred = strcmp(emulator, "ScarletNX") != 0;
    r.p.driver_version = version;
    r.p.commit = (char*)commit;
    r.p.built = built;
    return r;
}
/* the rules over runs, each one's result of case 1 given in results (ST_*), in the runs' order */
static Rules rules_of(const StProbeRun* runs, int n, const int* results) {
    StProbeResult r[64];
    Rules out;
    int i;
    for (i = 0; i < n; ++i) {
        r[i].report = runs[i].report;
        r[i].name = 1;
        r[i].result = results[i];
    }
    for (i = 1; i < n; ++i) /* st_probe_results' order: by case, then report */
        CHECK(r[i - 1].report < r[i].report);
    CHECK(!rules_make(runs, n, r, n, &out));
    return out;
}

static void test_rules(void) {
    /* radv fails the case from 26.1.0 to 26.1.8 for two people, and passes on 26.2.0; two other drivers pass it */
    StProbeRun base[] = {
        rule_run(1, 101, "ScarletNX", 3, V(26, 1, 0), "linux", "1111111", D0, "graphics"),
        rule_run(2, 102, "ScarletNX", 3, V(26, 1, 8), "linux", "1111111", D0 + DAY, "graphics"),
        rule_run(3, 101, "ScarletNX", 3, V(26, 1, 8), "linux", "2222222", D0 + 2 * DAY, "graphics"),
        rule_run(4, 103, "ScarletNX", 3, V(26, 2, 0), "linux", "2222222", D0 + 3 * DAY, "graphics"),
        rule_run(5, 104, "ScarletNX", 4, 0x9b404000UL, "windows", "1111111", D0, "graphics"),
        rule_run(6, 105, "ScarletNX", 4, 0x9b404000UL, "windows", "1111111", D0 + DAY, "graphics"),
        rule_run(7, 106, "ScarletNX", 6, V(25, 3, 5), "linux", "2222222", D0 + 2 * DAY, "graphics"),
        rule_run(8, 107, "Eden", 3, V(26, 1, 8), "unknown", NULL, 0, "graphics"),
        rule_run(9, 108, "console", 0, 0, "unknown", NULL, 0, "graphics"),
    };
    int results[] = {ST_FAIL, ST_FAIL, ST_FAIL, ST_PASS, ST_PASS, ST_PASS, ST_PASS, ST_FAIL, ST_PASS};
    StProbeRun runs[16];
    int res[16];
    Rules r = rules_of(base, 9, results);
    char* line;
    CHECK(r.ncandidates == 1 && r.ngeneral == 0);
    if (r.ncandidates == 1) {
        RuleCandidate* c = &r.candidates[0];
        CHECK(c->name == 1 && c->driver_id == 3 && !c->mixed && c->low == V(26, 1, 0) && c->high == V(26, 2, 0));
        SAME(c->os, "linux");
        SAME(c->versions, ">= 26.1.0 < 26.2.0");
        SAME(c->broken, "26.1.8");
        CHECK(c->nfailing == 3 && c->failing[0] == 1 && c->failing[2] == 3 && c->people == 2);
        CHECK(c->npassing == 3 && c->pass_drivers == 2 && c->passing[0] == 5);
        CHECK(c->eden_fail == 1 && c->eden_pass == 0);
        CHECK(!c->extensions);
        line = rules_draft(c, "astc", "a 4x4 block | decodes");
        SAME(line, "<key> | off | radv | >= 26.1.0 < 26.2.0 | linux | * | fails edenprobe's astc case \"a 4x4 block / "
                   "decodes\" | Tracer #1, #2, #3; edenprobe 0.9.0 \"a 4x4 block / decodes\" | edenprobe:astc: a 4x4 "
                   "block / decodes | 2026-11-03 broken on 26.1.8 | -");
        free(line);
    }
    rules_free(&r);
    /* one person's failures are not enough */
    memcpy(runs, base, sizeof base);
    memcpy(res, results, sizeof results);
    runs[1].poster = 101;
    r = rules_of(runs, 9, res);
    CHECK(r.ncandidates == 0);
    rules_free(&r);
    /* nor two reports */
    memcpy(runs, base, sizeof base);
    res[2] = ST_SKIP;
    r = rules_of(runs, 9, res);
    CHECK(r.ncandidates == 0);
    rules_free(&r);
    memcpy(res, results, sizeof results);
    /* the passes of one other driver only: the failure may be ScarletNX's */
    runs[6].p.driver_id = 4;
    r = rules_of(runs, 9, res);
    CHECK(r.ncandidates == 0);
    rules_free(&r);
    /* two passing reports only */
    memcpy(runs, base, sizeof base);
    res[5] = ST_SKIP;
    r = rules_of(runs, 9, res);
    CHECK(r.ncandidates == 0);
    rules_free(&r);
    memcpy(res, results, sizeof results);
    /* a passing build seven days from a failing one counts, eight days does not; before it as after it */
    runs[5].p.built = D0 + 9 * DAY;
    r = rules_of(runs, 9, res);
    CHECK(r.ncandidates == 1);
    rules_free(&r);
    runs[5].p.built = D0 + 10 * DAY;
    r = rules_of(runs, 9, res);
    CHECK(r.ncandidates == 0);
    rules_free(&r);
    runs[5].p.built = D0 - 7 * DAY;
    r = rules_of(runs, 9, res);
    CHECK(r.ncandidates == 1);
    rules_free(&r);
    runs[5].p.built = D0 - 8 * DAY;
    r = rules_of(runs, 9, res);
    CHECK(r.ncandidates == 0);
    rules_free(&r);
    runs[5].p.built = 0; /* no build day: the day it came */
    runs[5].when = D0 - 8 * DAY;
    r = rules_of(runs, 9, res);
    CHECK(r.ncandidates == 0);
    rules_free(&r);
    /* the version above that fails too is a mixed end; with none above, no end */
    memcpy(runs, base, sizeof base);
    runs[9] = rule_run(10, 109, "ScarletNX", 3, V(26, 2, 0), "linux", "2222222", D0 + 3 * DAY, "graphics");
    res[9] = ST_FAIL;
    r = rules_of(runs, 10, res);
    CHECK(r.ncandidates == 1 && r.candidates[0].mixed && r.candidates[0].high == V(26, 2, 0));
    rules_free(&r);
    res[3] = ST_SKIP; /* 26.2.0 passes nowhere now */
    r = rules_of(runs, 9, res);
    CHECK(r.ncandidates == 1 && r.candidates[0].high == 0);
    if (r.ncandidates == 1) SAME(r.candidates[0].versions, ">= 26.1.0");
    rules_free(&r);
    memcpy(res, results, sizeof results);
    /* a version that passes splits the range: the part below has too few failures, the part above enough */
    {
        StProbeRun split[] = {
            rule_run(1, 101, "ScarletNX", 3, V(26, 1, 0), "linux", "1111111", D0, "graphics"),
            rule_run(2, 102, "ScarletNX", 3, V(26, 1, 0), "linux", "1111111", D0, "graphics"),
            rule_run(3, 103, "ScarletNX", 3, V(26, 1, 4), "linux", "1111111", D0, "graphics"),
            rule_run(4, 101, "ScarletNX", 3, V(26, 1, 8), "linux", "1111111", D0, "graphics"),
            rule_run(5, 102, "ScarletNX", 3, V(26, 1, 8), "linux", "1111111", D0, "graphics"),
            rule_run(6, 104, "ScarletNX", 3, V(26, 1, 8), "linux", "1111111", D0, "graphics"),
            rule_run(7, 105, "ScarletNX", 4, 0x9b404000UL, "windows", "1111111", D0, "graphics"),
            rule_run(8, 106, "ScarletNX", 4, 0x9b404000UL, "windows", "1111111", D0, "graphics"),
            rule_run(9, 107, "ScarletNX", 6, V(25, 3, 5), "linux", "1111111", D0, "graphics"),
        };
        int split_res[] = {ST_FAIL, ST_FAIL, ST_PASS, ST_FAIL, ST_FAIL, ST_FAIL, ST_PASS, ST_PASS, ST_PASS};
        r = rules_of(split, 9, split_res);
        CHECK(r.ncandidates == 1);
        if (r.ncandidates == 1) {
            SAME(r.candidates[0].versions, ">= 26.1.8");
            CHECK(r.candidates[0].nfailing == 3 && r.candidates[0].people == 3);
        }
        rules_free(&r);
        /* the same driver on another system is another range: its runs do not join */
        split[3].p.os = (char*)"android";
        r = rules_of(split, 9, split_res);
        CHECK(r.ncandidates == 0);
        rules_free(&r);
    }
    /* Eden's failures show beside a candidate and count for none; another mode is another comparison */
    memcpy(runs, base, sizeof base);
    runs[2].p.emulator = (char*)"Eden";
    r = rules_of(runs, 9, res);
    CHECK(r.ncandidates == 0);
    rules_free(&r);
    memcpy(runs, base, sizeof base);
    runs[2].p.words = (char*)"graphics notime";
    r = rules_of(runs, 9, res);
    CHECK(r.ncandidates == 0);
    rules_free(&r);
    /* the extensions on in every failing run */
    memcpy(runs, base, sizeof base);
    runs[0].p.extensions = (char*)"VK_EXT_a, VK_EXT_b, VK_EXT_c";
    runs[1].p.extensions = (char*)"VK_EXT_c,VK_EXT_a";
    runs[2].p.extensions = (char*)" VK_EXT_a , VK_EXT_c, VK_EXT_d";
    r = rules_of(runs, 9, res);
    CHECK(r.ncandidates == 1);
    if (r.ncandidates == 1) SAME(r.candidates[0].extensions, "VK_EXT_a, VK_EXT_c");
    rules_free(&r);
    /* ScarletNX's own: failing on three drivers, which no candidate is then; on every driver seen, two of them */
    memcpy(runs, base, sizeof base);
    res[3] = res[4] = res[5] = res[6] = ST_FAIL;
    r = rules_of(runs, 9, res);
    CHECK(r.ngeneral == 1 && r.ncandidates == 0);
    if (r.ngeneral == 1) {
        CHECK(r.general[0].ndrivers == 3 && r.general[0].seen == 3 && r.general[0].nreports == 7);
        CHECK(r.general[0].eden_runs == 1 && r.general[0].eden_pass == 0);
    }
    rules_free(&r);
    res[3] = ST_PASS; /* radv passes on 26.2.0: a driver with a pass is not one that fails */
    r = rules_of(runs, 9, res);
    CHECK(r.ngeneral == 0);
    rules_free(&r);
    res[3] = ST_FAIL;
    res[6] = ST_PASS; /* anv passes: two of three drivers fail, radv and nvidia */
    r = rules_of(runs, 9, res);
    CHECK(r.ngeneral == 0);
    rules_free(&r);
    res[6] = ST_SKIP; /* anv says nothing: both drivers seen fail */
    r = rules_of(runs, 9, res);
    CHECK(r.ngeneral == 1);
    rules_free(&r);
    {
        int one[] = {ST_FAIL, ST_FAIL, ST_FAIL, ST_FAIL, ST_SKIP, ST_SKIP, ST_SKIP, ST_FAIL, ST_PASS};
        r = rules_of(runs, 9, one); /* one driver seen: no rule says whose */
        CHECK(r.ngeneral == 0 && r.ncandidates == 0);
        rules_free(&r);
        runs[7].p.driver_id = 24; /* Eden failing on another driver is no driver ScarletNX was seen on */
        r = rules_of(runs, 9, one);
        CHECK(r.ngeneral == 0);
        rules_free(&r);
        runs[7].p.driver_id = 3;
    }
    memcpy(res, results, sizeof results);
    /* three drivers of five fail it: ScarletNX's, though two others pass it often enough to clear radv */
    {
        StProbeRun five[] = {
            rule_run(1, 101, "ScarletNX", 3, V(26, 1, 8), "linux", "1111111", D0, "graphics"),
            rule_run(2, 102, "ScarletNX", 3, V(26, 1, 8), "linux", "1111111", D0, "graphics"),
            rule_run(3, 103, "ScarletNX", 3, V(26, 1, 8), "linux", "1111111", D0, "graphics"),
            rule_run(4, 104, "ScarletNX", 4, 0x9b404000UL, "windows", "1111111", D0, "graphics"),
            rule_run(5, 105, "ScarletNX", 6, V(25, 3, 5), "linux", "1111111", D0, "graphics"),
            rule_run(6, 106, "ScarletNX", 18, V(26, 1, 8), "android", "1111111", D0, "graphics"),
            rule_run(7, 107, "ScarletNX", 18, V(26, 1, 8), "android", "1111111", D0, "graphics"),
            rule_run(8, 108, "ScarletNX", 24, V(26, 1, 8), "linux", "1111111", D0, "graphics"),
        };
        int five_res[] = {ST_FAIL, ST_FAIL, ST_FAIL, ST_FAIL, ST_FAIL, ST_PASS, ST_PASS, ST_PASS};
        r = rules_of(five, 8, five_res);
        CHECK(r.ngeneral == 1 && r.ncandidates == 0);
        if (r.ngeneral == 1) CHECK(r.general[0].ndrivers == 3 && r.general[0].seen == 5);
        rules_free(&r);
        five_res[4] = ST_PASS; /* two drivers fail of five: radv is the candidate, and the failure no one's own */
        r = rules_of(five, 8, five_res);
        CHECK(r.ngeneral == 0 && r.ncandidates == 1);
        rules_free(&r);
    }
    /* a regression: passed on a build, failed on each after it, on one driver and version */
    {
        StProbeRun builds[] = {
            rule_run(1, 101, "ScarletNX", 3, V(26, 1, 8), "linux", "aaaaaaa", D0, "graphics"),
            rule_run(2, 102, "ScarletNX", 3, V(26, 1, 8), "linux", "aaaaaaa", D0, "graphics"),
            rule_run(3, 101, "ScarletNX", 3, V(26, 1, 8), "linux", "bbbbbbb", D0 + DAY, "graphics"),
            rule_run(4, 103, "ScarletNX", 3, V(26, 1, 8), "linux", "ccccccc", D0 + 2 * DAY, "graphics"),
            rule_run(5, 104, "ScarletNX", 3, V(26, 1, 8), "linux", "ccccccc", D0 + 2 * DAY, "graphics"),
            rule_run(6, 105, "ScarletNX", 3, V(26, 2, 0), "linux", "ccccccc", D0 + 2 * DAY, "graphics"),
            rule_run(7, 106, "ScarletNX", 3, V(26, 1, 8), "linux", "ddddddd-dirty", D0 + 3 * DAY, "graphics"),
        };
        int regress[] = {ST_PASS, ST_PASS, ST_FAIL, ST_FAIL, ST_FAIL, ST_PASS, ST_PASS};
        r = rules_of(builds, 7, regress);
        CHECK(r.nregressions == 1);
        if (r.nregressions == 1) {
            RuleRegression* g = &r.regressions[0];
            SAME(g->passed_commit, "aaaaaaa");
            SAME(g->failed_commit, "bbbbbbb");
            CHECK(g->passed_report == 1 && g->failed_report == 3 && g->builds == 2 && g->driver_id == 3);
            CHECK(g->driver_version == V(26, 1, 8) && g->failed_built == D0 + DAY);
        }
        rules_free(&r);
        regress[4] = ST_PASS; /* the newest build passes in one run: not a build that fails */
        r = rules_of(builds, 7, regress);
        CHECK(r.nregressions == 0);
        rules_free(&r);
        regress[4] = ST_FAIL;
        regress[1] = ST_FAIL; /* the build before passes in one run only: no build that passed */
        r = rules_of(builds, 7, regress);
        CHECK(r.nregressions == 0);
        rules_free(&r);
        regress[1] = ST_PASS;
        builds[6].p.commit = (char*)"ddddddd"; /* a published build after them that passes: fixed */
        r = rules_of(builds, 7, regress);
        CHECK(r.nregressions == 0);
        rules_free(&r);
    }
}

static int rules_only(void) {
    test_rules();
    fprintf(stderr, "tracer rules: %d of %d checks passed\n", checks - failures, checks);
    return failures != 0;
}

int main(int argc, char** argv) {
    char *db, *command;
    if (argc == 2 && !strcmp(argv[1], "--rules")) return rules_only(); /* scripts/test-rule-mutants.sh's */
    if (argc == 2) return as_worker(argv[1]);
    rei_now = clock_now;
    strcpy(site, "/tmp/tracer-test-XXXXXX");
    if (!mkdtemp(site)) {
        perror("mkdtemp");
        return 1;
    }
    db = rei_format("%s/tracer.db", site);
    tracer.store = st_open(db);
    tracer.root = site;
    tracer.work = rei_format("%s/work", site);
    CHECK(!mkdir(tracer.work, 0700));
    {
        char* files = rei_format("%s/served", site);
        CHECK(!mkdir(files, 0700));
        setenv("WORKER_TEST_SERVED", files, 1);
        setenv("WORKER_TEST_SITE", site, 1);
        free(files);
    }
    tracer.url = "https://reports.example";
    tracer.get = fake_get;
    tracer.administers = fake_administers;
    tracer.admins = NULL;
    CHECK(tracer.store != NULL);
    SAME(REI_AGENT, "DiscordBot (https://github.com/scarletnx/wimsy-core, 1) tracer");
    test_message();
    test_command();
    test_kind_and_text();
    test_images();
    test_unpack();
    test_public_log();
    test_channels();
    test_game();
    test_probe_parts();
    test_probe_lines();
    test_probe_zip();
    test_rules();
    if (tracer.store) {
        test_reports();
        test_mcp();
        test_upgrade();
        test_rescan();
        test_titles();
        test_names();
        test_missed();
        test_limits();
        test_other_guild();
        test_replies();
        test_mcp_grep();
        test_mcp_keys();
        test_worker(argv[0]);
        test_untraceable(argv[0]);
        test_issues();
        test_age();
        test_archive_download();
        test_probe_reports();
        test_probe_site();
        test_probe_rescan();
    }
    st_close(tracer.store);
    if (getenv("TRACER_TEST_KEEP")) fprintf(stderr, "kept %s\n", site);
    command = rei_format(getenv("TRACER_TEST_KEEP") ? "true %s" : "rm -rf '%s'", site);
    if (system(command)) fprintf(stderr, "%s failed\n", command);
    free(command);
    free(db);
    fprintf(stderr, "tracer: %d of %d checks passed\n", checks - failures, checks);
    return failures != 0;
}
