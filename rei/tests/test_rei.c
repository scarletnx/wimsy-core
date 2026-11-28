/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* ReiWimsy without Discord or the network: what a message names, how GitHub's answers read, what is remembered, the
   REST calls' waits and answers, and the cards: drawn, posted, edited, deleted a day after they close */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cards.h"
#include "event.h"
#include "link.h"
#include "look.h"
#include "refs.h"
#include "rest.h"
#include "state.h"

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
#define HAS(text, part) CHECK((text) && strstr((text), (part)))

/* ---- the clock ---- */
static double now_is;
static double clock_now(void) { return now_is; }

/* ---- a fake network: each answer by method and URL, every request kept ---- */
typedef struct {
    char *method, *url, *body, *etag;
    int status;
} Fixture;
static Fixture fixtures[256];
static int nfixtures;
static char* asked[4096];
static int nasked;
static char* posted_body; /* the last POST's or PATCH's body */
static void answer(const char* method, const char* url, int status, const char* body) {
    int i;
    for (i = 0; i < nfixtures; ++i)
        if (!strcmp(fixtures[i].method, method) && !strcmp(fixtures[i].url, url)) break;
    if (i == nfixtures) {
        ++nfixtures;
        fixtures[i].method = rei_strdup(method);
        fixtures[i].url = rei_strdup(url);
    } else {
        free(fixtures[i].body);
        free(fixtures[i].etag);
    }
    fixtures[i].status = status;
    fixtures[i].body = rei_strdup(body);
    fixtures[i].etag = NULL;
}
static void gh(const char* path, const char* body) {
    char* url = rei_format("https://api.github.com/%s", path);
    answer("GET", url, 200, body);
    free(url);
}
static void forget(void) {
    int i;
    for (i = 0; i < nfixtures; ++i) {
        free(fixtures[i].method);
        free(fixtures[i].url);
        free(fixtures[i].body);
        free(fixtures[i].etag);
    }
    for (i = 0; i < nasked; ++i) free(asked[i]);
    nfixtures = nasked = 0;
}
static int times_asked(const char* method, const char* url) {
    int i, n = 0;
    char* want = rei_format("%s %s", method, url);
    for (i = 0; i < nasked; ++i) n += !strcmp(asked[i], want);
    free(want);
    return n;
}
static void fake(void* ctx, const char* method, const char* url, const char* body, const char* etag, RgAnswer* out) {
    int i;
    (void)ctx;
    memset(out, 0, sizeof *out);
    if (nasked < 4096) asked[nasked++] = rei_format("%s %s", method, url);
    if (body) {
        free(posted_body);
        posted_body = rei_strdup(body);
    }
    for (i = 0; i < nfixtures; ++i)
        if (!strcmp(fixtures[i].method, method) && !strcmp(fixtures[i].url, url)) {
            if (fixtures[i].etag && etag && !strcmp(etag, fixtures[i].etag)) {
                out->status = 304;
                return;
            }
            out->status = fixtures[i].status;
            if (!out->status) { /* no answer: the body is why, as http.c says it */
                out->error = rei_strdup(fixtures[i].body);
                return;
            }
            out->body = rei_strdup(fixtures[i].body);
            if (fixtures[i].etag) out->etag = rei_strdup(fixtures[i].etag);
            return;
        }
    out->status = 404;
    out->body = rei_strdup("{\"message\":\"Not Found\"}");
}

/* ---- refs ---- */
static void refs(void) {
    int n[REI_MOST], c, number;
    c = rei_references("see #160, issue #12, PR #305, pull request #7 and pull #8", n);
    CHECK(c == 5 && n[0] == 160 && n[1] == 12 && n[2] == 305 && n[3] == 7 && n[4] == 8);
    c = rei_references("#1 #1 #2 #3 #4 #5 #6", n);
    CHECK(c == 5 && n[0] == 1 && n[4] == 5);
    c = rei_references("run `make #12`, ```\n#13\n``` see https://x/issues/14#15 a#16 &#17 #0 color #fff", n);
    CHECK(c == 0);
    c = rei_references("Issue #9 and pr #10", n);
    CHECK(c == 2 && n[0] == 9 && n[1] == 10);
    CHECK(rei_references("issue 12, PR 305, pull request 7, pull 8 and pr 2 broke", n) == 0);
    CHECK(rei_references("#1234567 #123456 <https://x/#5> #7.", n) == 2 && n[0] == 123456 && n[1] == 7);
    CHECK(rei_references("\xc3\xa9#4 #4", n) == 1 && n[0] == 4); /* after a letter of UTF-8: in a word */
    /* Tracer's numbers are its reports', never an issue: "report #74", "Tracer #12", "reports #3 and #4" (the second
       stands alone, an issue) */
    CHECK(rei_references("Tracer report #74 crashes, as tracer #12 and Reports  #3 did", n) == 0);
    c = rei_references("reports #3 and #4; the report: #5; reported #6", n);
    CHECK(c == 3 && n[0] == 4 && n[1] == 5 && n[2] == 6);
    CHECK(rei_command("!nightly", &number) == REI_NIGHTLY);
    CHECK(rei_command("  !Release ", &number) == REI_RELEASE);
    CHECK(rei_command("!release #355", &number) == REI_RELEASE_PR && number == 355);
    CHECK(rei_command("try !nightly later", &number) == REI_NO_COMMAND);
    CHECK(rei_command("!nightlies", &number) == REI_NO_COMMAND);
    CHECK(rei_command("!release#355", &number) == REI_NO_COMMAND);
    CHECK(rei_command("!release #1234567", &number) == REI_NO_COMMAND);
}

/* ---- look ---- */
#define ISSUE                                                                                                          \
    "{\"number\":160,\"title\":\"todo: aarch64 [builds]\",\"html_url\":\"https://x/issues/160\",\"state\":\"closed\"," \
    "\"state_reason\":\"completed\",\"labels\":[{\"name\":\"priority: low\",\"color\":\"0e8a16\"}]}"
#define PULL                                                                                                           \
    "{\"number\":305,\"title\":\"Players\",\"html_url\":\"https://x/pull/305\",\"state\":\"closed\",\"labels\":[],"    \
    "\"pull_request\":{\"url\":\"x\"}}"
static void look(void) {
    static const char* const colors[] = {"b60205", "d93f0b", "fbca04", "0075ca", "d4c5f9", "ededed", "5c3317"};
    static const char* const squares[] = {"\xF0\x9F\x9F\xA5", "\xF0\x9F\x9F\xA7", "\xF0\x9F\x9F\xA8",
                                          "\xF0\x9F\x9F\xA6", "\xF0\x9F\x9F\xAA", "\xE2\xAC\x9C",
                                          "\xF0\x9F\x9F\xAB"};
    cJSON *pull = cJSON_Parse(PULL), *issue = cJSON_Parse(ISSUE);
    char* s;
    int i;
    for (i = 0; i < 7; ++i) SAME(rei_square(colors[i]), squares[i]);
    cJSON_ReplaceItemInObject(cJSON_GetObjectItem(pull, "pull_request"), "url", cJSON_CreateString("x"));
    cJSON_AddStringToObject(cJSON_GetObjectItem(pull, "pull_request"), "merged_at", "2026");
    SAME(rei_state(pull), "merged");
    cJSON_DeleteItemFromObject(cJSON_GetObjectItem(pull, "pull_request"), "merged_at");
    cJSON_AddNullToObject(cJSON_GetObjectItem(pull, "pull_request"), "merged_at");
    SAME(rei_state(pull), "closed");
    cJSON_ReplaceItemInObject(pull, "state", cJSON_CreateString("open"));
    cJSON_AddTrueToObject(pull, "draft");
    SAME(rei_state(pull), "draft");
    cJSON_ReplaceItemInObject(issue, "state_reason", cJSON_CreateString("not_planned"));
    SAME(rei_state(issue), "not planned");
    s = rei_escape("a*b_[c]`|~<x>\\");
    SAME(s, "a\\*b\\_\\[c\\]\\`\\|\\~\\<x\\>\\\\");
    free(s);
    s = rei_excerpt("hi <!-- hidden --> there\r\n\n\n\n![pic](u) <img src=x> <DETAILS>x</details>", 300);
    SAME(s, "> hi  there\n>\n> [image] [image] [details]");
    free(s);
    s = rei_excerpt("one two three", 7);
    SAME(s, "> one...");
    free(s);
    s = rei_excerpt("```c\ncode that goes on", 12);
    SAME(s, "> ```c\n> code...\n> ```");
    free(s);
    s = rei_excerpt("\xc3\xa1\xc3\xa1\xc3\xa1 b", 3); /* letters, not bytes */
    SAME(s, "> \xc3\xa1\xc3\xa1\xc3\xa1...");
    free(s);
    s = rei_excerpt("", 300);
    SAME(s, "");
    free(s);
    cJSON_Delete(pull);
    cJSON_Delete(issue);
}

/* ---- link ---- */
static Rg g;
static Link l;
static void link_start(void) {
    forget();
    now_is = 0;
    rg_init(&g, "o/r", NULL, fake, NULL);
    lk_init(&l, &g);
}
static void link_end(void) {
    lk_free(&l);
    rg_free(&g);
}
static int edits;
static char* edited;
static int gone_too;
static int fake_edit(void* ctx, u64 channel, u64 message, const cJSON* embed) {
    (void)ctx;
    CHECK(channel == 7 && message == 70);
    ++edits;
    free(edited);
    edited = rei_strdup(jstr(embed, "description") ? jstr(embed, "description") : "");
    return gone_too ? 404 : 0;
}
static void link_tests(void) {
    const cJSON* items[2];
    cJSON *issue = cJSON_Parse(ISSUE), *pull = cJSON_Parse(PULL), *e, *saved;
    int numbers[REI_MOST], n;
    char* text;
    SAME(
        cJSON_GetStringValue(cJSON_GetObjectItem(e = lk_reply((items[0] = issue, items), 1), "description")),
        "**Issue [#160](https://x/issues/160)**: todo: aarch64 \\[builds\\]  (done)\n\xF0\x9F\x9F\xA9 `priority: low`");
    CHECK(jnum(e, "color") == 0x8250DF);
    cJSON_Delete(e);
    items[1] = pull;
    e = lk_reply(items, 2);
    CHECK(jnum(e, "color") == 0x6E7781);
    cJSON_Delete(e);

    /* a pull request is asked whether it merged; an open one asked again after LK_CACHE, a closed one after
       LK_SETTLED */
    link_start();
    gh("repos/o/r/issues/305", PULL);
    gh("repos/o/r/pulls/305", "{\"merged_at\":\"2026-10-17\",\"draft\":false}");
    SAME(rei_state(lk_item(&l, 305)), "merged");
    answer("GET", "https://api.github.com/repos/o/r/issues/160", 200, ISSUE);
    lk_item(&l, 160);
    now_is += LK_CACHE + 1;
    lk_item(&l, 160);
    CHECK(times_asked("GET", "https://api.github.com/repos/o/r/issues/160") == 1);
    now_is += LK_SETTLED;
    lk_item(&l, 160);
    CHECK(times_asked("GET", "https://api.github.com/repos/o/r/issues/160") == 2);
    link_end();

    link_start();
    text = rei_strdup(ISSUE);
    memcpy(strstr(text, "\"closed\""), "\"open\"  ", 8);
    gh("repos/o/r/issues/160", text);
    lk_item(&l, 160);
    lk_item(&l, 160);
    now_is += LK_CACHE + 1;
    lk_item(&l, 160);
    CHECK(times_asked("GET", "https://api.github.com/repos/o/r/issues/160") == 2);
    /* a reply kept and edited when its item changes; GitHub's unknown numbers left out */
    e = lk_answer(&l, "#160 and #999", numbers, &n);
    CHECK(e && n == 1 && numbers[0] == 160);
    HAS(jstr(e, "description"), "#160");
    CHECK(!strstr(jstr(e, "description"), "999"));
    lk_keep(&l, 7, 70, numbers, n, e);
    cJSON_Delete(e);
    edits = 0;
    lk_refresh(&l, fake_edit, NULL);
    CHECK(edits == 0); /* nothing changed */
    gh("repos/o/r/issues/160", ISSUE);
    now_is += LK_CACHE;
    lk_refresh(&l, fake_edit, NULL);
    CHECK(edits == 1);
    HAS(edited, "(done)");
    CHECK(!lk_answer(&l, "#999 only", numbers, &n) && !lk_answer(&l, "nothing here", numbers, &n));
    /* the state: what GitHub said and the replies, ids whole */
    lk_keep(&l, 1234567890123456789ULL, 9876543210987654321ULL, numbers, 1, issue);
    saved = lk_save(&l);
    {
        char* written = cJSON_PrintUnformatted(saved);
        Rg g2;
        Link l2;
        rg_init(&g2, "o/r", NULL, fake, NULL);
        lk_init(&l2, &g2);
        cJSON* parsed = cJSON_Parse(written);
        lk_load(&l2, parsed);
        cJSON_Delete(parsed);
        CHECK(l2.nreplies == 2 && l2.replies[1].channel == 1234567890123456789ULL &&
              l2.replies[1].message == 9876543210987654321ULL);
        {
            int before = nasked;
            CHECK(lk_item(&l2, 160) && nasked == before); /* from the file, not asked */
        }
        lk_free(&l2);
        rg_free(&g2);
        free(written);
    }
    cJSON_Delete(saved);
    /* too old to edit: dropped */
    now_is += LK_AGE;
    lk_refresh(&l, fake_edit, NULL);
    CHECK(l.nreplies == 0);
    free(text);
    link_end();

    /* a reply whose message is gone (404) is dropped */
    link_start();
    gh("repos/o/r/issues/160", ISSUE);
    numbers[0] = 160;
    e = lk_reply((items[0] = issue, items), 1);
    cJSON_ReplaceItemInObject(e, "description", cJSON_CreateString("old"));
    lk_keep(&l, 7, 70, numbers, 1, e);
    cJSON_Delete(e);
    gone_too = 1;
    lk_refresh(&l, fake_edit, NULL);
    gone_too = 0;
    CHECK(l.nreplies == 0);
    /* at most LK_TRACK replies */
    for (n = 0; n < LK_TRACK + 3; ++n) lk_keep(&l, 1, (u64)n, numbers, 1, issue);
    CHECK(l.nreplies == LK_TRACK && l.replies[0].message == 3);
    link_end();
    cJSON_Delete(issue);
    cJSON_Delete(pull);
}

