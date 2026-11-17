/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "issues.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define GITHUB_API "https://api.github.com/"
#define TITLE_MAX  200 /* an issue title's bytes at most */

static int repo_name(const char* s) {
    const char* slash = strchr(s, '/');
    size_t i;
    if (!slash || slash == s || !slash[1] || strchr(slash + 1, '/')) return 0;
    for (i = 0; s[i]; ++i)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9') ||
              strchr("._-/", s[i])))
            return 0;
    return 1;
}

int tr_issues_settings(TrIssues* is, const char* (*get)(const char* name), const char** token, char** why) {
    const char *repo = get("TRACER_ISSUES_REPO"), *key = get("TRACER_ISSUES_TOKEN"),
               *emoji = get("TRACER_ISSUES_EMOJI"), *labels = get("TRACER_ISSUES_LABELS");
    memset(is, 0, sizeof *is);
    *token = NULL;
    *why = NULL;
    if (repo && !*repo) repo = NULL;
    if (key && !*key) key = NULL;
    if (!repo) {
        if (key)
            *why = rei_strdup("TRACER_ISSUES_TOKEN is set, and TRACER_ISSUES_REPO, the issues' repository, is not");
        if (!*why && emoji && *emoji)
            *why = rei_format("TRACER_ISSUES_EMOJI=%s is set, and TRACER_ISSUES_REPO, the issues' repository, is not",
                              emoji);
        if (!*why && labels && *labels)
            *why = rei_format("TRACER_ISSUES_LABELS=%s is set, and TRACER_ISSUES_REPO, the issues' repository, is not",
                              labels);
        return *why ? -1 : 0;
    }
    if (!repo_name(repo)) {
        *why = rei_format("TRACER_ISSUES_REPO=%s is not OWNER/NAME", repo);
        return -1;
    }
    if (!key) {
        *why = rei_format(
            "TRACER_ISSUES_REPO=%s needs TRACER_ISSUES_TOKEN, a fine-grained token that writes its issues", repo);
        return -1;
    }
    is->repo = repo;
    is->emoji = emoji && *emoji ? emoji : "\xe2\x9c\x85"; /* U+2705, the white check mark on green */
    while (labels && *labels) {
        size_t n = strcspn(labels, ",");
        while (n && labels[0] == ' ') ++labels, --n;
        if (n) {
            if (is->nlabels == TR_LABELS) {
                *why = rei_format("TRACER_ISSUES_LABELS names more than %d labels", TR_LABELS);
                tr_issues_free(is);
                return -1;
            }
            is->labels[is->nlabels++] = rei_format("%.*s", (int)n, labels);
        }
        labels += n + (labels[n] == ',');
    }
    *token = key;
    return 0;
}

void tr_issues_free(TrIssues* is) {
    int i;
    for (i = 0; i < is->nlabels; ++i) free(is->labels[i]);
    memset(is, 0, sizeof *is);
}

char* tr_issues_said(const TrIssues* is) {
    Buf b = {0};
    int i;
    if (!is->repo) return rei_strdup("issues: none, as TRACER_ISSUES_REPO is not set");
    buf_printf(&b, "issues: into %s, once a server's administrator reacts to a report with %s; labels: ", is->repo,
               is->emoji);
    if (!is->nlabels) buf_add(&b, "none");
    for (i = 0; i < is->nlabels; ++i) buf_printf(&b, "%s%s", i ? "," : "", is->labels[i]);
    return buf_take(&b);
}

/* text on one line, its control characters as spaces, cut to max bytes on a whole UTF-8 sequence; owned */
static char* one_line(const char* text, size_t max) {
    char *t = rei_strdup(text ? text : ""), *p;
    size_t n = strlen(t);
    for (p = t; *p; ++p)
        if ((unsigned char)*p < 0x20 || *p == 0x7f) *p = ' ';
    if (n > max) {
        n = max;
        while (n && ((unsigned char)t[n] & 0xc0) == 0x80) --n;
        t[n] = 0;
    }
    return t;
}

