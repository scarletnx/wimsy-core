/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "site.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ---- pieces of a page ---- */

static void esc(Buf* b, const char* s) {
    for (; s && *s; ++s) switch (*s) {
            case '&': buf_add(b, "&amp;"); break;
            case '<': buf_add(b, "&lt;"); break;
            case '>': buf_add(b, "&gt;"); break;
            case '"': buf_add(b, "&quot;"); break;
            case '\'': buf_add(b, "&#39;"); break;
            default: buf_addn(b, s, 1);
        }
}
/* text lowered (ASCII) and escaped, for the attribute the search reads */
static void esc_lower(Buf* b, const char* s) {
    char* low = rei_strdup(s ? s : "");
    char* p;
    for (p = low; *p; ++p) *p = (char)tolower((unsigned char)*p);
    esc(b, low);
    buf_add(b, " ");
    free(low);
}
static void when(Buf* b, double t) {
    time_t s = (time_t)t;
    struct tm tm;
    char iso[32], plain[32];
    gmtime_r(&s, &tm);
    strftime(iso, sizeof iso, "%Y-%m-%dT%H:%M:%SZ", &tm);
    strftime(plain, sizeof plain, "%Y-%m-%d %H:%M UTC", &tm);
    buf_printf(b, "<time datetime=\"%s\">%s</time>", iso, plain);
}
static void bytes(Buf* b, double n) {
    if (n >= 1 << 20)
        buf_printf(b, "%.1f MB", n / (1 << 20));
    else if (n >= 1 << 10)
        buf_printf(b, "%.0f KB", n / (1 << 10));
    else
        buf_printf(b, "%.0f bytes", n);
}
/* an outcome's tone, for its colour: "ok", "warn" or "bad" */
static const char* tone(const char* outcome) {
    if (!outcome || !strcmp(outcome, "no failure logged")) return "ok";
    if (!strcmp(outcome, "unsupported instruction") || !strcmp(outcome, "thread stopped")) return "warn";
    return "bad";
}
/* how the run ended, and beside it that the runtime missed something (what, counted, when the pointer is over it) */
static void badge(Buf* b, const char* outcome, const char* missed) {
    buf_printf(b, "<span class=\"badges\"><span class=\"badge %s\">", tone(outcome));
    esc(b, outcome ? outcome : "no failure logged");
    buf_add(b, "</span>");
    if (missed) {
        buf_add(b, "<span class=\"badge missed\" title=\"Missed: ");
        esc(b, missed);
        buf_add(b, "\">something missed</span>");
    }
    buf_add(b, "</span>");
}
static int hex(const char* s) {
    if (!s || !*s) return 0;
    for (; *s; ++s)
        if (!isxdigit((unsigned char)*s)) return 0;
    return 1;
}

static void head(Buf* b, const char* root, const char* title, const char* description) {
    buf_add(b, "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
               "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>");
    esc(b, title);
    buf_add(b, "</title>\n<meta name=\"description\" content=\"");
    esc(b, description);
    buf_printf(b,
               "\">\n<link rel=\"stylesheet\" href=\"%sstyle.css\">\n<link rel=\"icon\" href=\"%stracer.svg\">\n"
               "<script src=\"%ssite.js\" defer></script>\n</head>\n<body>\n<header class=\"top\">\n"
               "<a class=\"brand\" href=\"%sindex.html\"><img src=\"%stracer.svg\" alt=\"\" width=\"36\" height=\"36\">"
               "<span>Tracer</span></a>\n<nav><a href=\"%sindex.html\">Games</a><a href=\"%sall.html\">All reports</a>"
               "</nav>\n</header>\n<main>\n",
               root, root, root, root, root, root, root);
}
static void foot(Buf* b) {
    buf_add(b, "</main>\n<footer>Reports that players sent to Tracer on Discord, with logs of "
               "<a href=\"" SITE_REPO "\">ScarletNX</a>. Home folders in the logs are shown as ~.</footer>\n"
               "</body>\n</html>\n");
}
/* the controls site.js reads: a search, and a choice of each key's values (filled in from the rows) */
/* the consoles ScarletNX runs, as the log summary names them */
static const char* const CONSOLES[] = {"Switch", "3DS", "Vita", "GBA"};