#define NIGHTLY                                                                                                        \
    "{\"name\":\"Nightly 2026-10-17 (44aa300)\",\"tag_name\":\"nightly-2026-10-17-44aa300\","                          \
    "\"html_url\":\"https://n/nightly-2026-10-17-44aa300\",\"prerelease\":false,\"assets\":["                          \
    "{\"name\":\"ScarletNX-windows-x86_64.zip\",\"size\":83886080,\"browser_download_url\":\"https://n/win\"},"        \
    "{\"name\":\"ScarletNX-linux-x86_64.AppImage\",\"size\":52428800,\"browser_download_url\":\"https://n/lin\"},"     \
    "{\"name\":\"ScarletNX-linux-x86_64.AppImage.zsync\",\"size\":156672,\"browser_download_url\":\"https://n/zs\"}]}"
static void commands(void) {
    int numbers[REI_MOST], n;
    cJSON* e;
    link_start();
    gh("repos/o/nightly/releases/latest", NIGHTLY);
    gh("repos/o/r/actions/workflows/build.yml/runs?event=schedule&branch=main&per_page=1",
       "{\"workflow_runs\":[{\"id\":76,\"status\":\"completed\"}]}");
    gh("repos/o/nightly/releases/tags/pr-355",
       "{\"name\":\"PR #355: Android menu files\",\"tag_name\":\"pr-355\",\"html_url\":\"https://n/pr-355\","
       "\"prerelease\":true,\"assets\":[]}");
    e = lk_answer(&l, "!nightly", numbers, &n);
    CHECK(n == 0);
    SAME(jstr(e, "title"), "Nightly 2026-10-17 (44aa300)");
    SAME(jstr(e, "url"), "https://n/nightly-2026-10-17-44aa300");
    /* a line a package, in the cards' order, its icon (the application's emoji), its system and its extension linked;
       a file the cards do not list (the AppImage's .zsync) left out, no size; the mascot beside them */
    {
        cJSON* emojis =
            cJSON_Parse("[{\"name\":\"wimsy_linux\",\"id\":\"11\"},{\"name\":\"wimsy_windows\",\"id\":\"22\"}]");
        char* printed;
        cJSON_Delete(e);
        cd_icons(emojis);
        e = lk_answer(&l, "!nightly", numbers, &n);
        printed = cJSON_PrintUnformatted(e);
        SAME(jstr(e, "description"), "<:wimsy_linux:11> Linux x86-64: [.AppImage](https://n/lin)\n"
                                     "<:wimsy_windows:22> Windows x86-64: [.zip](https://n/win)");
        CHECK(printed && !strstr(printed, "zsync") && !strstr(printed, "MB") &&
              !cJSON_GetObjectItemCaseSensitive(e, "fields"));
        SAME(jstr(cJSON_GetObjectItemCaseSensitive(e, "thumbnail"), "url"),
             "https://raw.githubusercontent.com/o/r/main/dist/icons/hicolor/128x128/apps/wimsy.png");
        free(printed);
        cJSON_Delete(e);
        /* an emoji the application lacks: its line without the icon */
        cd_icons(NULL);
        e = lk_answer(&l, "!nightly", numbers, &n);
        HAS(jstr(e, "description"), "Linux x86-64: [.AppImage](https://n/lin)");
        CHECK(!strstr(jstr(e, "description"), "<:"));
        cJSON_Delete(emojis);
    }
    cJSON_Delete(e);
    e = lk_answer(&l, "!nightly", numbers, &n);
    CHECK(times_asked("GET", "https://api.github.com/repos/o/nightly/releases/latest") == 1);
    cJSON_Delete(e);
    /* a scheduled Build of main not ended: the nightly being built, its run and when it began, above the newest */
    gh("repos/o/r/actions/workflows/build.yml/runs?event=schedule&branch=main&per_page=1",
       "{\"workflow_runs\":[{\"id\":77,\"status\":\"in_progress\",\"html_url\":\"https://g/77\","
       "\"run_started_at\":\"2026-11-26T00:23:11Z\"}]}");
    e = lk_answer(&l, "!nightly", numbers, &n);
    CHECK(e && !strncmp(jstr(e, "description"),
                        "A nightly is being built: [run 77](https://g/77), begun 2026-11-26 00:23 UTC.\n", 78));
    SAME(jstr(e, "url"), "https://n/nightly-2026-10-17-44aa300");
    cJSON_Delete(e);
    /* GitHub not listing the scheduled builds: said, the newest still given */
    answer("GET",
           "https://api.github.com/repos/o/r/actions/workflows/build.yml/runs?event=schedule&branch=main&per_page=1",
           502, "{}");
    e = lk_answer(&l, "!nightly", numbers, &n);
    CHECK(e && !strncmp(jstr(e, "description"), "Whether a nightly is being built is not known", 45));
    SAME(jstr(e, "url"), "https://n/nightly-2026-10-17-44aa300");
    cJSON_Delete(e);
    e = lk_answer(&l, "!release", numbers, &n);
    CHECK(e && !strncmp(jstr(e, "description"), "No version is released yet. The newest nightly:", 47));
    SAME(jstr(e, "url"), "https://n/nightly-2026-10-17-44aa300");
    cJSON_Delete(e);
    e = lk_answer(&l, "!release #355", numbers, &n);
    SAME(jstr(e, "url"), "https://n/pr-355");
    CHECK(jnum(e, "color") == 0x6E7781);
    HAS(jstr(e, "description"), "No downloads on it yet.");
    cJSON_Delete(e);
    e = lk_answer(&l, "!release #354", numbers, &n);
    CHECK(e && !strncmp(jstr(e, "description"), "No builds for #354", 18));
    cJSON_Delete(e);
    CHECK(!lk_answer(&l, "try !nightly later", numbers, &n) && !lk_answer(&l, "!nightlies", numbers, &n));
    link_end();
    link_start();
    gh("repos/o/r/releases/latest",
       "{\"name\":\"ScarletNX 0.1.0\",\"tag_name\":\"v0.1.0\",\"html_url\":\"https://x/v0.1.0\",\"assets\":[]}");
    e = lk_answer(&l, "  !Release ", numbers, &n);
    SAME(jstr(e, "url"), "https://x/v0.1.0");
    CHECK(jnum(e, "color") == REI_OPEN);
    cJSON_Delete(e);
    /* no nightly yet, one being built: both said */
    gh("repos/o/r/actions/workflows/build.yml/runs?event=schedule&branch=main&per_page=1",
       "{\"workflow_runs\":[{\"id\":78,\"status\":\"queued\",\"html_url\":\"https://g/78\","
       "\"created_at\":\"2026-11-26T03:23:00Z\"}]}");
    e = lk_answer(&l, "!nightly", numbers, &n);
    SAME(jstr(e, "description"),
         "No nightly is published yet. A nightly is being built: [run 78](https://g/78), begun 2026-11-26 03:23 UTC.");
    cJSON_Delete(e);
    link_end();
    rg_init(&g, "o/r", "p/builds", fake, NULL);
    SAME(g.nightly, "p/builds");
    rg_free(&g);
}

/* ---- GitHub's ETags and pages ---- */
static void github_tests(void) {
    cJSON* got;
    int status, i;
    link_start();
    gh("repos/o/r/thing", "{\"n\":1}");
    fixtures[nfixtures - 1].etag = rei_strdup("\"e1\"");
    got = rg_get(&g, "repos/o/r/thing", &status);
    CHECK(status == 200 && jnum(got, "n") == 1);
    cJSON_Delete(got);
    got = rg_get(&g, "repos/o/r/thing", &status); /* asked with its ETag: 304, the kept answer */
    CHECK(status == 304 && jnum(got, "n") == 1 && g.unchanged == 1);
    cJSON_Delete(got);
    got = rg_get(&g, "repos/o/r/none", &status);
    CHECK(!got && status == 404);
    {
        Buf page = {0};
        buf_add(&page, "[");
        for (i = 0; i < 100; ++i) buf_printf(&page, "%s{\"i\":%d}", i ? "," : "", i);
        buf_add(&page, "]");
        gh("repos/o/r/list?per_page=100&page=1", page.text);
        gh("repos/o/r/list?per_page=100&page=2", "[{\"i\":100}]");
        buf_free(&page);
    }
    got = rg_pages(&g, "repos/o/r/list", NULL);
    CHECK(cJSON_GetArraySize(got) == 101 && jnum(cJSON_GetArrayItem(got, 100), "i") == 100);
    cJSON_Delete(got);
    gh("repos/o/r/runs?x=1&per_page=100&page=1", "{\"workflow_runs\":[{\"id\":5}]}");
    got = rg_pages(&g, "repos/o/r/runs?x=1", "workflow_runs");
    CHECK(cJSON_GetArraySize(got) == 1);
    cJSON_Delete(got);
    CHECK(!rg_pages(&g, "repos/o/r/missing", NULL));
    link_end();
}

