/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* Tracer without Discord or the network: what a message asks, which files are logs and screenshots, the screenshots'
   metadata taken out, the logs unpacked, the game a log names, and whole reports kept, drawn, refused and deleted
   through a fake download into a database and a site of a temporary folder */
#define _POSIX_C_SOURCE 200809L
#include <archive.h>
#include <archive_entry.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "files.h"
#include "mcp.h"
#include "message.h"
#include "report.h"
#include "site.h"
#include "store.h"

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
static Served served[16];
static int nserved, downloads;
static void serve(const char* url, const char* data, size_t n) {
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
    CHECK(tr_strip_image("GIF89a...", 9) == 9);
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
    buf_free(&out);
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
    game("{\"title\":\"SUPER GAME (0100000000010000/0100000000010000)\",\"title_id\":null}", "", 1, "0100000000010000",
         "SUPER GAME", "0100000000010000", 1);
    game("{\"title\":\"010074F013262000\"}", "it stutters", 1, "010074F013262000", "010074F013262000",
         "010074F013262000", 0);
    game("{\"title\":\"E\xef\xbf\xbdJ\x01G\",\"title_id\":\"01007EF00011E000\"}", "", 1, "01007EF00011E000",
         "01007EF00011E000", "01007EF00011E000", 0);
    game("{\"title\":\"010074F013262000\"}", "", 1, "010074F013262000", "010074F013262000", "010074F013262000", 0);
    game("{\"title\":\"Life: Living the Dream\"}", "", 1, "n-life-living-the-dream", "Life: Living the Dream", NULL, 1);
    game("{\"build\":\"ScarletNX main 1\"}", "My Game!", 1, "n-my-game", "My Game!", NULL, 0);
    game("{\"build\":\"ScarletNX main 1\"}", "\xe3\x82\xb2\xe3\x83\xbc\xe3\x83\xa0", 1, "n-0adc07b2",
         "\xe3\x82\xb2\xe3\x83\xbc\xe3\x83\xa0", NULL, 0);
    game("{\"build\":\"ScarletNX main 1\"}", "", 0, NULL, NULL, NULL, 0);
}

/* ---- whole reports ---- */
static char site[64];
static Tracer tracer;
/* user 88 administers guild 33 through role 5 */
static int fake_administers(void* ctx, u64 guild, u64 user, const u64* roles, int nroles) {
    (void)ctx;
    (void)user;
    return guild == 33 && nroles == 1 && roles[0] == 5;
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
    free(p);
    /* the JSON: the report, its files and summaries, and no Discord id */
    p = page("api/r/1.json");
    HAS(p, "\"game\":\"0100AAAABBBB0000\"");
    HAS(p, "\"path\":\"r/1/1.log\"");
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
        /* a reply to the update's answer reaches the report too */
        m = message(4006, 101, "<@99> add #16");
        attach(&m, "shot.png", "https://cdn.discordapp.com/1/shot.png", (double)npng);
        free(answer_text(respond(&m)));
        tr_message_free(&m);
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
    free(answer_text(respond(&add))); /* report 2 gets a screenshot, 2.png */
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
    CHECK(cJSON_GetArraySize(jpath(o, "result.tools")) == 6);
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
    t = call(&m, "list_games", "{\"query\":\"zelda\"}");
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
    HAS(t, "12 lines; 1 from line 1 on hold");
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
        CHECK(st_counts(s, 1, &nfiles, &nfiles) == 1);
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
}

int main(void) {
    char *db, *command;
    rei_now = clock_now;
    strcpy(site, "/tmp/tracer-test-XXXXXX");
    if (!mkdtemp(site)) {
        perror("mkdtemp");
        return 1;
    }
    db = rei_format("%s/tracer.db", site);
    tracer.store = st_open(db);
    tracer.root = site;
    tracer.url = "https://reports.example";
    tracer.get = fake_get;
    tracer.administers = fake_administers;
    tracer.admins = NULL;
    CHECK(tracer.store != NULL);
    test_message();
    test_command();
    test_kind_and_text();
    test_images();
    test_unpack();
    test_game();
    if (tracer.store) {
        test_reports();
        test_mcp();
        test_upgrade();
        test_rescan();
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
