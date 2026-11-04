/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* How far a report's build is behind its repository's main, and the newest nightly to try it on instead, for the reply
   and the report's page. Asked of GitHub's GraphQL API from the bot's own process with TRACER_ISSUES_TOKEN, one query a
   commit, its answer kept in the database for TR_AGE_FRESH (main and the nightlies move); a build with local changes
   (-dirty) is said not to be a published one without asking. The nightlies are <owner>/nightly's releases, OWNER the
   repository's, tagged nightly-<day>-<commit> (ScarletNX's publish.yml) */
#ifndef TR_AGE_H
#define TR_AGE_H
#include "github.h"
#include "store.h"

#define TR_AGE_FRESH 3600 /* the seconds a commit's answer is kept before GitHub is asked again */

typedef struct {
    char repo[128]; /* TRACER_UPSTREAM_REPO, OWNER/NAME: the repository whose main a build is compared with */
    char owner[64]; /* its OWNER, the nightlies' too */
    char name[64];  /* its NAME */
    RgFetch fetch;  /* GitHub's API with the token (http.c's rh_fetch, or the tests'); NULL: nothing is asked */
    void* ctx;
} TrAge;

/* TRACER_UPSTREAM_REPO (scarletnx/ScarletNX when not set) read through get into a, fetch and ctx left to the caller.
   0, or -1 with why (owned): a repository that is not OWNER/NAME */
int tr_age_settings(TrAge* a, const char* (*get)(const char* name), char** why);
/* the start's line saying it; owned */
char* tr_age_said(const TrAge* a);
/* the line for a build of commit (as the log summary gives it: a short hash, "-dirty" after it for a tree with
   changes) on branch, owned: "Build <commit> (<branch>) is <n> commits and <n> days behind main; the newest nightly is
   <day> (<commit>): retry on it before we dig in.", or "Build <commit> is not a published build (local changes):
   numbers from it may not match main." for one with changes or a commit the repository does not have. NULL for none:
   a build with every commit of main, one of the newest nightly, a commit that is no hash, no token, or no answer from
   GitHub (said in the log) */
char* tr_age_line(const TrAge* a, Store* s, const char* commit, const char* branch);
#endif