/* ---- Discord's REST ---- */
static double slept;
static void no_sleep(double s) { slept += s; }
static void rest_tests(void) {
    Dc d;
    SAME(REI_AGENT, "DiscordBot (https://github.com/scarletnx/wimsy-core, 1) reiwimsy");
    u64 ids[4];
    int webhook[4], mine[4];
    cJSON* e = cJSON_Parse("{\"description\":\"x\"}");
    forget();
    dc_init(&d, fake, NULL);
    d.sleep = no_sleep;
    answer("POST", DC_API "channels/7/messages", 200, "{\"id\":\"1234567890123456789\"}");
    CHECK(dc_post(&d, 7, e, 99) == 1234567890123456789ULL);
    HAS(posted_body, "\"message_reference\":{\"message_id\":\"99\"");
    HAS(posted_body, "\"allowed_mentions\":{\"parse\":[],\"replied_user\":false}");
    /* a reply told to ping: its author alone */
    CHECK(dc_reply(&d, 7, e, 98, 1) == 1234567890123456789ULL);
    HAS(posted_body, "\"message_reference\":{\"message_id\":\"98\"");
    HAS(posted_body, "\"allowed_mentions\":{\"parse\":[],\"replied_user\":true}");
    CHECK(dc_reply(&d, 7, e, 97, 0) && strstr(posted_body, "\"replied_user\":false"));
    answer("PATCH", DC_API "channels/7/messages/70", 404, "{\"code\":10008}");
    CHECK(dc_edit(&d, 7, 70, e) == 404);
    answer("PATCH", DC_API "channels/7/messages/71", 429, "{\"retry_after\":0.25}");
    slept = 0;
    CHECK(dc_edit(&d, 7, 71, e) == 429 && times_asked("PATCH", DC_API "channels/7/messages/71") == DC_TRIES);
    CHECK(slept > 0.99 && slept < 1.01); /* four waits of 0.25 s */
    answer("DELETE", DC_API "channels/7/messages/72", 204, "");
    CHECK(dc_remove(&d, 7, 72) == 0);
    answer("DELETE", DC_API "channels/7/messages/73", 403, "{\"code\":50013}");
    CHECK(dc_remove(&d, 7, 73) == 403);
    answer("GET", DC_API "users/@me", 200, "{\"id\":\"42\"}");
    answer("GET", DC_API "channels/7/messages?limit=4", 200,
           "[{\"id\":\"9\",\"webhook_id\":\"1\",\"author\":{\"id\":\"5\"}},{\"id\":\"8\",\"author\":{\"id\":\"42\"}},"
           "{\"id\":\"7\",\"author\":{\"id\":\"3\"}}]");
    CHECK(dc_history(&d, 7, 0, ids, webhook, mine, 4) == 3);
    CHECK(ids[0] == 9 && webhook[0] && !mine[0] && ids[1] == 8 && !webhook[1] && mine[1] && !webhook[2] && !mine[2]);
    CHECK(dc_history(&d, 8, 0, ids, webhook, mine, 4) == -1);
    { /* what Discord answers now, for the gateway's end: the status, and past a 2xx the body or why none came */
        char* said;
        answer("GET", DC_API "gateway/bot", 200, "{\"url\":\"wss://gateway.discord.gg\"}");
        SAME(said = dc_said(&d, "gateway/bot"), "200");
        free(said);
        answer("GET", DC_API "gateway/bot", 401, "{\"message\": \"401: Unauthorized\", \"code\": 0}");
        SAME(said = dc_said(&d, "gateway/bot"), "401: {\"message\": \"401: Unauthorized\", \"code\": 0}");
        free(said);
        answer("GET", DC_API "gateway/bot", 0,
               "GET https://discord.com/api/v10/gateway/bot: Connection timed out after 30001 milliseconds");
        SAME(said = dc_said(&d, "gateway/bot"),
             "0: GET https://discord.com/api/v10/gateway/bot: Connection timed out after 30001 milliseconds");
        free(said);
    }
    cJSON_Delete(e);
    forget();
}

/* ---- the cards ---- */
typedef struct {
    u64 channel, message;
    char* description; /* the embed, as JSON */
    int removed;
    u64 author;  /* 0: the bot, or a webhook */
    int webhook; /* a webhook's */
} Sent;
static Sent sent[1024];
static int nsent, cd_edits, cd_removes, edit_gone, remove_fails, post_fails;
static u64 next_id = 1000;
static u64 c_post(void* ctx, u64 channel, const cJSON* embed) {
    (void)ctx;
    if (post_fails) return 0;
    sent[nsent].removed = sent[nsent].webhook = 0;
    sent[nsent].author = 0;
    sent[nsent].channel = channel;
    sent[nsent].message = ++next_id;
    sent[nsent].description = cJSON_PrintUnformatted(embed);
    return sent[nsent++].message;
}
static Sent* sent_of(u64 message) {
    int i;
    for (i = 0; i < nsent; ++i)
        if (sent[i].message == message) return &sent[i];
    return NULL;
}
static int c_edit(void* ctx, u64 channel, u64 message, const cJSON* embed) {
    Sent* s = sent_of(message);
    (void)ctx;
    (void)channel;
    ++cd_edits;
    if (edit_gone || !s || s->removed) return 404;
    free(s->description);
    s->description = cJSON_PrintUnformatted(embed);
    return 0;
}
static int c_remove(void* ctx, u64 channel, u64 message) {
    Sent* s = sent_of(message);
    (void)ctx;
    (void)channel;
    ++cd_removes;
    if (remove_fails) return remove_fails; /* Discord's status */
    if (s) s->removed = 1;
    return 0;
}
/* a channel's messages as Discord lists them: channel 1's an old webhook card and someone talking; any other's those
   sent there and not removed, the newest first, older than before. history_calls counts channel 1's */
static int history_calls;
static int c_history(void* ctx, u64 channel, u64 before, u64* ids, int* webhook, int* mine, int max) {
    int n = 0;
    u64 last = before ? before : (u64)-1;
    (void)ctx;
    if (channel == 1) {
        ++history_calls;
        if (before) return 0;
        ids[0] = 51; /* an old webhook card */
        webhook[0] = 1;
        mine[0] = 0;
        ids[1] = 52; /* someone talking */
        webhook[1] = 0;
        mine[1] = 0;
        return 2;
    }
    while (n < max) {
        int i, best = -1;
        for (i = 0; i < nsent; ++i)
            if (sent[i].channel == channel && !sent[i].removed && sent[i].message < last &&
                (best < 0 || sent[i].message > sent[best].message))
                best = i;
        if (best < 0) break;
        ids[n] = sent[best].message;
        webhook[n] = sent[best].webhook;
        mine[n] = !sent[best].author && !sent[best].webhook;
        last = sent[best].message;
        ++n;
    }
    return n;
}
static const char* issue_label = "kind: bug";
static void cards_fixtures(const char* issue_state, const char* closed_at) {
    char* issue = rei_format(
        "{\"number\":12,\"title\":\"Crash on start\",\"html_url\":\"https://github.com/o/r/issues/12\",\"state\":\"%s\","
        "\"state_reason\":%s,\"closed_at\":%s,\"comments\":1,\"body\":\"It crashes <!-- x -->here.\","
        "\"labels\":[{\"name\":\"%s\",\"color\":\"d73a4a\"}],\"assignees\":[],\"user\":{\"login\":\"ana\","
        "\"html_url\":\"https://github.com/ana\"},\"type\":{\"name\":\"Bug\"}}",
        issue_state, strcmp(issue_state, "open") ? "\"completed\"" : "null", closed_at, issue_label);
    char* listing = rei_format("[%s]", issue);
    gh("repos/o/r/issues/12", issue);
    gh("repos/o/r/issues/12/comments?per_page=100&page=1",
       "[{\"user\":{\"login\":\"bot\",\"type\":\"Bot\"},\"body\":\"beep\",\"created_at\":\"2026-10-20T10:00:00Z\"},"
       "{\"user\":{\"login\":\"bia\",\"type\":\"User\"},\"body\":\"Same **here**\",\"html_url\":\"https://c/1\","
       "\"created_at\":\"2026-10-20T09:00:00Z\"}]");
    answer("POST", "https://api.github.com/graphql", 200,
           "{\"data\":{\"repository\":{\"issue\":{\"closedByPullRequestsReferences\":{\"nodes\":[{\"number\":13,"
           "\"url\":\"https://github.com/o/r/pull/13\"}]}}}}}");
    gh("repos/o/r/issues?state=open&per_page=100&page=1", strcmp(issue_state, "open") ? "[]" : listing);
    free(issue);
    free(listing);
    gh("repos/o/nightly/releases?per_page=100&page=1", "[]");
    gh("repos/o/r/releases?per_page=100&page=1", "[]");
    gh("repos/o/r/actions/workflows/build.yml/runs?branch=main&event=push&per_page=1", "{\"workflow_runs\":[]}");
    gh("repos/o/r/actions/workflows/build.yml/runs?per_page=20", "{\"workflow_runs\":[]}");
}
#define COMMENTS_DAN                                                                                                   \
    "[{\"user\":{\"login\":\"bia\",\"type\":\"User\"},\"body\":\"Same\",\"html_url\":\"https://c/1\","                 \
    "\"created_at\":\"2026-10-20T09:00:00Z\"},{\"user\":{\"login\":\"dan\",\"type\":\"User\"},\"body\":\"Mine too\","  \
    "\"html_url\":\"https://c/5\",\"created_at\":\"2026-10-20T11:00:00Z\"}]"
