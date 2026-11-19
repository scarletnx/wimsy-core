/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "follow.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "site.h"

#define GITHUB_API "https://api.github.com/"
#define GRAPHQL    GITHUB_API "graphql"

int tr_notify_of(const char* value, int issues, TrNotify* notify, char** why) {
    *why = NULL;
    *notify = TR_NOTIFY_OFF;
    if (!value || !*value || !strcmp(value, "off")) return 0;
    if (!strcmp(value, "reply"))
        *notify = TR_NOTIFY_REPLY;
    else if (!strcmp(value, "ping"))
        *notify = TR_NOTIFY_PING;
    else {
        *why = rei_format("TRACER_NOTIFY=%s is none of off, reply and ping", value);
        return -1;
    }
    if (!issues) {
        *why = rei_format("TRACER_NOTIFY=%s needs TRACER_ISSUES_REPO: a report's fix is known by its issue", value);
        *notify = TR_NOTIFY_OFF;
        return -1;
    }
    return 0;
}

char* tr_follow_said(const Tracer* t) {
    static const char* const how[] = {"no one is told (TRACER_NOTIFY=off)",
                                      "its sender is told by a reply (TRACER_NOTIFY=reply)",
                                      "its sender is told by a reply that pings them (TRACER_NOTIFY=ping)"};
    if (!t->issues || !t->issues->repo)
        return rei_strdup("follow: no issue followed, as TRACER_ISSUES_REPO is not set");
    return rei_format("follow: the reports' issues in %s asked of GitHub every %d s; once a report's fix is in a "
                      "nightly of %s, %s",
                      t->issues->repo, TR_FOLLOW, t->age ? t->age->owner : "the upstream's owner", how[t->notify]);
}

/* the query for issues[0..n) of owner/name, and the newest nightly of nightly_owner: an alias i<number> each */
static char* query_of(const long* issues, int n) {
    Buf b = {0};
    int i;
    buf_add(&b, "query($owner:String!,$name:String!,$nightly:String!){repository(owner:$owner,name:$name){");
    for (i = 0; i < n; ++i)
        buf_printf(&b,
                   "i%ld:issueOrPullRequest(number:%ld){__typename ...on Issue{state stateReason "
                   "timelineItems(itemTypes:[CLOSED_EVENT],last:1){nodes{...on ClosedEvent{closer{__typename "
                   "...on PullRequest{number merged mergeCommit{oid}} ...on Commit{oid}}}}}} ...on PullRequest{state "
                   "merged mergeCommit{oid}}}",
                   issues[i], issues[i]);
    buf_add(&b, "}nightly:repository(owner:$nightly,name:\"nightly\"){latestRelease{tagName}}}");
    return buf_take(&b);
}

/* a string's copy, lower-cased, or NULL */
static char* lower(const char* s) {
    char *t = s ? rei_strdup(s) : NULL, *p;
    for (p = t; p && *p; ++p)
        if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
    return t;
}
static int same_text(const char* a, const char* b) { return a == b || (a && b && !strcmp(a, b)); }

/* r's issue as GitHub's node o says it now, into *now (its strings owned): its state and reason, and the fix that a
   merged pull request or the pull request whose merge closed the issue as completed is; the nightly kept while the
   fix is the same */