static void filters(Buf* b, const char* list, const char* placeholder, const char* const* keys,
                    const char* const* names, int nkeys) {
    int i;
    buf_printf(b, "<div class=\"filters\" data-for=\"%s\">\n<input type=\"search\" data-search placeholder=\"", list);
    esc(b, placeholder);
    buf_add(b, "\" aria-label=\"Search\">\n");
    for (i = 0; i < nkeys; ++i) {
        /* the console's choice is always there, with each console ScarletNX runs, so a list can be one console's
           before it has another's report; site.js counts them. The others list the values the rows have */
        int console = !strcmp(keys[i], "console");
        size_t k;
        buf_printf(b, "<select data-key=\"%s\"%s aria-label=\"", keys[i], console ? " data-fixed" : "");
        esc(b, names[i]);
        buf_add(b, console ? "\">" : "\" hidden>");
        buf_add(b, "<option value=\"\">");
        esc(b, names[i]);
        buf_add(b, ": all</option>");
        for (k = 0; console && k < sizeof CONSOLES / sizeof *CONSOLES; ++k)
            buf_printf(b, "<option value=\"%s\">%s</option>", CONSOLES[k], CONSOLES[k]);
        buf_add(b, "</select>\n");
    }
    buf_add(b, "<span class=\"count\" data-count></span>\n</div>\n");
}

/* ---- the lists ---- */

char* site_index_html(const StGame* games, int n) {
    static const char* const keys[] = {"console", "outcome"};
    static const char* const names[] = {"Console", "Latest outcome"};
    Buf b = {0};
    int i, total = 0;
    for (i = 0; i < n; ++i) total += games[i].reports;
    head(&b, "", "Tracer: game reports", "ScarletNX game reports from Discord, grouped by game.");
    buf_add(&b,
            "<section class=\"hero\">\n<h1>Game reports</h1>\n<p>Logs and screenshots that players sent to Tracer on "
            "Discord, grouped by game. Mention <code>@Tracer</code> with a log attached to add one.</p>\n");
    buf_printf(&b, "<p class=\"stats\"><b>%d</b> %s, <b>%d</b> %s</p>\n</section>\n", n, n == 1 ? "game" : "games",
               total, total == 1 ? "report" : "reports");
    if (!n) {
        buf_add(&b, "<p class=\"empty\">No reports yet.</p>\n");
        foot(&b);
        return buf_take(&b);
    }
    filters(&b, "games", "Search games or title IDs", keys, names, 2);
    buf_add(&b, "<ul class=\"games\" id=\"games\" data-list>\n");
    for (i = 0; i < n; ++i) {
        const StGame* g = &games[i];
        buf_add(&b, "<li data-text=\"");
        esc_lower(&b, g->title);
        esc_lower(&b, g->title_id);
        esc_lower(&b, g->console);
        buf_add(&b, "\" data-console=\"");
        esc(&b, g->console ? g->console : "unknown");
        buf_add(&b, "\" data-outcome=\"");
        esc(&b, g->outcome ? g->outcome : "no failure logged");
        buf_printf(&b, "\"><a href=\"g/%s.html\">\n<span class=\"name\">", g->key);
        esc(&b, g->title);
        buf_add(&b, "</span>\n<span class=\"id\">");
        if (g->console) {
            esc(&b, g->console);
            buf_add(&b, " · ");
        }
        if (g->title_id && !strcmp(g->title, g->title_id))
            esc(&b, "no name in its logs");
        else
            esc(&b, g->title_id ? g->title_id : "no title ID");
        buf_printf(&b, "</span>\n<span class=\"meta\">%d %s, the latest ", g->reports,
                   g->reports == 1 ? "report" : "reports");
        when(&b, g->last);
        buf_add(&b, "</span>\n");
        badge(&b, g->outcome, g->missed);
        buf_add(&b, "\n</a></li>\n");
    }
    buf_add(&b, "</ul>\n<p class=\"empty\" data-empty hidden>No game matches.</p>\n");
    foot(&b);
    return buf_take(&b);
}