/* the issue's comments: one by who, at when */
static void comment_by(const char* who, const char* when) {
    char* list = rei_format("[{\"user\":{\"login\":\"%s\",\"type\":\"User\"},\"body\":\"hi\",\"html_url\":"
                            "\"https://c/%s\",\"created_at\":\"%s\"}]",
                            who, who, when);
    gh("repos/o/r/issues/12/comments?per_page=100&page=1", list);
    free(list);
}
/* a round 30 s on, GitHub listing these issues as updated since the last */
static void updated(Cards* c, const char* listed) {
    char* listing =
        rei_format("repos/o/r/issues?state=all&sort=updated&direction=desc&since=%s&per_page=100&page=1", c->since);
    gh(listing, listed);
    free(listing);
    now_is += 30;
    cd_round(c);
}
static void cards_tests(void) {
    Cards c;
    CdDiscord dc = {NULL, c_post, c_edit, c_remove, c_history};
    char* listing;
    forget();
    now_is = 1760954400; /* 2025-10-20T10:00:00Z */
    rg_init(&g, "o/r", NULL, fake, NULL);
    cd_init(&c, &g, dc, 0, 1, 3);
    cards_fixtures("open", "null");
    /* the first round: the webhook's old card deleted, talk left; the open issue's card posted, nothing asked of
       the closed ones */
    cd_round(&c);
    CHECK(history_calls == 1 && cd_removes == 1 && sent_of(51) == NULL);
    CHECK(!times_asked("GET", "https://api.github.com/repos/o/r/issues?state=closed&per_page=100&page=1"));
    CHECK(nsent == 1 && sent[0].channel == 1);
    HAS(sent[0].description, "\"title\":\"#12: Crash on start\"");
    HAS(sent[0].description,
        "**Open**\\n**Bug** | \xF0\x9F\x9F\xA5 `kind: bug` | fixed by [#13](https://github.com/o/r/pull/13)");
    HAS(sent[0].description, "> It crashes here.");
    HAS(sent[0].description, "**Latest comment**, by **bia** ([on GitHub](https://c/1))\\n> Same **here**");
    HAS(sent[0].description, "\"footer\":{\"text\":\"1 comment, edited at each change and comment\"}");
    HAS(sent[0].description, "\"author\":{\"name\":\"ana\"");
    CHECK(!strstr(sent[0].description, "beep"));
    /* a second round with nothing new: no edit, no post */
    listing =
        rei_format("repos/o/r/issues?state=all&sort=updated&direction=desc&since=%s&per_page=100&page=1", c.since);
    gh(listing, "[]");
    free(listing);
    now_is += 30;
    cd_round(&c);
    CHECK(nsent == 1 && cd_edits == 0 && history_calls == 1);
    /* listed as updated, drawn the same: no edit either */
    listing =
        rei_format("repos/o/r/issues?state=all&sort=updated&direction=desc&since=%s&per_page=100&page=1", c.since);
    gh(listing, "[{\"number\":12}]");
    free(listing);
    now_is += 30;
    cd_round(&c);
    CHECK(nsent == 1 && cd_edits == 0 && !c.cards[0].dirty);
    /* a label changed: edited where it is */
    issue_label = "kind: crash";
    cards_fixtures("open", "null");
    updated(&c, "[{\"number\":12}]");
    CHECK(nsent == 1 && cd_edits == 1 && !sent[0].removed);
    HAS(sent[0].description, "`kind: crash`");
    /* a person's comment: to the end of the channel, the new card posted before the old one is deleted */
    gh("repos/o/r/issues/12/comments?per_page=100&page=1", COMMENTS_DAN);
    updated(&c, "[{\"number\":12}]");
    CHECK(nsent == 2 && sent[0].removed && !sent[1].removed && c.cards[0].message == sent[1].message);
    CHECK(cd_edits == 1 && cd_removes == 2 && !c.cards[0].stale);
    HAS(sent[1].description, "by **dan**");
    HAS(sent[1].description, "`kind: crash`");
    /* closed: to the end as it closed, deleted a day after */
    issue_label = "kind: bug";
    cards_fixtures("closed", "\"2025-10-20T10:01:00Z\"");
    gh("repos/o/r/issues/12/comments?per_page=100&page=1", COMMENTS_DAN);
    updated(&c, "[{\"number\":12}]");
    CHECK(nsent == 3 && sent[1].removed && !sent[2].removed && cd_edits == 1 && cd_removes == 3);
    HAS(sent[2].description, "**Closed** as done");
    now_is = 1760954460 + CD_DAY;
    updated(&c, "[]");
    CHECK(sent[2].removed && cd_removes == 4);
    {
        cJSON* saved = cd_save(&c);
        char* text = cJSON_PrintUnformatted(saved);
        HAS(text, "\"key\":\"issue-12\",\"message\":\"0\"");
        HAS(text, "\"gone\":true");
        free(text);
        cJSON_Delete(saved);
    }
    /* reopened: a card again, at the end; the next round leaves it */
    cards_fixtures("open", "null");
    gh("repos/o/r/issues/12/comments?per_page=100&page=1", COMMENTS_DAN);
    updated(&c, "[{\"number\":12}]");
    CHECK(nsent == 4 && sent[3].channel == 1 && !sent[3].removed && !c.cards[0].gone && !c.cards[0].closed);
    updated(&c, "[{\"number\":12}]");
    CHECK(nsent == 4 && cd_edits == 1 && cd_removes == 4);
    /* a card deleted by hand on Discord: posted again at its next change */
    edit_gone = 1;
    issue_label = "kind: crash";
    cards_fixtures("open", "null");
    gh("repos/o/r/issues/12/comments?per_page=100&page=1", COMMENTS_DAN);
    updated(&c, "[{\"number\":12}]");
    edit_gone = 0;
    CHECK(nsent == 5 && c.cards[0].message == sent[4].message);
    HAS(sent[4].description, "`kind: crash`");
    gh("repos/o/r/issues/12/comments?per_page=100&page=1",
       "[{\"user\":{\"login\":\"cid\",\"type\":\"User\"},\"body\":\"newer\",\"html_url\":\"https://c/2\","
       "\"created_at\":\"2026-10-21T09:00:00Z\"}]");
    updated(&c, "[{\"number\":12}]");
    CHECK(nsent == 6 && sent[4].removed);
    HAS(sent[5].description, "by **cid**");
    /* the old card not deleted at a move: kept as stale, deleted by a later round, even with nothing new */
    remove_fails = 500;
    comment_by("eve", "2026-10-22T09:00:00Z");
    updated(&c, "[{\"number\":12}]");
    CHECK(nsent == 7 && c.cards[0].message == sent[6].message && c.cards[0].stale == sent[5].message);
    CHECK(!sent[5].removed && !c.cards[0].dirty);
    updated(&c, "[]");
    CHECK(c.cards[0].stale == sent[5].message);
    remove_fails = 0;
    updated(&c, "[]");
    CHECK(sent[5].removed && !c.cards[0].stale && nsent == 7);
    /* no move while one stays: the activity waits, the card dirty, until the old one is gone (404: deleted by hand) */
    remove_fails = 500;
    comment_by("fay", "2026-10-23T09:00:00Z");
    updated(&c, "[{\"number\":12}]");
    CHECK(nsent == 8 && c.cards[0].stale == sent[6].message);
    comment_by("gus", "2026-10-24T09:00:00Z");
    updated(&c, "[{\"number\":12}]");
    CHECK(nsent == 8 && c.cards[0].dirty && c.cards[0].stale == sent[6].message);
    remove_fails = 404;
    updated(&c, "[]");
    remove_fails = 0;
    CHECK(!c.cards[0].stale && nsent == 9 && c.cards[0].message == sent[8].message && !c.cards[0].dirty);
    HAS(sent[8].description, "by **gus**");
    /* GitHub not answering: the card left as it was drawn, and drawn at the next round */
    {
        int before = nsent, edits_before = cd_edits;
        answer("GET", "https://api.github.com/repos/o/r/issues/12", 502, "{}");
        listing =
            rei_format("repos/o/r/issues?state=all&sort=updated&direction=desc&since=%s&per_page=100&page=1", c.since);
        gh(listing, "[{\"number\":12}]");
        free(listing);
        now_is += 30;
        cd_round(&c);
        CHECK(nsent == before && cd_edits == edits_before);
        CHECK(c.ncards == 1 && c.cards[0].dirty);
        /* the issue there but who fixes it not (GraphQL): no card from half of it either */
        cards_fixtures("open", "null");
        gh("repos/o/r/issues/12/comments?per_page=100&page=1", "[]");
        answer("POST", "https://api.github.com/graphql", 502, "{}");
        now_is += 30;
        cd_round(&c);
        CHECK(nsent == before && cd_edits == edits_before && c.cards[0].dirty);
    }
    {
        cJSON* saved = cd_save(&c);
        Cards again;
        cd_init(&again, &g, dc, 0, 1, 3);
        cd_load(&again, saved);
        CHECK(again.ncards == 1 && again.cards[0].message == c.cards[0].message && again.purged_issues);
        SAME(again.since, c.since);
        SAME(again.cards[0].moved, c.cards[0].moved);
        CHECK(*again.cards[0].moved && again.cards[0].stale == c.cards[0].stale);
        cd_free(&again);
        cJSON_Delete(saved);
    }
    /* cards kept before their activity was: an issue's drawn again once, where it is, to learn what moves it; a
       nightly's, of a state from before the nightly channel's two cards, not kept, its message deleted */
    {
        int before = nsent, removes = cd_removes;
        char* old = rei_format("{\"cards\":[{\"key\":\"issue-12\",\"message\":\"%llu\",\"drawn\":\"x\",\"closed\":0,"
                               "\"gone\":false,\"dirty\":false},{\"key\":\"nightly-abc\",\"message\":\"5\",\"drawn\":"
                               "\"y\",\"closed\":0,\"gone\":false,\"dirty\":false}],\"since\":\"%s\",\"head\":\"a\","
                               "\"purged_issues\":true}",
                               (unsigned long long)c.cards[0].message, c.since);
        cJSON* saved = cJSON_Parse(old);
        Cards again;
        cd_init(&again, &g, dc, 0, 1, 3);
        cd_load(&again, saved);
        CHECK(again.ncards == 1 && again.cards[0].dirty && !*again.cards[0].moved);
        cards_fixtures("open", "null");
        updated(&again, "[]");
        CHECK(nsent == before && cd_removes == removes + 1 && again.cards[0].message == c.cards[0].message);
        CHECK(*again.cards[0].moved && !again.cards[0].dirty && again.ncards == 1);
        cd_free(&again);
        cJSON_Delete(saved);
        free(old);
    }
    cd_free(&c);
    rg_free(&g);
    forget();
}

/* s with each @x written out as the commit of 40 x's; owned */
static char* shas(const char* s) {
    Buf b = {0};
    int i;
    for (; *s; ++s)
        if (*s == '@' && s[1])
            for (++s, i = 0; i < 40; ++i) buf_addn(&b, s, 1);
        else
            buf_addn(&b, s, 1);
    return buf_take(&b);
}
static void gh_shas(const char* path, const char* body) {
    char *p = shas(path), *b = shas(body);
    gh(p, b);
    free(p);
    free(b);
}
/* ---- the nightly channel ---- */
#define NC 3 /* its id */
/* a nightly's release, @x its commit, published on day at 03:00 with one package */
static char* nightly_of(char x, const char* day) {
    return rei_format(
        "{\"tag_name\":\"nightly-%s-%c%c%c%c%c%c%c\",\"name\":\"Nightly %s (%c%c%c%c%c%c%c)\",\"html_url\":"
        "\"https://n/%c\",\"published_at\":\"%sT03:00:00Z\",\"body\":\"Built from main at "
        "[`%c%c%c%c%c%c%c`](https://github.com/o/r/commit/@%c).\",\"assets\":[{\"name\":"
        "\"ScarletNX-linux-x86_64.AppImage\",\"size\":1,\"browser_download_url\":\"https://n/%c/lin\"}]}",
        day, x, x, x, x, x, x, x, day, x, x, x, x, x, x, x, x, day, x, x, x, x, x, x, x, x, x);
}
/* the nightly repository's releases: list, the nightlies (x day) separated by spaces, listed oldest first as GitHub
   may */
static void nightlies_are(const char* list) {
    Buf b = {0};
    const char* p;
    buf_add(&b, "[");
    for (p = list; *p; p += *p == ' ') {
        char day[11], *one;
        snprintf(day, sizeof day, "%.10s", p + 2);
        one = nightly_of(*p, day);
        buf_printf(&b, "%s%s", b.len > 1 ? "," : "", one);
        free(one);
        p += 12;
    }
    buf_add(&b, "]");
    gh_shas("repos/o/nightly/releases?per_page=100&page=1", b.text);
    buf_free(&b);
}
/* the repository's releases: a version, and main's prerelease of @x when x is not 0 */
static void main_is(char x) {
    char* list =
        x ? rei_format("[{\"tag_name\":\"v1.0.0\",\"name\":\"v1.0.0\",\"published_at\":\"2026-10-01T00:00:00Z\","
                       "\"body\":\"The first.\",\"assets\":[]},{\"tag_name\":\"main-%c%c%c%c%c%c%c\",\"name\":"
                       "\"main (%c%c%c%c%c%c%c)\",\"html_url\":\"https://g/main-%c\",\"prerelease\":true,"
                       "\"published_at\":\"2026-11-26T10:00:00Z\",\"body\":\"The build of `main` at "
                       "[`%c%c%c%c%c%c%c`](https://github.com/o/r/commit/@%c).\",\"assets\":[{\"name\":"
                       "\"ScarletNX-linux-x86_64.AppImage\",\"size\":2,\"browser_download_url\":"
                       "\"https://g/main-%c/lin\"}]}]",
                       x, x, x, x, x, x, x, x, x, x, x, x, x, x, x, x, x, x, x, x, x, x, x, x)
          : rei_strdup("[{\"tag_name\":\"v1.0.0\",\"name\":\"v1.0.0\",\"published_at\":"
                       "\"2026-10-01T00:00:00Z\",\"body\":\"The first.\",\"assets\":[]}]");
    gh_shas("repos/o/r/releases?per_page=100&page=1", list);
    free(list);
}
/* what drawing @x asks GitHub: its commit, the comparison from @from with a change subject, its builds (run, when
   not 0, a push's that succeeded, with no job) */
static void commit_is(char x, char from, const char* subject, int run) {
    char *path = rei_format("repos/o/r/commits/@%c", x), *cmp = rei_format("repos/o/r/compare/@%c...@%c", from, x),
         *runs = rei_format("repos/o/r/actions/workflows/build.yml/runs?head_sha=@%c&per_page=100&page=1", x),
         *changed = rei_format("{\"status\":\"ahead\",\"commits\":[{\"sha\":\"@%c\",\"parents\":[{\"sha\":\"@%c\"}],"
                               "\"commit\":{\"message\":\"%s\"}}]}",
                               x, from, subject),
         *listed = run ? rei_format("{\"workflow_runs\":[{\"id\":%d,\"event\":\"push\",\"status\":\"completed\","
                                    "\"conclusion\":\"success\",\"created_at\":\"2026-11-26T09:00:00Z\"}]}",
                                    run)
                       : rei_strdup("{\"workflow_runs\":[]}"),
         *jobs = rei_format("repos/o/r/actions/runs/%d/jobs?per_page=100&page=1", run);
    gh_shas(path, "{\"commit\":{\"committer\":{\"date\":\"2026-11-26T00:10:00Z\"}}}");
    gh_shas(cmp, changed);
    gh_shas(runs, listed);
    if (run) gh(jobs, "{\"jobs\":[]}");
    free(path);
    free(cmp);
    free(runs);
    free(changed);
    free(listed);
    free(jobs);
}
/* a message in the nightly channel, posted at when (seconds), n apart from others of that second, by the bot (author
   0), a webhook or a person, its embed's JSON with each @x a commit; its id */
static u64 posted_id(double when, int n) { return (((u64)(when * 1000) - 1420070400000ULL) << 22) + (u64)n; }
static u64 in_channel(double when, int n, u64 author, int webhook, const char* embed) {
    sent[nsent].channel = NC;
    sent[nsent].message = posted_id(when, n);
    sent[nsent].description = shas(embed);
    sent[nsent].removed = 0;
    sent[nsent].author = author;
    sent[nsent].webhook = webhook;
    return sent[nsent++].message;
}
/* the nightly channel's cards left, the bot's and the webhooks', in its order (oldest first), into each: their
   count */
static int cards_left(Sent** each, int most) {
    int n = 0, i, j;
    for (i = 0; i < nsent; ++i)
        if (sent[i].channel == NC && !sent[i].removed && !sent[i].author) {
            for (j = n; j > 0 && each[j - 1]->message > sent[i].message; --j)
                if (j < most) each[j] = each[j - 1];
            if (j < most) each[j] = &sent[i];
            ++n;
        }
    return n;
}
#define NIGHTLY_TITLE_IS "\"title\":\"Nightly (recommended)\""
#define MAIN_TITLE_IS    "\"title\":\"Latest development build (main): newer, may be less stable\""
/* the channel holds the nightly's card and main's, in that order, and nothing else of the bot's or a webhook's: 1 */
static int two_cards(Sent** each) {
    return cards_left(each, 4) == 2 && strstr(each[0]->description, NIGHTLY_TITLE_IS) &&
           strstr(each[1]->description, MAIN_TITLE_IS);
}
/* The nightly channel, from a state of one card per nightly: two cards left, the newest nightly's then main's newest
   build's, every other message of the bot's and of a webhook deleted, a person's kept. Main's new build edits its card
   where it is; a new nightly posts both again, in order, and deletes the old two; main's newest build failing with no
   package says so on its card, pointing at the nightly; a post that fails deletes nothing; an older nightly than the
   one shown (the newest missing from a listing) edits the card where it is; the nightly's card deleted by hand is
   posted again with main's under it; an old message not deleted at a move is deleted by a later round */
