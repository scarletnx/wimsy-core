/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* One pass: each repository of the GitHub organization, the new ones too, the same on the Forgejo. Its branches and
   tags (git.h); its labels; its issues and pull requests by their numbers, which the Forgejo then gives the same (a
   number GitHub has no more, deleted or moved away, a closed issue saying so); a pull request as a pull request into
   the same branch, from its head (GitHub keeps every one's, a fork's too) pushed as a branch of its own while it is
   open, merged by GitHub's merge commit, closed or reopened as there; as an issue saying why only when it cannot be
   one (its head or the branch it went into gone from GitHub); the comments. Nothing is lost here when GitHub loses it:
   a branch or a tag GitHub has no more stays ACCORDWIMSY_GRACE_DAYS before it is deleted (said when first seen), a
   rewritten branch's old tip is kept that long (git.h), and an issue, a comment or a repository is never deleted.
   Each says its GitHub author and links its GitHub page; an edit on GitHub (its updated_at) edits it. Releases, with
   their files, as ACCORDWIMSY_RELEASES's rules pick them (config.h): a release the rules no longer pick is deleted
   here once those they pick are here, one GitHub has no more stays the grace; a repository no rule names has its
   Releases tab turned off. */
#ifndef AW_SYNC_H
#define AW_SYNC_H
#include "aw.h"
#include "config.h"

typedef struct {
    const AwConfig* config;
    AwFetch fetch;
    AwDownload download; /* a release's files: from GitHub */
    AwUpload upload;     /* and to the Forgejo */
    void* ctx;
    cJSON* state;  /* state.json: per repository, what was made of it */
    long writes;   /* the Forgejo's POST, PATCH and PUT, for the log */
    int failures;  /* repositories a pass stopped on */
    long now;      /* the pass's time, seconds since the epoch */
    int numbers_off; /* a number the Forgejo gave was not GitHub's, this repository */
    char* state_path;
} Aw;

/* every repository, state.json written after each, then config->fresh: 0, or 1 when one or more stopped (each said
   why) */
int aw_sync(Aw* a);
/* name's state rebuilt, in place of what state.json had of it, from the Forgejo's copy a migration of GitHub's made
   (the Forgejo's own, which keeps each issue's, pull request's and comment's GitHub author and number): each number up
   to its last the migration's, its text GitHub's alone from then on; GitHub's updated_at of each, so that the next
   pass edits only what changed since; each pull request's state, and the branch here it is made from (GitHub's own,
   or the one the migration fetched a fork's open one into; a fork's closed one has none, and stays closed if GitHub
   reopens it); each of GitHub's comments matched to the Forgejo's of the same issue, author and time, the rest made
   by the next pass. Refused, nothing written, when the Forgejo's was not made by a migration (no original_url), has an
   issue with no GitHub author, misses a number GitHub has below its last, or has a comment that matches none of
   GitHub's (the matching wrong, the next pass would make them again). 0, or 1 (said on stderr) */
int aw_adopt(Aw* a, const char* name);
/* name made again on the Forgejo by its own migration from GitHub (issues, pull requests, comments, labels and
   milestones; no release, no wiki), then adopted (aw_adopt). The copy this program made is moved aside first, renamed
   <name>-bot-<YYYYMMDD-HHMMSS>, archived and private, for someone to delete; one a migration made is left alone. 0,
   or 1 (said on stderr) */
int aw_migrate(Aw* a, const char* name);
/* what state.json says of each repository, on stdout: its last good pass, or that GitHub lists it no more, and what
   waits out the grace before it is deleted here (a branch, a tag, a release) */
void aw_status(const Aw* a);
#endif
