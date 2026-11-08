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

#include <dirent.h>

#include "files.h"
#include "rules.h"

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
               "<script src=\"%ssite.js\" defer></script>\n</head>\n<body>\n" SITE_THEMES "<header class=\"top\">\n"
               "<a class=\"brand\" href=\"%sindex.html\"><img src=\"%stracer.svg\" alt=\"\" width=\"36\" height=\"36\">"
               "<span>Tracer</span></a>\n<nav><a href=\"%sindex.html\">Games</a><a href=\"%sall.html\">All reports</a>"
               "<a href=\"%sprobe/index.html\">Probe</a></nav>\n</header>\n<main>\n",
               root, root, root, root, root, root, root, root);
}
static void foot(Buf* b) {
    buf_add(b, "</main>\n<footer>Reports that players sent to Tracer on Discord, with logs of "
               "<a href=\"" SITE_REPO "\">ScarletNX</a>. Home folders in the logs, and the folders of a path outside "
               "them, are shown as ~.</footer>\n"
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
    char* saved = tr_unpacked_name(f->name); /* the file is the log's text, also when it came in a .zip or a .gz */
    buf_add(b, "<section class=\"log\">\n<h2>");
    esc(b, f->name);
    buf_add(b, " <small>");
    bytes(b, f->bytes);
    buf_add(b, "</small></h2>\n<p class=\"actions\"><a class=\"button\" href=\"");
    esc(b, file);
    buf_add(b, "\">View the log</a> <a class=\"button quiet\" href=\"");
    esc(b, file);
    buf_add(b, "\" download=\"");
    esc(b, saved);
    buf_add(b, "\">Download</a></p>\n");
    free(saved);
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

/* ---- a run of edenprobe's pieces ---- */

static const char* os_name(const char* os) {
    static const char* const names[][2] = {{"android", "Android"}, {"windows", "Windows"}, {"macos", "macOS"},
                                           {"ios", "iOS"},         {"freebsd", "FreeBSD"}, {"openbsd", "OpenBSD"},
                                           {"netbsd", "NetBSD"},   {"linux", "Linux"}};
    size_t i;
    for (i = 0; os && i < sizeof names / sizeof *names; ++i)
        if (!strcmp(names[i][0], os)) return names[i][1];
    return "system unknown";
}
/* the driver's page name: the ban table's name, or "unknown" */
static const char* driver_key(int id) { return probe_driver_name(id) ? probe_driver_name(id) : "unknown"; }
/* a machine as a column says it: "ScarletNX, RADV 26.1.8, Linux"; a report.jsonl alone, "the console" */
static void machine_text(Buf* b, const Probe* p) {
    if (!strcmp(p->emulator, "console") && !p->driver_id && !p->driver_name) {
        buf_add(b, "the console");
        return;
    }
    esc(b, p->emulator);
    buf_add(b, ", ");
    if (p->driver_id)
        esc(b, probe_driver_label(p->driver_id));
    else
        esc(b, p->driver_name ? p->driver_name : "a driver it does not name");
    if (p->driver_text) {
        buf_add(b, " ");
        esc(b, p->driver_text);
    }
    if (p->inferred && p->driver_id) buf_add(b, " (driver ID inferred)");
    buf_add(b, ", ");
    esc(b, os_name(p->os));
}
static const char* result_name(int result) {
    return result == ST_PASS ? "pass" : result == ST_FAIL ? "fail" : result == ST_CRASHED ? "crashed" : "skip";
}
static const char* result_tone(int result) { return result == ST_PASS ? "ok" : result == ST_FAIL ? "bad" : "warn"; }
/* a case's results on a machine: its failures, crashes, skips and passes as badges, the worst first; empty for none */
static void counts_cell(Buf* b, const int c[4]) {
    static const int order[] = {ST_FAIL, ST_CRASHED, ST_SKIP, ST_PASS};
    size_t k;
    int any = 0;
    for (k = 0; k < sizeof order / sizeof *order; ++k)
        if (c[order[k]]) {
            buf_printf(b, "%s<span class=\"badge %s\">%d %s</span>", any ? " " : "", result_tone(order[k]), c[order[k]],
                       result_name(order[k]));
            any = 1;
        }
}
static int probe_code(const ProbeCase* c) {
    if (!strcmp(c->result, "pass")) return ST_PASS;
    if (!strcmp(c->result, "fail")) return ST_FAIL;
    return !strcmp(c->reason, "crashed") ? ST_CRASHED : ST_SKIP;
}

/* a run of edenprobe on its report's page: what ran where, the counts, and each case that did not pass */
static void probe_section(Buf* b, const Probe* p) {
    char* counts = probe_counts(p);
    char key[17];
    int i, k;
    buf_add(b, "<section class=\"probe\">\n<h2>edenprobe <small>");
    esc(b, counts);
    buf_add(b, "</small></h2>\n<dl class=\"facts\">\n");
    buf_add(b, "<dt>edenprobe</dt><dd>");
    esc(b, p->version);
    if (p->describe) {
        buf_add(b, " (");
        esc(b, p->describe);
        buf_add(b, ")");
    }
    buf_add(b, "</dd>\n");
    field(b, "What ran", *p->words ? p->words : "the families a mode file naming none runs");
    buf_add(b, "<dt>Emulator</dt><dd>");
    esc(b, p->emulator);
    if (p->emulator_version) {
        buf_add(b, " ");
        esc(b, p->emulator_version);
    }
    buf_add(b, "</dd>\n");
    buf_add(b, "<dt>Machine</dt><dd>");
    machine_text(b, p);
    if (p->driver_id)
        buf_printf(b, " <a href=\"../../probe/d/%s.html\">the driver's page</a>", driver_key(p->driver_id));
    buf_add(b, "</dd>\n");
    if (p->driver_name || p->driver_info) {
        buf_add(b, "<dt>Driver</dt><dd>");
        esc(b, p->driver_name ? p->driver_name : "");
        if (p->driver_info) {
            buf_add(b, ", ");
            esc(b, p->driver_info);
        }
        if (p->driver_id)
            buf_printf(b, " (driver ID %d%s)", p->driver_id, p->inferred ? ", inferred from its name" : "");
        buf_add(b, "</dd>\n");
    }
    field(b, "Device", p->device);
    field(b, "Optional extensions on", p->extensions);
    if (!p->whole) field(b, "Summary", "none: the run stopped before its end");
    if (p->cut_cases || p->cut_notes) {
        char* left = rei_format("the detail of %d %s of the service, net and pad families (or of no family edenprobe "
                                "names) and %d net %s, which can name the player",
                                p->cut_cases, p->cut_cases == 1 ? "case" : "cases", p->cut_notes,
                                p->cut_notes == 1 ? "note" : "notes");
        field(b, "Left out", left);
        free(left);
    }
    if (p->unread) {
        char* odd = rei_format("%d lines that are none edenprobe writes", p->unread);
        field(b, "Not read", odd);
        free(odd);
    }
    buf_add(b, "</dl>\n");
    for (k = 0; k < 3; ++k) { /* the failures, the crashes, the skips */
        static const int codes[] = {ST_FAIL, ST_CRASHED, ST_SKIP};
        static const char* const headings[] = {"Failed", "Crashed", "Skipped"};
        int n = 0;
        for (i = 0; i < p->ncases; ++i) n += probe_code(&p->cases[i]) == codes[k];
        if (!n) continue;
        buf_printf(b,
                   "<h3>%s: %d</h3>\n<div class=\"scroll\"><table class=\"reports\">\n<thead><tr><th>Suite</th>"
                   "<th>Case</th><th>Tier</th><th>Detail</th></tr></thead>\n<tbody>\n",
                   headings[k], n);
        for (i = 0; i < p->ncases; ++i) {
            const ProbeCase* c = &p->cases[i];
            if (probe_code(c) != codes[k]) continue;
            probe_case_key(c->suite, c->name, key);
            buf_add(b, "<tr><td class=\"mono\">");
            esc(b, c->suite);
            buf_printf(b, "</td><td><a href=\"../../probe/c/%s.html\">", key);
            esc(b, c->name);
            buf_add(b, "</a></td><td class=\"mono\">");
            esc(b, c->tier);
            buf_add(b, "</td><td class=\"mono\">");
            if (*c->reason) {
                esc(b, c->reason);
                if (*c->detail) buf_add(b, ": ");
            }
            esc(b, c->detail);
            buf_add(b, "</td></tr>\n");
        }
        buf_add(b, "</tbody>\n</table></div>\n");
    }
    buf_add(b, "</section>\n");
    free(counts);
}

/* a run of edenprobe in its report's JSON: the run without its passes, each other case with its page */
static cJSON* probe_object(const Probe* p) {
    cJSON *o = probe_json(p), *cases = cJSON_DetachItemFromObject(o, "cases"), *kept = cJSON_CreateArray(), *c;
    char* counts = probe_counts(p);
    char key[17];
    jset(o, "counts", counts);
    jset(o, "driver", probe_driver_name(p->driver_id));
    cJSON_ArrayForEach(c, cases) {
        const char* result = jstr(c, "result");
        if (result && !strcmp(result, "pass")) continue;
        probe_case_key(jstr(c, "suite"), jstr(c, "name"), key);
        jset(c, "case", key);
        cJSON_AddItemToArray(kept, cJSON_Duplicate(c, 1));
    }
    cJSON_AddNumberToObject(o, "cases", (double)p->ncases);
    cJSON_AddItemToObject(o, "not_passed", kept);
    cJSON_Delete(cases);
    free(counts);
    return o;
}

char* site_report_html(const StReport* r, const StFile* files, int nfiles, const StNote* notes, int nnotes,
                       const Probe* probe) {
    Buf b = {0}, title = {0};
    int i, images = 0, videos = 0;
    buf_printf(&title, "Report #%ld: %s", r->id, r->title);
    head(&b, "../../", title.text,
         probe ? "A run of edenprobe sent to Tracer." : "A ScarletNX game report sent to Tracer.");
    buf_free(&title);
    if (probe)
        buf_add(&b, "<section class=\"hero\">\n<p class=\"crumbs\"><a href=\"../../probe/index.html\">Probe</a> / ");
    else
        buf_printf(&b,
                   "<section class=\"hero\">\n<p class=\"crumbs\"><a href=\"../../index.html\">Games</a> / "
                   "<a href=\"../../g/%s.html\">",
                   r->key);
    esc(&b, r->title);
    buf_printf(&b, "%s</p>\n<h1>Report #%ld</h1>\n<p class=\"meta\">Sent ", probe ? "" : "</a>", r->id);
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
    if (probe) probe_section(&b, probe);
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

char* site_report_json(const StReport* r, const StFile* files, int nfiles, const StNote* notes, int nnotes,
                       const Probe* probe) {
    cJSON *o = report_object(r), *later = cJSON_AddArrayToObject(o, "notes_added"),
          *list = cJSON_AddArrayToObject(o, "files");
    int i;
    if (probe) cJSON_AddItemToObject(o, "probe", probe_object(probe));
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
    Probe probe;
    int nfiles, nnotes = 0, done = -1, run = 0;
    if (st_get(s, id, &r, &files, &nfiles) == 1) {
        char* rel = rei_format("r/%ld/index.html", id);
        st_notes(s, id, &notes, &nnotes);
        if (!strcmp(r.key, PROBE_KEY)) run = st_probe_get(s, id, &probe, 1) == 1;
        done = write_page(root, rel, site_report_html(&r, files, nfiles, notes, nnotes, run ? &probe : NULL));
        free(rel);
        rel = rei_format("api/r/%ld.json", id);
        done |= write_page(root, rel, site_report_json(&r, files, nfiles, notes, nnotes, run ? &probe : NULL));
        free(rel);
        if (run) probe_free(&probe);
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
    int n, i, games_n = 0, done = write_page(root, "index.html", site_index_html(games, ngames));
    done |= write_page(root, "api/games.json", site_games_json(games, ngames));
    if (st_reports(s, NULL, &all, &n)) return -1;
    for (i = 0; i < n; ++i) /* the game reports: the runs of edenprobe have the Probe pages */
        if (strcmp(all[i].key, PROBE_KEY)) {
            StReport keep = all[i];
            all[i] = all[games_n];
            all[games_n++] = keep;
        }
    done |= write_page(root, "all.html", site_all_html(all, games_n));
    done |= write_page(root, "api/reports.json", site_reports_json(all, games_n));
    st_reports_free(all, n);
    return done;
}

int site_update(Store* s, const char* root, long id, const char* key) {
    StGame* games;
    int n, done = 0, probe = key && !strcmp(key, PROBE_KEY);
    if (id) done |= report_page(s, root, id);
    if (st_games(s, &games, &n)) return -1;
    if (!probe) done |= game_page(s, root, key, games, n);
    done |= lists(s, root, games, n);
    st_games_free(games, n);
    if (probe) done |= site_probe(s, root); /* a run came, changed or went: every page of the runs is drawn again */
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
    done |= site_probe(s, root);
    st_reports_free(all, n);
    st_games_free(games, ngames);
    return done ? -1 : 0;
}

/* ---- the runs of edenprobe's pages ---- */

#define CASE_ROWS 200 /* a case's failures and skips its page lists, the newest */
#define SITE_RULES                                                                                                     \
    "https://github.com/scarletnx/wimsy-core/blob/main/docs/TRACER.md#what-the-runs-say-together" /* the rules, said */

typedef struct {
    const Probe* p; /* a run's of it: the machine's fields */
    int runs;
    double last;
} Machine;

typedef struct {
    StProbeRun* runs;
    int nruns;
    StProbeName* names;
    int nnames;
    StProbeResult* results;
    int nresults;
    Rules rules;
    Machine* machines;
    int nmachines;
    int* machine_of; /* each run's machine */
    int* order;      /* the runs by report, for run_of */
    int* cells;      /* each case's results (4 a machine, ST_*) on each machine: nnames * nmachines * 4 */
    int* totals;     /* each case's results: nnames * 4 */
} Runs;

static const Runs* g_site_runs; /* for the comparators, during one site_probe */

static int emulator_rank(const char* e) {
    return !strcmp(e, "ScarletNX") ? 0 : !strcmp(e, "Eden") ? 1 : !strcmp(e, "rEden") ? 2 : 3;
}
static int same_machine(const Probe* p, const Probe* q) {
    return !strcmp(p->emulator, q->emulator) && p->driver_id == q->driver_id &&
           p->driver_version == q->driver_version && !strcmp(p->os, q->os) && p->inferred == q->inferred &&
           (p->driver_id || !strcmp(p->driver_name ? p->driver_name : "", q->driver_name ? q->driver_name : ""));
}
static int by_machine(const void* a, const void* b) {
    const Probe *p = ((const Machine*)a)->p, *q = ((const Machine*)b)->p;
    int c = emulator_rank(p->emulator) - emulator_rank(q->emulator);
    if (!c) c = p->driver_id - q->driver_id;
    if (!c && p->driver_version != q->driver_version) c = p->driver_version < q->driver_version ? -1 : 1;
    if (!c) c = strcmp(p->os, q->os);
    return c;
}
static int by_run_report(const void* a, const void* b) {
    long x = g_site_runs->runs[*(const int*)a].report, y = g_site_runs->runs[*(const int*)b].report;
    return x < y ? -1 : x > y;
}
/* the run of report, its index; -1 for none */
static int run_of(const Runs* r, long report) {
    int lo = 0, hi = r->nruns - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        long at = r->runs[r->order[mid]].report;
        if (at == report) return r->order[mid];
        if (at < report)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}
/* the case of id, its index in names; -1 for none */
static int name_of(const Runs* r, int id) {
    int lo = 0, hi = r->nnames - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (r->names[mid].id == id) return mid;
        if (r->names[mid].id < id)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}
static void runs_free(Runs* r) {
    rules_free(&r->rules);
    st_probe_runs_free(r->runs, r->nruns);
    st_probe_names_free(r->names, r->nnames);
    free(r->results);
    free(r->machines);
    free(r->machine_of);
    free(r->order);
    free(r->cells);
    free(r->totals);
    memset(r, 0, sizeof *r);
}
static void* zeroed(size_t n, size_t size) {
    void* p = calloc(n ? n : 1, size);
    if (!p) {
        fputs(REI_NAME ": out of memory\n", stderr);
        abort();
    }
    return p;
}
/* every run, case and result, the machines they ran on, each case's results by machine, and the rules: 0, or -1 */
static int runs_load(Store* s, Runs* r) {
    int i, k;
    memset(r, 0, sizeof *r);
    if (st_probe_runs(s, &r->runs, &r->nruns) || st_probe_names(s, &r->names, &r->nnames) ||
        st_probe_results(s, &r->results, &r->nresults) ||
        rules_make(r->runs, r->nruns, r->results, r->nresults, &r->rules)) {
        runs_free(r);
        return -1;
    }
    r->machines = zeroed((size_t)r->nruns, sizeof *r->machines);
    r->machine_of = zeroed((size_t)r->nruns, sizeof *r->machine_of);
    r->order = zeroed((size_t)r->nruns, sizeof *r->order);
    for (i = 0; i < r->nruns; ++i) { /* the machines, each once */
        for (k = 0; k < r->nmachines && !same_machine(r->machines[k].p, &r->runs[i].p); ++k) continue;
        if (k == r->nmachines) r->machines[r->nmachines++].p = &r->runs[i].p;
    }
    qsort(r->machines, (size_t)r->nmachines, sizeof *r->machines, by_machine);
    for (i = 0; i < r->nruns; ++i) {
        for (k = 0; !same_machine(r->machines[k].p, &r->runs[i].p); ++k) continue;
        r->machine_of[i] = k;
        ++r->machines[k].runs;
        if (r->runs[i].when > r->machines[k].last) r->machines[k].last = r->runs[i].when;
        r->order[i] = i;
    }
    g_site_runs = r;
    qsort(r->order, (size_t)r->nruns, sizeof *r->order, by_run_report);
    r->cells = zeroed((size_t)r->nnames * (size_t)(r->nmachines ? r->nmachines : 1) * 4, sizeof *r->cells);
    r->totals = zeroed((size_t)r->nnames * 4, sizeof *r->totals);
    for (i = 0; i < r->nresults; ++i) {
        int run = run_of(r, r->results[i].report), n = name_of(r, r->results[i].name), res = r->results[i].result;
        if (run < 0 || n < 0 || res < ST_PASS || res > ST_CRASHED) continue;
        ++r->cells[((size_t)n * (size_t)r->nmachines + (size_t)r->machine_of[run]) * 4 + (size_t)res];
        ++r->totals[(size_t)n * 4 + (size_t)res];
    }
    return 0;
}
static const int* cell_of(const Runs* r, int n, int m) {
    return &r->cells[((size_t)n * (size_t)r->nmachines + (size_t)m) * 4];
}
static int fails_somewhere(const Runs* r, int n) {
    return r->totals[(size_t)n * 4 + ST_FAIL] || r->totals[(size_t)n * 4 + ST_CRASHED];
}
/* a machine's label for a column's head: the emulator, then the driver and its version, then the system */
static void machine_head(Buf* b, const Probe* p) {
    if (!strcmp(p->emulator, "console") && !p->driver_id && !p->driver_name) {
        buf_add(b, "the console");
        return;
    }
    esc(b, p->emulator);
    buf_add(b, "<br>");
    esc(b, p->driver_id ? probe_driver_name(p->driver_id) : p->driver_name ? p->driver_name : "driver unknown");
    if (p->driver_text) {
        buf_add(b, " ");
        esc(b, p->driver_text);
    }
    if (p->inferred && p->driver_id) buf_add(b, "*");
    buf_add(b, "<br>");
    esc(b, os_name(p->os));
}
static void run_counts(Buf* b, const Probe* p) {
    int c[4] = {0, 0, 0, 0};
    c[ST_PASS] = (int)p->passed;
    c[ST_FAIL] = (int)p->failed;
    c[ST_SKIP] = (int)p->skipped;
    c[ST_CRASHED] = (int)p->crashed;
    if (p->passed < 0)
        buf_add(b, "<span class=\"badge warn\">no summary</span>");
    else
        counts_cell(b, c);
}
/* the runs' table: each run with its report, root before each link */
static void runs_table(Buf* b, const char* root, const Runs* r, const int* which, int n) {
    int i;
    buf_add(b,
            "<div class=\"scroll\"><table class=\"reports\">\n<thead><tr><th>Report</th><th>Sent</th><th>edenprobe</th>"
            "<th>What ran</th><th>Machine</th><th>Counts</th></tr></thead>\n<tbody>\n");
    for (i = 0; i < n; ++i) {
        const StProbeRun* run = &r->runs[which ? which[i] : i];
        buf_printf(b, "<tr><td><a href=\"%sr/%ld/\">#%ld</a></td><td>", root, run->report, run->report);
        when(b, run->when);
        buf_add(b, "</td><td class=\"mono\">");
        esc(b, run->p.version);
        buf_add(b, "</td><td class=\"mono\">");
        esc(b, run->p.words);
        buf_add(b, "</td><td>");
        machine_text(b, &run->p);
        buf_add(b, "</td><td>");
        run_counts(b, &run->p);
        buf_add(b, "</td></tr>\n");
    }
    buf_add(b, "</tbody>\n</table></div>\n");
}
static cJSON* machine_object(const Machine* m) {
    cJSON* o = cJSON_CreateObject();
    Buf label = {0};
    machine_text(&label, m->p);
    jset(o, "machine", label.text);
    jset(o, "emulator", m->p->emulator);
    jset(o, "driver", probe_driver_name(m->p->driver_id));
    cJSON_AddNumberToObject(o, "driver_id", m->p->driver_id);
    cJSON_AddBoolToObject(o, "inferred", m->p->inferred);
    jset(o, "driver_name", m->p->driver_name);
    jset(o, "version", m->p->driver_text);
    jset(o, "os", m->p->os);
    cJSON_AddNumberToObject(o, "runs", m->runs);
    buf_free(&label);
    return o;
}
static cJSON* counts_array(const int c[4]) {
    int k;
    cJSON* a = cJSON_CreateArray();
    for (k = 0; k < 4; ++k) cJSON_AddItemToArray(a, cJSON_CreateNumber(c[k]));
    return a;
}

/* probe/index.html and api/probe/matrix.json */
static int probe_index(const char* root, const Runs* r) {
    static const char* const keys[] = {"family", "suite"};
    static const char* const names[] = {"Family", "Suite"};
    Buf b = {0};
    cJSON *o = cJSON_CreateObject(), *machines = cJSON_AddArrayToObject(o, "machines"),
          *cases = cJSON_AddArrayToObject(o, "cases");
    int i, m, failing = 0, done;
    char* suites_seen = NULL;
    for (i = 0; i < r->nnames; ++i) failing += fails_somewhere(r, i);
    head(&b, "../", "Tracer: runs of edenprobe",
         "Runs of edenprobe sent to Tracer: each case on each emulator and driver.");
    buf_add(&b, "<section class=\"hero\">\n<h1>Runs of edenprobe</h1>\n<p>Runs of <a "
                "href=\"https://github.com/scarletnx/edenprobe\">edenprobe</a> that players sent to Tracer on Discord: "
                "which of its cases pass on each emulator, driver and system. Every version and mode of edenprobe is "
                "counted together here; a case's page tells them apart. What the runs say together, by fixed rules: "
                "<a href=\"candidates.html\">ban candidates, ScarletNX's own failures and regressions</a>.</p>\n");
    buf_printf(&b,
               "<p class=\"stats\"><b>%d</b> %s on <b>%d</b> %s, <b>%d</b> %s, <b>%d</b> failing somewhere</p>\n"
               "</section>\n",
               r->nruns, r->nruns == 1 ? "run" : "runs", r->nmachines, r->nmachines == 1 ? "machine" : "machines",
               r->nnames, r->nnames == 1 ? "case" : "cases", failing);
    cJSON_AddNumberToObject(o, "runs", r->nruns);
    if (!r->nruns) {
        buf_add(&b, "<p class=\"empty\">No runs yet. Mention <code>@Tracer</code> with a log of a run of edenprobe "
                    "attached to add one.</p>\n");
        foot(&b);
        done = write_page(root, "probe/index.html", buf_take(&b));
        done |= write_page(root, "api/probe/matrix.json", printed(o));
        return done;
    }
    buf_add(&b, "<h2>Machines</h2>\n<div class=\"scroll\"><table class=\"reports\">\n<thead><tr><th>Machine</th>"
                "<th>Runs</th><th>The latest</th></tr></thead>\n<tbody>\n");
    for (m = 0; m < r->nmachines; ++m) {
        const Probe* p = r->machines[m].p;
        buf_add(&b, "<tr><td>");
        machine_text(&b, p);
        if (p->driver_id) buf_printf(&b, " <a href=\"d/%s.html\">the driver</a>", driver_key(p->driver_id));
        buf_printf(&b, "</td><td>%d</td><td>", r->machines[m].runs);
        when(&b, r->machines[m].last);
        buf_add(&b, "</td></tr>\n");
        cJSON_AddItemToArray(machines, machine_object(&r->machines[m]));
    }
    buf_add(&b, "</tbody>\n</table></div>\n<p class=\"meta\">* the driver ID read from the driver's name: Eden logs "
                "none.</p>\n");
    /* the cases that fail somewhere, a row each, a column for each machine */
    buf_printf(&b, "<h2>Cases that fail somewhere <small>%d of %d</small></h2>\n", failing, r->nnames);
    filters(&b, "cases", "Search cases or suites", keys, names, 2);
    buf_add(&b, "<div class=\"scroll\"><table class=\"reports\" id=\"cases\" data-list>\n<thead><tr><th>Case</th>");
    for (m = 0; m < r->nmachines; ++m) {
        buf_add(&b, "<th>");
        machine_head(&b, r->machines[m].p);
        buf_add(&b, "</th>");
    }
    buf_add(&b, "</tr></thead>\n<tbody>\n");
    for (i = 0; i < r->nnames; ++i) {
        const StProbeName* n = &r->names[i];
        cJSON *c = cJSON_CreateObject(), *results;
        const char* family = probe_family_name(probe_family(n->suite));
        jset(c, "case", n->key);
        jset(c, "suite", n->suite);
        jset(c, "family", family);
        jset(c, "name", n->name);
        cJSON_AddItemToObject(c, "total", counts_array(&r->totals[(size_t)i * 4]));
        results = cJSON_AddArrayToObject(c, "results");
        for (m = 0; m < r->nmachines; ++m) cJSON_AddItemToArray(results, counts_array(cell_of(r, i, m)));
        cJSON_AddItemToArray(cases, c);
        if (!fails_somewhere(r, i)) continue;
        buf_add(&b, "<tr data-text=\"");
        esc_lower(&b, n->suite);
        esc_lower(&b, n->name);
        buf_printf(&b, "\" data-family=\"%s\" data-suite=\"", family);
        esc(&b, n->suite);
        buf_printf(&b, "\"><td><span class=\"id\">");
        esc(&b, n->suite);
        buf_printf(&b, "</span><br><a href=\"c/%s.html\">", n->key);
        esc(&b, n->name);
        buf_add(&b, "</a></td>");
        for (m = 0; m < r->nmachines; ++m) {
            buf_add(&b, "<td>");
            counts_cell(&b, cell_of(r, i, m));
            buf_add(&b, "</td>");
        }
        buf_add(&b, "</tr>\n");
    }
    buf_add(&b, "</tbody>\n</table></div>\n<p class=\"empty\" data-empty hidden>No case matches.</p>\n");
    /* each suite: its cases' results summed on each machine */
    buf_add(&b, "<h2>Suites</h2>\n<div class=\"scroll\"><table class=\"reports\">\n<thead><tr><th>Suite</th>");
    for (m = 0; m < r->nmachines; ++m) {
        buf_add(&b, "<th>");
        machine_head(&b, r->machines[m].p);
        buf_add(&b, "</th>");
    }
    buf_add(&b, "</tr></thead>\n<tbody>\n");
    suites_seen = zeroed((size_t)r->nnames, 1);
    for (i = 0; i < r->nnames; ++i) {
        int k;
        if (suites_seen[i]) continue;
        buf_add(&b, "<tr><td><span class=\"mono\">");
        esc(&b, r->names[i].suite);
        buf_printf(&b, "</span> <small>%s</small></td>", probe_family_name(probe_family(r->names[i].suite)));
        for (m = 0; m < r->nmachines; ++m) {
            int sum[4] = {0, 0, 0, 0}, x;
            for (k = i; k < r->nnames; ++k)
                if (!strcmp(r->names[k].suite, r->names[i].suite))
                    for (x = 0; x < 4; ++x) sum[x] += cell_of(r, k, m)[x];
            buf_add(&b, "<td>");
            counts_cell(&b, sum);
            buf_add(&b, "</td>");
        }
        for (k = i; k < r->nnames; ++k) suites_seen[k] |= !strcmp(r->names[k].suite, r->names[i].suite);
        buf_add(&b, "</tr>\n");
    }
    free(suites_seen);
    buf_add(&b, "</tbody>\n</table></div>\n<h2>Runs</h2>\n");
    runs_table(&b, "../", r, NULL, r->nruns);
    foot(&b);
    done = write_page(root, "probe/index.html", buf_take(&b));
    done |= write_page(root, "api/probe/matrix.json", printed(o));
    return done;
}

/* the rules' entries about case n: links into candidates.html */
static void rules_of_case(Buf* b, const Runs* r, int id) {
    int i, any = 0;
    for (i = 0; i < r->rules.ncandidates; ++i)
        if (r->rules.candidates[i].name == id) {
            const RuleCandidate* c = &r->rules.candidates[i];
            buf_printf(b, "%s<li><a href=\"../candidates.html#candidate-%d\">a ban candidate</a> on %s %s, %s</li>\n",
                       any++ ? "" : "<h2>What the runs say</h2>\n<ul>\n", i + 1, probe_driver_label(c->driver_id),
                       c->versions, os_name(c->os));
        }
    for (i = 0; i < r->rules.ngeneral; ++i)
        if (r->rules.general[i].name == id)
            buf_printf(b,
                       "%s<li><a href=\"../candidates.html#general-%d\">ScarletNX's own failure</a>, on %d drivers "
                       "(edenprobe %s)</li>\n",
                       any++ ? "" : "<h2>What the runs say</h2>\n<ul>\n", i + 1, r->rules.general[i].ndrivers,
                       r->rules.general[i].version);
    for (i = 0; i < r->rules.nregressions; ++i)
        if (r->rules.regressions[i].name == id)
            buf_printf(b, "%s<li><a href=\"../candidates.html#regression-%d\">a regression</a> on %s</li>\n",
                       any++ ? "" : "<h2>What the runs say</h2>\n<ul>\n", i + 1,
                       probe_driver_label(r->rules.regressions[i].driver_id));
    if (any) buf_add(b, "</ul>\n");
}

/* probe/c/<key>.html and its JSON, case n */
static int case_page(Store* s, const char* root, const Runs* r, int n) {
    const StProbeName* name = &r->names[n];
    const char* family = probe_family_name(probe_family(name->suite));
    StProbeDetail* d = NULL;
    int nd = 0, i, m, done;
    Buf b = {0}, title = {0};
    char* rel;
    cJSON *o = cJSON_CreateObject(), *machines = cJSON_AddArrayToObject(o, "machines"), *rows;
    st_probe_details(s, name->id, CASE_ROWS, &d, &nd);
    buf_printf(&title, "%s: %s", name->suite, name->name);
    head(&b, "../../", title.text, "A case of edenprobe on each emulator and driver, from the runs sent to Tracer.");
    buf_free(&title);
    buf_add(&b, "<section class=\"hero\">\n<p class=\"crumbs\"><a href=\"../index.html\">Probe</a></p>\n<h1>");
    esc(&b, name->name);
    buf_add(&b, "</h1>\n<p class=\"id\">");
    esc(&b, name->suite);
    buf_printf(&b, " · %s</p>\n<p class=\"stats\">", family);
    counts_cell(&b, &r->totals[(size_t)n * 4]);
    buf_add(&b, "</p>\n</section>\n");
    jset(o, "case", name->key);
    jset(o, "suite", name->suite);
    jset(o, "family", family);
    jset(o, "name", name->name);
    cJSON_AddItemToObject(o, "total", counts_array(&r->totals[(size_t)n * 4]));
    buf_add(&b, "<h2>By machine</h2>\n<div class=\"scroll\"><table class=\"reports\">\n<thead><tr><th>Machine</th>"
                "<th>Results</th></tr></thead>\n<tbody>\n");
    for (m = 0; m < r->nmachines; ++m) {
        const int* c = cell_of(r, n, m);
        cJSON* e;
        if (!c[0] && !c[1] && !c[2] && !c[3]) continue;
        buf_add(&b, "<tr><td>");
        machine_text(&b, r->machines[m].p);
        buf_add(&b, "</td><td>");
        counts_cell(&b, c);
        buf_add(&b, "</td></tr>\n");
        e = machine_object(&r->machines[m]);
        cJSON_AddItemToObject(e, "results", counts_array(c));
        cJSON_AddItemToArray(machines, e);
    }
    buf_add(&b, "</tbody>\n</table></div>\n");
    rules_of_case(&b, r, name->id);
    rows = cJSON_AddArrayToObject(o, "not_passed");
    if (nd) {
        buf_printf(&b,
                   "<h2>Failures and skips <small>the newest %d at most</small></h2>\n<div class=\"scroll\"><table "
                   "class=\"reports\">\n<thead><tr><th>Report</th><th>Sent</th><th>Machine</th><th>edenprobe</th>"
                   "<th>Result</th><th>Detail</th></tr></thead>\n<tbody>\n",
                   CASE_ROWS);
        for (i = 0; i < nd; ++i) {
            int run = run_of(r, d[i].report);
            const StProbeRun* x = run >= 0 ? &r->runs[run] : NULL;
            cJSON* e = cJSON_CreateObject();
            Buf label = {0};
            if (!x) {
                cJSON_Delete(e);
                continue;
            }
            machine_text(&label, &x->p);
            buf_printf(&b, "<tr><td><a href=\"../../r/%ld/\">#%ld</a></td><td>", x->report, x->report);
            when(&b, x->when);
            buf_printf(&b, "</td><td>%s</td><td class=\"mono\">", label.text);
            esc(&b, x->p.version);
            buf_add(&b, " ");
            esc(&b, x->p.words);
            buf_printf(&b, "</td><td><span class=\"badge %s\">%s</span></td><td class=\"mono\">",
                       result_tone(d[i].result), result_name(d[i].result));
            if (*d[i].reason) {
                esc(&b, d[i].reason);
                if (*d[i].detail) buf_add(&b, ": ");
            }
            esc(&b, d[i].detail);
            buf_add(&b, "</td></tr>\n");
            cJSON_AddNumberToObject(e, "report", (double)x->report);
            jset(e, "machine", label.text);
            jset(e, "emulator", x->p.emulator);
            jset(e, "edenprobe", x->p.version);
            jset(e, "words", x->p.words);
            jset(e, "result", result_name(d[i].result));
            if (*d[i].reason) jset(e, "reason", d[i].reason);
            if (*d[i].detail) jset(e, "detail", d[i].detail);
            cJSON_AddItemToArray(rows, e);
            buf_free(&label);
        }
        buf_add(&b, "</tbody>\n</table></div>\n");
    }
    foot(&b);
    st_probe_details_free(d, nd);
    rel = rei_format("probe/c/%s.html", name->key);
    done = write_page(root, rel, buf_take(&b));
    free(rel);
    rel = rei_format("api/probe/c/%s.json", name->key);
    done |= write_page(root, rel, printed(o));
    free(rel);
    return done;
}

/* a candidate's facts: its case, driver, range, the reports either way, Eden, the extensions, the draft line */
static void candidate_block(Buf* b, const char* root, const Runs* r, int i) {
    const RuleCandidate* c = &r->rules.candidates[i];
    int n = name_of(r, c->name), k;
    char* draft = n >= 0 ? rules_draft(c, r->names[n].suite, r->names[n].name) : NULL;
    if (n < 0) return;
    buf_printf(b, "<h3 id=\"candidate-%d\"><a href=\"%sprobe/c/%s.html\">", i + 1, root, r->names[n].key);
    esc(b, r->names[n].suite);
    buf_add(b, ": ");
    esc(b, r->names[n].name);
    buf_printf(b,
               "</a></h3>\n<dl class=\"facts\">\n<dt>Driver</dt><dd><a href=\"%sprobe/d/%s.html\">%s</a> on %s</dd>\n",
               root, driver_key(c->driver_id), probe_driver_label(c->driver_id), os_name(c->os));
    buf_add(b, "<dt>Versions</dt><dd class=\"mono\">");
    esc(b, c->versions);
    if (c->mixed) buf_add(b, " (it both passes and fails on the version that ends it)");
    buf_add(b, "</dd>\n<dt>edenprobe</dt><dd class=\"mono\">");
    esc(b, c->version);
    buf_add(b, " ");
    esc(b, c->words);
    buf_printf(b, "</dd>\n<dt>Fails</dt><dd>%d reports from %d people:", c->nfailing, c->people);
    for (k = 0; k < c->nfailing; ++k)
        buf_printf(b, " <a href=\"%sr/%ld/\">#%ld</a>", root, c->failing[k], c->failing[k]);
    buf_printf(b, "</dd>\n<dt>Passes elsewhere</dt><dd>%d reports of %d other drivers:", c->npassing, c->pass_drivers);
    for (k = 0; k < c->npassing; ++k)
        buf_printf(b, " <a href=\"%sr/%ld/\">#%ld</a>", root, c->passing[k], c->passing[k]);
    buf_printf(b, "</dd>\n<dt>Eden on this driver</dt><dd>%d %s, %d %s</dd>\n", c->eden_fail,
               c->eden_fail == 1 ? "run fails it" : "runs fail it", c->eden_pass,
               c->eden_pass == 1 ? "passes it" : "pass it");
    buf_add(b, "<dt>Optional extensions on in every failing run</dt><dd class=\"mono\">");
    esc(b, c->extensions ? (*c->extensions ? c->extensions : "none in common")
                         : "a failing run's log does not list them (vulkan: optional extensions on)");
    buf_add(b, "</dd>\n</dl>\n<pre>");
    esc(b, draft);
    buf_add(b, "</pre>\n");
    free(draft);
}
static cJSON* candidate_object(const Runs* r, const RuleCandidate* c) {
    cJSON *o = cJSON_CreateObject(), *fail = cJSON_AddArrayToObject(o, "failing"), *pass;
    int n = name_of(r, c->name), k;
    char* draft = n >= 0 ? rules_draft(c, r->names[n].suite, r->names[n].name) : NULL;
    if (n >= 0) {
        jset(o, "case", r->names[n].key);
        jset(o, "suite", r->names[n].suite);
        jset(o, "name", r->names[n].name);
    }
    jset(o, "driver", probe_driver_name(c->driver_id));
    cJSON_AddNumberToObject(o, "driver_id", c->driver_id);
    jset(o, "os", c->os);
    jset(o, "versions", c->versions);
    cJSON_AddBoolToObject(o, "mixed", c->mixed);
    jset(o, "broken", c->broken);
    jset(o, "edenprobe", c->version);
    jset(o, "words", c->words);
    for (k = 0; k < c->nfailing; ++k) cJSON_AddItemToArray(fail, cJSON_CreateNumber((double)c->failing[k]));
    cJSON_AddNumberToObject(o, "people", c->people);
    pass = cJSON_AddArrayToObject(o, "passing");
    for (k = 0; k < c->npassing; ++k) cJSON_AddItemToArray(pass, cJSON_CreateNumber((double)c->passing[k]));
    cJSON_AddNumberToObject(o, "pass_drivers", c->pass_drivers);
    cJSON_AddNumberToObject(o, "eden_fail", c->eden_fail);
    cJSON_AddNumberToObject(o, "eden_pass", c->eden_pass);
    jset(o, "extensions", c->extensions);
    jset(o, "draft", draft);
    free(draft);
    return o;
}

/* probe/candidates.html and api/probe/candidates.json */
static int candidates_page(const char* root, const Runs* r) {
    Buf b = {0};
    cJSON *o = cJSON_CreateObject(), *list = cJSON_AddArrayToObject(o, "candidates"),
          *general = cJSON_AddArrayToObject(o, "general"), *regressions = cJSON_AddArrayToObject(o, "regressions");
    int i, k, done;
    head(&b, "../", "Tracer: what the runs of edenprobe say",
         "Ban candidates, ScarletNX's own failures and regressions, from the runs of edenprobe sent to Tracer.");
    buf_printf(
        &b,
        "<section class=\"hero\">\n<p class=\"crumbs\"><a href=\"index.html\">Probe</a></p>\n<h1>What the runs "
        "say together</h1>\n<p>By <a href=\"" SITE_RULES "\">fixed rules</a> over ScarletNX's runs of each case "
        "of each version and mode of edenprobe, Eden's beside them. Nothing here is applied: a ban candidate's "
        "line goes into ScarletNX's ban table (src/nx/gpu/vk/extension_bans.txt) once a person has chosen its key "
        "and checked it.</p>\n<p class=\"stats\"><b>%d</b> ban %s, <b>%d</b> of ScarletNX's own %s, <b>%d</b> "
        "%s</p>\n</section>\n",
        r->rules.ncandidates, r->rules.ncandidates == 1 ? "candidate" : "candidates", r->rules.ngeneral,
        r->rules.ngeneral == 1 ? "failure" : "failures", r->rules.nregressions,
        r->rules.nregressions == 1 ? "regression" : "regressions");
    buf_printf(&b, "<h2>Ban candidates <small>%d</small></h2>\n", r->rules.ncandidates);
    if (!r->rules.ncandidates)
        buf_printf(&b,
                   "<p class=\"empty\">None: no case fails on one driver in %d reports from %d people and passes on "
                   "%d others.</p>\n",
                   RULE_FAIL_REPORTS, RULE_FAIL_PEOPLE, RULE_PASS_DRIVERS);
    for (i = 0; i < r->rules.ncandidates; ++i) {
        candidate_block(&b, "../", r, i);
        cJSON_AddItemToArray(list, candidate_object(r, &r->rules.candidates[i]));
    }
    buf_printf(&b, "<h2>ScarletNX's own failures <small>%d</small></h2>\n", r->rules.ngeneral);
    if (r->rules.ngeneral)
        buf_add(&b, "<div class=\"scroll\"><table class=\"reports\">\n<thead><tr><th>Case</th><th>edenprobe</th>"
                    "<th>Fails on</th><th>Reports</th><th>Eden on those drivers</th></tr></thead>\n<tbody>\n");
    for (i = 0; i < r->rules.ngeneral; ++i) {
        const RuleGeneral* g = &r->rules.general[i];
        int n = name_of(r, g->name);
        cJSON *e = cJSON_CreateObject(), *drivers = cJSON_AddArrayToObject(e, "drivers"), *reports;
        if (n < 0) {
            cJSON_Delete(e);
            continue;
        }
        buf_printf(&b, "<tr id=\"general-%d\"><td><span class=\"id\">", i + 1);
        esc(&b, r->names[n].suite);
        buf_printf(&b, "</span><br><a href=\"c/%s.html\">", r->names[n].key);
        esc(&b, r->names[n].name);
        buf_add(&b, "</a></td><td class=\"mono\">");
        esc(&b, g->version);
        buf_add(&b, " ");
        esc(&b, g->words);
        buf_add(&b, "</td><td>");
        for (k = 0; k < g->ndrivers; ++k) {
            buf_printf(&b, "%s<a href=\"d/%s.html\">%s</a>", k ? ", " : "", driver_key(g->drivers[k]),
                       driver_key(g->drivers[k]));
            cJSON_AddItemToArray(drivers, cJSON_CreateString(driver_key(g->drivers[k])));
        }
        buf_printf(&b, " of the %d seen</td><td>", g->seen);
        for (k = 0; k < g->nreports && k < 12; ++k)
            buf_printf(&b, "%s<a href=\"../r/%ld/\">#%ld</a>", k ? " " : "", g->reports[k], g->reports[k]);
        if (g->nreports > 12) buf_printf(&b, " and %d more", g->nreports - 12);
        buf_printf(&b, "</td><td>%s</td></tr>\n",
                   !g->eden_runs                  ? "no run"
                   : g->eden_pass == g->eden_runs ? "passes it: the failure is ScarletNX's to fix"
                   : g->eden_pass                 ? "passes it in some runs"
                                                  : "fails it too");
        jset(e, "case", r->names[n].key);
        jset(e, "suite", r->names[n].suite);
        jset(e, "name", r->names[n].name);
        jset(e, "edenprobe", g->version);
        jset(e, "words", g->words);
        cJSON_AddNumberToObject(e, "seen", g->seen);
        reports = cJSON_AddArrayToObject(e, "reports");
        for (k = 0; k < g->nreports; ++k) cJSON_AddItemToArray(reports, cJSON_CreateNumber((double)g->reports[k]));
        cJSON_AddNumberToObject(e, "eden_runs", g->eden_runs);
        cJSON_AddNumberToObject(e, "eden_pass", g->eden_pass);
        cJSON_AddItemToArray(general, e);
    }
    if (r->rules.ngeneral) buf_add(&b, "</tbody>\n</table></div>\n");
    buf_printf(&b, "<h2>Regressions <small>%d</small></h2>\n", r->rules.nregressions);
    if (r->rules.nregressions)
        buf_add(&b, "<div class=\"scroll\"><table class=\"reports\">\n<thead><tr><th>Case</th><th>Driver</th>"
                    "<th>Passed on</th><th>Fails from</th><th>Builds that fail</th></tr></thead>\n<tbody>\n");
    for (i = 0; i < r->rules.nregressions; ++i) {
        const RuleRegression* g = &r->rules.regressions[i];
        int n = name_of(r, g->name);
        char version[32];
        cJSON* e = cJSON_CreateObject();
        if (n < 0) {
            cJSON_Delete(e);
            continue;
        }
        probe_version_text(g->driver_id, g->driver_version, version, sizeof version);
        buf_printf(&b, "<tr id=\"regression-%d\"><td><span class=\"id\">", i + 1);
        esc(&b, r->names[n].suite);
        buf_printf(&b, "</span><br><a href=\"c/%s.html\">", r->names[n].key);
        esc(&b, r->names[n].name);
        buf_printf(&b, "</a></td><td><a href=\"d/%s.html\">%s</a> %s, %s</td><td class=\"mono\"><a href=\"../r/%ld/\">",
                   driver_key(g->driver_id), driver_key(g->driver_id), version, os_name(g->os), g->passed_report);
        esc(&b, g->passed_commit);
        buf_add(&b, "</a></td><td class=\"mono\"><a href=\"../r/");
        buf_printf(&b, "%ld/\">", g->failed_report);
        esc(&b, g->failed_commit);
        buf_printf(&b, "</a> (<a href=\"" SITE_REPO "/compare/");
        esc(&b, g->passed_commit);
        buf_add(&b, "...");
        esc(&b, g->failed_commit);
        buf_printf(&b, "\">the commits between</a>)</td><td>%d</td></tr>\n", g->builds);
        jset(e, "case", r->names[n].key);
        jset(e, "suite", r->names[n].suite);
        jset(e, "name", r->names[n].name);
        jset(e, "edenprobe", g->version);
        jset(e, "words", g->words);
        jset(e, "driver", probe_driver_name(g->driver_id));
        jset(e, "version", version);
        jset(e, "os", g->os);
        jset(e, "passed_commit", g->passed_commit);
        jset(e, "failed_commit", g->failed_commit);
        cJSON_AddNumberToObject(e, "passed_report", (double)g->passed_report);
        cJSON_AddNumberToObject(e, "failed_report", (double)g->failed_report);
        cJSON_AddNumberToObject(e, "builds", g->builds);
        cJSON_AddItemToArray(regressions, e);
    }
    if (r->rules.nregressions) buf_add(&b, "</tbody>\n</table></div>\n");
    foot(&b);
    done = write_page(root, "probe/candidates.html", buf_take(&b));
    done |= write_page(root, "api/probe/candidates.json", printed(o));
    return done;
}

/* probe/d/<driver>.html and its JSON: driver id's versions, runs, the cases that fail on it and its candidates */
static int driver_page(const char* root, const Runs* r, int id) {
    Buf b = {0}, title = {0};
    cJSON *o = cJSON_CreateObject(), *versions = cJSON_AddArrayToObject(o, "versions"),
          *failing = cJSON_AddArrayToObject(o, "failing"), *list = cJSON_AddArrayToObject(o, "candidates"),
          *reports = cJSON_AddArrayToObject(o, "runs");
    int *mine = zeroed((size_t)r->nruns, sizeof(int)), nmine = 0, i, m, k, done, any = 0;
    const char* label = id ? probe_driver_label(id) : "drivers no log names";
    char* rel;
    for (i = 0; i < r->nruns; ++i)
        if (r->runs[i].p.driver_id == id) {
            mine[nmine++] = i;
            cJSON_AddItemToArray(reports, cJSON_CreateNumber((double)r->runs[i].report));
        }
    buf_printf(&title, "%s: runs of edenprobe", label);
    head(&b, "../../", title.text,
         "A driver's runs of edenprobe sent to Tracer: its versions and the cases that fail.");
    buf_free(&title);
    buf_add(&b, "<section class=\"hero\">\n<p class=\"crumbs\"><a href=\"../index.html\">Probe</a></p>\n<h1>");
    esc(&b, label);
    buf_add(&b, "</h1>\n<p class=\"id\">");
    if (id)
        buf_printf(&b, "%s · VkDriverId %d", driver_key(id), id);
    else
        buf_add(&b, "the console's reports, and logs that name no driver ID");
    buf_printf(&b, "</p>\n<p class=\"stats\"><b>%d</b> %s</p>\n</section>\n", nmine, nmine == 1 ? "run" : "runs");
    jset(o, "driver", probe_driver_name(id));
    cJSON_AddNumberToObject(o, "driver_id", id);
    jset(o, "label", label);
    buf_add(&b, "<h2>Its machines</h2>\n<div class=\"scroll\"><table class=\"reports\">\n<thead><tr><th>Machine</th>"
                "<th>Runs</th><th>The latest</th></tr></thead>\n<tbody>\n");
    for (m = 0; m < r->nmachines; ++m) {
        if (r->machines[m].p->driver_id != id) continue;
        buf_add(&b, "<tr><td>");
        machine_text(&b, r->machines[m].p);
        buf_printf(&b, "</td><td>%d</td><td>", r->machines[m].runs);
        when(&b, r->machines[m].last);
        buf_add(&b, "</td></tr>\n");
        cJSON_AddItemToArray(versions, machine_object(&r->machines[m]));
    }
    buf_add(&b, "</tbody>\n</table></div>\n<h2>Cases that fail on it</h2>\n");
    for (i = 0; i < r->nnames; ++i) {
        int c[4] = {0, 0, 0, 0};
        cJSON* e;
        for (m = 0; m < r->nmachines; ++m)
            if (r->machines[m].p->driver_id == id)
                for (k = 0; k < 4; ++k) c[k] += cell_of(r, i, m)[k];
        if (!c[ST_FAIL] && !c[ST_CRASHED]) continue;
        if (!any++)
            buf_add(&b, "<div class=\"scroll\"><table class=\"reports\">\n<thead><tr><th>Case</th><th>Results</th>"
                        "</tr></thead>\n<tbody>\n");
        buf_add(&b, "<tr><td><span class=\"id\">");
        esc(&b, r->names[i].suite);
        buf_printf(&b, "</span><br><a href=\"../c/%s.html\">", r->names[i].key);
        esc(&b, r->names[i].name);
        buf_add(&b, "</a></td><td>");
        counts_cell(&b, c);
        buf_add(&b, "</td></tr>\n");
        e = cJSON_CreateObject();
        jset(e, "case", r->names[i].key);
        jset(e, "suite", r->names[i].suite);
        jset(e, "name", r->names[i].name);
        cJSON_AddItemToObject(e, "results", counts_array(c));
        cJSON_AddItemToArray(failing, e);
    }
    buf_add(&b, any ? "</tbody>\n</table></div>\n" : "<p class=\"empty\">None.</p>\n");
    for (i = 0, any = 0; i < r->rules.ncandidates; ++i)
        if (r->rules.candidates[i].driver_id == id) {
            if (!any++) buf_add(&b, "<h2>Ban candidates</h2>\n");
            candidate_block(&b, "../../", r, i);
            cJSON_AddItemToArray(list, candidate_object(r, &r->rules.candidates[i]));
        }
    buf_add(&b, "<h2>Runs</h2>\n");
    runs_table(&b, "../../", r, mine, nmine);
    foot(&b);
    free(mine);
    rel = rei_format("probe/d/%s.html", driver_key(id));
    done = write_page(root, rel, buf_take(&b));
    free(rel);
    rel = rei_format("api/probe/d/%s.json", driver_key(id));
    done |= write_page(root, rel, printed(o));
    free(rel);
    return done;
}

/* the files of root/folder whose name is none of keep's with its ending, removed: the pages of cases and drivers no run
   names any more */
static void prune(const char* root, const char* folder, const char* ending, char* const* keep, int nkeep) {
    char* path = rei_format("%s/%s", root, folder);
    DIR* d = opendir(path);
    struct dirent* e;
    size_t tail = strlen(ending);
    while (d && (e = readdir(d))) {
        size_t n = strlen(e->d_name);
        int i, kept = 0;
        if (n <= tail || strcmp(e->d_name + n - tail, ending)) continue;
        for (i = 0; i < nkeep && !kept; ++i)
            kept = strlen(keep[i]) == n - tail && !strncmp(keep[i], e->d_name, n - tail);
        if (!kept) {
            char* gone = rei_format("%s/%s", path, e->d_name);
            unlink(gone);
            free(gone);
        }
    }
    if (d) closedir(d);
    free(path);
}

int site_probe(Store* s, const char* root) {
    Runs r;
    int i, done = 0, ndrivers = 0, k;
    char** keys;
    char* drivers[64];
    if (runs_load(s, &r)) return -1;
    done |= probe_index(root, &r);
    done |= candidates_page(root, &r);
    keys = zeroed((size_t)r.nnames, sizeof *keys);
    for (i = 0; i < r.nnames; ++i) {
        done |= case_page(s, root, &r, i);
        keys[i] = r.names[i].key;
    }
    for (i = 0; i < r.nruns; ++i) { /* each driver once */
        const char* key = driver_key(r.runs[i].p.driver_id);
        for (k = 0; k < ndrivers && strcmp(drivers[k], key); ++k) continue;
        if (k == ndrivers && ndrivers < (int)(sizeof drivers / sizeof *drivers)) {
            drivers[ndrivers++] = (char*)key;
            done |= driver_page(root, &r, r.runs[i].p.driver_id);
        }
    }
    prune(root, "probe/c", ".html", keys, r.nnames);
    prune(root, "api/probe/c", ".json", keys, r.nnames);
    prune(root, "probe/d", ".html", drivers, ndrivers);
    prune(root, "api/probe/d", ".json", drivers, ndrivers);
    free(keys);
    runs_free(&r);
    g_site_runs = NULL;
    return done ? -1 : 0;
}
