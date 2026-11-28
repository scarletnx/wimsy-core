/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The cards: a message for each open pull request (its channel's) and each open issue, drawn from what GitHub says now
   and edited as it changes: its state, its commits, the latest comment a person wrote, and each package of its build.
   Its activity moves it to the end of its channel, posted again before the old one is deleted: a push, a person's
   comment, a change of state (draft, ready, closed, merged, reopened), and a pull request's builds published or
   failed; the rest is edited where it is. A pull request or an issue closed keeps its card CD_DAY seconds, as it
   closed, then the card is deleted; reopened, it has one again. The first round on those channels deletes the cards a
   webhook posted there before (the GitHub Actions ones), and a message of ours no card keeps.
   The nightly channel holds two cards, in this order: the newest nightly's (cd_nightly), the one to download, and main's
   newest build's (cd_main), newer and less tried. A newer nightly is posted at the end, main's card posted again under
   it, and the old two deleted, so the channel shows a nightly came out; main's newer builds edit its card where it is,
   as a push to main comes many times a day. Each start's first round, once both are up, deletes every other message of
   ours and every webhook's there; people's stay.
   Each round (cd_round) asks GitHub what changed since the last: the issues and pull requests updated, the builds that
   run, the nightly releases and main's prerelease. A card that could not be drawn whole (GitHub did not answer) waits
   for the next round, as it was */
#ifndef REI_CARDS_H
#define REI_CARDS_H
#include "github.h"

#define CD_DAY     86400
#define CD_RUNS    20   /* the newest builds a round looks at */
#define CD_HISTORY 1000 /* the pull requests' and issues' channels' messages the first round looks through */

/* Discord, as the cards use it */
typedef struct {
    void* ctx;
    u64 (*post)(void* ctx, u64 channel, const cJSON* embed);              /* the message's id, 0 on a failure */
    int (*edit)(void* ctx, u64 channel, u64 message, const cJSON* embed); /* 0, 404 gone, else a failure */
    int (*remove)(void* ctx, u64 channel, u64 message);                   /* 0, 404 gone, else a failure */
    /* up to max messages of channel older than before (0: from the newest), newest first: each one's id, whether a
       webhook sent it, whether this bot did. Their count, or -1 on a failure */
    int (*history)(void* ctx, u64 channel, u64 before, u64* ids, int* webhook, int* mine, int max);
} CdDiscord;

typedef struct {
    char key[56];   /* pr-N, issue-N; the nightly channel's: nightly, main */
    u64 message;    /* 0: none posted */
    char drawn[17]; /* the hash of what it shows, its time left out */
    char moved[17]; /* the hash of its activity when it went to the end of its channel; "" not seen yet */
    u64 stale;      /* its message before a move, still to delete */
    char builds[56]; /* a pull request's last outcome of its builds, "published SHA" or "failed SHA"; "" none */
    char shown[32];  /* the nightly's card: the newest publishing time of a nightly it showed, GitHub's; "" none */
    double closed;  /* when its pull request or issue closed, 0 while open */
    int gone;       /* deleted, its day closed over */
    int dirty;      /* to draw this round */
} CdCard;
typedef struct {
    long long id;
    char sig[17];
} CdRun;
typedef struct {
    char tag[96];
    char sig[17];
} CdRelease;

/* the nightly channel's cards, in its order */
enum { CD_NIGHTLY, CD_MAIN, CD_NEWEST };

typedef struct {
    Rg* gh;
    CdDiscord dc;
    u64 prs, issues, nightly; /* the channels; 0 leaves those cards out */
    CdCard* cards;            /* the pull requests' and the issues' */
    int ncards, capcards;
    CdCard newest[CD_NEWEST]; /* the nightly channel's */
    CdRun runs[CD_RUNS];
    int nruns;
    CdRelease* releases;
    int nreleases, capreleases;
    char since[32]; /* the issues asked for were updated from then: GitHub's time, ISO 8601 */
    int purged_prs, purged_issues;
    int purged_nightly; /* this run's first look through the nightly channel done */
    u64* drop;          /* messages of ours in the nightly channel to delete: the cards of a state from before */
    int ndrop, capdrop;
    char main_sig[17];    /* the hash of main's prereleases as the last round saw them */
    cJSON* nightlies;     /* this round's releases of gh->nightly, the newest published first */
    cJSON* repo_releases; /* this round's releases of gh->repo, the newest published first */
    int failed;       /* the card being drawn lacks something GitHub did not give */
    char activity[17]; /* the hash of the drawn card's activity: cd_pr's, cd_issue's, or draw's (the nightly's) */
    char builds[56];   /* cd_pr's: the card's outcome of its builds as it was, then as it is (CdCard's) */
    char shown[32];    /* cd_nightly's: when the nightly it drew was published, GitHub's time */
} Cards;

void cd_init(Cards* c, Rg* gh, CdDiscord dc, u64 prs, u64 issues, u64 nightly);
void cd_free(Cards* c);
/* one round: what changed asked of GitHub, its cards drawn, the closed ones over their day deleted */
void cd_round(Cards* c);
/* each card's embed, as it would be posted now: owned, or NULL (and c->failed) when GitHub did not answer; *open
   whether its pull request or issue is open, *closed_at when it closed; c->activity its activity */
cJSON* cd_pr(Cards* c, int number, int* open, double* closed_at);
cJSON* cd_issue(Cards* c, int number, int* open, double* closed_at);
/* the nightly channel's: the newest nightly published with its packages, c->shown when it was published; main's
   newest build, its prerelease, or while it has none the newest push's Build run, building or failed. NULL with
   c->failed when GitHub did not answer, NULL without when there is none */
cJSON* cd_nightly(Cards* c);
cJSON* cd_main(Cards* c);
/* the application's emojis (Discord's list of them), each package's icon from now on; one a package lacks is said,
   and its lines go without it */
void cd_icons(const cJSON* emojis);
/* a line per release file the cards list that release has, in their order, onto b: its icon, its system and its
   extension linked; a nightly's or a version's downloads as a command's answer shows them. How many */
int cd_downloads(Buf* b, const cJSON* release);
/* the mascot beside each card, of the repository repo (OWNER/NAME): owned */
char* cd_mascot(const char* repo);
/* the hash of an embed without its time, as hex */
void cd_hash(const cJSON* embed, char out[17]);

/* what lasts across a restart: the cards, the builds and releases seen, since, the channels purged, the messages to
   delete. A state from before the nightly channel's two cards had a card per nightly (nightly-SHA): their messages are
   deleted, said once */
cJSON* cd_save(const Cards* c);
void cd_load(Cards* c, const cJSON* saved);
#endif
