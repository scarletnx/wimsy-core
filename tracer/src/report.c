/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "report.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "common/utilities/demangle.h"
#include "common/utilities/logscan.h"
#include "files.h"
#include "site.h"
#include "titles.h"

#define TONE_OK   0x22d3ee /* the phosphor palette's cyan, purple, rose and blue */
#define TONE_WARN 0xa855f7
#define TONE_BAD  0xf43f5e
#define TONE_SAID 0x3b82f6

static cJSON* embed(const char* title, const char* description, int color) {
    cJSON* e = cJSON_CreateObject();
    jset(e, "title", title);
    jset(e, "description", description);
    cJSON_AddNumberToObject(e, "color", color);
    return e;
}
static cJSON* said(const char* description) { return embed(NULL, description, TONE_SAID); }
static void add_field(cJSON* e, const char* name, const char* value) {
    cJSON *fields = cJSON_GetObjectItemCaseSensitive(e, "fields"), *f;
    if (!fields) fields = cJSON_AddArrayToObject(e, "fields");
    f = cJSON_CreateObject();
    jset(f, "name", name);
    jset(f, "value", value);
    cJSON_AddItemToArray(fields, f);
}
/* text for Discord's markdown: no backticks, cut to max bytes on a whole UTF-8 sequence */
static char* plain(const char* text, size_t max) {
    char *t = rei_strdup(text ? text : ""), *p;
    size_t n = strlen(t);
    for (p = t; *p; ++p)
        if (*p == '`') *p = '\'';
    if (n > max) {
        n = max;
        while (n && ((unsigned char)t[n] & 0xc0) == 0x80) --n;
        t[n] = 0;
    }
    return t;
}

/* ---- the game ---- */

static int title_id(const char* s, size_t n) {
    size_t i;
    if (n != 16) return 0;
    for (i = 0; i < n; ++i)
        if (!isxdigit((unsigned char)s[i])) return 0;
    return 1;
}
static char* upper(const char* s, size_t n) {
    char* t = rei_format("%.*s", (int)n, s);
    char* p;
    for (p = t; *p; ++p) *p = (char)toupper((unsigned char)*p);
    return t;
}
/* n- and the name's letters and digits, lowered, other runs a -; FNV-1a of the name when it has none (a name in
   another script) */
static int on_switch(const char* console) { return !console || !strcmp(console, "Switch"); }
static int on_gba(const char* console) { return console && !strcmp(console, "GBA"); }

/* n-, the console's name but the Switch's ("n-vita-"), so that two consoles' games of one name stay apart */
static char* name_key(const char* name, const char* console) {
    Buf b = {0};
    const char* s;
    size_t start;
    int dash = 0;
    buf_add(&b, "n-");
    for (s = console; !on_switch(console) && *s; ++s)
        if (isalnum((unsigned char)*s)) {
            char c = (char)tolower((unsigned char)*s);
            buf_addn(&b, &c, 1);
        }
    if (b.len > 2) buf_add(&b, "-");
    start = b.len;
    for (s = name; *s && b.len < TR_KEY - 1; ++s)
        if (isalnum((unsigned char)*s) && !((unsigned char)*s & 0x80)) {
            char c = (char)tolower((unsigned char)*s);
            if (dash && b.len > start) buf_add(&b, "-");
            dash = 0;
            buf_addn(&b, &c, 1);
        } else
            dash = 1;
    if (b.len == start) {
        unsigned long h = 2166136261UL;
        for (s = name; *s; ++s) h = ((h ^ (unsigned char)*s) * 16777619UL) & 0xffffffffUL;
        buf_printf(&b, "%08lx", h);
    }
    return buf_take(&b);
}

/* a name ScarletNX's launcher wrote into a shared log: its Share Log writes each byte outside printable ASCII as '?', so
   each other letter, two to four bytes in UTF-8, comes as two '?' or more in a row ("Pok??mon"). Not the game's name
   then: another report's log's, or the list's, comes before it */
static int mangled(const char* name) { return name && strstr(name, "??") != NULL; }

/* a name and not bytes the log misread: no control characters, no U+FFFD (tr_utf8's mark of an invalid byte) */
static int named_well(const char* name, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i)
        if ((unsigned char)name[i] < 0x20 || (unsigned char)name[i] == 0x7f ||
            (i + 2 < n && !memcmp(name + i, "\xef\xbf\xbd", 3)))
            return 0;
    return 1;
}

char* tr_missed(const cJSON* summary) {
    /* what each kind of the summary counts as, by the kinds' names of logscan.c's gap_kinds */
    static const struct {
        const char *kind, *one, *many;
    } kinds[] = {{"shader", "shader", "shaders"},
                 {"compute shader", "shader", "shaders"},
                 {"class", "engine class", "engine classes"},
                 {"texture format", "texture format", "texture formats"},
                 {"target format", "render target format", "render target formats"},
                 {"depth format", "depth format", "depth formats"},
                 {"gpu", "GPU operation", "GPU operations"},
                 {"macro", "macro", "macros"},
                 {"macro compile", "macro", "macros"},
                 {"mme", "macro operation", "macro operations"},
                 {"svc", "SVC", "SVCs"},
                 {"cpu", "SVC argument", "SVC arguments"}};
    double counts[sizeof kinds / sizeof *kinds] = {0}, commands = 0;
    const cJSON* g;
    Buf b = {0};
    size_t i, k;
    cJSON_ArrayForEach(g, cJSON_GetObjectItemCaseSensitive(summary, "missed")) {
        const char* kind = jstr(g, "kind");
        for (k = 0; kind && k < sizeof kinds / sizeof *kinds; ++k)
            if (!strcmp(kind, kinds[k].kind))
                counts[k] += !strcmp(kind, "shader") || !strcmp(kind, "compute shader") ? jnum(g, "count") : 1;
    }
    commands = cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(summary, "ipc"));
    for (k = 0; k < sizeof kinds / sizeof *kinds; ++k) {
        double n = counts[k];
        if (!n) continue;
        for (i = k + 1; i < sizeof kinds / sizeof *kinds; ++i) /* the kinds counted as one ("shader") together */
            if (!strcmp(kinds[i].many, kinds[k].many)) {
                n += counts[i];
                counts[i] = 0;
            }
        buf_printf(&b, "%s%.0f %s", b.len ? ", " : "", n, n == 1 ? kinds[k].one : kinds[k].many);
    }
    if (commands)
        buf_printf(&b, "%s%.0f %s", b.len ? ", " : "", commands,
                   commands == 1 ? "service command" : "service commands");
    return b.len ? buf_take(&b) : NULL;
}

