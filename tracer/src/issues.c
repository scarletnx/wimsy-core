/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "issues.h"
#include <stdlib.h>
#include <string.h>

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

char* tr_issue_title(const StReport* r) {
    /* no "#12": GitHub would link the repository's own issue 12 */
    char *raw = rei_format("Report %ld: %s (%s)", r->id, r->title, r->outcome ? r->outcome : "no failure logged"),
         *t = one_line(raw, TITLE_MAX);
    free(raw);
    return t;
}

/* a line of the code block: name and value, when there is one */
static void fact(Buf* b, const char* name, const char* value) {
    char* v;
    if (!value || !*value) return;
    v = one_line(value, 500);
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
    const cJSON *frame, *line;
    size_t fence;
    int i;
    char* build =
        r->branch || r->commit ? rei_format("%s %s", r->branch ? r->branch : "", r->commit ? r->commit : "") : NULL;
    for (i = 0; i < nfiles && !log; ++i)
        if (!strcmp(files[i].kind, "log")) log = &files[i];
    if (log && log->summary) s = cJSON_Parse(log->summary);
    fact(&facts, "Game", r->title);
    fact(&facts, "Title ID", r->title_id);
    fact(&facts, "Console", r->console);
    fact(&facts, "Game version", r->version);
    fact(&facts, "Build", build ? build : r->build);
    fact(&facts, "System", r->system);
    fact(&facts, "GPU", r->gpu);
    fact(&facts, "Driver", r->driver);
    fact(&facts, "Outcome", r->outcome ? r->outcome : "no failure logged");
    fact(&facts, "Signature", r->signature);
    fact(&facts, "Missed", r->missed);
    fact(&facts, "Note", r->note);
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
        first = 1;
        cJSON_ArrayForEach(line, cJSON_GetObjectItemCaseSensitive(s, "tail")) if (cJSON_IsString(line)) {
            fact(&facts, first ? "Last lines" : "", line->valuestring);
            first = 0;
        }
    }
    fence = ticks(facts.text) + 1;
    if (fence < 3) fence = 3;
    buf_printf(&b,
               "Tracer's report %ld, approved on Discord by a server's administrator: its logs, screenshots and "
               "videos are on %s/r/%ld/\n\n",
               r->id, url, r->id);
    for (i = 0; i < (int)fence; ++i) buf_add(&b, "`");
    buf_add(&b, "text\n");
    buf_add(&b, facts.text ? facts.text : "");
    for (i = 0; i < (int)fence; ++i) buf_add(&b, "`");
    buf_add(&b, "\n\nThe log's home folders are written as ~ and its addresses as [ip] and [email]; the crash report's "
                "dumps of the game's bytes are not in it.\n");
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