static void nightly_channel(void) {
    Cards c;
    CdDiscord dc = {NULL, c_post, c_edit, c_remove, c_history};
    Sent* each[4];
    u64 hook, person, old_c, old_a, old_b, lost_a, nightly_card, main_card;
    int removes, edits_at, posts;
    char *old, *text;
    cJSON *state, *saved;
    forget();
    now_is = 1795000000;
    rg_init(&g, "o/r", NULL, fake, NULL);
    cd_init(&c, &g, dc, 0, 0, NC);
    gh("repos/o/r/actions/workflows/build.yml/runs?per_page=20", "{\"workflow_runs\":[]}");
    nightlies_are("a 2026-11-24 b 2026-11-25 c 2026-11-26");
    main_is('d');
    commit_is('c', 'b', "[gui] a row", 0);
    commit_is('d', 'c', "[vk] a fix", 95);
    /* the channel as the cards of one per nightly left it: a webhook's old card, someone talking, the newest
       nightly's card at the top, older ones below it, one of them twice (forgotten, posted again) */
    hook = in_channel(now_is - 9000, 0, 0, 1, "{\"title\":\"Nightly from the workflow\"}");
    person = in_channel(now_is - 8000, 0, 7777, 0, "{\"description\":\"nice\"}");
    old_c = in_channel(now_is - 7000, 0, 0, 0, "{\"title\":\"Nightly 2026-11-26 (ccccccc)\"}");
    old_a = in_channel(now_is - 6000, 0, 0, 0, "{\"title\":\"Nightly 2026-11-24 (aaaaaaa)\"}");
    old_b = in_channel(now_is - 5000, 0, 0, 0, "{\"title\":\"Nightly 2026-11-25 (bbbbbbb)\"}");
    lost_a = in_channel(now_is - 4000, 0, 0, 0, "{\"title\":\"Nightly 2026-11-24 (aaaaaaa)\"}");
    old = rei_format("{\"cards\":[{\"key\":\"nightly-%s\",\"message\":\"%llu\",\"drawn\":\"x\",\"dirty\":false},"
                     "{\"key\":\"nightly-%s\",\"message\":\"%llu\",\"drawn\":\"x\",\"dirty\":false},"
                     "{\"key\":\"nightly-%s\",\"message\":\"%llu\",\"drawn\":\"x\",\"dirty\":false}]}",
                     "cccccccccccccccccccccccccccccccccccccccc", (unsigned long long)old_c,
                     "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", (unsigned long long)old_a,
                     "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", (unsigned long long)old_b);
    state = cJSON_Parse(old);
    cd_load(&c, state);
    cJSON_Delete(state);
    free(old);
    removes = cd_removes;
    now_is += 30;
    cd_round(&c);
    /* two cards, in order, every old one and the webhook's deleted, the person's kept */
    CHECK(two_cards(each));
    CHECK(sent_of(hook)->removed && sent_of(old_c)->removed && sent_of(old_a)->removed && sent_of(old_b)->removed &&
          sent_of(lost_a)->removed && !sent_of(person)->removed && cd_removes == removes + 5);
    if (cards_left(each, 4) != 2) {
        cd_free(&c);
        rg_free(&g);
        forget();
        return; /* the rest stands on these two */
    }
    nightly_card = each[0]->message;
    main_card = each[1]->message;
    HAS(each[0]->description, "The one to download if you are not sure: [Nightly 2026-11-26](https://n/c)");
    HAS(each[0]->description, "made once a day from main at [`ccccccc`]");
    HAS(each[0]->description, "**Changes since the nightly before**");
    HAS(each[0]->description, "a row");
    HAS(each[0]->description, "https://n/c/lin");
    HAS(each[1]->description, "The newest build of main, made from [`ddddddd`]");
    HAS(each[1]->description, "the nightly above and the changes below. Newer, and it may have new bugs");
    HAS(each[1]->description, "**Changes since the nightly** ([`ccccccc...ddddddd`]");
    HAS(each[1]->description, "a fix");
    HAS(each[1]->description, "https://g/main-d/lin");
    HAS(each[1]->description, "\"color\":2060267"); /* 0x1F6FEB */
    /* what lasts: both cards, by their keys */
    saved = cd_save(&c);
    text = cJSON_PrintUnformatted(saved);
    {
        char* want = rei_format("\"key\":\"nightly\",\"message\":\"%llu\"", (unsigned long long)nightly_card);
        HAS(text, want);
        free(want);
        want = rei_format("\"key\":\"main\",\"message\":\"%llu\"", (unsigned long long)main_card);
        HAS(text, want);
        free(want);
    }
    CHECK(!strstr(text, "\"key\":\"nightly-"));
    free(text);
    cJSON_Delete(saved);
    /* nothing new: nothing posted, edits_at or deleted, the channel not read again */
    posts = nsent;
    edits_at = cd_edits;
    removes = cd_removes;
    now_is += 30;
    cd_round(&c);
    CHECK(nsent == posts && cd_edits == edits_at && cd_removes == removes);
    /* main's newer build: its card edits_at where it is */
    main_is('e');
    commit_is('e', 'c', "[cpu] faster", 96);
    now_is += 30;
    cd_round(&c);
    CHECK(nsent == posts && cd_edits == edits_at + 1 && cd_removes == removes && two_cards(each));
    CHECK(each[0]->message == nightly_card && each[1]->message == main_card);
    HAS(each[1]->description, "made from [`eeeeeee`]");
    HAS(each[1]->description, "faster");
    /* a newer nightly: posted at the end, main's posted again under it, the old two deleted */
    nightlies_are("a 2026-11-24 b 2026-11-25 c 2026-11-26 e 2026-11-27");
    now_is += 30;
    cd_round(&c);
    CHECK(nsent == posts + 2 && cd_removes == removes + 2 && two_cards(each));
    CHECK(each[0]->message > main_card && each[1]->message > each[0]->message);
    CHECK(sent_of(nightly_card)->removed && sent_of(main_card)->removed);
    HAS(each[0]->description, "[Nightly 2026-11-27](https://n/e)");
    HAS(each[0]->description, "faster");
    HAS(each[1]->description, "made from [`eeeeeee`](https://github.com/o/r/commit/");
    HAS(each[1]->description, "the same commit as the nightly above.");
    nightly_card = each[0]->message;
    main_card = each[1]->message;
    /* main's newest build failed with no package (its prerelease gone): said on its card, where it is */
    main_is(0);
    gh_shas("repos/o/r/actions/workflows/build.yml/runs?branch=main&event=push&per_page=1",
            "{\"workflow_runs\":[{\"id\":97,\"event\":\"push\",\"head_branch\":\"main\",\"head_sha\":\"@f\","
            "\"status\":\"completed\",\"conclusion\":\"failure\",\"html_url\":\"https://g/97\"}]}");
    gh("repos/o/r/actions/runs/97/jobs?per_page=100&page=1",
       "{\"jobs\":[{\"name\":\"Linux AppImage (x86_64)\",\"status\":\"completed\",\"conclusion\":\"failure\","
       "\"html_url\":\"https://g/97/lin\"}]}");
    posts = nsent;
    edits_at = cd_edits;
    removes = cd_removes;
    now_is += 30;
    cd_round(&c);
    CHECK(nsent == posts && cd_edits == edits_at + 1 && cd_removes == removes && two_cards(each));
    CHECK(each[0]->message == nightly_card && each[1]->message == main_card);
    HAS(each[1]->description, "The newest build of main, from [`fffffff`]");
    HAS(each[1]->description, "failed, so it has no downloads: use the nightly above. ([Its build](https://g/97))");
    HAS(each[1]->description, "Failed ([log](https://g/97/lin))");
    HAS(each[1]->description, "\"color\":13801762"); /* 0xD29922 */
    /* the next push's build running, main still with no prerelease: said too */
    gh_shas("repos/o/r/actions/workflows/build.yml/runs?branch=main&event=push&per_page=1",
            "{\"workflow_runs\":[{\"id\":98,\"event\":\"push\",\"head_branch\":\"main\",\"head_sha\":\"@9\","
            "\"status\":\"in_progress\",\"html_url\":\"https://g/98\"}]}");
    gh("repos/o/r/actions/runs/98/jobs?per_page=100&page=1",
       "{\"jobs\":[{\"name\":\"Linux AppImage (x86_64)\",\"status\":\"in_progress\",\"html_url\":"
       "\"https://g/98/lin\"}]}");
    gh_shas("repos/o/r/actions/workflows/build.yml/runs?per_page=20",
            "{\"workflow_runs\":[{\"id\":98,\"event\":\"push\",\"head_branch\":\"main\",\"head_sha\":\"@9\","
            "\"status\":\"in_progress\",\"head_repository\":{\"full_name\":\"o/r\"}}]}");
    now_is += 30;
    cd_round(&c);
    CHECK(nsent == posts && cd_edits == edits_at + 2 && two_cards(each) && each[1]->message == main_card);
    HAS(each[1]->description, "from [`9999999`](https://github.com/o/r/commit/");
    HAS(each[1]->description,
        "is being built: its downloads appear here as each one is ready. Until then, use the nightly");
    HAS(each[1]->description, "Building ([log](https://g/98/lin))");
    HAS(each[1]->description, "\"color\":2060267");
    /* it ended well, its downloads not published yet; then stopped with no job failed: each said */
    gh_shas("repos/o/r/actions/workflows/build.yml/runs?branch=main&event=push&per_page=1",
            "{\"workflow_runs\":[{\"id\":98,\"event\":\"push\",\"head_branch\":\"main\",\"head_sha\":\"@9\","
            "\"status\":\"completed\",\"conclusion\":\"success\",\"html_url\":\"https://g/98\"}]}");
    gh("repos/o/r/actions/runs/98/jobs?per_page=100&page=1",
       "{\"jobs\":[{\"name\":\"Linux AppImage (x86_64)\",\"status\":\"completed\",\"conclusion\":\"success\","
       "\"html_url\":\"https://g/98/lin\"}]}");
    gh_shas("repos/o/r/actions/workflows/build.yml/runs?per_page=20",
            "{\"workflow_runs\":[{\"id\":98,\"event\":\"push\",\"head_branch\":\"main\",\"head_sha\":\"@9\","
            "\"status\":\"queued\",\"head_repository\":{\"full_name\":\"o/r\"}}]}");
    now_is += 30;
    cd_round(&c);
    HAS(each[1]->description,
        "is built: its downloads appear here in a few minutes. Until then, use the nightly above.");
    HAS(each[1]->description, "\"color\":2060267");
    gh_shas("repos/o/r/actions/workflows/build.yml/runs?branch=main&event=push&per_page=1",
            "{\"workflow_runs\":[{\"id\":98,\"event\":\"push\",\"head_branch\":\"main\",\"head_sha\":\"@9\","
            "\"status\":\"completed\",\"conclusion\":\"cancelled\",\"html_url\":\"https://g/98\"}]}");
    gh("repos/o/r/actions/runs/98/jobs?per_page=100&page=1",
       "{\"jobs\":[{\"name\":\"Linux AppImage (x86_64)\",\"status\":\"completed\",\"conclusion\":\"cancelled\","
       "\"html_url\":\"https://g/98/lin\"}]}");
    gh_shas("repos/o/r/actions/workflows/build.yml/runs?per_page=20",
            "{\"workflow_runs\":[{\"id\":98,\"event\":\"push\",\"head_branch\":\"main\",\"head_sha\":\"@9\","
            "\"status\":\"in_progress\",\"head_repository\":{\"full_name\":\"o/r\"}}]}");
    now_is += 30;
    cd_round(&c);
    HAS(each[1]->description, "was stopped before it made any download: use the nightly above.");
    HAS(each[1]->description, "\"color\":13801762");
    gh_shas("repos/o/r/actions/workflows/build.yml/runs?branch=main&event=push&per_page=1",
            "{\"workflow_runs\":[{\"id\":97,\"event\":\"push\",\"head_branch\":\"main\",\"head_sha\":\"@f\","
            "\"status\":\"completed\",\"conclusion\":\"failure\",\"html_url\":\"https://g/97\"}]}");
    gh_shas("repos/o/r/actions/workflows/build.yml/runs?per_page=20",
            "{\"workflow_runs\":[{\"id\":98,\"event\":\"push\",\"head_branch\":\"main\",\"head_sha\":\"@9\","
            "\"status\":\"completed\",\"conclusion\":\"failure\",\"head_repository\":{\"full_name\":\"o/r\"}}]}");
    now_is += 30;
    cd_round(&c);
    HAS(each[1]->description, "failed, so it has no downloads");
    /* a newer nightly whose post fails: nothing deleted; posted at the next round */
    nightlies_are("a 2026-11-24 b 2026-11-25 c 2026-11-26 e 2026-11-27 7 2026-11-28");
    commit_is('7', 'e', "[audio] louder", 0);
    post_fails = 1;
    now_is += 30;
    cd_round(&c);
    post_fails = 0;
    CHECK(nsent == posts && cd_removes == removes && two_cards(each));
    CHECK(each[0]->message == nightly_card && each[1]->message == main_card);
    now_is += 30;
    cd_round(&c);
    CHECK(nsent == posts + 2 && cd_removes == removes + 2 && two_cards(each));
    HAS(each[0]->description, "[Nightly 2026-11-28](https://n/7)");
    HAS(each[1]->description, "failed, so it has no downloads");
    nightly_card = each[0]->message;
    main_card = each[1]->message;
    /* the newest missing from a listing: the older one shown where it is, and the newest again the same way */
    posts = nsent;
    removes = cd_removes;
    nightlies_are("a 2026-11-24 b 2026-11-25 c 2026-11-26 e 2026-11-27");
    now_is += 30;
    cd_round(&c);
    CHECK(nsent == posts && cd_removes == removes && two_cards(each) && each[0]->message == nightly_card);
    HAS(each[0]->description, "[Nightly 2026-11-27](https://n/e)");
    nightlies_are("a 2026-11-24 b 2026-11-25 c 2026-11-26 e 2026-11-27 7 2026-11-28");
    now_is += 30;
    cd_round(&c);
    CHECK(nsent == posts && cd_removes == removes && two_cards(each) && each[0]->message == nightly_card);
    HAS(each[0]->description, "[Nightly 2026-11-28](https://n/7)");
    /* the nightly's card deleted by hand: posted again at its next change, and main's moved under it */
    sent_of(nightly_card)->removed = 1;
    gh_shas("repos/o/nightly/releases?per_page=100&page=1",
            "[{\"tag_name\":\"nightly-2026-11-28-7777777\",\"name\":\"Nightly 2026-11-28 (7777777)\",\"html_url\":"
            "\"https://n/7\",\"published_at\":\"2026-11-28T03:00:00Z\",\"body\":\"Built from main at "
            "[`7777777`](https://github.com/o/r/commit/@7). A note.\",\"assets\":[{\"name\":"
            "\"ScarletNX-linux-x86_64.AppImage\",\"size\":1,\"browser_download_url\":\"https://n/7/lin\"}]}]");
    now_is += 30;
    cd_round(&c);
    CHECK(nsent == posts + 2 && two_cards(each) && sent_of(main_card)->removed);
    CHECK(each[0]->message > main_card && each[1]->message > each[0]->message);
    nightly_card = each[0]->message;
    main_card = each[1]->message;
    /* the old two not deleted at a move: left, and deleted by a later round */
    nightlies_are("a 2026-11-24 b 2026-11-25 c 2026-11-26 e 2026-11-27 7 2026-11-28 8 2026-11-29");
    commit_is('8', '7', "[ci] green", 0);
    remove_fails = 500;
    now_is += 30;
    cd_round(&c);
    remove_fails = 0;
    CHECK(cards_left(each, 4) == 4 && !sent_of(nightly_card)->removed && !sent_of(main_card)->removed);
    now_is += 30;
    cd_round(&c);
    CHECK(two_cards(each) && sent_of(nightly_card)->removed && sent_of(main_card)->removed);
    HAS(each[0]->description, "[Nightly 2026-11-29](https://n/8)");
    main_card = each[1]->message;
    /* main's prerelease of a commit older than the nightly's (its newest push still building): the nightly to take */
    main_is('d');
    gh_shas("repos/o/r/compare/@8...@d", "{\"status\":\"behind\",\"commits\":[]}");
    now_is += 30;
    cd_round(&c);
    CHECK(two_cards(each) && each[1]->message == main_card);
    HAS(each[1]->description, "made from [`ddddddd`]");
    HAS(each[1]->description, ". The nightly above is newer: take the nightly.");
    CHECK(!strstr(each[1]->description, "Changes since the nightly"));
    /* a start with no state, its deletes failing on Discord's side (no refusal): the channel looked through again at
       the next round, the cards of before and a stray one deleted then */
    {
        Cards again;
        u64 stray = in_channel(now_is - 100, 0, 0, 0, "{\"title\":\"Nightly 2026-11-24 (aaaaaaa)\"}");
        cd_init(&again, &g, dc, 0, 0, NC);
        remove_fails = 500;
        now_is += 30;
        cd_round(&again);
        remove_fails = 0;
        CHECK(cards_left(each, 4) == 5 && !sent_of(stray)->removed);
        now_is += 30;
        cd_round(&again);
        CHECK(two_cards(each) && sent_of(stray)->removed && each[0]->message > main_card);
        cd_free(&again);
    }
    cd_free(&c);
    rg_free(&g);
    forget();
}

