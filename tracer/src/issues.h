/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* A report filed as a GitHub issue once an administrator of its server approves it, reacting to one of its messages
   with the approval's emoji: the issue holds what the site shows of it (its facts and its first log's summary, home
   folders as ~, no dumps, no addresses, its screenshots as the site's) and the report's page. Asked of GitHub from the bot's own process (the worker
   never holds a token), once a report */
#ifndef TR_ISSUES_H
#define TR_ISSUES_H
#include "github.h"
#include "store.h"

#define TR_LABELS 16 /* the labels an issue gets, at most */

typedef struct {
    const char* repo;  /* OWNER/NAME, NULL when TRACER_ISSUES_REPO is not set: no issue is filed */
    const char* emoji; /* the approval's emoji: its character, or a server emoji's name */
    char* labels[TR_LABELS];
    int nlabels;
    RgFetch fetch; /* GitHub's API: http.c's rh_fetch with the token, or the tests' */
    void* ctx;
} TrIssues;

/* TRACER_ISSUES_REPO, _TOKEN, _EMOJI (default the check mark) and _LABELS (default none), each read through get, into
   is (fetch and ctx left to the caller), *token the token or NULL. 0, or -1 with why (owned): a repository that is not
   OWNER/NAME, a token without a repository or a repository without a token, an emoji or labels without a repository */
int tr_issues_settings(TrIssues* is, const char* (*get)(const char* name), const char** token, char** why);
void tr_issues_free(TrIssues* is);
/* the start's line saying them, every value but the token's; owned */
char* tr_issues_said(const TrIssues* is);

/* report r's issue: its title, and its body of what the site shows of the failure (its facts, the first log's fatal
   and frames, its page at url, its screenshots as the site's images linked to them), each of a log's words in a code
   block, where GitHub links no @name and no #number, and no "#" before the report's own number; owned. Neither names
   the game: no title, no title ID (a value holding one has it as <game>), no game file's name (a value with one is
   cut), no player's note and no last lines, which may; the report's page has them */
char* tr_issue_title(const StReport* r);
/* text as an issue may hold it, on one line, cut to max bytes: each title ID (16 hex digits) as <game>, and the whole
   text cut when a word of it names a game's file (.nsp, .xci, .3ds...), as the name before it may have spaces; owned */
char* tr_upstream_text(const char* text, size_t max);
char* tr_issue_body(const StReport* r, const StFile* files, int nfiles, const char* url);
/* the issue opened: 0 with *number and *url (owned, its page on GitHub), or -1 with why (owned): GitHub's status and
   message, or no answer */
int tr_issue_open(const TrIssues* is, const char* title, const char* body, long* number, char** url, char** why);
/* issue number of is->repo as GitHub has it (an issue or a pull request, which GitHub numbers alike): 0 with *url
   (owned, its page), *pull (1 for a pull request) and *state (owned: "open", "closed"), or -1 with why (owned):
   GitHub's status and message, or no answer */
int tr_issue_get(const TrIssues* is, long number, char** url, int* pull, char** state, char** why);
/* body as a comment on issue number: 0, or -1 with why (owned) */
int tr_issue_comment(const TrIssues* is, long number, const char* body, char** why);
#endif
