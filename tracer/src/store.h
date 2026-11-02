/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The reports, in SQLite: each report's game, what its first log says, who sent it (an id, never shown), its files
   (their paths under the site's folder, a log's summary as JSON) and the notes added later. Each message of a report
   (its own, the bot's replies, the updates) leads to it; each file and note knows the message that added it. One
   connection, used by one thread at a time */
#ifndef TR_STORE_H
#define TR_STORE_H
#include "tracer.h"

typedef struct Store Store;

typedef struct {
    long id;
    double when;
    u64 guild, channel, message, poster;
    int named; /* the title is a log's name of the game, not the message's words or the title ID */
    /* owned; NULL where the log says nothing. key and title are never NULL */
    char *key, *title, *title_id, *note, *version, *build, *commit, *branch, *system, *gpu, *driver, *outcome,
        *signature;
    char* missed;  /* owned: what the runtime missed, counted ("1116 shaders, 1 texture format"), NULL for nothing */
    char* console; /* owned: "Switch", "3DS", "Vita", "GBA" as the log summary names it, NULL when it doesn't say */
} StReport;

typedef struct {
    int n;             /* 1, 2... in the report */
    char kind[8];      /* "log" or "image" */
    char *name, *path; /* owned: the attachment's name, the file's name in the report's folder r/<id>/ */
    double bytes;
    char* summary; /* owned: a log's summary (snx_logscan_json), NULL for an image */
} StFile;

typedef struct {
    double when;
    char* text; /* owned */
} StNote;

typedef struct {
    char *key, *title, *title_id; /* owned; title is the newest named report's (else the newest's), title_id the
                                     newest known, NULL for a game known by its name alone */
    int reports;
    double last;
    char *outcome, *missed, *console; /* owned: the newest report's */
} StGame;

/* path's database, its tables made when they are not there; NULL, said in the log, when it cannot be opened */
Store* st_open(const char* path);
void st_close(Store* s);

/* r's id given to r (r->id ignored) with its files: 0, or -1 said in the log */
int st_add(Store* s, StReport* r, const StFile* files, int nfiles);
/* the reports poster sent since */
int st_count_since(Store* s, u64 poster, double since);
/* the key of the newest report of a game with this title and a title ID, owned; NULL when there is none */
char* st_key_of_title(Store* s, const char* title);
/* the newest name a log gave the game of key, owned; NULL when no log named it */
char* st_title_of_key(Store* s, const char* key);
/* message (a reply of the bot's, an update) leads to report too: 0, or -1 said in the log */
int st_link(Store* s, u64 message, long report);
/* the report a message leads to: the one it sent, updated or answered; 0 for none */
long st_report_of(Store* s, u64 message);
/* report's files: the last one's number, and how many are logs, screenshots and videos */
int st_counts(Store* s, long report, int* logs, int* images, int* videos);
/* files (their numbers set) and a note (NULL or "" for none) added to report by message, which then leads to it: 0,
   or -1 said in the log, nothing added */
int st_update(Store* s, long report, u64 message, double when, const StFile* files, int nfiles, const char* note);
/* the notes added to report later, oldest first */
int st_notes(Store* s, long report, StNote** notes, int* n);
/* what message added to a report, gone: its files (*files owned, for the caller to delete), its notes. The report's
   id, or 0 when message updated none */
long st_take_back(Store* s, u64 message, StFile** files, int* nfiles);
/* a value the program keeps of itself (key's, owned), NULL when there is none; and one kept: 0, or -1 */
char* st_meta(Store* s, const char* key);
int st_set_meta(Store* s, const char* key, const char* value);
/* file n of report's summary made again, and the report's facts (what its first log says) again: 0, or -1 */
int st_set_summary(Store* s, long report, int n, const char* summary);
int st_set_facts(Store* s, const StReport* r);
/* report id's game named again, as a later log names it: 0, or -1 */
int st_set_game(Store* s, long id, const char* key, const char* title, const char* title_id, int named);
/* the report sent as message, or 0 */
long st_by_message(Store* s, u64 message);
/* report id, and its files (*files owned, NULL when not wanted): 1, 0 when there is none, -1 said in the log */
int st_get(Store* s, long id, StReport* r, StFile** files, int* nfiles);
/* report id and its files gone from the database: 0, or -1 */
int st_remove(Store* s, long id);

/* every game, the newest report first */
int st_games(Store* s, StGame** games, int* n);
/* the game's reports, or every report when key is NULL, the newest first */
int st_reports(Store* s, const char* key, StReport** reports, int* n);

void st_report_free(StReport* r);
void st_files_free(StFile* f, int n);
void st_notes_free(StNote* notes, int n);
void st_games_free(StGame* g, int n);
void st_reports_free(StReport* r, int n);
#endif