/* the reports' table: root before each link, the game's column when game */
static void table(Buf* b, const char* root, const StReport* reports, int n, int game) {
    static const char* const keys[] = {"console", "outcome", "missed", "branch", "gpu"};
    static const char* const names[] = {"Console", "Outcome", "Missed", "Branch", "GPU"};
    int i;
    filters(b, "reports", "Search signatures, builds, GPUs, notes", keys, names, 5);
    buf_printf(b,
               "<div class=\"scroll\"><table class=\"reports\" id=\"reports\" data-list>\n<thead><tr><th>Report</th>"
               "<th>Sent</th>%s<th>Outcome</th><th>Signature</th><th>Build</th><th>GPU</th></tr></thead>\n<tbody>\n",
               game ? "<th>Game</th>" : "");
    for (i = 0; i < n; ++i) {
        const StReport* r = &reports[i];
        buf_printf(b, "<tr data-text=\"#%ld ", r->id);
        esc_lower(b, r->title);
        esc_lower(b, r->title_id);
        esc_lower(b, r->outcome);
        esc_lower(b, r->signature);
        esc_lower(b, r->build);
        esc_lower(b, r->gpu);
        esc_lower(b, r->system);
        esc_lower(b, r->note);
        esc_lower(b, r->missed);
        buf_add(b, "\" data-outcome=\"");
        esc(b, r->outcome ? r->outcome : "no failure logged");
        buf_printf(b, "\" data-missed=\"%s", r->missed ? "something missed" : "nothing missed");
        buf_add(b, "\" data-console=\"");
        esc(b, r->console ? r->console : "unknown");
        buf_add(b, "\" data-branch=\"");
        esc(b, r->branch ? r->branch : "unknown");
        buf_add(b, "\" data-gpu=\"");
        esc(b, r->gpu ? r->gpu : "unknown");
        buf_printf(b, "\">\n<td><a href=\"%sr/%ld/\">#%ld</a></td><td>", root, r->id, r->id);
        when(b, r->when);
        buf_add(b, "</td>");
        if (game) {
            buf_printf(b, "<td><a href=\"%sg/%s.html\">", root, r->key);
            esc(b, r->title);
            buf_add(b, "</a>");
            if (r->console) {
                buf_add(b, "<br><small>");
                esc(b, r->console);
                buf_add(b, "</small>");
            }
            buf_add(b, "</td>");
        }
        buf_add(b, "<td>");
        badge(b, r->outcome, r->missed);
        buf_add(b, "</td><td class=\"mono\">");
        esc(b, r->signature ? r->signature : "");
        buf_add(b, "</td><td class=\"mono\">");
        if (r->branch || r->commit) {
            esc(b, r->branch ? r->branch : "");
            buf_add(b, " ");
            esc(b, r->commit ? r->commit : "");
        } else
            esc(b, r->build ? r->build : "");
        buf_add(b, "</td><td>");
        esc(b, r->gpu ? r->gpu : "");
        buf_add(b, "</td>\n</tr>\n");
    }
    buf_add(b, "</tbody>\n</table></div>\n<p class=\"empty\" data-empty hidden>No report matches.</p>\n");
}

char* site_all_html(const StReport* reports, int n) {
    Buf b = {0};
    head(&b, "", "Tracer: every report", "Every ScarletNX game report sent to Tracer, the newest first.");
    buf_printf(&b,
               "<section class=\"hero\">\n<h1>Every report</h1>\n<p class=\"stats\"><b>%d</b> %s, the newest "
               "first</p>\n</section>\n",
               n, n == 1 ? "report" : "reports");
    if (n)
        table(&b, "", reports, n, 1);
    else
        buf_add(&b, "<p class=\"empty\">No reports yet.</p>\n");
    foot(&b);
    return buf_take(&b);
}

char* site_game_html(const StGame* game, const StReport* reports, int n) {
    Buf b = {0}, title = {0};
    int i, ok = 0;
    for (i = 0; i < n; ++i) ok += !strcmp(tone(reports[i].outcome), "ok");
    buf_printf(&title, "%s: reports", game->title);
    head(&b, "../", title.text, "ScarletNX reports of a game, sent to Tracer.");
    buf_free(&title);
    buf_add(&b, "<section class=\"hero\">\n<p class=\"crumbs\"><a href=\"../index.html\">Games</a></p>\n<h1>");
    esc(&b, game->title);
    buf_add(&b, "</h1>\n<p class=\"id\">");
    if (game->console) {
        esc(&b, game->console);
        buf_add(&b, " · ");
    }
    esc(&b, game->title_id ? game->title_id : "no title ID");
    buf_printf(&b, "</p>\n<p class=\"stats\"><b>%d</b> %s, <b>%d</b> with no failure logged</p>\n</section>\n", n,
               n == 1 ? "report" : "reports", ok);
    table(&b, "../", reports, n, 0);
    foot(&b);
    return buf_take(&b);
}

