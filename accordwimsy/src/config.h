/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The env file's settings, KEY=value a line (# a comment): the README's table */
#ifndef AW_CONFIG_H
#define AW_CONFIG_H
#include <stddef.h>

/* how many rules ACCORDWIMSY_RELEASES takes */
#define AW_RULES 16

typedef struct {
    char forgejo[256];       /* ACCORDWIMSY_FORGEJO: the Forgejo's address, its API under /api/v1 */
    char forgejo_token[512]; /* ACCORDWIMSY_FORGEJO_TOKEN */
    char forgejo_user[128];  /* ACCORDWIMSY_FORGEJO_USER: whose the token is, for git's push */
    char org[128];           /* ACCORDWIMSY_ORG: the GitHub organization */
    char forgejo_org[128];   /* ACCORDWIMSY_FORGEJO_ORG: the Forgejo's, the GitHub one's name by default */
    char github_token[512];  /* GITHUB_TOKEN */
    char github_api[256];    /* ACCORDWIMSY_GITHUB_API */
    char github[256];        /* ACCORDWIMSY_GITHUB: where its repositories clone from */
    char state[1024];        /* ACCORDWIMSY_STATE: state.json, the lock and mirrors/ */
    char git[256];           /* ACCORDWIMSY_GIT */
    char grace_days[16];     /* ACCORDWIMSY_GRACE_DAYS: what GitHub has no more stays on the Forgejo that long */
    char fresh[1024];        /* ACCORDWIMSY_FRESH: each repository's last good pass, for the host to read */
    char releases[1024];     /* ACCORDWIMSY_RELEASES: which releases come, "repo:tag pattern[:newest]" each */
    long grace;              /* grace_days, in seconds */
    /* releases, read: a repository's releases whose tag matches pattern (fnmatch), of them the newest that many by
       when each was published (0: every one) */
    struct {
        char repo[128], pattern[128];
        int newest;
    } rule[AW_RULES];
    int rules;
} AwConfig;

/* the file at path read into c, the defaults first: 0, or nonzero with why (a key it does not know, a value too long,
   one needed and missing, days that are no number, a release rule that is none) */
int aw_config_read(AwConfig* c, const char* path, char* why, size_t size);
#endif