static void state_of(const StReport* r, const cJSON* o, StReport* now) {
    const char* kind = jstr(o, "__typename");
    memset(now, 0, sizeof *now);
    now->id = r->id;
    if (kind && !strcmp(kind, "PullRequest")) {
        now->issue_state = lower(jstr(o, "state"));
        if (jbool(o, "merged") && jstr(jpath(o, "mergeCommit"), "oid")) {
            now->fix_pr = r->issue;
            now->fix_commit = rei_strdup(jstr(jpath(o, "mergeCommit"), "oid"));
        }
    } else if (kind && !strcmp(kind, "Issue")) {
        const cJSON* closer = jpath(cJSON_GetArrayItem(jpath(o, "timelineItems.nodes"),
                                                       cJSON_GetArraySize(jpath(o, "timelineItems.nodes")) - 1),
                                    "closer");
        const char *closer_kind = jstr(closer, "__typename"), *reason = jstr(o, "stateReason");
        now->issue_state = lower(jstr(o, "state"));
        now->issue_reason = reason ? rei_strdup(reason) : NULL;
        if (now->issue_state && !strcmp(now->issue_state, "closed") && reason && !strcmp(reason, "COMPLETED")) {
            if (closer_kind && !strcmp(closer_kind, "PullRequest") && jbool(closer, "merged") &&
                jstr(jpath(closer, "mergeCommit"), "oid")) {
                now->fix_pr = (long)jnum(closer, "number");
                now->fix_commit = rei_strdup(jstr(jpath(closer, "mergeCommit"), "oid"));
            } else if (closer_kind && !strcmp(closer_kind, "Commit") && jstr(closer, "oid"))
                now->fix_commit = rei_strdup(jstr(closer, "oid"));
        }
    }
    if (now->fix_commit && same_text(now->fix_commit, r->fix_commit) && r->fix_nightly)
        now->fix_nightly = rei_strdup(r->fix_nightly);
}

/* whether the nightly tagged tag ("nightly-<day>-<7 hex>") has commit, GitHub's compare of the two in the upstream
   repository: 1, 0, or -1 when GitHub did not say (said) */
static int nightly_has(const Tracer* t, const char* tag, const char* commit) {
    char* url = rei_format(GITHUB_API "repos/%s/compare/%s...%s", t->age->repo, commit, tag + 19);
    RgAnswer a;
    cJSON* got;
    const char* status;
    int has = -1;
    memset(&a, 0, sizeof a);
    t->issues->fetch(t->issues->ctx, "GET", url, NULL, NULL, &a);
    got = a.body ? cJSON_Parse(a.body) : NULL;
    status = jstr(got, "status");
    if (a.status == 200 && status)
        has = !strcmp(status, "ahead") || !strcmp(status, "identical");
    else
        rei_log("follow: GitHub answered %d to GET %s: %.200s", a.status, url,
                a.error                ? a.error
                : jstr(got, "message") ? jstr(got, "message")
                                       : "no message");
    cJSON_Delete(got);
    free(a.body);
    free(a.etag);
    free(a.error);
    free(url);
    return has;
}

/* r's sender told that its fix is in its nightly, once: 1 when told */
static int told(Tracer* t, const StReport* r) {
    char *title = rei_format("Report #%ld: fixed", r->id), *page = rei_format("%s/r/%ld/", t->url, r->id),
         *by = r->fix_pr ? rei_format("pull request #%ld (%.7s)", r->fix_pr, r->fix_commit)
                         : rei_format("commit %.7s", r->fix_commit),
         *text = rei_format("Fixed by %s, in nightly %.10s (%s). Try it, and send a new log if it still fails.", by,
                            r->fix_nightly + 8, r->fix_nightly + 19);
    cJSON* e = cJSON_CreateObject();
    u64 reply;
    jset(e, "title", title);
    jset(e, "description", text);
    jset(e, "url", page);
    cJSON_AddNumberToObject(e, "color", 0x22d3ee);
    reply = t->tell ? t->tell(t->tell_ctx, r->channel, r->message, e, t->notify == TR_NOTIFY_PING) : 0;
    if (reply) {
        st_set_told(t->store, r->id, rei_now());
        st_link(t->store, reply, r->id);
        rei_log("report #%ld: its sender told it is fixed by %s, in %s", r->id, by, r->fix_nightly);
    } else
        rei_log("report #%ld: its sender could not be told it is fixed (Discord took no reply): asked again next round",
                r->id);
    cJSON_Delete(e);
    free(text);
    free(by);
    free(page);
    free(title);
    return reply != 0;
}