/* ---- a report ---- */

static void field(Buf* b, const char* name, const char* value) {
    if (!value || !*value) return;
    buf_add(b, "<dt>");
    esc(b, name);
    buf_add(b, "</dt><dd>");
    esc(b, value);
    buf_add(b, "</dd>\n");
}
/* a symbol of the summary: its demangled name and offset */
static void symbol(Buf* b, const cJSON* sym) {
    const char *name = jstr(sym, "demangled"), *offset = jstr(sym, "offset");
    if (!name) name = jstr(sym, "name");
    if (!name) return;
    buf_add(b, "<code>");
    esc(b, name);
    if (offset) {
        buf_add(b, "+");
        esc(b, offset);
    }
    buf_add(b, "</code>");
}
static void scalar(Buf* b, const cJSON* v) {
    if (cJSON_IsString(v))
        esc(b, v->valuestring);
    else if (cJSON_IsNumber(v))
        buf_printf(b, "%.15g", v->valuedouble);
    else if (cJSON_IsBool(v))
        buf_add(b, cJSON_IsTrue(v) ? "yes" : "no");
}
/* a list of the summary: a table when its items are objects (their columns the first item's keys whose values are
   no objects), else a list */
static void listed(Buf* b, const char* heading, const cJSON* list) {
    const cJSON *first = cJSON_GetArrayItem(list, 0), *item, *v;
    if (!first) return;
    buf_add(b, "<h3>");
    esc(b, heading);
    buf_add(b, "</h3>\n");
    if (!cJSON_IsObject(first)) {
        buf_add(b, "<ul class=\"mono\">\n");
        cJSON_ArrayForEach(item, list) {
            buf_add(b, "<li>");
            scalar(b, item);
            buf_add(b, "</li>\n");
        }
        buf_add(b, "</ul>\n");
        return;
    }
    buf_add(b, "<div class=\"scroll\"><table class=\"plain\">\n<thead><tr>");
    cJSON_ArrayForEach(v, first) if (!cJSON_IsObject(v) && !cJSON_IsArray(v)) {
        buf_add(b, "<th>");
        esc(b, v->string);
        buf_add(b, "</th>");
    }
    buf_add(b, "</tr></thead>\n<tbody>\n");
    cJSON_ArrayForEach(item, list) {
        buf_add(b, "<tr>");
        cJSON_ArrayForEach(v, first) if (!cJSON_IsObject(v) && !cJSON_IsArray(v)) {
            buf_add(b, "<td class=\"mono\">");
            scalar(b, cJSON_GetObjectItemCaseSensitive(item, v->string));
            buf_add(b, "</td>");
        }
        buf_add(b, "</tr>\n");
    }
    buf_add(b, "</tbody>\n</table></div>\n");
}
static void lines(Buf* b, const char* heading, const cJSON* list) {
    const cJSON* line;
    if (!cJSON_GetArraySize(list)) return;
    buf_add(b, "<h3>");
    esc(b, heading);
    buf_add(b, "</h3>\n<pre>");
    cJSON_ArrayForEach(line, list) {
        scalar(b, line);
        buf_add(b, "\n");
    }
    buf_add(b, "</pre>\n");
}
static void log_section(Buf* b, const StFile* f) {
    cJSON* s = f->summary ? cJSON_Parse(f->summary) : NULL;
    const cJSON *failure = cJSON_GetObjectItemCaseSensitive(s, "failure"), *frame;
    const char* file = f->path;
    buf_add(b, "<section class=\"log\">\n<h2>");
    esc(b, f->name);
    buf_add(b, " <small>");
    bytes(b, f->bytes);
    buf_add(b, "</small></h2>\n<p class=\"actions\"><a class=\"button\" href=\"");
    esc(b, file);
    buf_add(b, "\">View the log</a> <a class=\"button quiet\" href=\"");
    esc(b, file);
    buf_add(b, "\" download=\"");
    esc(b, f->name);
    buf_add(b, "\">Download</a></p>\n");
    if (cJSON_IsObject(failure)) {
        const cJSON* fn = cJSON_GetObjectItemCaseSensitive(failure, "function");
        buf_add(b, "<div class=\"failure\">\n");
        if (jstr(failure, "fatal")) {
            buf_add(b, "<p class=\"fatal\">");
            esc(b, jstr(failure, "fatal"));
            buf_add(b, "</p>\n");
        }
        if (cJSON_IsObject(fn)) {
            buf_add(b, "<p>In ");
            symbol(b, fn);
            buf_add(b, "</p>\n");
        }
        if (cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(failure, "frames"))) {
            buf_add(b, "<ol class=\"frames\" start=\"0\">\n");
            cJSON_ArrayForEach(frame, cJSON_GetObjectItemCaseSensitive(failure, "frames")) {
                buf_add(b, "<li><code class=\"address\">");
                esc(b, jstr(frame, "address"));
                buf_add(b, "</code> ");
                symbol(b, cJSON_GetObjectItemCaseSensitive(frame, "symbol"));
                buf_add(b, "</li>\n");
            }
            buf_add(b, "</ol>\n");
        }
        buf_add(b, "</div>\n");
    }
    listed(b, "Service commands the runtime does not handle", cJSON_GetObjectItemCaseSensitive(s, "ipc"));
    listed(b, "What the runtime missed", cJSON_GetObjectItemCaseSensitive(s, "missed"));
    listed(b, "Warnings", cJSON_GetObjectItemCaseSensitive(s, "warnings"));
    lines(b, "The game's output", cJSON_GetObjectItemCaseSensitive(s, "game_output"));
    lines(b, "The last lines", cJSON_GetObjectItemCaseSensitive(s, "tail"));
    buf_add(b, "</section>\n");
    cJSON_Delete(s);
}