/* the extensions of a game's files: a dump's, an update's, a patch's, a mod's (tools/tracer.py's --upstream's) */
static const char* const GAME_FILES[] = {"nsp", "nsz", "xci", "xcz", "nro", "nso", "3ds",    "3dsx",
                                         "cia", "cci", "cxi", "gba", "agb", "vpk", "pchtxt", "ips"};

/* whether the n bytes at s, a word, end in a game file's extension */
static int game_file(const char* s, size_t n) {
    size_t i, k;
    for (i = n; i > 0 && s[i - 1] != '.'; --i)
        if (!((s[i - 1] >= 'a' && s[i - 1] <= 'z') || (s[i - 1] >= 'A' && s[i - 1] <= 'Z') ||
              (s[i - 1] >= '0' && s[i - 1] <= '9')))
            return 0;
    if (i <= 1 || i == n) return 0; /* a name before the dot, and an extension after it */
    for (k = 0; k < sizeof GAME_FILES / sizeof *GAME_FILES; ++k)
        if (n - i == strlen(GAME_FILES[k]) && !strncasecmp(s + i, GAME_FILES[k], n - i)) return 1;
    return 0;
}
static int hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

/* the n bytes at s, a word, with each title ID in it (16 hex digits, no more) as <game>, into b */
static void put_word(Buf* b, const char* s, size_t n) {
    size_t at, run = 0, from = 0;
    for (at = 0; at < n; ++at) {
        run = hex(s[at]) ? run + 1 : 0;
        if (run == 16 && (at + 1 == n || !hex(s[at + 1]))) {
            buf_addn(b, s + from, at + 1 - 16 - from);
            buf_add(b, "<game>");
            from = at + 1;
            run = 0;
        }
    }
    buf_addn(b, s + from, n - from);
}

char* tr_upstream_text(const char* text, size_t max) {
    Buf b = {0};
    char *line = one_line(text, (size_t)-1), *out;
    const char* s;
    for (s = line; *s;) { /* a game's file anywhere: the whole text goes, as a name with spaces is before it */
        size_t n = strcspn(s, " "), end = n;
        while (end && strchr(".,;:)]}>'\"", s[end - 1])) --end; /* the punctuation after it */
        if (game_file(s, end)) {
            free(line);
            return rei_strdup("<cut: it names a game's file>");
        }
        s += n + (s[n] == ' ');
    }
    for (s = line; *s;) {
        size_t n = strcspn(s, " ");
        put_word(&b, s, n);
        if (s[n] == ' ') buf_add(&b, " ");
        s += n + (s[n] == ' ');
    }
    free(line);
    out = one_line(b.text ? b.text : "", max);
    buf_free(&b);
    return out;
}

char* tr_issue_title(const StReport* r) {
    /* no "#12": GitHub would link the repository's own issue 12; no game: the issue is upstream's */
    char *raw = r->signature
                    ? rei_format("Tracer report %ld: %s (%s)", r->id, r->signature, r->console ? r->console : "a game")
                    : rei_format("Tracer report %ld: %s (%s)", r->id, r->outcome ? r->outcome : "no failure logged",
                                 r->console ? r->console : "a game"),
         *t = tr_upstream_text(raw, TITLE_MAX);
    free(raw);
    return t;
}

/* a line of the code block: name and value, when there is one, as an issue may hold it */
static void fact(Buf* b, const char* name, const char* value) {
    char* v;
    if (!value || !*value) return;
    v = tr_upstream_text(value, 500);
    buf_printf(b, "%-13s %s\n", name, v);
    free(v);
}
/* the longest run of backticks in text */
static size_t ticks(const char* text) {
    size_t most = 0, run = 0;
    for (; text && *text; ++text) {
        run = *text == '`' ? run + 1 : 0;
        if (run > most) most = run;
    }
    return most;
}

/* the report's screenshots in the issue as its page shows them: each the site's image, 320 pixels wide, that links to
   it, as the issue holds no file of its own (GitHub's API takes none; its image proxy fetches each from the site). The
   text is "Screenshot N", N its number in the report: a file's name is a player's words, outside a code block */
