/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* A repository's branches and tags through a bare mirror of them: fetched from GitHub, pushed to the Forgejo */
#ifndef AW_GIT_H
#define AW_GIT_H
#include "config.h"

/* The Forgejo's branch of GitHub's pull request number, which its pull request is made from: GitHub keeps every pull
   request's head in its own repository (refs/pull/<number>/head), a fork's too */
#define AW_PULL_BRANCH "accordwimsy/pull/%d"
/* Where the Forgejo keeps the old tip of a branch (or a tag) GitHub rewrote: AW_KEPT_BRANCH/branch/<name>/<stamp> */
#define AW_KEPT_BRANCH "accordwimsy/kept"

/* mirrors/<name>.git made when there is none, then from's branches and tags fetched into it (pruned of what from has
   not), its pull requests' heads as refs/pull/<n>/head, and the branches and tags pushed to to, which loses nothing:
   a rewritten branch's (or a moved tag's) old tip is kept there as AW_KEPT_BRANCH/.../<stamp>, said in the log, and
   on standard error, for someone to see, when it is main_branch's (the default branch; NULL: none); nothing is
   deleted. own: the other branches pull requests are made from there (a migration's), a ref a line, or NULL.
   *stale (owned): what to has under refs/heads and refs/tags that from has not, a ref a line (never an AW_PULL_BRANCH
   one, nor one of own), for aw_delete_refs once it has been gone long enough; *pull_branches (owned): the
   AW_PULL_BRANCH branches and those of own to has, a ref a line. The tokens go in git's environment, never its
   arguments. 0, or git's status, said */
int aw_mirror(const AwConfig* c, const char* name, const char* from, const char* to, const char* stamp,
              const char* main_branch, const char* own, char** stale, char** pull_branches);
/* how many branches and tags the mirror of name has (GitHub's, as of its last fetch) */
int aw_ref_count(const AwConfig* c, const char* name);
/* refs (a ref a line) deleted from to: 0, or git's status, said */
int aw_delete_refs(const AwConfig* c, const char* name, const char* to, const char* refs);
/* 1 when the mirror of name has the branch */
int aw_has_branch(const AwConfig* c, const char* name, const char* branch);
/* 1 when the mirror of name has the tag */
int aw_has_tag(const AwConfig* c, const char* name, const char* tag);
/* 1 when the mirror of name has GitHub's pull request number's head */
int aw_has_pull(const AwConfig* c, const char* name, int number);
/* 1 when from has ref now, 0 when it has not, -1 when it could not be asked (said) */
int aw_remote_has(const AwConfig* c, const char* name, const char* from, const char* ref);
/* the head of pull request number pushed to to as the branch, made or moved there: 0, or git's status, said */
int aw_push_pull(const AwConfig* c, const char* name, const char* to, int number, const char* branch);
#endif