char* site_report_html(const StReport* r, const StFile* files, int nfiles, const StNote* notes, int nnotes) {
    Buf b = {0}, title = {0};
    int i, images = 0, videos = 0;
    buf_printf(&title, "Report #%ld: %s", r->id, r->title);
    head(&b, "../../", title.text, "A ScarletNX game report sent to Tracer.");
    buf_free(&title);
    buf_printf(&b,
               "<section class=\"hero\">\n<p class=\"crumbs\"><a href=\"../../index.html\">Games</a> / "
               "<a href=\"../../g/%s.html\">",
               r->key);
    esc(&b, r->title);
    buf_printf(&b, "</a></p>\n<h1>Report #%ld</h1>\n<p class=\"meta\">Sent ", r->id);
    when(&b, r->when);
    buf_add(&b, " ");
    badge(&b, r->outcome, r->missed);
    buf_add(&b, "</p>\n");
    if (r->note && *r->note) {
        buf_add(&b, "<blockquote>");
        esc(&b, r->note);
        buf_add(&b, "</blockquote>\n");
    }
    for (i = 0; i < nnotes; ++i) { /* the notes added later */
        buf_add(&b, "<blockquote class=\"later\"><span class=\"meta\">Added ");
        when(&b, notes[i].when);
        buf_add(&b, "</span><br>");
        esc(&b, notes[i].text);
        buf_add(&b, "</blockquote>\n");
    }
    buf_add(&b, "</section>\n<dl class=\"facts\">\n");
    field(&b, "Game", r->title);
    field(&b, "Console", r->console);
    field(&b, "Title ID", r->title_id);
    field(&b, "Game version", r->version);
    field(&b, "Signature", r->signature);
    field(&b, "Missed", r->missed);
    if (hex(r->commit) || (r->build && *r->build) || r->age) { /* the build, its commit linked, and how old it was */
        buf_add(&b, "<dt>Build</dt><dd>");
        esc(&b, r->build ? r->build : r->commit ? r->commit : "");
        if (hex(r->commit)) buf_printf(&b, " <a href=\"" SITE_REPO "/commit/%s\">%s</a>", r->commit, r->commit);
        if (r->age) {
            buf_add(&b, "<br>");
            esc(&b, r->age);
        }
        buf_add(&b, "</dd>\n");
    }
    field(&b, "System", r->system);
    field(&b, "GPU", r->gpu);
    field(&b, "Driver", r->driver);
    if (r->issue > 0 && r->issue_url && !strncmp(r->issue_url, "https://github.com/", 19)) {
        buf_add(&b, "<dt>Issue</dt><dd><a href=\"");
        esc(&b, r->issue_url);
        buf_printf(&b, "\">#%ld</a></dd>\n", r->issue);
    }
    buf_add(&b, "</dl>\n");
    for (i = 0; i < nfiles; ++i) images += !strcmp(files[i].kind, "image");
    if (images) {
        buf_add(&b, "<section class=\"shots\">\n<h2>Screenshots</h2>\n<div class=\"gallery\">\n");
        for (i = 0; i < nfiles; ++i) {
            const char* file = files[i].path;
            if (strcmp(files[i].kind, "image")) continue;
            buf_add(&b, "<a href=\"");
            esc(&b, file);
            buf_add(&b, "\"><img src=\"");
            esc(&b, file);
            buf_add(&b, "\" alt=\"");
            esc(&b, files[i].name);
            buf_add(&b, "\" loading=\"lazy\"></a>\n");
        }
        buf_add(&b, "</div>\n</section>\n");
    }
    for (i = 0; i < nfiles; ++i) videos += !strcmp(files[i].kind, "video");
    if (videos) { /* the videos, each played where it is; a browser asks for its parts as it plays */
        buf_add(&b, "<section class=\"shots\">\n<h2>Videos</h2>\n<div class=\"videos\">\n");
        for (i = 0; i < nfiles; ++i) {
            const char* file = files[i].path;
            if (strcmp(files[i].kind, "video")) continue;
            buf_add(&b, "<figure><video controls preload=\"metadata\" src=\"");
            esc(&b, file);
            buf_add(&b, "\"></video><figcaption><a href=\"");
            esc(&b, file);
            buf_add(&b, "\" download=\"");
            esc(&b, files[i].name);
            buf_add(&b, "\">");
            esc(&b, files[i].name);
            buf_add(&b, "</a> ");
            bytes(&b, files[i].bytes);
            buf_add(&b, "</figcaption></figure>\n");
        }
        buf_add(&b, "</div>\n</section>\n");
    }
    for (i = 0; i < nfiles; ++i)
        if (!strcmp(files[i].kind, "log")) log_section(&b, &files[i]);
    foot(&b);
    return buf_take(&b);
}

