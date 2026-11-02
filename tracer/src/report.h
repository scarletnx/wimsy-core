/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* What Tracer does with a message that mentions it: a report made of its logs and screenshots (downloaded, the logs'
   home folders shown as ~, their summaries made, the screenshots' metadata taken out), kept in the database and the
   site's folder; "delete N" and "help" answered. Each answer is an embed for a reply */
#ifndef TR_REPORT_H
#define TR_REPORT_H
#include "message.h"
#include "store.h"
#include "titles.h"

typedef struct {
    Store* store;
    const char* root; /* the site's folder */
    const char* url;  /* the site's address, without its last / */
    TrGet get;
    void* ctx;
    const u64* admins; /* the people who delete any report, beside the guild's administrators */
    int nadmins;
    /* whether user, with these roles, is guild's owner or has the Administrator permission there */
    int (*administers)(void* ctx, u64 guild, u64 user, const u64* roles, int nroles);
    void* admin_ctx;
    const Titles* titles; /* the games' names, for a game its logs don't name; NULL for none */
} Tracer;

/* the game a log's summary names: *key (its title ID, else n- and the name's letters), *title (its name, else its
   title ID, else the message's note), *title_id (NULL without one), all owned, and whether the title is the log's name
   of the game. 0, or -1 (nothing owned) when neither says one */
int tr_game(const cJSON* summary, const char* note, char** key, char** title, char** title_id, int* named);

/* what a log's summary says the runtime missed, counted, most first by area: "1116 shaders, 1 texture format, 2 service
   commands" (shaders by the shaders, the rest by the different ones); owned, NULL for nothing */
char* tr_missed(const cJSON* summary);
/* what a report keeps of its logs, as a version: a new one summarizes the kept reports again (tr_rescan) */
#define TR_FACTS "facts 2"

/* the reply to m, a message that mentions the bot: an embed (owned), or NULL for a message already handled (the
   gateway sends a message again after a resume). *report is the report it made or updated, 0 for none: the reply,
   once posted, is given to tr_replied. A reply to a report's message (its own, an update, the bot's answer) adds to
   that report, as "add 12" does: for the person who sent it, or an administrator */
cJSON* tr_answer(Tracer* t, const TrMessage* m, long* report);
/* each kept log summarized again, each report's facts drawn again from its first log, and its game named or filed
   again, when the log summary or the list of names differs from the one that made them (version: TR_LOGSCAN and the
   list's). The reports summarized again, -1 for an error; the site's pages are not drawn again (site_rebuild) */
int tr_rescan(Tracer* t, const char* version);
/* reply, the bot's answer, leads to report too, so a reply to it adds to the report */
void tr_replied(Tracer* t, long report, u64 reply);
/* message deleted on Discord: a report's own message deletes the report, an update's takes back what it added. 1, or 0
   when it was neither */
int tr_forget(Tracer* t, u64 message);
#endif