static void shots(Buf* b, long id, const StFile* files, int nfiles, const char* url) {
    int i, any = 0;
    for (i = 0; i < nfiles; ++i) {
        const char* path = files[i].path;
        if (strcmp(files[i].kind, "image") || !path || !*path ||
            strspn(path, "0123456789.abcdefghijklmnopqrstuvwxyz") != strlen(path))
            continue;
        if (!any++) buf_add(b, "\nScreenshots, as the report's page shows them:\n\n");
        buf_printf(b, "<a href=\"%s/r/%ld/%s\"><img src=\"%s/r/%ld/%s\" width=\"320\" alt=\"Screenshot %d\"></a>\n",
                   url, id, path, url, id, path, files[i].n);
    }
}

char* tr_issue_body(const StReport* r, const StFile* files, int nfiles, const char* url) {
    Buf facts = {0}, b = {0};
    const StFile* log = NULL;
    cJSON* s = NULL;
    const cJSON* frame;
    size_t fence;
    int i;
    char* build =
        r->branch || r->commit ? rei_format("%s %s", r->branch ? r->branch : "", r->commit ? r->commit : "") : NULL;
    for (i = 0; i < nfiles && !log; ++i)
        if (!strcmp(files[i].kind, "log")) log = &files[i];
    if (log && log->summary) s = cJSON_Parse(log->summary);
    fact(&facts, "Console", r->console);
    fact(&facts, "Game version", r->version);
    fact(&facts, "Build", build ? build : r->build);
    fact(&facts, "System", r->system);
    fact(&facts, "GPU", r->gpu);
    fact(&facts, "Driver", r->driver);
    fact(&facts, "Outcome", r->outcome ? r->outcome : "no failure logged");
    fact(&facts, "Signature", r->signature);
    fact(&facts, "Missed", r->missed);
    free(build);
    {
        const cJSON* failure = cJSON_GetObjectItemCaseSensitive(s, "failure");
        const char* fatal = jstr(failure, "fatal");
        int first = 1;
        if (fatal) fact(&facts, "Fatal", fatal);
        cJSON_ArrayForEach(frame, cJSON_GetObjectItemCaseSensitive(failure, "frames")) {
            const cJSON* sym = cJSON_GetObjectItemCaseSensitive(frame, "symbol");
            const char *name = jstr(sym, "demangled") ? jstr(sym, "demangled") : jstr(sym, "name"),
                       *offset = jstr(sym, "offset");
            char* text = rei_format("%s %s%s%s", jstr(frame, "address") ? jstr(frame, "address") : "?",
                                    name ? name : "", offset ? "+" : "", offset ? offset : "");
            fact(&facts, first ? "Frames" : "", text);
            free(text);
            first = 0;
        }
    }
    fence = ticks(facts.text) + 1;
    if (fence < 3) fence = 3;
    buf_printf(&b,
               "Tracer's report %ld, approved on Discord by a server's administrator: the game, its logs, the "
               "player's words, screenshots and videos are on %s/r/%ld/\n\n",
               r->id, url, r->id);
    for (i = 0; i < (int)fence; ++i) buf_add(&b, "`");
    buf_add(&b, "text\n");
    buf_add(&b, facts.text ? facts.text : "");
    for (i = 0; i < (int)fence; ++i) buf_add(&b, "`");
    buf_add(
        &b,
        "\n\nThe game is named on the report's page, not here: a title ID is written as <game>, and a line that "
        "names a game's file is cut. The log's home folders, and the folders of a path outside them, are written as ~ and "
        "its addresses as [ip] and [email]; the crash report's dumps of the game's bytes are not in it.\n");
    shots(&b, r->id, files, nfiles, url);
    buf_free(&facts);
    cJSON_Delete(s);
    return buf_take(&b);
}