/* ---- the JSON ---- */

static cJSON* report_object(const StReport* r) {
    cJSON* o = cJSON_CreateObject();
    char when[32];
    time_t s = (time_t)r->when;
    struct tm tm;
    gmtime_r(&s, &tm);
    strftime(when, sizeof when, "%Y-%m-%dT%H:%M:%SZ", &tm);
    cJSON_AddNumberToObject(o, "id", (double)r->id);
    jset(o, "sent", when);
    jset(o, "game", r->key);
    jset(o, "title", r->title);
    jset(o, "title_id", r->title_id);
    jset(o, "note", r->note);
    jset(o, "game_version", r->version);
    jset(o, "build", r->build);
    jset(o, "commit", r->commit);
    jset(o, "branch", r->branch);
    jset(o, "system", r->system);
    jset(o, "gpu", r->gpu);
    jset(o, "driver", r->driver);
    jset(o, "outcome", r->outcome ? r->outcome : "no failure logged");
    jset(o, "signature", r->signature);
    jset(o, "missed", r->missed);
    jset(o, "console", r->console);
    jset(o, "build_age", r->age);
    if (r->issue > 0 && r->issue_url) {
        cJSON_AddNumberToObject(o, "issue", (double)r->issue);
        jset(o, "issue_url", r->issue_url);
    }
    return o;
}
static char* printed(cJSON* o) {
    char* text = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!text) {
        fputs(REI_NAME ": out of memory\n", stderr);
        abort();
    }
    return text;
}

char* site_games_json(const StGame* games, int n) {
    cJSON *o = cJSON_CreateObject(), *list = cJSON_AddArrayToObject(o, "games");
    int i;
    for (i = 0; i < n; ++i) {
        cJSON* g = cJSON_CreateObject();
        char when[32];
        time_t s = (time_t)games[i].last;
        struct tm tm;
        gmtime_r(&s, &tm);
        strftime(when, sizeof when, "%Y-%m-%dT%H:%M:%SZ", &tm);
        jset(g, "game", games[i].key);
        jset(g, "title", games[i].title);
        jset(g, "title_id", games[i].title_id);
        cJSON_AddNumberToObject(g, "reports", games[i].reports);
        jset(g, "latest", when);
        jset(g, "outcome", games[i].outcome ? games[i].outcome : "no failure logged");
        jset(g, "missed", games[i].missed);
        jset(g, "console", games[i].console);
        cJSON_AddItemToArray(list, g);
    }
    return printed(o);
}

