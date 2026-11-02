/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "report.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

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
        *named = 1;
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
static int admin(const Tracer* t, const TrMessage* m) {
    int i;
    for (i = 0; i < t->nadmins; ++i)
        if (t->admins[i] == m->author) return 1;
    return t->administers && m->guild && t->administers(t->admin_ctx, m->guild, m->author, m->roles, m->nroles);
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
        "them. I keep them as a report on the site, grouped by game. The log says which game it is: when it "
        "does not, write the game's name in the message.\n\n"
        "Home folders in a log are shown as ~, and screenshots lose their metadata. The site does not show "
        "who sent a report.\n\n"
        "To add logs, screenshots or a note to a report, reply to it (or to my answer) and mention me, or write "
        "`@Tracer add 12` with them.\n\n"
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
    if (r.poster != m->author && !admin(t, m))
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

typedef struct {
    StFile files[TR_LOGS + TR_IMAGES];
    char* data[TR_LOGS + TR_IMAGES]; /* each file's bytes, owned */
    int n, logs, images;
    int base, logs_had, images_had; /* a report's last file number, its logs and screenshots, before these */
    cJSON* first;                   /* the first log's summary */
    Buf skipped;                    /* a line for each attachment not taken */
} Taken;

static void skip(Taken* k, const char* name, const char* why) {
    char* n = plain(name, 100);
    buf_printf(&k->skipped, "%s: %s\n", n, why);
    free(n);
}
static void taken_free(Taken* k) {
    int i;
    for (i = 0; i < k->n; ++i) {
        free(k->files[i].name);
        free(k->files[i].path);
        free(k->files[i].summary);
        free(k->data[i]);
    }
    cJSON_Delete(k->first);
    buf_free(&k->skipped);
}

static void take_log(Taken* k, const TrAttachment* a, Buf* got) {
    Buf text = {0};
    char *why = NULL, *tilded, *summary;
    size_t n;
    cJSON* parsed;
    StFile* f;
    if (tr_unpack(a->name, got->text, got->len, TR_LOG_MAX, &text, &why)) {
        skip(k, a->name, why);
        free(why);
        return;
    }
    if (!text.len) {
        skip(k, a->name, "it is empty");
        return;
    }
    tilded = snx_logscan_tilde(text.text, text.len, &n);
    buf_free(&text);
    if (!tilded) {
        skip(k, a->name, "out of memory");
        return;
    }
    summary = summary_of(tilded, n);
    parsed = summary ? cJSON_Parse(summary) : NULL;
    if (!is_log(parsed)) {
        skip(k, a->name, "not a ScarletNX log");
        cJSON_Delete(parsed);
        free(summary);
        free(tilded);
        return;
    }
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
    k->data[k->n++] = tilded;
    ++k->logs;
}

static void take_image(Taken* k, const TrAttachment* a, Buf* got) {
    const char* ext = tr_image_ext(got->text, got->len);
    size_t n;
    if (ext && !strcmp(ext, "ppm")) { /* a frame ScarletNX saved, as a PNG a browser shows */
        Buf png = {0};
        if (tr_ppm_png(got->text, got->len, &png)) {
            skip(k, a->name, "a PPM that isn't whole, or of more than 255 a channel");
            return;
        }
        buf_free(got);
        *got = png;
        ext = "png";
    }
    n = ext ? tr_strip_image(got->text, got->len) : 0;
    StFile* f;
    if (!n) {
        skip(k, a->name, "not a PNG, JPEG, WebP, GIF or PPM image");
        return;
    }
    f = &k->files[k->n];
    f->n = k->base + k->n + 1;
    strcpy(f->kind, "image");
    f->name = rei_strdup(a->name);
    f->path = rei_format("%d.%s", f->n, ext);
    f->bytes = (double)n;
    k->data[k->n++] = buf_take(got);
    ++k->images;
}

static void take(Tracer* t, Taken* k, const TrMessage* m) {
    int i;
    for (i = 0; i < m->nfiles; ++i) {
        const TrAttachment* a = &m->files[i];
        TrKind kind = tr_kind(a->name, a->type);
        size_t max = kind == TR_LOG ? TR_LOG_MAX : TR_IMAGE_MAX;
        Buf got = {0};
        char* why = NULL;
        if (kind == TR_OTHER) {
            skip(k, a->name, "not a log or a screenshot");
            continue;
        }
        if (kind == TR_LOG && k->logs_had + k->logs >= TR_LOGS) {
            skip(k, a->name, "a report keeps 3 logs");
            continue;
        }
        if (kind == TR_IMAGE && k->images_had + k->images >= TR_IMAGES) {
            skip(k, a->name, "a report keeps 8 screenshots");
            continue;
        }
        if (!from_discord(a->url)) {
            skip(k, a->name, "not a file on Discord");
            continue;
        }
        if (a->size > (double)max) {
            why = rei_format("over %zu MiB", max >> 20);
            skip(k, a->name, why);
            free(why);
            continue;
        }
        if (t->get(t->ctx, a->url, max, &got, &why)) {
            rei_log("a download failed: %s", why ? why : "no reason given");
            skip(k, a->name, "the download failed");
            free(why);
            continue;
        }
        if (kind == TR_LOG)
            take_log(k, a, &got);
        else
            take_image(k, a, &got);
        buf_free(&got);
    }
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
    e = embed(cut, text, !strcmp(tone, "ok") ? TONE_OK : !strcmp(tone, "warn") ? TONE_WARN : TONE_BAD);
    jset(e, "url", url);
    if (r->branch || r->commit) {
        char* build = rei_format("%s %s", r->branch ? r->branch : "", r->commit ? r->commit : "");
        char* clean = plain(build, 200);
        add_field(e, "Build", clean);
        free(clean);
        free(build);
    }
    files = rei_format("%d %s, %d %s", k->logs, k->logs == 1 ? "log" : "logs", k->images,
                       k->images == 1 ? "screenshot" : "screenshots");
    add_field(e, "Kept", files);
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

static cJSON* reported(Tracer* t, const TrMessage* m, char* note, long* report) {
    Taken k;
    StReport r;
    cJSON* e;
    char* text;
    int i, failed = 0;
    if (st_by_message(t->store, m->id)) return NULL; /* the gateway's event again, after a resume */
    if (st_count_since(t->store, m->author, rei_now() - 3600) >= TR_HOURLY)
        return said("You sent 10 reports in the last hour: send this one later, please.");
    memset(&k, 0, sizeof k);
    take(t, &k, m);
    if (!k.logs) {
        text = k.skipped.len ? rei_format("No ScarletNX log here, so no report:\n%s", k.skipped.text)
                             : rei_strdup("Attach the game's ScarletNX log (.log, .txt, .gz or .zip), and screenshots "
                                          "if you have them. `@Tracer help` says more.");
        e = said(text);
        free(text);
        taken_free(&k);
        return e;
    }
    memset(&r, 0, sizeof r);
    if (tr_game(k.first, note, &r.key, &r.title, &r.title_id, &r.named)) {
        taken_free(&k);
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
    if (st_add(t->store, &r, k.files, k.n)) {
        st_report_free(&r);
        taken_free(&k);
        return said("The report could not be kept: try again later, please.");
    }
    for (i = 0; i < k.n && !failed; ++i) {
        char* rel = rei_format("r/%ld/%s", r.id, k.files[i].path);
        failed = site_write(t->root, rel, k.data[i], (size_t)k.files[i].bytes) != 0;
        free(rel);
    }
    if (failed) {
        site_forget(t->root, r.id, k.files, k.n);
        st_remove(t->store, r.id);
        st_report_free(&r);
        taken_free(&k);
        return said("The report could not be kept: try again later, please.");
    }
    site_update(t->store, t->root, r.id, r.key);
    rei_log("report #%ld: %d logs, %d screenshots, %s", r.id, k.logs, k.images, r.key);
    e = made(t, &r, &k);
    *report = r.id;
    st_report_free(&r);
    taken_free(&k);
    return e;
}

/* the files of k written into report id's folder: 0, or -1 with those written deleted */
static int written(Tracer* t, long id, const Taken* k) {
    int i, failed = 0;
    for (i = 0; i < k->n && !failed; ++i) {
        char* rel = rei_format("r/%ld/%s", id, k->files[i].path);
        failed = site_write(t->root, rel, k->data[i], (size_t)k->files[i].bytes) != 0;
        free(rel);
    }
    for (i = 0; failed && i < k->n; ++i) {
        char* path = rei_format("%s/r/%ld/%s", t->root, id, k->files[i].path);
        remove(path);
        free(path);
    }
    return failed ? -1 : 0;
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
    int logs, images;
    if (st_report_of(t->store, m->id)) return NULL; /* the gateway's event again, after a resume */
    if (st_get(t->store, id, &r, NULL, NULL) != 1) {
        text = rei_format("There is no report #%ld.", id);
        e = said(text);
        free(text);
        return e;
    }
    if (r.poster != m->author && !admin(t, m)) {
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
    memset(&k, 0, sizeof k);
    k.base = st_counts(t->store, id, &logs, &images);
    k.logs_had = logs;
    k.images_had = images;
    take(t, &k, m);
    if (!k.n && !*note) {
        text = rei_format("Nothing was added to report #%ld:\n%s", id, k.skipped.text ? k.skipped.text : "");
        e = said(text);
        free(text);
        taken_free(&k);
        st_report_free(&r);
        return e;
    }
    if (written(t, id, &k) || st_update(t->store, id, m->id, rei_now(), k.files, k.n, note)) {
        StFile* back;
        int nback;
        st_take_back(t->store, m->id, &back, &nback);
        st_files_free(back, nback);
        site_forget_files(t->root, id, k.files, k.n);
        taken_free(&k);
        st_report_free(&r);
        return said("The update could not be kept: try again later, please.");
    }
    if (renamed(t, &r, &k, &old)) site_update(t->store, t->root, 0, old);
    site_update(t->store, t->root, id, r.key);
    rei_log("report #%ld: %d logs, %d screenshots%s added", id, k.logs, k.images, *note ? ", a note" : "");
    text = rei_format("Report #%ld: %s", id, r.title);
    added = rei_format("%d %s, %d %s%s", k.logs, k.logs == 1 ? "log" : "logs", k.images,
                       k.images == 1 ? "screenshot" : "screenshots", *note ? ", a note" : "");
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
    taken_free(&k);
    st_report_free(&r);
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
        case TR_REPORT: /* a reply to a report adds to it */
            e = target ? updated(t, m, target, note, report) : reported(t, m, note, report);
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
    } else if ((!r->named || on_gba(jstr(s, "console"))) && strcmp(title, r->title) &&
               !st_set_game(t->store, r->id, key, title, tid, named))
        rei_log("report #%ld named %s", r->id, title);
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
        int nfiles, k, first = 1;
        if (st_get(t->store, all[i].id, r, &files, &nfiles) != 1) continue;
        for (k = 0; k < nfiles; ++k) {
            char *path, *text, *summary;
            size_t len = 0;
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
            summary = summary_of(text, len);
            free(text);
            if (!summary) continue;
            st_set_summary(t->store, r->id, files[k].n, summary);
            if (first) { /* the report's facts are its first log's, and so is its game when the log names another */
                cJSON* s = cJSON_Parse(summary);
                StReport facts = *r;
                refile(t, r, s);
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
                st_set_facts(t->store, &facts);
                free(facts.missed);
                cJSON_Delete(s);
                first = 0;
            }
            free(summary);
        }
        st_files_free(files, nfiles);
        st_report_free(r);
        ++done;
    }
    st_reports_free(all, n);
    st_set_meta(t->store, "logscan", version);
    rei_log("%d reports summarized again, with the log summary %s", done, version);
    return done;
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
