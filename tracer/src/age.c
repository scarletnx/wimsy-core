/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "age.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GRAPHQL "https://api.github.com/graphql"

/* the commit's day and main's, how many of main's commits it lacks, and the newest nightly's tag: object is null for a
   commit the repository does not have */
static const char QUERY[] =
    "query($owner:String!,$name:String!,$commit:String!){repository(owner:$owner,name:$name){"
    "object(expression:$commit){...on Commit{committedDate}}ref(qualifiedName:\"refs/heads/main\"){"
    "target{...on Commit{committedDate}}compare(headRef:$commit){behindBy}}}"
    "nightly:repository(owner:$owner,name:\"nightly\"){latestRelease{tagName}}}";

static int repo_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
           c == '-';
}

int tr_age_settings(TrAge* a, const char* (*get)(const char* name), char** why) {
    const char *repo = get("TRACER_UPSTREAM_REPO"), *slash, *c;
    memset(a, 0, sizeof *a);
    *why = NULL;
    if (!repo || !*repo) repo = "scarletnx/ScarletNX";
    slash = strchr(repo, '/');
    for (c = repo; *c && (c == slash || repo_char(*c)); ++c) continue;
    if (*c || !slash || slash == repo || !slash[1] || (size_t)(slash - repo) >= sizeof a->owner ||
        strlen(slash + 1) >= sizeof a->name) {
        *why = rei_format("TRACER_UPSTREAM_REPO=%s is not OWNER/NAME", repo);
        return -1;
    }
    snprintf(a->repo, sizeof a->repo, "%s", repo);
    snprintf(a->owner, sizeof a->owner, "%.*s", (int)(slash - repo), repo);
    snprintf(a->name, sizeof a->name, "%s", slash + 1);
    return 0;
}

char* tr_age_said(const TrAge* a) {
    if (!a->fetch)
        return rei_strdup("build age: a -dirty build said not to be a published one, nothing asked of GitHub, as "
                          "TRACER_ISSUES_TOKEN is not set");
    return rei_format("build age: each report's commit against %s's main and %s/nightly's newest nightly, asked of "
                      "GitHub once in %d s a commit",
                      a->repo, a->owner, TR_AGE_FRESH);
}

/* the days from 1970-01-01 to the UTC day of an ISO 8601 time, "2026-11-04T01:17:08Z", through *day; 0, or -1 for a
   time that is not one (Howard Hinnant's days_from_civil) */
static int day_of(const char* when, long* day) {
    long y, m, d, era, yoe, doy;
    int i;
    for (i = 0; i < 10; ++i)
        if (!when || (i == 4 || i == 7 ? when[i] != '-' : when[i] < '0' || when[i] > '9')) return -1;
    y = atol(when);
    m = atol(when + 5);
    d = atol(when + 8);
    if (m < 1 || m > 12 || d < 1 || d > 31) return -1;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    *day = era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
    return 0;
}

/* "nightly-YYYY-MM-DD-<7 hex>", the tag publish.yml gives a nightly */
static int nightly_tag(const char* tag) {
    long day;
    size_t i;
    if (!tag || strncmp(tag, "nightly-", 8) || day_of(tag + 8, &day) || tag[18] != '-' || strlen(tag) != 26) return 0;
    for (i = 19; i < 26; ++i)
        if (!strchr("0123456789abcdef", tag[i])) return 0;
    return 1;
}