char* site_reports_json(const StReport* reports, int n) {
    cJSON *o = cJSON_CreateObject(), *list = cJSON_AddArrayToObject(o, "reports");
    int i;
    for (i = 0; i < n; ++i) cJSON_AddItemToArray(list, report_object(&reports[i]));
    return printed(o);
}

char* site_report_json(const StReport* r, const StFile* files, int nfiles, const StNote* notes, int nnotes) {
    cJSON *o = report_object(r), *later = cJSON_AddArrayToObject(o, "notes_added"),
          *list = cJSON_AddArrayToObject(o, "files");
    int i;
    for (i = 0; i < nnotes; ++i) {
        cJSON* n = cJSON_CreateObject();
        char when[32];
        time_t s = (time_t)notes[i].when;
        struct tm tm;
        gmtime_r(&s, &tm);
        strftime(when, sizeof when, "%Y-%m-%dT%H:%M:%SZ", &tm);
        jset(n, "sent", when);
        jset(n, "text", notes[i].text);
        cJSON_AddItemToArray(later, n);
    }
    for (i = 0; i < nfiles; ++i) {
        cJSON* f = cJSON_CreateObject();
        char* path = rei_format("r/%ld/%s", r->id, files[i].path);
        cJSON* summary = files[i].summary ? cJSON_Parse(files[i].summary) : NULL;
        jset(f, "kind", files[i].kind);
        jset(f, "name", files[i].name);
        jset(f, "path", path);
        cJSON_AddNumberToObject(f, "bytes", files[i].bytes);
        if (summary) cJSON_AddItemToObject(f, "summary", summary);
        cJSON_AddItemToArray(list, f);
        free(path);
    }
    return printed(o);
}

/* ---- the folder ---- */

/* root/rel's folders made, as mkdir -p would */
static int folders(const char* root, const char* rel) {
    char* path = rei_format("%s/%s", root, rel);
    char* p;
    int ok = 1;
    for (p = path + strlen(root) + 1; ok && (p = strchr(p, '/')); ++p) {
        *p = 0;
        ok = !mkdir(path, 0755) || errno == EEXIST;
        if (!ok) rei_log("%s could not be made: %s", path, strerror(errno));
        *p = '/';
    }
    free(path);
    return ok ? 0 : -1;
}

int site_write(const char* root, const char* rel, const char* text, size_t n) {
    char *path = rei_format("%s/%s", root, rel), *temp = rei_format("%s/%s.new", root, rel);
    FILE* f;
    int ok = 0;
    if (!folders(root, rel) && (f = fopen(temp, "wb"))) {
        ok = fwrite(text, 1, n, f) == n;
        ok = !fclose(f) && ok;
        ok = ok && !rename(temp, path);
        if (!ok) unlink(temp);
    }
    if (!ok) rei_log("%s could not be written: %s", path, strerror(errno));
    free(path);
    free(temp);
    return ok ? 0 : -1;
}

int site_move(const char* root, const char* rel, const char* from) {
    char* path = rei_format("%s/%s", root, rel);
    int done = -1;
    if (!folders(root, rel)) {
        if (!rename(from, path))
            done = 0;
        else if (errno == EXDEV) { /* another file system: a copy, then the file taken away */
            FILE* f = fopen(from, "rb");
            Buf text = {0};
            char piece[65536];
            size_t got;
            if (f) {
                while ((got = fread(piece, 1, sizeof piece, f))) buf_addn(&text, piece, got);
                fclose(f);
                done = site_write(root, rel, text.text ? text.text : "", text.len);
                if (!done) unlink(from);
            }
            buf_free(&text);
        }
        if (done) rei_log("%s could not be moved to %s: %s", from, path, strerror(errno));
    }
    free(path);
    return done;
}

int site_assets(const char* from, const char* root) {
    static const char* const names[] = {"style.css", "site.js", "tracer.svg"};
    size_t i;
    int failed = 0;
    for (i = 0; i < sizeof names / sizeof *names; ++i) {
        char* path = rei_format("%s/%s", from, names[i]);
        FILE* f = fopen(path, "rb");
        Buf text = {0};
        char piece[8192];
        size_t got;
        if (!f) {
            rei_log("%s could not be read: %s", path, strerror(errno));
            failed = 1;
            free(path);
            continue;
        }
        while ((got = fread(piece, 1, sizeof piece, f))) buf_addn(&text, piece, got);
        fclose(f);
        failed |= site_write(root, names[i], text.text ? text.text : "", text.len) != 0;
        buf_free(&text);
        free(path);
    }
    return failed ? -1 : 0;
}