/* pull request 7 drawn, as a round draws it: its releases asked again */
static void drawn_pr(Cards* c) {
    int open;
    double closed;
    cJSON_Delete(c->nightlies);
    c->nightlies = NULL;
    cJSON_Delete(cd_pr(c, 7, &open, &closed));
}
/* its push's Build run in that status, then drawn */
static void run_is(Cards* c, const char* status) {
    char* runs = rei_format("{\"workflow_runs\":[{\"id\":9,\"event\":\"pull_request\",\"status\":%s,\"html_url\":"
                            "\"https://r/9\",\"created_at\":\"2026-10-20T11:00:00Z\"}]}",
                            status);
    gh("repos/o/r/actions/workflows/build.yml/runs?head_sha=1111111111111111111111111111111111111111&per_page=100&"
       "page=1",
       runs);
    free(runs);
    drawn_pr(c);
}
/* a pull request's card: its state, its commits, its builds */
static void pr_card(void) {
    Cards c;
    CdDiscord dc = {NULL, c_post, c_edit, c_remove, c_history};
    cJSON* e;
    int open;
    double closed;
    char* text;
    forget();
    rg_init(&g, "o/r", NULL, fake, NULL);
    cd_init(&c, &g, dc, 2, 0, 0);
    gh("repos/o/r/pulls/7",
       "{\"number\":7,\"title\":\"Faster [blit]\",\"html_url\":\"https://github.com/o/r/pull/7\",\"state\":\"open\","
       "\"draft\":false,\"merged\":false,\"comments\":0,\"review_comments\":0,\"labels\":[],\"assignees\":[],"
       "\"user\":{\"login\":\"ana\",\"html_url\":\"https://github.com/ana\",\"avatar_url\":\"https://a/1\"},"
       "\"head\":{\"sha\":\"1111111111111111111111111111111111111111\",\"repo\":{\"full_name\":\"o/r\"}},"
       "\"base\":{\"sha\":\"2222222222222222222222222222222222222222\"}}");
    gh("repos/o/r/pulls/7/commits?per_page=100&page=1",
       "[{\"commit\":{\"message\":\"[gpu] First\\n\\nbody\"}},{\"commit\":{\"message\":\"[gpu] Second *one*\"}}]");
    gh("repos/o/r/pulls/7/reviews?per_page=100&page=1", "[]");
    gh("repos/o/r/compare/2222222222222222222222222222222222222222...1111111111111111111111111111111111111111",
       "{\"merge_base_commit\":{\"sha\":\"3333333333333333333333333333333333333333\"}}");
    gh("repos/o/nightly/releases?per_page=100&page=1",
       "[{\"tag_name\":\"nightly-2026-10-20-3333333\",\"html_url\":\"https://n/1\",\"published_at\":\"2026-10-20T10:00:00Z\"},"
       "{\"tag_name\":\"pr-7\",\"html_url\":\"https://n/pr-7\",\"published_at\":\"2026-10-20T11:00:00Z\","
       "\"body\":\"built from /commits/1111111111111111111111111111111111111111\",\"assets\":[{\"name\":"
       "\"ScarletNX-windows-x86_64.zip\",\"size\":12345678,\"browser_download_url\":\"https://n/w\"},"
       "{\"name\":\"ScarletNX-linux-ppc64.AppImage\",\"size\":2345678,\"browser_download_url\":\"https://n/p\"}]}]");
    gh("repos/o/r/actions/workflows/build.yml/runs?head_sha=1111111111111111111111111111111111111111&per_page=100&page=1",
       "{\"workflow_runs\":[{\"id\":9,\"event\":\"pull_request\",\"status\":\"in_progress\",\"html_url\":\"https://r/9\","
       "\"created_at\":\"2026-10-20T11:00:00Z\"}]}");
    gh("repos/o/r/actions/runs/9/jobs?per_page=100&page=1",
       "{\"jobs\":[{\"name\":\"Linux AppImage (x86_64)\",\"status\":\"in_progress\",\"html_url\":\"https://j/1\"},"
       "{\"name\":\"Android APK\",\"status\":\"completed\",\"conclusion\":\"failure\",\"html_url\":\"https://j/2\"},"
       "{\"name\":\"Linux AppImage (aarch64)\",\"status\":\"completed\",\"conclusion\":\"success\"},"
       "{\"name\":\"macOS and iOS (arm64)\",\"status\":\"queued\",\"html_url\":\"https://j/3\"},"
       "{\"name\":\"Linux (x86_64, ASan + UBSan)\",\"status\":\"completed\",\"conclusion\":\"failure\","
       "\"html_url\":\"https://j/5\"},"
       "{\"name\":\"Linux (x86_64)\",\"status\":\"completed\",\"conclusion\":\"success\",\"html_url\":\"https://j/6\"},"
       "{\"name\":\"edenprobe (Linux x86_64, lavapipe)\",\"status\":\"in_progress\",\"html_url\":\"https://j/7\"}]}");
    answer("POST", "https://api.github.com/graphql", 200,
           "{\"data\":{\"repository\":{\"pullRequest\":{\"closingIssuesReferences\":{\"nodes\":[]}}}}}");
    {
        cJSON* emojis =
            cJSON_Parse("[{\"name\":\"wimsy_linux\",\"id\":\"11\"},{\"name\":\"wimsy_windows\",\"id\":\"22\"},"
                        "{\"name\":\"wimsy_android\",\"id\":\"33\"},{\"name\":\"wimsy_macos\",\"id\":\"44\"},"
                        "{\"name\":\"wimsy_ios\",\"id\":\"55\"}]");
        cd_icons(emojis);
        cJSON_Delete(emojis);
    }
    e = cd_pr(&c, 7, &open, &closed);
    CHECK(e && open && !closed);
    text = cJSON_PrintUnformatted(e);
    HAS(text, "\"title\":\"PR #7: Faster [blit]\"");
    HAS(text,
        "**Open** at [`1111111`](https://github.com/o/r/pull/7/commits/1111111111111111111111111111111111111111), "
        "on main at [`3333333`](https://github.com/o/r/commit/3333333333333333333333333333333333333333) "
        "([its nightly](https://n/1)).");
    HAS(text, "**2 commits**, the newest first\\n- \\\\[gpu\\\\] Second \\\\*one\\\\*\\n- \\\\[gpu\\\\] First");
    /* a line a package in one Downloads field, its icon, then its link or its job's state, but a nightly's package
       its run has no job for and its release no file; then in Tests the other jobs that make no file and failed, not
       those running or passed, each with the icon of the system its name names.
       No Build field (each job has its line) and no Builds while this push's are published (each has its link) */
    HAS(text,
        "{\"name\":\"Downloads\",\"value\":\"<:wimsy_linux:11> Linux x86-64: Building ([log](https://j/1))\\n"
        "<:wimsy_linux:11> Linux ARM64: Built, being published\\n"
        "<:wimsy_linux:11> Linux PowerPC64 BE: [.AppImage](https://n/p) (Linux programs)\\n"
        "<:wimsy_windows:22> Windows x86-64: [.zip](https://n/w)\\n"
        "<:wimsy_windows:22> Windows ARM64: Waiting\\n"
        "<:wimsy_android:33> Android: Failed ([log](https://j/2))\\n"
        "<:wimsy_macos:44> macOS ARM64: Waiting\\n<:wimsy_ios:55> iOS ARM64: Waiting\",\"inline\":false},"
        "{\"name\":\"Tests\",\"value\":\"<:wimsy_linux:11> Linux (x86_64, ASan + UBSan): Failed ([log](https://j/5))\","
        "\"inline\":false}");
    CHECK(!strstr(text, " MB") && !strstr(text, "https://j/6") && !strstr(text, "https://j/7"));
    CHECK(!strstr(text, "\"name\":\"Build\"") && !strstr(text, "\"name\":\"Builds\""));
    HAS(text, "\"color\":2066493"); /* GitHub's green: open */
    free(text);
    cJSON_Delete(e);
    /* every package building, at Discord's own lengths (an emoji's id, a log's address): the lines in two fields
       within FIELD letters each, the second named GO_ON (none shown) so they read as one; this push's builds not
       published, the push before's linked */
    {
        static const char* const names[] = {"Linux AppImage (x86_64)",  "Linux AppImage (aarch64)",
                                            "Linux AppImage (ppc64le)", "Linux AppImage (ppc64)",
                                            "Linux AppImage (riscv64)", "Linux AppImage (loongarch64)",
                                            "Windows (Clang + LLVM)",   "Android APK",
                                            "macOS and iOS (arm64)"};
        cJSON *emojis = cJSON_Parse("[{\"name\":\"wimsy_linux\",\"id\":\"1563256571297923183\"},"
                                    "{\"name\":\"wimsy_windows\",\"id\":\"1563256573802057808\"},"
                                    "{\"name\":\"wimsy_android\",\"id\":\"1563256568382759065\"},"
                                    "{\"name\":\"wimsy_macos\",\"id\":\"1563256572677988502\"},"
                                    "{\"name\":\"wimsy_ios\",\"id\":\"1563256569997557891\"}]"),
              *f;
        const cJSON *first = NULL, *second = NULL;
        Buf jobs = {0};
        int i, n = 0;
        cd_icons(emojis);
        cJSON_Delete(emojis);
        buf_add(&jobs, "{\"jobs\":[");
        for (i = 0; i < 9; ++i)
            buf_printf(&jobs,
                       "%s{\"name\":\"%s\",\"status\":\"in_progress\",\"html_url\":"
                       "\"https://github.com/scarletnx/ScarletNX/actions/runs/40008179076/job/12060136688%d\"}",
                       i ? "," : "", names[i], i);
        buf_add(&jobs, "]}");
        gh("repos/o/r/actions/runs/9/jobs?per_page=100&page=1", jobs.text);
        buf_free(&jobs);
        gh("repos/o/nightly/releases?per_page=100&page=1",
           "[{\"tag_name\":\"pr-7\",\"html_url\":\"https://n/pr-7\",\"published_at\":\"2026-10-20T11:00:00Z\","
           "\"body\":\"built from /commits/0000000000000000000000000000000000000000\",\"assets\":[]}]");
        cJSON_Delete(c.nightlies);
        c.nightlies = NULL;
        e = cd_pr(&c, 7, &open, &closed);
        cJSON_ArrayForEach(f, cJSON_GetObjectItemCaseSensitive(e, "fields")) {
            const char* name = cJSON_GetObjectItemCaseSensitive(f, "name")->valuestring;
            if (!strcmp(name, "Downloads")) first = f;
            if (!strcmp(name, "\xe2\x80\x8b")) second = f;
            n += !strcmp(name, "Downloads") || !strcmp(name, "\xe2\x80\x8b");
        }
        CHECK(first && second && n == 2);
        CHECK(rei_letters(cJSON_GetObjectItemCaseSensitive(first, "value")->valuestring) <= 1024);
        CHECK(rei_letters(cJSON_GetObjectItemCaseSensitive(second, "value")->valuestring) <= 1024);
        /* a nightly's package its run has a job for: listed on a pull request's card too */
        CHECK(strstr(cJSON_GetObjectItemCaseSensitive(first, "value")->valuestring, "Linux LoongArch64: Building"));
        {
            const char* windows = "<:wimsy_windows:1563256573802057808> Windows x86-64: Building ([log](";
            CHECK(!strncmp(cJSON_GetObjectItemCaseSensitive(second, "value")->valuestring, windows, strlen(windows)));
        }
        text = cJSON_PrintUnformatted(e);
        CHECK(!strstr(text, "ELFv1") && !strstr(text, "\"Tests\""));
        HAS(text, "{\"name\":\"Builds\",\"value\":\"[The push before](https://n/pr-7)\",\"inline\":true}");
        free(text);
        cJSON_Delete(e);
    }
    /* what moves it to the end: its push's builds published or failed (not a job's progress, a run again or
       cancelled), a comment, a draft, a push; not a label */
    {
        char was[17];
        const char* pr7 =
            "{\"number\":7,\"title\":\"Faster [blit]\",\"html_url\":\"https://github.com/o/r/pull/7\",\"state\":\"open\","
            "\"draft\":%s,\"merged\":false,\"comments\":%d,\"review_comments\":0,\"labels\":[%s],\"assignees\":[],"
            "\"user\":{\"login\":\"ana\",\"html_url\":\"https://github.com/ana\",\"avatar_url\":\"https://a/1\"},"
            "\"head\":{\"sha\":\"%s\",\"repo\":{\"full_name\":\"o/r\"}},\"base\":{\"sha\":"
            "\"2222222222222222222222222222222222222222\"}}";
        char* json;
        /* the push before's builds, this one's running */
        gh("repos/o/nightly/releases?per_page=100&page=1",
           "[{\"tag_name\":\"pr-7\",\"html_url\":\"https://n/pr-7\",\"published_at\":\"2026-10-20T11:00:00Z\","
           "\"body\":\"built from /commits/0000000000000000000000000000000000000000\",\"assets\":[]}]");
        c.builds[0] = 0;
        drawn_pr(&c);
        snprintf(was, sizeof was, "%s", c.activity);
        CHECK(*was && !*c.builds);
        gh("repos/o/r/actions/runs/9/jobs?per_page=100&page=1",
           "{\"jobs\":[{\"name\":\"Linux AppImage (x86_64)\",\"status\":\"completed\",\"conclusion\":\"success\"}]}");
        drawn_pr(&c);
        SAME(c.activity, was); /* a job done */
        run_is(&c, "\"completed\",\"conclusion\":\"cancelled\"");
        SAME(c.activity, was); /* cancelled, none failed before */
        gh("repos/o/r/actions/runs/9/jobs?per_page=100&page=1",
           "{\"jobs\":[{\"name\":\"Linux AppImage (x86_64)\",\"status\":\"completed\",\"conclusion\":\"success\"},"
           "{\"name\":\"Linux (aarch64)\",\"status\":\"completed\",\"conclusion\":\"failure\"}]}");
        run_is(&c, "\"completed\",\"conclusion\":\"cancelled\"");
        CHECK(strcmp(c.activity, was)); /* cancelled once a job had failed (ScarletNX's fail-fast): failed */
        SAME(c.builds, "failed 1111111111111111111111111111111111111111");
        gh("repos/o/r/actions/runs/9/jobs?per_page=100&page=1",
           "{\"jobs\":[{\"name\":\"Linux AppImage (x86_64)\",\"status\":\"completed\",\"conclusion\":\"success\"}]}");
        run_is(&c, "\"completed\",\"conclusion\":\"failure\"");
        CHECK(strcmp(c.activity, was)); /* failed */
        SAME(c.builds, "failed 1111111111111111111111111111111111111111");
        snprintf(was, sizeof was, "%s", c.activity);
        run_is(&c, "\"in_progress\"");
        SAME(c.activity, was); /* run again */
        run_is(&c, "\"completed\",\"conclusion\":\"cancelled\"");
        SAME(c.activity, was); /* cancelled */
        gh("repos/o/nightly/releases?per_page=100&page=1",
           "[{\"tag_name\":\"pr-7\",\"html_url\":\"https://n/pr-7\",\"published_at\":\"2026-10-20T12:00:00Z\","
           "\"body\":\"built from /commits/1111111111111111111111111111111111111111\",\"assets\":[]}]");
        drawn_pr(&c);
        CHECK(strcmp(c.activity, was)); /* published */
        SAME(c.builds, "published 1111111111111111111111111111111111111111");
        snprintf(was, sizeof was, "%s", c.activity);
        json = rei_format(pr7, "false", 1, "", "1111111111111111111111111111111111111111");
        gh("repos/o/r/pulls/7", json);
        free(json);
        gh("repos/o/r/issues/7/comments?per_page=100&page=1",
           "[{\"user\":{\"login\":\"bia\",\"type\":\"User\"},\"body\":\"Works\",\"html_url\":\"https://c/7\","
           "\"created_at\":\"2026-10-20T12:00:00Z\"}]");
        drawn_pr(&c);
        CHECK(strcmp(c.activity, was)); /* a comment */
        snprintf(was, sizeof was, "%s", c.activity);
        json = rei_format(pr7, "true", 1, "", "1111111111111111111111111111111111111111");
        gh("repos/o/r/pulls/7", json);
        free(json);
        drawn_pr(&c);
        CHECK(strcmp(c.activity, was)); /* a draft */
        snprintf(was, sizeof was, "%s", c.activity);
        json = rei_format(pr7, "true", 1, "{\"name\":\"x\"}", "1111111111111111111111111111111111111111");
        gh("repos/o/r/pulls/7", json);
        free(json);
        drawn_pr(&c);
        SAME(c.activity, was); /* a label: where it is */
        /* a push: its builds none yet, the push before's outcome not its own; then its own published */
        json = rei_format(pr7, "true", 1, "{\"name\":\"x\"}", "4444444444444444444444444444444444444444");
        gh("repos/o/r/pulls/7", json);
        free(json);
        gh("repos/o/r/compare/2222222222222222222222222222222222222222...4444444444444444444444444444444444444444",
           "{\"merge_base_commit\":{\"sha\":\"3333333333333333333333333333333333333333\"}}");
        gh("repos/o/r/actions/workflows/build.yml/runs?head_sha=4444444444444444444444444444444444444444&per_page=100&"
           "page=1",
           "{\"workflow_runs\":[]}");
        drawn_pr(&c);
        CHECK(strcmp(c.activity, was) && !*c.builds); /* a push */
        snprintf(was, sizeof was, "%s", c.activity);
        gh("repos/o/nightly/releases?per_page=100&page=1",
           "[{\"tag_name\":\"pr-7\",\"html_url\":\"https://n/pr-7\",\"published_at\":\"2026-10-20T13:00:00Z\","
           "\"body\":\"built from /commits/4444444444444444444444444444444444444444\",\"assets\":[]}]");
        drawn_pr(&c);
        CHECK(strcmp(c.activity, was)); /* its own published */
        SAME(c.builds, "published 4444444444444444444444444444444444444444");
        json = rei_format(pr7, "true", 1, "{\"name\":\"x\"}", "5555555555555555555555555555555555555555");
        gh("repos/o/r/pulls/7", json);
        free(json);
        gh("repos/o/r/compare/2222222222222222222222222222222222222222...5555555555555555555555555555555555555555",
           "{\"merge_base_commit\":{\"sha\":\"3333333333333333333333333333333333333333\"}}");
        gh("repos/o/r/actions/workflows/build.yml/runs?head_sha=5555555555555555555555555555555555555555&per_page=100&"
           "page=1",
           "{\"workflow_runs\":[]}");
        drawn_pr(&c);
        snprintf(was, sizeof was, "%s", c.activity);
        json = rei_format(pr7, "true", 1, "{\"name\":\"x\"}", "6666666666666666666666666666666666666666");
        gh("repos/o/r/pulls/7", json);
        free(json);
        gh("repos/o/r/compare/2222222222222222222222222222222222222222...6666666666666666666666666666666666666666",
           "{\"merge_base_commit\":{\"sha\":\"3333333333333333333333333333333333333333\"}}");
        gh("repos/o/r/actions/workflows/build.yml/runs?head_sha=6666666666666666666666666666666666666666&per_page=100&"
           "page=1",
           "{\"workflow_runs\":[]}");
        drawn_pr(&c);
        CHECK(strcmp(c.activity, was) && !*c.builds); /* a push, no builds on either side */
        /* in a round: the card's outcome carried into its drawing and out of it, and kept in the state */
        {
            Cards r;
            cJSON *saved, *back;
            char *state, *kept, failed_moved[17], running_moved[17];
            u64 message = c_post(NULL, 2, NULL);
            int before;
            json = rei_format(pr7, "false", 1, "", "1111111111111111111111111111111111111111");
            gh("repos/o/r/pulls/7", json);
            free(json);
            gh("repos/o/nightly/releases?per_page=100&page=1", "[]");
            c.builds[0] = 0;
            run_is(&c, "\"in_progress\"");
            snprintf(running_moved, sizeof running_moved, "%s", c.activity);
            run_is(&c, "\"completed\",\"conclusion\":\"failure\"");
            snprintf(failed_moved, sizeof failed_moved, "%s", c.activity);
            gh("repos/o/r/issues?state=all&sort=updated&direction=desc&since=2026-10-20T10:00:00Z&per_page=100&page=1",
               "[]");
            gh("repos/o/r/actions/workflows/build.yml/runs?per_page=20", "{\"workflow_runs\":[]}");
            gh("repos/o/r/commits/main", "{\"sha\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}");
            /* failed before, running again now: no move, the outcome kept */
            run_is(&c, "\"in_progress\"");
            state =
                rei_format("{\"cards\":[{\"key\":\"pr-7\",\"message\":\"%llu\",\"drawn\":\"x\",\"moved\":\"%s\","
                           "\"builds\":\"failed 1111111111111111111111111111111111111111\",\"dirty\":true}],"
                           "\"since\":\"2026-10-20T10:00:00Z\",\"head\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
                           "\"purged_prs\":true}",
                           (unsigned long long)message, failed_moved);
            saved = cJSON_Parse(state);
            cd_init(&r, &g, dc, 2, 0, 0);
            cd_load(&r, saved);
            before = nsent;
            now_is += 30;
            cd_round(&r);
            CHECK(nsent == before && r.cards[0].message == message && !r.cards[0].dirty);
            SAME(r.cards[0].builds, "failed 1111111111111111111111111111111111111111");
            cd_free(&r);
            cJSON_Delete(saved);
            free(state);
            /* running before, failed now: moved, the outcome learnt and saved */
            run_is(&c, "\"completed\",\"conclusion\":\"failure\"");
            state = rei_format("{\"cards\":[{\"key\":\"pr-7\",\"message\":\"%llu\",\"drawn\":\"x\",\"moved\":\"%s\","
                               "\"builds\":\"\",\"dirty\":true}],\"since\":\"2026-10-20T10:00:00Z\",\"head\":"
                               "\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"purged_prs\":true}",
                               (unsigned long long)message, running_moved);
            saved = cJSON_Parse(state);
            cd_init(&r, &g, dc, 2, 0, 0);
            cd_load(&r, saved);
            now_is += 30;
            cd_round(&r);
            CHECK(nsent == before + 1 && r.cards[0].message == sent[nsent - 1].message);
            SAME(r.cards[0].builds, "failed 1111111111111111111111111111111111111111");
            back = cd_save(&r);
            kept = cJSON_PrintUnformatted(back);
            HAS(kept, "\"builds\":\"failed 1111111111111111111111111111111111111111\"");
            free(kept);
            cJSON_Delete(back);
            cd_free(&r);
            cJSON_Delete(saved);
            free(state);
        }
    }
    /* GraphQL failing: no card from what is missing */
    answer("POST", "https://api.github.com/graphql", 502, "{}");
    CHECK(!cd_pr(&c, 7, &open, &closed) && c.failed);
    cd_free(&c);
    rg_free(&g);
    forget();
}