int tr_issue_open(const TrIssues* is, const char* title, const char* body, long* number, char** url, char** why) {
    char* path = rei_format(GITHUB_API "repos/%s/issues", is->repo);
    cJSON *ask = cJSON_CreateObject(), *labels = cJSON_AddArrayToObject(ask, "labels"), *got;
    char* request;
    RgAnswer a;
    int i;
    *number = 0;
    *url = NULL;
    *why = NULL;
    jset(ask, "title", title);
    jset(ask, "body", body);
    for (i = 0; i < is->nlabels; ++i) cJSON_AddItemToArray(labels, cJSON_CreateString(is->labels[i]));
    request = cJSON_PrintUnformatted(ask);
    cJSON_Delete(ask);
    memset(&a, 0, sizeof a);
    is->fetch(is->ctx, "POST", path, request, NULL, &a);
    free(request);
    got = a.body ? cJSON_Parse(a.body) : NULL;
    if (a.status == 201 && jnum(got, "number") > 0 && jstr(got, "html_url") &&
        !strncmp(jstr(got, "html_url"), "https://github.com/", 19)) {
        *number = (long)jnum(got, "number");
        *url = rei_strdup(jstr(got, "html_url"));
    } else if (a.status)
        *why = rei_format("GitHub answered %d to POST repos/%s/issues: %s", a.status, is->repo,
                          jstr(got, "message") ? jstr(got, "message") : "no message");
    else
        *why = rei_format("GitHub did not answer POST repos/%s/issues: %s", is->repo, a.error ? a.error : "no reason");
    cJSON_Delete(got);
    free(a.body);
    free(a.etag);
    free(a.error);
    free(path);
    return *url ? 0 : -1;
}

/* GitHub's answer as why: its status and message, or why none came; owned */
static char* refused(const RgAnswer* a, const cJSON* got, const char* method, const char* path) {
    return a->status ? rei_format("GitHub answered %d to %s %s: %s", a->status, method, path,
                                  jstr(got, "message") ? jstr(got, "message") : "no message")
                     : rei_format("GitHub did not answer %s %s: %s", method, path, a->error ? a->error : "no reason");
}

int tr_issue_get(const TrIssues* is, long number, char** url, int* pull, char** state, char** why) {
    char *path = rei_format("repos/%s/issues/%ld", is->repo, number), *full = rei_format(GITHUB_API "%s", path);
    RgAnswer a;
    cJSON* got;
    *url = *state = *why = NULL;
    *pull = 0;
    memset(&a, 0, sizeof a);
    is->fetch(is->ctx, "GET", full, NULL, NULL, &a);
    got = a.body ? cJSON_Parse(a.body) : NULL;
    if (a.status == 200 && (long)jnum(got, "number") == number && jstr(got, "html_url") &&
        !strncmp(jstr(got, "html_url"), "https://github.com/", 19)) {
        *url = rei_strdup(jstr(got, "html_url"));
        *pull = cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(got, "pull_request"));
        *state = rei_strdup(jstr(got, "state") ? jstr(got, "state") : "open");
    } else
        *why = refused(&a, got, "GET", path);
    cJSON_Delete(got);
    free(a.body);
    free(a.etag);
    free(a.error);
    free(full);
    free(path);
    return *url ? 0 : -1;
}

int tr_issue_comment(const TrIssues* is, long number, const char* body, char** why) {
    char *path = rei_format("repos/%s/issues/%ld/comments", is->repo, number),
         *full = rei_format(GITHUB_API "%s", path), *request;
    cJSON *ask = cJSON_CreateObject(), *got;
    RgAnswer a;
    int ok;
    *why = NULL;
    jset(ask, "body", body);
    request = cJSON_PrintUnformatted(ask);
    cJSON_Delete(ask);
    memset(&a, 0, sizeof a);
    is->fetch(is->ctx, "POST", full, request, NULL, &a);
    got = a.body ? cJSON_Parse(a.body) : NULL;
    ok = a.status == 201;
    if (!ok) *why = refused(&a, got, "POST", path);
    cJSON_Delete(got);
    free(a.body);
    free(a.etag);
    free(a.error);
    free(request);
    free(full);
    free(path);
    return ok ? 0 : -1;
}