static int write_page(const char* root, const char* rel, char* html) {
    int done = site_write(root, rel, html, strlen(html));
    free(html);
    return done;
}

static int report_page(Store* s, const char* root, long id) {
    StReport r;
    StFile* files;
    StNote* notes = NULL;
    int nfiles, nnotes = 0, done = -1;
    if (st_get(s, id, &r, &files, &nfiles) == 1) {
        char* rel = rei_format("r/%ld/index.html", id);
        st_notes(s, id, &notes, &nnotes);
        done = write_page(root, rel, site_report_html(&r, files, nfiles, notes, nnotes));
        free(rel);
        rel = rei_format("api/r/%ld.json", id);
        done |= write_page(root, rel, site_report_json(&r, files, nfiles, notes, nnotes));
        free(rel);
        st_report_free(&r);
        st_files_free(files, nfiles);
        st_notes_free(notes, nnotes);
    }
    return done;
}

/* a game's page drawn from games, or removed when it is not among them */
static int game_page(Store* s, const char* root, const char* key, const StGame* games, int ngames) {
    char* rel = rei_format("g/%s.html", key);
    StReport* reports;
    int i, n, done = 0;
    for (i = 0; i < ngames && strcmp(games[i].key, key); ++i) continue;
    if (i == ngames) {
        char* path = rei_format("%s/%s", root, rel);
        unlink(path);
        free(path);
    } else if (!st_reports(s, key, &reports, &n)) {
        done = write_page(root, rel, site_game_html(&games[i], reports, n));
        st_reports_free(reports, n);
    } else
        done = -1;
    free(rel);
    return done;
}

static int lists(Store* s, const char* root, StGame* games, int ngames) {
    StReport* all;
    int n, done = write_page(root, "index.html", site_index_html(games, ngames));
    done |= write_page(root, "api/games.json", site_games_json(games, ngames));
    if (st_reports(s, NULL, &all, &n)) return -1;
    done |= write_page(root, "all.html", site_all_html(all, n));
    done |= write_page(root, "api/reports.json", site_reports_json(all, n));
    st_reports_free(all, n);
    return done;
}

int site_update(Store* s, const char* root, long id, const char* key) {
    StGame* games;
    int n, done = 0;
    if (id) done |= report_page(s, root, id);
    if (st_games(s, &games, &n)) return -1;
    done |= game_page(s, root, key, games, n);
    done |= lists(s, root, games, n);
    st_games_free(games, n);
    return done ? -1 : 0;
}

void site_forget_files(const char* root, long id, const StFile* files, int nfiles) {
    int i;
    for (i = 0; i < nfiles; ++i) {
        char* path = rei_format("%s/r/%ld/%s", root, id, files[i].path);
        unlink(path);
        free(path);
    }
}

void site_forget(const char* root, long id, const StFile* files, int nfiles) {
    char* path;
    int i;
    for (i = 0; i < nfiles; ++i) {
        path = rei_format("%s/r/%ld/%s", root, id, files[i].path);
        unlink(path);
        free(path);
    }
    path = rei_format("%s/r/%ld/index.html", root, id);
    unlink(path);
    free(path);
    path = rei_format("%s/api/r/%ld.json", root, id);
    unlink(path);
    free(path);
    path = rei_format("%s/r/%ld", root, id);
    rmdir(path);
    free(path);
}

int site_rebuild(Store* s, const char* root) {
    StGame* games;
    StReport* all;
    int i, ngames, n, done = 0;
    if (st_games(s, &games, &ngames)) return -1;
    if (st_reports(s, NULL, &all, &n)) {
        st_games_free(games, ngames);
        return -1;
    }
    for (i = 0; i < n; ++i) done |= report_page(s, root, all[i].id);
    for (i = 0; i < ngames; ++i) done |= game_page(s, root, games[i].key, games, ngames);
    done |= lists(s, root, games, ngames);
    st_reports_free(all, n);
    st_games_free(games, ngames);
    return done ? -1 : 0;
}