int tr_game(const cJSON* summary, const char* note, char** key, char** title, char** tid, int* named) {
    const char *id = jstr(summary, "title_id"), *name = jstr(summary, "title"), *console = jstr(summary, "console");
    char* got = NULL;
    size_t n = 0;
    *key = *title = *tid = NULL;
    *named = 0;
    if (id && title_id(id, strlen(id)))
        *tid = upper(id, 16);
    else if (on_gba(console) && id && strlen(id) == 4 && strspn(id, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ") == 4)
        *tid = upper(id, 4); /* the GBA's game code */
    if (name) {              /* "Name", "0100...", or as older builds wrote it "Name (0100.../0100...)" */
        const char* open = strstr(name, " (");
        n = strlen(name);
        if (title_id(name, n)) {
            if (!*tid) *tid = upper(name, n);
            n = 0;
        } else if (open && n && name[n - 1] == ')' && title_id(open + 2, strspn(open + 2, "0123456789abcdefABCDEF"))) {
            if (!*tid) *tid = upper(open + 2, 16);
            n = (size_t)(open - name);
        }
        while (n && isspace((unsigned char)name[n - 1])) --n;
        while (n && isspace((unsigned char)*name)) ++name, --n;
        if (n && named_well(name, n)) got = rei_format("%.*s", (int)n, name);
    }
    if (got) {
        *title = got;
        *named = !mangled(got);
    } else if (*tid) /* the message's words are often what went wrong, not the game's name */
        *title = rei_strdup(*tid);
    else if (note && *note)
        *title = rei_strdup(note);
    else
        return -1;
    *key = *tid ? (on_gba(console) ? rei_format("gba-%s", *tid) : rei_strdup(*tid)) : name_key(*title, console);
    return 0;
}

/* ---- a report ---- */

static int from_discord(const char* url) {
    return !strncmp(url, "https://cdn.discordapp.com/", 27) || !strncmp(url, "https://media.discordapp.net/", 29);
}
/* the summary of n bytes of a log, valid UTF-8; owned, NULL out of memory */
static char* summary_of(const char* text, size_t n) {
    SnxLogScan* s = calloc(1, sizeof *s);
    char *json = NULL, *clean = NULL;
    if (!s) return NULL;
    if (!snx_logscan(s, text, n)) json = snx_logscan_json(s, snx_demangle);
    snx_logscan_free(s);
    free(s);
    if (json) clean = tr_utf8(json, strlen(json));
    free(json);
    return clean;
}
static int is_log(const cJSON* s) { return jstr(s, "title") || jstr(s, "title_id") || jstr(s, "build"); }
static char* dup_of(const cJSON* o, const char* key) { return jstr(o, key) ? rei_strdup(jstr(o, key)) : NULL; }
/* whether m's author deletes or adds to any report of guild: TRACER_ADMINS, or guild's own owner and administrators
   (the bot may be in other servers, whose administrators are not this one's) */
static int admin_of(const Tracer* t, u64 user, u64 here, const u64* roles, int nroles, u64 guild) {
    int i;
    for (i = 0; i < t->nadmins; ++i)
        if (t->admins[i] == user) return 1;
    return t->administers && here && here == guild && t->administers(t->admin_ctx, here, user, roles, nroles);
}
static int admin(const Tracer* t, const TrMessage* m, u64 guild) {
    return admin_of(t, m->author, m->guild, m->roles, m->nroles, guild);
}
static const char* tone_name(const char* outcome) {
    if (!outcome || !strcmp(outcome, "no failure logged")) return "ok";
    if (!strcmp(outcome, "unsupported instruction") || !strcmp(outcome, "thread stopped")) return "warn";
    return "bad";
}

static cJSON* help(void) {
    return embed(
        "Tracer",
        "Mention me with a ScarletNX log attached (.log, .txt, .gz or .zip), and screenshots if you have "
        "them, attached or in the .zip. I keep them as a report on the site, grouped by game. The log says which "
        "game it is: when it does not, write the game's name in the message.\n\n"
        "A run of edenprobe is a report of its own, apart from the games: ScarletNX's log of the run (alone, or in "
        "the Share Log zip), Eden's log of it, or its report.jsonl (the console's, as it names no machine). The "
        "detail of its service, net and pad cases is left out, as it can name you.\n\n"
        "Home folders in a log are shown as ~, and screenshots lose their metadata. The site does not show "
        "who sent a report.\n\n"
        "To add logs, screenshots or a note to a report, reply to it (or to my answer) and mention me, or write "
        "`@Tracer add 12` with them. For a log someone sent without mentioning me, reply to their message and "
        "mention me: I make a report of it.\n\n"
        "`@Tracer delete 12`, or a reply with `@Tracer delete`, deletes a report (yours, or any for an "
        "administrator). Deleting your message deletes what it sent: the report, or what it added.",
        TONE_SAID);
}

/* report id gone: its files, its page, its row, and the pages that listed it */
static void forget(Tracer* t, long id) {
    StReport r;
    StFile* files;
    int nfiles;
    if (st_get(t->store, id, &r, &files, &nfiles) != 1) return;
    if (!st_remove(t->store, id)) {
        site_forget(t->root, id, files, nfiles);
        site_update(t->store, t->root, 0, r.key);
    }
    st_report_free(&r);
    st_files_free(files, nfiles);
}

static cJSON* deleted(Tracer* t, const TrMessage* m, long id) {
    StReport r;
    char* text;
    cJSON* e;
    int found = st_get(t->store, id, &r, NULL, NULL);
    if (found != 1) {
        text = rei_format("There is no report #%ld.", id);
        e = said(text);
        free(text);
        return e;
    }
    if (r.poster != m->author && !admin(t, m, r.guild))
        text =
            rei_format("Report #%ld is not yours: only the person who sent it, or an administrator, deletes it.", id);
    else {
        forget(t, id);
        text = rei_format("Report #%ld is deleted, with its log and screenshots.", id);
    }
    st_report_free(&r);
    e = said(text);
    free(text);
    return e;
}

static void skip(Taken* k, const char* name, const char* why) {
    char* n = plain(name, 100);
    buf_printf(&k->skipped, "%s: %s\n", n, why);
    free(n);
}
void tr_taken_free(Taken* k) {
    int i;
    for (i = 0; i < k->n; ++i) {
        free(k->files[i].name);
        free(k->files[i].path);
        free(k->files[i].summary);
        if (k->taken[i]) unlink(k->taken[i]);
        free(k->taken[i]);
    }
    if (k->dir) rmdir(k->dir);
    free(k->dir);
    cJSON_Delete(k->first);
    buf_free(&k->skipped);
    probe_free(&k->probe);
    free(k->probe_why);
    memset(k, 0, sizeof *k);
}
/* n bytes of data written into the folder dirfd (k->dir) as the next file's, by its name alone, as a worker shut in
   its folder opens nothing else: 0, or -1 said in the log */
static int stash(Taken* k, int dirfd, const char* data, size_t n) {
    char name[16];
    int fd, ok;
    size_t done = 0;
    snprintf(name, sizeof name, "%d", k->n);
    fd = openat(dirfd, name, O_WRONLY | O_CREAT | O_EXCL, 0644);
    ok = fd >= 0;
    while (ok && done < n) {
        ssize_t w = write(fd, data + done, n - done);
        if (w < 0 && errno == EINTR) continue;
        ok = w > 0;
        if (ok) done += (size_t)w;
    }
    if (fd >= 0) ok = !close(fd) && ok;
    if (!ok) {
        rei_log("%s/%s could not be written: %s", k->dir, name, strerror(errno));
        unlinkat(dirfd, name, 0);
        return -1;
    }
    k->taken[k->n] = rei_format("%s/%s", k->dir, name);
    return 0;
}

/* what a file of kind may weigh at most, and what it is called in a reply */
static size_t max_of(TrKind kind) {
    return kind == TR_LOG ? TR_LOG_MAX : kind == TR_VIDEO ? TR_VIDEO_MAX : TR_IMAGE_MAX;
}
static const char* kind_word(TrKind kind) { return kind == TR_LOG ? "log" : kind == TR_VIDEO ? "video" : "screenshot"; }
/* why a file of kind that weighs bytes is not kept: "it is 30.2 MiB, over the 25 MiB a screenshot may be"; owned */
static char* too_big(TrKind kind, double bytes) {
    return rei_format("it is %.1f MiB, over the %zu MiB a %s may be", bytes / (1 << 20), max_of(kind) >> 20,
                      kind_word(kind));
}

/* a screenshot, named name in the report; zip names the .zip it came in, for the reply, NULL for one attached */
static void take_image(Taken* k, int dirfd, const char* name, const char* zip, Buf* got) {
    const char* ext = tr_image_ext(got->text, got->len);
    char* said = zip ? rei_format("%s in %s", name, zip) : rei_strdup(name);
    size_t n;
    StFile* f;
    if (ext && !strcmp(ext, "ppm")) { /* a frame ScarletNX saved, as a PNG a browser shows */
        Buf png = {0};
        if (tr_ppm_png(got->text, got->len, &png)) {
            skip(k, said, "a PPM that isn't whole, or of more than 255 a channel");
            free(said);
            return;
        }
        buf_free(got);
        *got = png;
        ext = "png";
    }
    n = ext ? tr_strip_image(got->text, got->len) : 0;
    if (!n)
        skip(k, said, "not a PNG, JPEG, WebP, GIF or PPM image");
    else if (stash(k, dirfd, got->text, n))
        skip(k, said, "it could not be written down");
    else {
        f = &k->files[k->n];
        f->n = k->base + k->n + 1;
        strcpy(f->kind, "image");
        f->name = rei_strdup(name);
        f->path = rei_format("%d.%s", f->n, ext);
        f->bytes = (double)n;
        ++k->n;
        ++k->images;
    }
    free(said);
}

/* why the report has no room left for one more of kind, given what it had and what this message kept; NULL for room */
static const char* full(const Taken* k, TrKind kind) {
    if (kind == TR_LOG && k->logs_had + k->logs >= TR_LOGS) return "a report keeps 3 logs";
    if (kind == TR_VIDEO && k->videos_had + k->videos >= TR_VIDEOS) return "a report keeps 2 videos";
    if (kind == TR_IMAGE && k->images_had + k->images >= TR_IMAGES) return "a report keeps 8 screenshots";
    return NULL;
}

/* the screenshots in the .zip a log came in, each taken as one attached is, under its entry's file name; those past
   the report's room named in one line with that room, as a zip may hold many. A zip that does not open is said when
   unsaid, as its log was not read */
static void take_zip_images(Taken* k, int dirfd, const char* zip, const Buf* got, int unsaid) {
    TrZipImages z;
    Buf over = {0};
    int more = 0;
    if (tr_zip_images(&z, got->text, got->len)) {
        if (unsaid) skip(k, zip, "it could not be unpacked");
        return;
    }
    while (tr_zip_next(&z)) {
        Buf image = {0};
        char *said = rei_format("%s in %s", z.name, zip), *why;
        if (full(k, TR_IMAGE)) {
            if (more++ < 3) buf_printf(&over, "%s%s", over.len ? ", " : "", z.name);
        } else if (z.size > (double)TR_IMAGE_MAX) {
            why = too_big(TR_IMAGE, z.size);
            skip(k, said, why);
            free(why);
        } else
            switch (tr_zip_read(&z, TR_IMAGE_MAX, &image)) {
                case 0: take_image(k, dirfd, z.name, zip, &image); break;
                case 1:
                    why = rei_format("it unpacks to over the %zu MiB a screenshot may be", TR_IMAGE_MAX >> 20);
                    skip(k, said, why);
                    free(why);
                    break;
                default: skip(k, said, "it could not be unpacked"); break;
            }
        buf_free(&image);
        free(said);
    }
    if (more) {
        char *names = more > 3 ? rei_format("%s and %d more in %s", over.text, more - 3, zip)
                               : rei_format("%s in %s", over.text, zip),
             *why = rei_format("a report keeps %d screenshots", TR_IMAGES);
        skip(k, names, why);
        free(why);
        free(names);
    }
    if (z.broken) skip(k, zip, "it could not be unpacked to its end: no screenshot after that point is kept");
    if (z.unnamed) {
        char* why = rei_format("%d of its files have names this system cannot read: none of them is kept as a "
                               "screenshot",
                               z.unnamed);
        skip(k, zip, why);
        free(why);
    }
    buf_free(&over);
    tr_zip_done(&z);
}

static void take_log(Taken* k, int dirfd, const TrAttachment* a, Buf* got) {
    Buf text = {0};
    char *why = NULL, *tilded, *shown, *summary, *cut;
    size_t n;
    cJSON* parsed;
    StFile* f;
    Probe probe;
    int run, cases, notes, first = !k->logs && !k->logs_had && !k->base;
    if (tr_unpack(a->name, got->text, got->len, TR_LOG_MAX, &text, &why)) {
        skip(k, a->name, why);
        free(why);
        return;
    }
    if (!text.len) {
        skip(k, a->name, "it is empty");
        return;
    }
    if (tr_image_ext(text.text, text.len)) { /* a picture, whatever its name says: never read as a log */
        if (full(k, TR_IMAGE))
            skip(k, a->name, full(k, TR_IMAGE));
        else if (text.len > TR_IMAGE_MAX) {
            why = too_big(TR_IMAGE, (double)text.len);
            skip(k, a->name, why);
            free(why);
        } else
            take_image(k, dirfd, a->name, NULL, &text);
        buf_free(&text);
        return;
    }
    tilded = snx_logscan_tilde(text.text, text.len, &n);
    buf_free(&text);
    if (!tilded) {
        skip(k, a->name, "out of memory");
        return;
    }
    shown = tr_public_log(tilded, n, &n);
    free(tilded);
    cut = probe_cut(shown, n, &n, &cases, &notes); /* what edenprobe's lines would say of the player, out */
    free(shown);
    shown = cut;
    summary = summary_of(shown, n);
    parsed = summary ? cJSON_Parse(summary) : NULL;
    run = probe_parse(shown, n, &probe, &why);
    if (!is_log(parsed) && run != 1) {
        skip(k, a->name, run < 0 ? why : "not a ScarletNX log, nor a run of edenprobe");
        free(why);
        cJSON_Delete(parsed);
        free(summary);
        free(shown);
        return;
    }
    if (stash(k, dirfd, shown, n)) {
        skip(k, a->name, "it could not be written down");
        if (run == 1) probe_free(&probe);
        free(why);
        cJSON_Delete(parsed);
        free(summary);
        free(shown);
        return;
    }
    free(shown);
    if (first && run == 1) { /* a new report's first log: the report is the run's */
        probe.cut_cases = cases;
        probe.cut_notes = notes;
        k->probe = probe;
        k->probed = 1;
    } else {
        if (first && run < 0) k->probe_why = rei_format("%s: %s", a->name, why);
        if (run == 1) probe_free(&probe);
    }
    free(why);
    if (!k->first)
        k->first = parsed;
    else
        cJSON_Delete(parsed);
    f = &k->files[k->n];
    f->n = k->base + k->n + 1;
    strcpy(f->kind, "log");
    f->name = rei_strdup(a->name);
    f->path = rei_format("%d.log", f->n);
    f->bytes = (double)n;
    f->summary = summary;
    ++k->n;
    ++k->logs;
}

/* an MP4 or a WebM, an MP4's place and camera blanked */
static void take_video(Taken* k, int dirfd, const TrAttachment* a, Buf* got) {
    const char* ext = tr_video_ext(got->text, got->len);
    size_t n = ext ? tr_strip_video(got->text, got->len) : 0;
    StFile* f;
    if (!n) {
        skip(k, a->name, "not an MP4 or WebM video");
        return;
    }
    if (stash(k, dirfd, got->text, n)) {
        skip(k, a->name, "it could not be written down");
        return;
    }
    f = &k->files[k->n];
    f->n = k->base + k->n + 1;
    strcpy(f->kind, "video");
    f->name = rei_strdup(a->name);
    f->path = rei_format("%d.%s", f->n, ext);
    f->bytes = (double)n;
    ++k->n;
    ++k->videos;
}

/* what a message added, as the reply says it: "1 log, 2 screenshots, 1 video", the videos only when there are */
static char* kept(const Taken* k, const char* more) {
    return rei_format("%d %s, %d %s%s%s", k->logs, k->logs == 1 ? "log" : "logs", k->images,
                      k->images == 1 ? "screenshot" : "screenshots",
                      k->videos ? (k->videos == 1 ? ", 1 video" : ", 2 videos") : "", more);
}

/* attachment a's kind and the bytes it may have at most */
static TrKind kind_of(const TrAttachment* a, size_t* max) {
    TrKind kind = tr_kind(a->name, a->type);
    *max = max_of(kind);
    return kind;
}
static int zip_named(const char* name) {
    size_t n = strlen(name);
    return n > 4 && !strcasecmp(name + n - 4, ".zip");
}
/* why attachment a has no room in the report, NULL for room: a .zip has room while its log or its screenshots do */
static const char* no_room(const Taken* k, const TrAttachment* a, TrKind kind) {
    if (kind == TR_LOG && zip_named(a->name) && !full(k, TR_IMAGE)) return NULL;
    return full(k, kind);
}

void tr_download(Tracer* t, Taken* k, const TrMessage* m, int dirfd, char* why[TR_FILES]) {
    int i;
    for (i = 0; i < m->nfiles; ++i) {
        const TrAttachment* a = &m->files[i];
        size_t max;
        TrKind kind = kind_of(a, &max);
        Buf got = {0};
        char *failed = NULL, name[16];
        int fd, ok;
        size_t done = 0;
        why[i] = NULL;
        if (kind == TR_OTHER)
            why[i] = rei_strdup("not a log or a screenshot");
        else if (no_room(k, a, kind)) /* full before this message: nothing of that kind is downloaded */
            why[i] = rei_strdup(no_room(k, a, kind));
        else if (!from_discord(a->url))
            why[i] = rei_strdup("not a file on Discord");
        else if (a->size > (double)max)
            why[i] = too_big(kind, a->size);
        else if (t->get(t->ctx, a->url, max, &got, &failed)) {
            rei_log("a download failed: %s", failed ? failed : "no reason given");
            why[i] = rei_strdup("the download failed");
        }
        free(failed);
        if (why[i]) continue;
        snprintf(name, sizeof name, "in.%d", i);
        fd = openat(dirfd, name, O_WRONLY | O_CREAT | O_EXCL, 0600);
        ok = fd >= 0;
        while (ok && done < got.len) {
            ssize_t w = write(fd, got.text + done, got.len - done);
            if (w < 0 && errno == EINTR) continue;
            ok = w > 0;
            if (ok) done += (size_t)w;
        }
        if (fd >= 0) ok = !close(fd) && ok;
        if (!ok) {
            rei_log("%s/%s could not be written: %s", k->dir, name, strerror(errno));
            unlinkat(dirfd, name, 0);
            why[i] = rei_strdup("it could not be written down");
        }
        buf_free(&got);
    }
}

void tr_read(Taken* k, const TrMessage* m, int dirfd, char* why[TR_FILES]) {
    int i;
    for (i = 0; i < m->nfiles; ++i) {
        const TrAttachment* a = &m->files[i];
        size_t max;
        TrKind kind = kind_of(a, &max);
        const char* no = why[i] ? why[i] : no_room(k, a, kind);
        char name[16], piece[65536];
        Buf got = {0};
        ssize_t r = 0;
        int fd, downloaded = !why[i];
        snprintf(name, sizeof name, "in.%d", i);
        fd = no ? -1 : openat(dirfd, name, O_RDONLY);
        while (fd >= 0 && got.len <= max && (r = read(fd, piece, sizeof piece)) != 0) {
            if (r < 0 && errno == EINTR) continue;
            if (r < 0) break;
            buf_addn(&got, piece, (size_t)r);
        }
        if (fd >= 0) close(fd);
        if (downloaded) unlinkat(dirfd, name, 0);
        if (!no && (fd < 0 || r < 0 || got.len > max)) no = "it could not be read back";
        if (no)
            skip(k, a->name, no);
        else if (kind == TR_LOG) {
            int logged = !full(k, TR_LOG); /* else a .zip, for its screenshots alone */
            if (logged)
                take_log(k, dirfd, a, &got);
            else
                skip(k, a->name, full(k, TR_LOG));
            if (zip_named(a->name)) take_zip_images(k, dirfd, a->name, &got, !logged);
        } else if (kind == TR_VIDEO)
            take_video(k, dirfd, a, &got);
        else
            take_image(k, dirfd, a->name, NULL, &got);
        buf_free(&got);
        free(why[i]);
        why[i] = NULL;
    }
}

void tr_take(Tracer* t, Taken* k, const TrMessage* m) {
    char* why[TR_FILES];
    int dirfd = open(k->dir, O_RDONLY | O_DIRECTORY), i;
    if (dirfd < 0) {
        rei_log("%s could not be opened: %s", k->dir, strerror(errno));
        for (i = 0; i < m->nfiles; ++i) skip(k, m->files[i].name, "it could not be written down");
        return;
    }
    tr_download(t, k, m, dirfd, why);
    tr_read(k, m, dirfd, why);
    close(dirfd);
}

/* m's files taken into k, its folder made in t->work: 0, or -1 said in the log (k freed by the caller). A message
   without files gets no folder and no worker, which anyone's mention would cost otherwise */
static int fetched(Tracer* t, const TrMessage* m, Taken* k) {
    char* why = NULL;
    if (!m->nfiles) return 0;
    k->dir = rei_format("%s/take.XXXXXX", t->work);
    if (!mkdtemp(k->dir)) {
        rei_log("a folder for message %llu's files could not be made in %s: %s", (unsigned long long)m->id, t->work,
                strerror(errno));
        free(k->dir);
        k->dir = NULL;
        return -1;
    }
    if (!t->take) {
        tr_take(t, k, m);
        return 0;
    }
    if (t->take(t->take_ctx, t, m, k, &why)) {
        rei_log("message %llu's files were not read: %s", (unsigned long long)m->id, why ? why : "no reason given");
        free(why);
        return -1;
    }
    return 0;
}
static cJSON* unread(Taken* k) {
    tr_taken_free(k);
    return said("Your files could not be read, so nothing was kept: try again later, please.");
}

static cJSON* made(Tracer* t, const StReport* r, const Taken* k) {
    char *title = on_switch(r->console) ? rei_format("Report #%ld: %s", r->id, r->title)
                                        : rei_format("Report #%ld: %s (%s)", r->id, r->title, r->console),
         *cut = plain(title, 250), *sig = plain(r->signature, 900);
    char *url = rei_format("%s/r/%ld/", t->url, r->id), *text, *files;
    const char* tone = tone_name(r->outcome);
    cJSON* e;
    text = r->signature ? rei_format("**%s**\n`%s`", r->outcome ? r->outcome : "no failure logged", sig)
                        : rei_format("**%s**", r->outcome ? r->outcome : "no failure logged");
    if (r->missed) { /* what the runtime missed, after how the run ended */
        char* with = rei_format("%s\nMissed: %s", text, r->missed);
        free(text);
        text = with;
        if (!strcmp(tone, "ok")) tone = "warn";
    }
    if (r->age) { /* an old build, or one with changes: said before anyone digs in */
        char *age = plain(r->age, 400), *with = rei_format("%s\n%s", text, age);
        free(text);
        free(age);
        text = with;
    }
    e = embed(cut, text, !strcmp(tone, "ok") ? TONE_OK : !strcmp(tone, "warn") ? TONE_WARN : TONE_BAD);
    jset(e, "url", url);
    if (r->branch || r->commit) {
        char* build = rei_format("%s %s", r->branch ? r->branch : "", r->commit ? r->commit : "");
        char* clean = plain(build, 200);
        add_field(e, "Build", clean);
        free(clean);
        free(build);
    }
    files = kept(k, "");
    add_field(e, "Kept", files);
    if (k->skipped.len) {
        char* skipped = plain(k->skipped.text, 1000);
        add_field(e, "Not kept", skipped);
        free(skipped);
    }
    if (k->probe_why) { /* lines of edenprobe that are no whole run: kept as a log, not on the Probe pages */
        char* why = plain(k->probe_why, 1000);
        add_field(e, "Not a run of edenprobe", why);
        free(why);
    }
    {
        cJSON* footer = cJSON_AddObjectToObject(e, "footer");
        char* how = rei_format("@Tracer delete %ld deletes it", r->id);
        jset(footer, "text", how);
        free(how);
    }
    free(files);
    free(text);
    free(url);
    free(sig);
    free(cut);
    free(title);
    return e;
}

/* the name of a game its logs did not name: one another report's log gave it, else the list's (titles.tsv); owned, or
   NULL */
static char* known_name(Tracer* t, const char* key, const char* title_id, const char* console) {
    char* known = on_gba(console) ? NULL : st_title_of_key(t->store, key);
    if (mangled(known)) { /* a report kept before the launcher's '?' were told from a name */
        free(known);
        known = NULL;
    }
    const char* listed = known ? NULL : tr_titles_name(t->titles, console, title_id);
    return known ? known : listed ? rei_strdup(listed) : NULL;
}
/* a game's name to give a report when its logs name none, or (a GBA cartridge's title being 12 letters at most) the
   list's for a GBA game; *named becomes 1 for the list's name of a GBA game. Owned, or NULL to keep the title */
static char* better_name(Tracer* t, const char* key, const char* title_id, const char* console, int* named) {
    char* known;
    if (*named && !on_gba(console)) return NULL;
    known = known_name(t, key, title_id, console);
    if (known && on_gba(console)) *named = 1;
    return known;
}

/* the answer when m's author asked for TR_HOURLY reports and updates in the last hour, or NULL with this one counted:
   each ask whose files are downloaded counts, kept, refused or deleted since */
static cJSON* limited(Tracer* t, const TrMessage* m) {
    char* text;
    cJSON* e;
    switch (st_ask(t->store, m->author, rei_now(), rei_now() - 3600, TR_HOURLY)) {
        case 0: return NULL;
        case 1:
            text =
                rei_format("You sent %d reports and updates in the last hour: send this one later, please.", TR_HOURLY);
            break;
        default: text = rei_strdup("The report could not be kept: try again later, please."); break;
    }
    e = said(text);
    free(text);
    return e;
}

/* the files of k moved into report id's folder: 0, or -1 with those moved deleted */
static int written(Tracer* t, long id, Taken* k) {
    int i, failed = 0;
    for (i = 0; i < k->n && !failed; ++i) {
        char* rel = rei_format("r/%ld/%s", id, k->files[i].path);
        failed = site_move(t->root, rel, k->taken[i]) != 0;
        if (!failed) {
            free(k->taken[i]);
            k->taken[i] = NULL;
        }
        free(rel);
    }
    for (i = 0; failed && i < k->n; ++i) {
        char* path = rei_format("%s/r/%ld/%s", t->root, id, k->files[i].path);
        if (!k->taken[i]) remove(path);
        free(path);
    }
    return failed ? -1 : 0;
}

/* the answer for report r, the run of edenprobe k's first log holds: "edenprobe 0.9.0 on radv, Mesa 26.1.8 (Linux):
   1034 passed, 26 failed (astc 15, ...), 6 skipped, 3 crashed", and what the run lacks or lost said under it */
static cJSON* probe_made(Tracer* t, const StReport* r, const Taken* k) {
    const Probe* p = &k->probe;
    char *title = rei_format("Report #%ld: edenprobe %s on %s", r->id, p->version, p->emulator),
         *cut = plain(title, 250), *url = rei_format("%s/r/%ld/", t->url, r->id), *machine = probe_machine(p),
         *counts = probe_counts(p), *files;
    long lines = 0, failed = 0, crashed = 0;
    int i;
    Buf text = {0};
    cJSON* e;
    for (i = 0; i < p->ncases; ++i) {
        failed += !strcmp(p->cases[i].result, "fail");
        crashed += !strcmp(p->cases[i].reason, "crashed");
    }
    lines = p->ncases + p->over;
    buf_printf(&text, "edenprobe %s on %s: %s", p->version, machine, counts);
    if (!p->whole)
        buf_printf(&text, "\nNo summary line: the run stopped before its end, after %ld case lines.", lines);
    else if (p->passed + p->failed + p->skipped + p->crashed != lines)
        buf_printf(&text,
                   "\nThe summary counts %ld cases and the log has %ld case lines: a launch that went on after a "
                   "crash leaves only its own; the run's report.jsonl has them all.",
                   p->passed + p->failed + p->skipped + p->crashed, lines);
    if (p->over) buf_printf(&text, "\n%d case lines past the first %d were not kept.", p->over, PROBE_CASES);
    if (p->unread)
        buf_printf(&text, "\n%d of its lines were none that edenprobe writes, and were not read.", p->unread);
    if (!strcmp(p->emulator, "console") && !p->driver_name)
        buf_add(&text, "\nA report.jsonl names no machine: it is kept as the console's reference. If an emulator ran "
                       "it, send that emulator's log instead.");
    if (p->inferred)
        buf_printf(&text, "\n%s logs no driver ID: %s.", p->emulator,
                   p->driver_id ? "it is read from the driver's name" : "its driver's name says none");
    {
        char* said_text = plain(text.text, 3900);
        e = embed(cut, said_text, failed || p->failed > 0 ? TONE_BAD : crashed || p->crashed > 0 ? TONE_WARN : TONE_OK);
        free(said_text);
    }
    jset(e, "url", url);
    if (p->emulator_version) {
        char* build = plain(p->emulator_version, 200);
        add_field(e, "Build", build);
        free(build);
    }
    files = kept(k, "");
    add_field(e, "Kept", files);
    if (p->cut_cases || p->cut_notes) {
        char* left = rei_format("the detail of %d %s of the service, net and pad families (or of no family edenprobe "
                                "names) and %d net %s, which can name the player",
                                p->cut_cases, p->cut_cases == 1 ? "case" : "cases", p->cut_notes,
                                p->cut_notes == 1 ? "note" : "notes");
        add_field(e, "Left out", left);
        free(left);
    }
    if (k->skipped.len) {
        char* skipped = plain(k->skipped.text, 1000);
        add_field(e, "Not kept", skipped);
        free(skipped);
    }
    {
        cJSON* footer = cJSON_AddObjectToObject(e, "footer");
        char* how = rei_format("@Tracer delete %ld deletes it", r->id);
        jset(footer, "text", how);
        free(how);
    }
    buf_free(&text);
    free(files);
    free(counts);
    free(machine);
    free(url);
    free(cut);
    free(title);
    return e;
}

/* the report of k's files, whose first log holds a run of edenprobe, sent as m: kept under PROBE_KEY with the run, or
   the link of the report m's author sent of the same run before */
static cJSON* probed(Tracer* t, const TrMessage* m, Taken* k, const char* note, long* report) {
    const Probe* p = &k->probe;
    char digest[17], *text;
    long seen;
    StReport r;
    cJSON* e;
    int scarletnx = !strcmp(p->emulator, "ScarletNX");
    probe_digest(p, digest);
    if ((seen = st_probe_seen(t->store, m->author, digest))) {
        char* url = rei_format("%s/r/%ld/", t->url, seen);
        text = rei_format("This run of edenprobe is report #%ld already: its lines are the same, so nothing was kept "
                          "again.",
                          seen);
        e = said(text);
        jset(e, "url", url);
        free(url);
        free(text);
        tr_taken_free(k);
        return e;
    }
    memset(&r, 0, sizeof r);
    r.key = rei_strdup(PROBE_KEY);
    r.title = rei_format("edenprobe %s on %s", p->version, p->emulator);
    r.named = 1;
    r.when = rei_now();
    r.guild = m->guild;
    r.channel = m->channel;
    r.message = m->id;
    r.poster = m->author;
    r.note = *note ? rei_strdup(note) : NULL;
    r.version = rei_strdup(p->version);
    if (scarletnx) { /* the log summary's facts of ScarletNX's own log, as any report's */
        r.build = dup_of(k->first, "build");
        r.commit = dup_of(k->first, "commit");
        r.branch = dup_of(k->first, "branch");
        r.outcome = dup_of(k->first, "outcome");
        r.signature = dup_of(k->first, "signature");
        r.missed = tr_missed(k->first);
        r.age = tr_age_line(t->age, t->store, r.commit, r.branch);
    } else if (p->emulator_version)
        r.build = rei_strdup(p->emulator_version);
    r.system = p->system ? rei_strdup(p->system) : NULL;
    r.gpu = p->gpu ? rei_strdup(p->gpu) : NULL;
    r.driver = p->driver_text ? rei_strdup(p->driver_text) : p->driver_info ? rei_strdup(p->driver_info) : NULL;
    if (st_add(t->store, &r, k->files, k->n) || st_probe_set(t->store, r.id, p, digest)) {
        if (r.id) st_remove(t->store, r.id);
        st_report_free(&r);
        tr_taken_free(k);
        return said("The report could not be kept: try again later, please.");
    }
    if (written(t, r.id, k)) {
        site_forget(t->root, r.id, k->files, k->n);
        st_remove(t->store, r.id);
        st_report_free(&r);
        tr_taken_free(k);
        return said("The report could not be kept: try again later, please.");
    }
    site_update(t->store, t->root, r.id, r.key);
    rei_log("report #%ld: a run of edenprobe %s on %s, %d cases", r.id, p->version, p->emulator, p->ncases);
    e = probe_made(t, &r, k);
    *report = r.id;
    st_report_free(&r);
    tr_taken_free(k);
    return e;
}

/* the report of m's files, which asker asked for: m itself, or the message asker replied to (its author the one who
   sent the report). The hourly limit counts asker */
static cJSON* reported(Tracer* t, const TrMessage* asker, const TrMessage* m, const char* note, long* report) {
    Taken k;
    StReport r;
    cJSON* e;
    char* text;
    if (st_by_message(t->store, m->id)) return NULL; /* the gateway's event again, after a resume */
    if (m->nfiles && (e = limited(t, asker))) return e;
    memset(&k, 0, sizeof k);
    if (fetched(t, m, &k)) return unread(&k);
    if (!k.logs) {
        text = k.skipped.len ? rei_format("%s, so no report:\n%s",
                                          asker == m ? "No ScarletNX log here"
                                                     : "The message you replied to has no ScarletNX log",
                                          k.skipped.text)
                             : rei_strdup("Attach the game's ScarletNX log (.log, .txt, .gz or .zip), and screenshots "
                                          "if you have them. `@Tracer help` says more.");
        e = said(text);
        free(text);
        tr_taken_free(&k);
        return e;
    }
    if (k.probed) return probed(t, m, &k, note, report);
    memset(&r, 0, sizeof r);
    if (tr_game(k.first, note, &r.key, &r.title, &r.title_id, &r.named)) {
        tr_taken_free(&k);
        return said("The log does not say which game it is: mention me again with the log and the game's name.");
    }
    if (!r.title_id && r.named) { /* a log without its title ID, of a game other reports know by its ID */
        char* known = st_key_of_title(t->store, r.title);
        if (known) {
            free(r.key);
            r.key = known;
        }
    }
    { /* a log without the game's name: another report's log's, else the list's; a GBA game's, the list's */
        char* known = better_name(t, r.key, r.title_id, jstr(k.first, "console"), &r.named);
        if (known) {
            free(r.title);
            r.title = known;
        }
    }
    r.when = rei_now();
    r.console = dup_of(k.first, "console");
    r.guild = m->guild;
    r.channel = m->channel;
    r.message = m->id;
    r.poster = m->author;
    r.note = *note ? rei_strdup(note) : NULL;
    r.version = dup_of(k.first, "game_version");
    r.build = dup_of(k.first, "build");
    r.commit = dup_of(k.first, "commit");
    r.branch = dup_of(k.first, "branch");
    r.system = dup_of(k.first, "system");
    r.gpu = dup_of(k.first, "gpu");
    r.driver = dup_of(k.first, "driver");
    r.outcome = dup_of(k.first, "outcome");
    r.signature = dup_of(k.first, "signature");
    r.missed = tr_missed(k.first);
    r.age = tr_age_line(t->age, t->store, r.commit, r.branch);
    if (st_add(t->store, &r, k.files, k.n)) {
        st_report_free(&r);
        tr_taken_free(&k);
        return said("The report could not be kept: try again later, please.");
    }
    if (written(t, r.id, &k)) {
        site_forget(t->root, r.id, k.files, k.n);
        st_remove(t->store, r.id);
        st_report_free(&r);
        tr_taken_free(&k);
        return said("The report could not be kept: try again later, please.");
    }
    site_update(t->store, t->root, r.id, r.key);
    rei_log("report #%ld: %d logs, %d screenshots, %s", r.id, k.logs, k.images, r.key);
    e = made(t, &r, &k);
    *report = r.id;
    st_report_free(&r);
    tr_taken_free(&k);
    return e;
}

/* r named again when it had no name and a log of k has one, of the same title ID: 1 when it was, *old (owned) the
   game's key before */
static int renamed(Tracer* t, StReport* r, const Taken* k, char** old) {
    char *key, *title, *tid;
    int named;
    *old = NULL;
    if (r->named || !k->first || tr_game(k->first, "", &key, &title, &tid, &named)) return 0;
    if (named && (!r->title_id || !tid || !strcmp(tid, r->title_id))) {
        const char* id = r->title_id ? r->title_id : tid;
        char* now = id ? rei_strdup(id) : st_key_of_title(t->store, title);
        if (!now) now = name_key(title, jstr(k->first, "console"));
        if (!st_set_game(t->store, r->id, now, title, id, 1)) {
            *old = r->key;
            r->key = now;
            free(r->title);
            r->title = title;
            title = NULL;
            if (!r->title_id && id) r->title_id = rei_strdup(id);
            r->named = 1;
        } else
            free(now);
    }
    free(key);
    free(title);
    free(tid);
    return *old != NULL;
}

static cJSON* updated(Tracer* t, const TrMessage* m, long id, const char* note, long* report) {
    StReport r;
    Taken k;
    char *text, *added, *old = NULL, *url;
    cJSON* e;
    int logs, images, videos;
    if (st_report_of(t->store, m->id)) return NULL; /* the gateway's event again, after a resume */
    if (st_get(t->store, id, &r, NULL, NULL) != 1) {
        text = rei_format("There is no report #%ld.", id);
        e = said(text);
        free(text);
        return e;
    }
    if (r.poster != m->author && !admin(t, m, r.guild)) {
        text = rei_format("Report #%ld is not yours: only the person who sent it, or an administrator, adds to it. "
                          "Send a report of your own: it goes with the same game.",
                          id);
        st_report_free(&r);
        e = said(text);
        free(text);
        return e;
    }
    if (!m->nfiles && !*note) {
        st_report_free(&r);
        return said("Nothing to add: attach logs or screenshots, or write a note.");
    }
    if ((e = limited(t, m))) {
        st_report_free(&r);
        return e;
    }
    memset(&k, 0, sizeof k);
    k.base = st_counts(t->store, id, &logs, &images, &videos);
    k.logs_had = logs;
    k.images_had = images;
    k.videos_had = videos;
    if (fetched(t, m, &k)) {
        st_report_free(&r);
        return unread(&k);
    }
    if (!k.n && !*note) {
        text = rei_format("Nothing was added to report #%ld:\n%s", id, k.skipped.text ? k.skipped.text : "");
        e = said(text);
        free(text);
        tr_taken_free(&k);
        st_report_free(&r);
        return e;
    }
    if (written(t, id, &k) || st_update(t->store, id, m->id, rei_now(), k.files, k.n, note)) {
        StFile* back;
        int nback;
        st_take_back(t->store, m->id, &back, &nback);
        st_files_free(back, nback);
        site_forget_files(t->root, id, k.files, k.n);
        tr_taken_free(&k);
        st_report_free(&r);
        return said("The update could not be kept: try again later, please.");
    }
    if (renamed(t, &r, &k, &old)) site_update(t->store, t->root, 0, old);
    site_update(t->store, t->root, id, r.key);
    rei_log("report #%ld: %d logs, %d screenshots%s added", id, k.logs, k.images, *note ? ", a note" : "");
    text = rei_format("Report #%ld: %s", id, r.title);
    added = kept(&k, *note ? ", a note" : "");
    url = rei_format("%s/r/%ld/", t->url, id);
    {
        char* cut = plain(text, 250);
        e = embed(cut, old ? "Updated, and named by its new log." : "Updated.", TONE_SAID);
        free(cut);
    }
    jset(e, "url", url);
    add_field(e, "Added", added);
    if (k.skipped.len) {
        char* skipped = plain(k.skipped.text, 1000);
        add_field(e, "Not kept", skipped);
        free(skipped);
    }
    *report = id;
    free(url);
    free(added);
    free(text);
    free(old);
    tr_taken_free(&k);
    st_report_free(&r);
    return e;
}

/* the answer to a reply that brings nothing to report id, a message of which it replies to: the report's link, as
   that message made it or added to it already. NULL for m again (it made the report, or was answered with it) */
static cJSON* known(Tracer* t, const TrMessage* m, long id) {
    StReport r;
    char *text, *title, *cut, *url;
    cJSON* e;
    if (st_report_of(t->store, m->id)) return NULL; /* the gateway's event again, after a resume */
    if (st_get(t->store, id, &r, NULL, NULL) != 1) {
        text = rei_format("There is no report #%ld.", id);
        e = said(text);
        free(text);
        return e;
    }
    title = rei_format("Report #%ld: %s", id, r.title);
    cut = plain(title, 250);
    text = rei_format("The message you replied to is report #%ld's already. To add to it, reply with logs, screenshots "
                      "or words.",
                      id);
    url = rei_format("%s/r/%ld/", t->url, id);
    e = embed(cut, text, TONE_SAID);
    jset(e, "url", url);
    free(url);
    free(text);
    free(cut);
    free(title);
    st_report_free(&r);
    return e;
}

/* what Discord's answer to the bot's GET of the message m replies to says when it is no message: owned */
static char* not_given(int status, const char* why) {
    if (status == 404) return rei_strdup("The message you replied to is gone (Discord answered 404), so no report.");
    if (status == 403)
        return rei_format("Discord does not let me read the message you replied to (it answered 403: %.200s): I need "
                          "the Read Message History permission in this channel.",
                          why ? why : "no reason given");
    if (!status)
        return rei_format("Discord did not answer when I asked for the message you replied to (%.200s): try again "
                          "later, please.",
                          why ? why : "no reason given");
    return rei_format("Discord answered %d when I asked for the message you replied to (%.200s): try again later, "
                      "please.",
                      status, why ? why : "no reason given");
}

/* m, a reply that brings no file, made a report of the message it replies to, in the same channel */
static cJSON* from_reply(Tracer* t, const TrMessage* m, const char* note, long* report) {
    TrMessage of;
    char *json, *why = NULL, *text, *words;
    int status = 0;
    cJSON* e;
    if (st_report_of(t->store, m->id)) return NULL; /* the gateway's event again, after a resume */
    if (m->reply_guild && m->reply_guild != m->guild) {
        text =
            rei_format("The message you replied to is in another server (%llu): I make a report of a message in this "
                       "channel only.",
                       (unsigned long long)m->reply_guild);
        e = said(text);
        free(text);
        return e;
    }
    if (m->reply_channel && m->reply_channel != m->channel) {
        text = rei_format("The message you replied to is in <#%llu>, not this channel: I make a report of a message in "
                          "the channel I am asked in only. Reply to it there.",
                          (unsigned long long)m->reply_channel);
        e = said(text);
        free(text);
        return e;
    }
    json = t->message(t->discord_ctx, m->channel, m->reply_to, &status, &why);
    if (status != 200 || !json || !tr_message_of(json, strlen(json), &of)) {
        if (status == 200) { /* an answer that is no message */
            free(why);
            why = rei_format("its answer is no message: %.120s", json ? json : "nothing");
            status = -1;
        }
        text = status == -1 ? rei_format("Discord's answer for the message you replied to is no message (%s): try "
                                         "again later, please.",
                                         why)
                            : not_given(status, why);
        rei_log("message %llu, which %llu replied to, was not read: %d, %s", (unsigned long long)m->reply_to,
                (unsigned long long)m->id, status, why ? why : "no reason given");
        free(json);
        free(why);
        e = said(text);
        free(text);
        return e;
    }
    free(json);
    free(why);
    why = NULL;
    if (of.bot) {
        tr_message_free(&of);
        return said("The message you replied to is a bot's or a webhook's, not a report: reply to the message that has "
                    "the log.");
    }
    if (!of.nfiles) { /* none, or none that Discord shows the bot */
        int sees = t->content_intent ? t->content_intent(t->discord_ctx, &why) : -1;
        if (sees == 1)
            text =
                rei_strdup("The message you replied to has no file, so it has no ScarletNX log to make a report of.");
        else if (!sees)
            text = rei_strdup("I can't see the files of the message you replied to: Discord shows a bot the files of a "
                              "message that mentions it, and of other messages only with the Message Content intent, "
                              "which I don't have. Send the log again with a mention of me.");
        else
            text = rei_format("Discord shows me no file in the message you replied to, and did not say whether it "
                              "would show me one (%.200s): send the log again with a mention of me.",
                              why ? why : "no reason given");
        free(why);
        tr_message_free(&of);
        e = said(text);
        free(text);
        return e;
    }
    of.guild = m->guild;
    of.channel = m->channel;
    words = tr_words(of.content);
    e = reported(t, m, &of, *words ? words : note, report);
    if (*report && st_link(t->store, m->id, *report))
        rei_log("report #%ld: reply %llu not linked to it", *report, (unsigned long long)m->id);
    free(words);
    tr_message_free(&of);
    return e;
}

cJSON* tr_answer(Tracer* t, const TrMessage* m, long* report) {
    char* note;
    long number, target;
    cJSON* e = NULL;
    TrVerb verb = tr_command(m->content, &note, &number);
    *report = 0;
    target = number ? number : m->reply_to ? st_report_of(t->store, m->reply_to) : 0;
    if (!target && m->reply_report) { /* an answer of the bot's that was not kept: its embed's link names the report */
        StReport r;
        target = m->reply_report;
        if (st_get(t->store, target, &r, NULL, NULL) == 1) {
            st_link(t->store, m->reply_to, target);
            st_report_free(&r);
        }
    }
    switch (verb) {
        case TR_HELP: e = help(); break;
        case TR_DELETE:
            e = target ? deleted(t, m, target)
                       : said("Which report? `@Tracer delete 12`, or a reply to the report with `@Tracer delete`.");
            break;
        case TR_ADD: e = updated(t, m, target, note, report); break;
        case TR_REPORT: /* a reply to a report adds to it; a reply alone to another message makes a report of it */
            if (target)
                e = m->nfiles || *note ? updated(t, m, target, note, report) : known(t, m, target);
            else if (m->reply_to && !m->nfiles && t->message)
                e = from_reply(t, m, note, report);
            else
                e = reported(t, m, m, note, report);
            break;
    }
    free(note);
    return e;
}

/* report r filed again under the game its first log's summary s names, when that is another game than r's (a log
   summary that read the log better): its old game's page drawn again, gone when that game has no report left */
static void refile(Tracer* t, const StReport* r, const cJSON* s) {
    char *key, *title, *tid;
    int named;
    if (tr_game(s, r->note, &key, &title, &tid, &named)) return;
    { /* a log without the game's name: another report's log's, else the list's; a GBA game's, the list's */
        char* known = better_name(t, key, tid, jstr(s, "console"), &named);
        if (known) {
            free(title);
            title = known;
        }
    }
    if (strcmp(key, r->key)) {
        if (!st_set_game(t->store, r->id, key, title, tid, named)) {
            rei_log("report #%ld filed again under %s, not %s", r->id, key, r->key);
            site_update(t->store, t->root, 0, r->key);
        }
    } else if ((!r->named || on_gba(jstr(s, "console")) || mangled(r->title)) && strcmp(title, r->title) &&
               !st_set_game(t->store, r->id, key, title, tid, named))
        rei_log("report #%ld named %s, which was %s", r->id, title, r->title);
    free(key);
    free(title);
    free(tid);
}

int tr_rescan(Tracer* t, const char* version) {
    char* was = st_meta(t->store, "logscan");
    StReport* all;
    int i, n, done = 0;
    if (was && !strcmp(was, version)) {
        free(was);
        return 0;
    }
    free(was);
    if (st_reports(t->store, NULL, &all, &n)) return -1;
    for (i = 0; i < n; ++i) {
        StReport now, *r = &now;
        StFile* files;
        Probe run;
        int nfiles, k, first = 1, has_run = 0;
        if (st_get(t->store, all[i].id, r, &files, &nfiles) != 1) continue;
        for (k = 0; k < nfiles; ++k) {
            char *path, *text, *shown, *summary;
            size_t len = 0, shown_len;
            FILE* f;
            Buf b = {0};
            char piece[65536];
            if (strcmp(files[k].kind, "log")) continue;
            path = rei_format("%s/r/%ld/%s", t->root, r->id, files[k].path);
            f = fopen(path, "rb");
            free(path);
            if (!f) continue;
            while ((len = fread(piece, 1, sizeof piece, f))) buf_addn(&b, piece, len);
            fclose(f);
            len = b.len;
            text = buf_take(&b);
            shown = tr_public_log(text, len, &shown_len);
            { /* and without what edenprobe's lines would say of the player */
                int cases, notes;
                char* cut = probe_cut(shown, shown_len, &shown_len, &cases, &notes);
                free(shown);
                shown = cut;
            }
            if (shown_len != len || memcmp(shown, text, len)) { /* a log kept before the site left these out */
                char* rel = rei_format("r/%ld/%s", r->id, files[k].path);
                if (!site_write(t->root, rel, shown, shown_len))
                    st_set_bytes(t->store, r->id, files[k].n, (double)shown_len);
                free(rel);
            }
            free(text);
            summary = summary_of(shown, shown_len);
            if (first) { /* the first log's run of edenprobe makes the report the run's, read again */
                char *why = NULL, digest[17];
                if ((has_run = probe_parse(shown, shown_len, &run, &why) == 1)) {
                    probe_digest(&run, digest);
                    if (!st_probe_set(t->store, r->id, &run, digest) && strcmp(r->key, PROBE_KEY)) {
                        char* title = rei_format("edenprobe %s on %s", run.version, run.emulator);
                        if (!st_set_game(t->store, r->id, PROBE_KEY, title, NULL, 1)) {
                            rei_log("report #%ld is a run of edenprobe, not a report of %s", r->id, r->key);
                            site_update(t->store, t->root, 0, r->key);
                            free(r->key);
                            r->key = rei_strdup(PROBE_KEY);
                        }
                        free(title);
                    }
                }
                free(why);
            }
            free(shown);
            if (!summary) continue;
            st_set_summary(t->store, r->id, files[k].n, summary);
            if (first) { /* the report's facts are its first log's, and so is its game when the log names another */
                cJSON* s = cJSON_Parse(summary);
                StReport facts = *r;
                if (strcmp(r->key, PROBE_KEY)) refile(t, r, s);
                facts.version = (char*)jstr(s, "game_version");
                facts.build = (char*)jstr(s, "build");
                facts.commit = (char*)jstr(s, "commit");
                facts.branch = (char*)jstr(s, "branch");
                facts.system = (char*)jstr(s, "system");
                facts.gpu = (char*)jstr(s, "gpu");
                facts.driver = (char*)jstr(s, "driver");
                facts.outcome = (char*)jstr(s, "outcome");
                facts.signature = (char*)jstr(s, "signature");
                facts.missed = tr_missed(s);
                facts.console = (char*)jstr(s, "console");
                if (has_run) { /* a run's facts, as probed keeps them: Eden's and a report.jsonl's from the run */
                    int scarletnx = !strcmp(run.emulator, "ScarletNX");
                    facts.version = run.version;
                    facts.system = run.system;
                    facts.gpu = run.gpu;
                    facts.driver = run.driver_text ? run.driver_text : run.driver_info;
                    facts.console = NULL;
                    if (!scarletnx) {
                        facts.build = run.emulator_version;
                        facts.commit = facts.branch = facts.outcome = facts.signature = NULL;
                        free(facts.missed);
                        facts.missed = NULL;
                    }
                }
                st_set_facts(t->store, &facts);
                free(facts.missed);
                cJSON_Delete(s);
                first = 0;
            }
            free(summary);
        }
        if (has_run) probe_free(&run);
        st_files_free(files, nfiles);
        st_report_free(r);
        ++done;
    }
    st_reports_free(all, n);
    st_set_meta(t->store, "logscan", version);
    rei_log("%d reports summarized again, with the log summary %s", done, version);
    return done;
}

cJSON* tr_approved(Tracer* t, const TrReaction* r) {
    StReport rep;
    StFile* files;
    int nfiles, claimed;
    long id, number;
    char *title, *body, *url = NULL, *why = NULL, *text;
    cJSON* e;
    if (!t->issues || !t->issues->repo || !r->emoji || strcmp(r->emoji, t->issues->emoji)) return NULL;
    if (!(id = st_report_of(t->store, r->message)) || st_get(t->store, id, &rep, &files, &nfiles) != 1) return NULL;
    if (!admin_of(t, r->user, r->guild, r->roles, r->nroles, rep.guild)) {
        rei_log("report #%ld: %s from %llu, who may not approve it", id, r->emoji, (unsigned long long)r->user);
        st_report_free(&rep);
        st_files_free(files, nfiles);
        return NULL;
    }
    if ((claimed = st_claim_issue(t->store, id)) != 1) {
        if (!claimed)
            rei_log("report #%ld approved again: %s", id,
                    rep.issue > 0 ? "it is an issue already" : "its issue is being opened");
        st_report_free(&rep);
        st_files_free(files, nfiles);
        return claimed ? said("The approval could not be kept: try again later, please.") : NULL;
    }
    title = tr_issue_title(&rep);
    body = tr_issue_body(&rep, files, nfiles, t->url);
    if (tr_issue_open(t->issues, title, body, &number, &url, &why)) {
        st_set_issue(t->store, id, 0, NULL); /* none again: a later approval tries again */
        rei_log("report #%ld was approved and not filed: %s", id, why);
        text = rei_format("Report #%ld could not be filed as an issue: %s", id, why);
        e = said(text);
    } else {
        if (st_set_issue(t->store, id, number, url))
            rei_log("report #%ld is issue #%ld (%s), and the database did not keep it", id, number, url);
        site_update(t->store, t->root, id, rep.key);
        rei_log("report #%ld is issue #%ld", id, number);
        text = rei_format("Report #%ld is issue #%ld now.", id, number);
        e = said(text);
        jset(e, "url", url);
    }
    free(text);
    free(url);
    free(why);
    free(title);
    free(body);
    st_report_free(&rep);
    st_files_free(files, nfiles);
    return e;
}

void tr_replied(Tracer* t, long report, u64 reply) {
    if (report && reply) st_link(t->store, reply, report);
}

int tr_forget(Tracer* t, u64 message) {
    long id = st_by_message(t->store, message);
    StFile* files;
    int nfiles;
    if (id) {
        forget(t, id);
        rei_log("report #%ld deleted with its message", id);
        return 1;
    }
    if (!(id = st_take_back(t->store, message, &files, &nfiles))) return 0;
    site_forget_files(t->root, id, files, nfiles);
    {
        StReport r;
        if (st_get(t->store, id, &r, NULL, NULL) == 1) {
            site_update(t->store, t->root, id, r.key);
            st_report_free(&r);
        }
    }
    if (nfiles) rei_log("report #%ld: %d files taken back with their message", id, nfiles);
    st_files_free(files, nfiles);
    return 1;
}