/* commit asked of GitHub into *age: 0, or -1 said in the log */
static int asked(const TrAge* a, const char* commit, StAge* age) {
    cJSON *ask = cJSON_CreateObject(), *vars = cJSON_AddObjectToObject(ask, "variables"), *got = NULL;
    const cJSON *data, *repo, *main_ref, *object, *errors;
    char* body;
    RgAnswer ans;
    long commit_day, main_day;
    int ok = 0;
    jset(ask, "query", QUERY);
    jset(vars, "owner", a->owner);
    jset(vars, "name", a->name);
    jset(vars, "commit", commit);
    body = cJSON_PrintUnformatted(ask);
    cJSON_Delete(ask);
    memset(&ans, 0, sizeof ans);
    a->fetch(a->ctx, "POST", GRAPHQL, body, NULL, &ans);
    free(body);
    got = ans.body ? cJSON_Parse(ans.body) : NULL;
    data = cJSON_GetObjectItemCaseSensitive(got, "data");
    repo = cJSON_GetObjectItemCaseSensitive(data, "repository");
    main_ref = cJSON_GetObjectItemCaseSensitive(repo, "ref");
    object = cJSON_GetObjectItemCaseSensitive(repo, "object");
    errors = cJSON_GetObjectItemCaseSensitive(got, "errors");
    memset(age, 0, sizeof *age);
    age->asked = rei_now();
    if (!ans.status)
        rei_log("build age: GitHub did not answer the query for %s: %s", commit, ans.error ? ans.error : "no reason");
    else if (ans.status != 200 || !cJSON_IsObject(repo))
        rei_log("build age: GitHub answered %d to the query for %s in %s: %s", ans.status, commit, a->repo,
                jstr(cJSON_GetArrayItem(errors, 0), "message") ? jstr(cJSON_GetArrayItem(errors, 0), "message")
                : jstr(got, "message")                         ? jstr(got, "message")
                                                               : "no repository in its answer");
    else if (!cJSON_IsObject(main_ref))
        rei_log("build age: %s has no main, so %s is not compared with it", a->repo, commit);
    else if (!cJSON_IsObject(object)) /* not the repository's commit: a build with commits of its own */
        ok = 1;
    else if (!jpath(main_ref, "compare.behindBy") || day_of(jstr(object, "committedDate"), &commit_day) ||
             day_of(jstr(jpath(main_ref, "target"), "committedDate"), &main_day))
        rei_log("build age: GitHub's answer for %s in %s holds no comparison with main or no days: %.300s", commit,
                a->repo, ans.body);
    else {
        const char* tag = jstr(jpath(data, "nightly.latestRelease"), "tagName");
        ok = 1;
        age->found = 1;
        age->behind = (long)jnum(jpath(main_ref, "compare"), "behindBy");
        age->days = main_day - commit_day;
        if (nightly_tag(tag))
            age->nightly = rei_strdup(tag);
        else
            rei_log("build age: %s/nightly's newest release is %s, not a nightly-<day>-<commit>: no nightly is named",
                    a->owner, tag ? tag : "none");
    }
    cJSON_Delete(got);
    free(ans.body);
    free(ans.etag);
    free(ans.error);
    return ok ? 0 : -1;
}

static char* not_published(const char* commit) {
    return rei_format("Build %s is not a published build (local changes): numbers from it may not match main.", commit);
}

char* tr_age_line(const TrAge* a, Store* s, const char* commit, const char* branch) {
    size_t n = commit ? strspn(commit, "0123456789abcdef") : 0;
    StAge age;
    char* line = NULL;
    if (!a || n < 7 || n > 40) return NULL; /* "unknown", a build without git */
    if (!strcmp(commit + n, "-dirty")) return not_published(commit);
    if (commit[n] || !a->fetch) return NULL;
    if (st_age(s, commit, &age) != 1 || rei_now() - age.asked >= TR_AGE_FRESH) {
        st_age_free(&age);
        if (asked(a, commit, &age)) return NULL;
        st_set_age(s, commit, &age); /* a failure said, and asked again next time */
    }
    if (!age.found)
        line = not_published(commit);
    else if (age.behind > 0 && age.nightly && strncmp(age.nightly + 19, commit, 7))
        line = rei_format("Build %s (%s) is %ld commit%s and %ld day%s behind main; the newest nightly is %.10s (%s): "
                          "retry on it before we dig in.",
                          commit, branch ? branch : "unknown", age.behind, age.behind == 1 ? "" : "s", age.days,
                          age.days == 1 || age.days == -1 ? "" : "s", age.nightly + 8, age.nightly + 19);
    st_age_free(&age);
    return line;
}