/* ---- the state file ---- */
static void state_tests(void) {
    char path[] = "/tmp/rei-state-XXXXXX";
    int fd = mkstemp(path);
    cJSON *s = cJSON_Parse("{\"a\":1}"), *back;
    FILE* f;
    CHECK(fd >= 0);
    close(fd);
    CHECK(rei_save(path, s) == 0);
    back = rei_load(path);
    CHECK(jnum(back, "a") == 1);
    cJSON_Delete(back);
    f = fopen(path, "w");
    fputs("{not json", f);
    fclose(f);
    back = rei_load(path); /* said in the log, and an empty state */
    CHECK(cJSON_IsObject(back) && !back->child);
    cJSON_Delete(back);
    remove(path);
    back = rei_load(path);
    CHECK(cJSON_IsObject(back) && !back->child);
    cJSON_Delete(back);
    back = rei_load(NULL);
    CHECK(cJSON_IsObject(back));
    cJSON_Delete(back);
    cJSON_Delete(s);
}

/* ---- the gateway's events, read from their JSON ---- */
static int message(const char* json, EvMessage* m) { return ev_message(json, strlen(json), m); }
static char* ready_user(const char* json) { return ev_ready_user(json, strlen(json)); }
static void event_tests(void) {
    EvMessage m;
    Buf b = {0};
    size_t before;
    const char* framed =
        "{\"id\":\"7\",\"channel_id\":\"8\",\"content\":\"#1\",\"author\":{\"id\":\"1\"}},\"s\":5,\"op\":0}";
    char* user;
    /* a person's, its strings unescaped (Concord left them as sent: \" and é in the text) */
    CHECK(message("{\"id\":\"1430000000000000000\",\"channel_id\":\"555555555555555555\",\"content\":\"he said "
                  "\\\"#160\\\" \\\\ caf\\u00e9 \\ud83d\\ude00\\n\",\"author\":{\"id\":\"1\",\"username\":\"ana\","
                  "\"bot\":false},\"embeds\":[],\"member\":{\"roles\":[]}}",
                  &m));
    CHECK(m.id == 1430000000000000000ULL && m.channel == 555555555555555555ULL);
    SAME(m.content, "he said \"#160\" \\ caf\xc3\xa9 \xf0\x9f\x98\x80\n");
    ev_message_free(&m);
    CHECK(!m.content && !m.id);
    /* whom it mentions: the users its mentions list, not a name in its text nor a role */
    {
        static const char mention[] =
            "{\"id\":\"7\",\"channel_id\":\"8\",\"content\":\"<@200> delete #12 <@&300>\",\"author\":{\"id\":\"1\"},"
            "\"mentions\":[{\"id\":\"200\",\"username\":\"Tracer\",\"bot\":true}],\"mention_roles\":[\"300\"]}";
        CHECK(ev_mentions(mention, strlen(mention), 200));
        CHECK(!ev_mentions(mention, strlen(mention), 300) && !ev_mentions(mention, strlen(mention), 1));
        CHECK(!ev_mentions(mention, strlen(mention), 0));
        CHECK(!ev_mentions("{\"id\":\"7\",\"content\":\"<@200>\"}", 29, 200)); /* no list: none */
    }
    /* a reply: its own author counts, not the one it answers */
    CHECK(message(
        "{\"id\":\"2\",\"channel_id\":\"3\",\"content\":\"#5\",\"author\":{\"id\":\"1\"},\"referenced_message\":"
        "{\"id\":\"9\",\"channel_id\":\"3\",\"content\":\"card\",\"author\":{\"id\":\"4\",\"bot\":true}}}",
        &m));
    SAME(m.content, "#5");
    ev_message_free(&m);
    CHECK(!message("{\"id\":\"9\",\"channel_id\":\"3\",\"content\":\"#5\",\"author\":{\"id\":\"4\",\"bot\":true},"
                   "\"referenced_message\":{\"id\":\"2\",\"channel_id\":\"3\",\"author\":{\"id\":\"1\"}}}",
                   &m) &&
          !m.content);
    /* a webhook's (the old cards), one with no author, ids that are no snowflakes, no JSON: never answered */
    CHECK(!message(
              "{\"id\":\"2\",\"channel_id\":\"3\",\"content\":\"#5\",\"author\":{\"id\":\"1\"},\"webhook_id\":\"6\"}",
              &m) &&
          !m.content && !m.id && !m.channel);
    CHECK(!message("{\"id\":\"2\",\"channel_id\":\"3\",\"content\":\"#5\"}", &m));
    CHECK(!message("{\"id\":\"2\",\"channel_id\":\"3\",\"content\":\"#5\",\"author\":null}", &m));
    CHECK(!message("{\"id\":\"2a\",\"channel_id\":\"3\",\"content\":\"#5\",\"author\":{\"id\":\"1\"}}", &m));
    CHECK(!message("{\"id\":2,\"channel_id\":\"3\",\"content\":\"#5\",\"author\":{\"id\":\"1\"}}", &m));
    CHECK(!message("{\"id\":\"-2\",\"channel_id\":\"3\",\"content\":\"#5\",\"author\":{\"id\":\"1\"}}", &m));
    CHECK(!message("{\"id\":\"2\",\"content\":\"#5\",\"author\":{\"id\":\"1\"}}", &m));
    CHECK(!message("{\"id\":\"2\",\"channel_id\":\"3\",", &m) && !message("", &m) && !message("[1]", &m));
    /* the event as the gateway hands it: its object, then the rest of the frame, read only as far as told */
    CHECK(!ev_message(framed, (size_t)(strstr(framed, "\"author\"") - framed), &m));
    CHECK(ev_message(framed, (size_t)(strstr(framed, ",\"s\"") - framed), &m));
    CHECK(m.id == 7 && m.channel == 8);
    SAME(m.content, "#1");
    ev_message_free(&m);
    /* files alone: no text */
    CHECK(
        message("{\"id\":\"2\",\"channel_id\":\"3\",\"author\":{\"id\":\"1\"},\"attachments\":[{\"id\":\"4\"}]}", &m));
    SAME(m.content, "");
    ev_message_free(&m);
    /* a long one, as the messages of 2 to 3 KB that crashed Concord's decoder */
    buf_add(&b,
            "{\"id\":\"2\",\"channel_id\":\"3\",\"author\":{\"id\":\"1\"},\"embeds\":[{\"type\":\"video\",\"provider\":"
            "{\"name\":\"x\"},\"video\":{\"url\":\"https://v\",\"width\":1280}}],\"content\":\"");
    before = b.len;
    while (b.len - before < 4000) buf_add(&b, "word ");
    buf_add(&b, "#160\"}");
    CHECK(message(b.text, &m));
    CHECK(m.content && strlen(m.content) == b.len - before - 2 && strstr(m.content, " #160"));
    ev_message_free(&m);
    buf_free(&b);
    /* READY's user */
    user = ready_user("{\"v\":10,\"user\":{\"id\":\"1\",\"username\":\"ReiWimsy\",\"bot\":true},\"session_id\":\"s\"}");
    SAME(user, "ReiWimsy");
    free(user);
    CHECK(!ready_user("{\"user\":{\"username\":7}}") && !ready_user("{\"v\":10}") && !ready_user("nope"));
}
/* the messages waiting: oldest first, across the end of the ring; a full queue refuses one, which stays its caller's */
static void queue_tests(void) {
    static EvQueue q;
    EvMessage m;
    int i, ordered = 1, taken = 1;
    ev_queue_init(&q);
    for (i = 0; i < EV_WAITING; ++i) {
        m.id = (u64)i + 1;
        m.channel = 3;
        m.content = rei_format("#%d", i + 1);
        taken &= ev_push(&q, &m) && !m.content;
    }
    CHECK(taken);
    m.id = 999;
    m.channel = 3;
    m.content = rei_strdup("#999");
    CHECK(!ev_push(&q, &m) && m.id == 999);
    SAME(m.content, "#999");
    ev_message_free(&m);
    /* half out, as many in: the ring wraps */
    for (i = 0; i < EV_WAITING / 2; ++i) {
        ev_pop(&q, &m);
        ordered &= m.id == (u64)i + 1;
        ev_message_free(&m);
        m.id = (u64)(EV_WAITING + i + 1);
        m.content = rei_format("#%d", EV_WAITING + i + 1);
        ev_push(&q, &m);
    }
    for (i = EV_WAITING / 2; i < EV_WAITING + EV_WAITING / 2; ++i) {
        char want[16];
        ev_pop(&q, &m);
        sprintf(want, "#%d", i + 1);
        ordered &= m.id == (u64)i + 1 && !strcmp(m.content, want);
        ev_message_free(&m);
    }
    CHECK(ordered && q.n == 0);
}

int main(void) {
    rei_now = clock_now;
    refs();
    look();
    link_tests();
    commands();
    github_tests();
    rest_tests();
    cards_tests();
    nightly_channel();
    pr_card();
    state_tests();
    event_tests();
    queue_tests();
    printf("reiwimsy: %d of %d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