int tr_follow(Tracer* t) {
    StReport* r;
    int n, i, from, changed = 0, failed = 0;
    if (!t->issues || !t->issues->repo || !t->issues->fetch || !t->age) return 0;
    if (st_followed(t->store, t->notify != TR_NOTIFY_OFF, &r, &n)) return -1;
    for (from = 0; from < n && !failed; from += TR_FOLLOW_BATCH) {
        long issues[TR_FOLLOW_BATCH];
        int k = 0, j;
        cJSON *ask = cJSON_CreateObject(), *vars = cJSON_AddObjectToObject(ask, "variables"), *got;
        const char* slash = strchr(t->issues->repo, '/');
        char *query, *owner = rei_format("%.*s", (int)(slash - t->issues->repo), t->issues->repo), *body;
        const char* tag;
        RgAnswer a;
        for (i = from; i < n && i < from + TR_FOLLOW_BATCH; ++i) { /* each issue once, though several reports link it */
            for (j = 0; j < k && issues[j] != r[i].issue; ++j) continue;
            if (j == k) issues[k++] = r[i].issue;
        }
        query = query_of(issues, k);
        jset(ask, "query", query);
        jset(vars, "owner", owner);
        jset(vars, "name", slash + 1);
        jset(vars, "nightly", t->age->owner);
        body = cJSON_PrintUnformatted(ask);
        cJSON_Delete(ask);
        memset(&a, 0, sizeof a);
        t->issues->fetch(t->issues->ctx, "POST", GRAPHQL, body, NULL, &a);
        got = a.body ? cJSON_Parse(a.body) : NULL;
        if (a.status != 200 || !cJSON_IsObject(jpath(got, "data.repository"))) {
            rei_log("follow: GitHub answered %d to the query of %d issues of %s: %.200s", a.status, k, t->issues->repo,
                    a.error ? a.error
                    : jstr(cJSON_GetArrayItem(jpath(got, "errors"), 0), "message")
                        ? jstr(cJSON_GetArrayItem(jpath(got, "errors"), 0), "message")
                    : a.body ? a.body
                             : "nothing");
            failed = 1;
        }
        tag = jstr(jpath(got, "data.nightly.latestRelease"), "tagName");
        if (tag && (strncmp(tag, "nightly-", 8) || strlen(tag) != 26)) tag = NULL; /* nightly-<day>-<7 hex> alone */
        for (i = from; !failed && i < n && i < from + TR_FOLLOW_BATCH; ++i) {
            char alias[32];
            StReport now;
            snprintf(alias, sizeof alias, "i%ld", r[i].issue);
            if (!cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(jpath(got, "data.repository"), alias))) {
                rei_log("follow: report #%ld's #%ld is not in GitHub's answer for %s", r[i].id, r[i].issue,
                        t->issues->repo);
                continue;
            }
            state_of(&r[i], cJSON_GetObjectItemCaseSensitive(jpath(got, "data.repository"), alias), &now);
            if (now.fix_commit && !now.fix_nightly && tag && nightly_has(t, tag, now.fix_commit) == 1)
                now.fix_nightly = rei_strdup(tag);
            if (!same_text(now.issue_state, r[i].issue_state) || !same_text(now.issue_reason, r[i].issue_reason) ||
                now.fix_pr != r[i].fix_pr || !same_text(now.fix_commit, r[i].fix_commit) ||
                !same_text(now.fix_nightly, r[i].fix_nightly)) {
                if (!st_set_followed(t->store, &now)) {
                    rei_log("report #%ld: #%ld is %s%s%s%s%s", r[i].id, r[i].issue,
                            now.issue_state ? now.issue_state : "unknown", now.issue_reason ? ", " : "",
                            now.issue_reason ? now.issue_reason : "", now.fix_nightly ? ", fixed in " : "",
                            now.fix_nightly ? now.fix_nightly : "");
                    site_update(t->store, t->root, r[i].id, r[i].key);
                    ++changed;
                }
            }
            if (t->notify != TR_NOTIFY_OFF && now.fix_nightly && !r[i].told) {
                StReport fixed = r[i];
                fixed.fix_pr = now.fix_pr;
                fixed.fix_commit = now.fix_commit;
                fixed.fix_nightly = now.fix_nightly;
                changed += told(t, &fixed);
            }
            free(now.issue_state);
            free(now.issue_reason);
            free(now.fix_commit);
            free(now.fix_nightly);
        }
        cJSON_Delete(got);
        free(a.body);
        free(a.etag);
        free(a.error);
        free(body);
        free(owner);
        free(query);
    }
    st_reports_free(r, n);
    return failed ? -1 : changed;
}
