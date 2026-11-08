/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* What Tracer does with a message that mentions it: a report made of its logs and screenshots (downloaded, the logs'
   home folders shown as ~, their summaries made, the screenshots' metadata taken out), kept in the database and the
   site's folder; "delete N" and "help" answered. Each answer is an embed for a reply */
#ifndef TR_REPORT_H
#define TR_REPORT_H
#include "age.h"
#include "issues.h"
#include "message.h"
#include "store.h"
#include "titles.h"

/* the files a message brought, taken: each in the folder dir until it is written into the site */
typedef struct {
    StFile files[TR_LOGS + TR_IMAGES + TR_VIDEOS];
    char* taken[TR_LOGS + TR_IMAGES + TR_VIDEOS]; /* owned: where each file waits, in dir; NULL once in the site */
    char* dir; /* owned: a folder of its own in Tracer's work, NULL before it is made */
    int n, logs, images, videos;
    int base, logs_had, images_had, videos_had; /* a report's last file number, its logs, screenshots and videos, before
                                                   these */
    cJSON* first;                               /* the first log's summary */
    Buf skipped;                                /* a line for each attachment not taken */
    Probe probe;     /* the run of edenprobe in a new report's first log (probe.h), when probed */
    int probed;      /* the first log of a new report holds a run: the report is the run's */
    char* probe_why; /* owned: why a first log with lines of edenprobe holds no run of it, NULL for none */
} Taken;

typedef struct Tracer Tracer;
struct Tracer {
    Store* store;
    const char* root; /* the site's folder */
    const char* work; /* a folder of the bot's own, never served, where the files wait between their download and the
                         site (on the site's file system, so that they move by a rename) */
    const char* url;  /* the site's address, without its last / */
    TrGet get;
    void* ctx;
    const u64* admins; /* the people who delete any report, beside the guild's administrators */
    int nadmins;
    /* whether user, with these roles, is guild's owner or has the Administrator permission there */
    int (*administers)(void* ctx, u64 guild, u64 user, const u64* roles, int nroles);
    void* admin_ctx;
    const Titles* titles;   /* the games' names, for a game its logs don't name; NULL for none */
    const TrIssues* issues; /* where an approved report is filed as an issue; NULL, or its repo NULL, for nowhere */
    const TrAge* age;       /* how far a new report's build is behind main; NULL for no line */
    /* m's files taken into k (its dir made, its base and counts set), where they are read: in a worker of their own
       (main.c's, without the bot's token), or here by tr_take when take is NULL. 0, or -1 with why (owned) */
    int (*take)(void* ctx, Tracer* t, const TrMessage* m, Taken* k, char** why);
    void* take_ctx;
    /* message id of channel as Discord has it now (GET channels/C/messages/ID): its JSON (owned) and Discord's status,
       or NULL, status 0 and why (owned) when Discord did not answer. NULL for none: a reply that brings no file is then
       asked for one, as any message without a log is */
    char* (*message)(void* ctx, u64 channel, u64 id, int* status, char** why);
    /* whether Discord shows the bot the files of a message that does not mention it (the Message Content intent): 1,
       0, or -1 with why (owned) when Discord did not say */
    int (*content_intent)(void* ctx, char** why);
    void* discord_ctx;
};

/* m's attachments downloaded through t's get and read: the logs unpacked, their home folders as ~, their dumps and
   addresses left out and summarized, the screenshots' metadata taken out; each kept one written into k->dir, each
   other named in k->skipped */
void tr_take(Tracer* t, Taken* k, const TrMessage* m);
/* tr_take's two halves, which a worker splits by shutting itself in its folder between them (wk_shut_in): m's
   attachments downloaded through t's get into the folder dirfd (k->dir), attachment i as "in.<i>", or why[i] (owned)
   says why it is not (not a log or a screenshot, the report full of its kind, not on Discord, over its size, the
   download failed); then each one read back from there and taken as tr_take takes it, its input removed, the others
   named in k->skipped (why[] freed) */
void tr_download(Tracer* t, Taken* k, const TrMessage* m, int dirfd, char* why[TR_FILES]);
void tr_read(Taken* k, const TrMessage* m, int dirfd, char* why[TR_FILES]);
/* k's files that were not moved into the site removed, with k->dir, and the rest freed */
void tr_taken_free(Taken* k);

/* the game a log's summary names: *key (its title ID, else n- and the name's letters), *title (its name, else its
   title ID, else the message's note), *title_id (NULL without one), all owned, and whether the title is the log's name
   of the game. 0, or -1 (nothing owned) when neither says one */
int tr_game(const cJSON* summary, const char* note, char** key, char** title, char** title_id, int* named);

/* what a log's summary says the runtime missed, counted, most first by area: "1116 shaders, 1 texture format, 2 service
   commands" (shaders by the shaders, the rest by the different ones); owned, NULL for nothing */
char* tr_missed(const cJSON* summary);
/* what a report keeps of its logs, as a version: a new one summarizes the kept reports again (tr_rescan) */
#define TR_FACTS "facts 6"

/* the reply to m, a message that mentions the bot: an embed (owned), or NULL for a message already handled (the
   gateway sends a message again after a resume). *report is the report it made or updated, 0 for none: the reply,
   once posted, is given to tr_replied. A reply to a report's message (its own, an update, the bot's answer) adds to
   that report, as "add 12" does: for the person who sent it, or an administrator; with nothing to add, it is answered
   with the report's link. A reply to another message of the same channel that brings no file of its own makes a
   report of that message (t->message): its files, its author as the one who sent it, its words as the note (else the
   reply's); the hourly limit counts the one who asked */
cJSON* tr_answer(Tracer* t, const TrMessage* m, long* report);
/* each kept log summarized again, each report's facts drawn again from its first log, and its game named or filed
   again, when the log summary or the list of names differs from the one that made them (version: TR_LOGSCAN and the
   list's). A report whose first log holds a run of edenprobe becomes the run's (or keeps its run, read again); each
   kept log loses what probe_cut leaves out. The reports summarized again, -1 for an error; the site's pages are not
   drawn again (site_rebuild) */
int tr_rescan(Tracer* t, const char* version);
/* reaction r, to a report's message: when it is the approval's emoji and its user may approve the report (the
   report's server's owner and administrators, and TRACER_ADMINS), the report filed as an issue once, the site drawn
   again with it, and the reply: an embed (owned) naming the issue or why GitHub refused it. NULL for any other
   reaction, and for a report that has its issue or is being filed */
cJSON* tr_approved(Tracer* t, const TrReaction* r);
/* reply, the bot's answer, leads to report too, so a reply to it adds to the report */
void tr_replied(Tracer* t, long report, u64 reply);
/* message deleted on Discord: a report's own message deletes the report, an update's takes back what it added. 1, or 0
   when it was neither */
int tr_forget(Tracer* t, u64 message);
#endif
