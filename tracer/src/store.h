/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The reports, in SQLite: each report's game, what its first log says, who sent it (an id, never shown), its files
   (their paths under the site's folder, a log's summary as JSON) and the notes added later. Each message of a report
   (its own, the bot's replies, the updates) leads to it; each file and note knows the message that added it. One
   connection, used by one thread at a time */
#ifndef TR_STORE_H
#define TR_STORE_H
#include "probe.h"
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
    char* missed;    /* owned: what the runtime missed, counted ("1116 shaders, 1 texture format"), NULL for nothing */
    char* console;   /* owned: "Switch", "3DS", "Vita", "GBA" as the log summary names it, NULL when it doesn't say */
    long issue;      /* its GitHub issue's number; 0 for none, -1 while one is being opened */
    char* issue_url; /* owned: that issue's page, NULL for none */
    char* age; /* owned: how far its build was behind main when it came, or that it is not a published one (age.h's
                  line), NULL for none */
} StReport;

/* what GitHub said of a commit (age.h), kept by the commit */
typedef struct {
    double asked;      /* when */
    int found;         /* the repository has the commit */
    long behind, days; /* main's commits it lacks, and the days from its commit's day to main's (UTC) */
    char* nightly;     /* owned: the newest nightly's tag, NULL for none */
} StAge;

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
/* poster's ask at when, a report or an update whose files are downloaded: 1 when poster asked max times since since
   (this one not counted), 0 when it is counted (and the asks before since forgotten), -1 for an error. A report deleted
   or refused still counts, as its downloads were made */
int st_ask(Store* s, u64 poster, double when, double since, int max);
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
/* file n of report's size written again, after its file was: 0, or -1 */
int st_set_bytes(Store* s, long report, int n, double bytes);
/* report's issue taken for one being opened (issue -1): 1 when it was, 0 when the report has one or one is being opened
   already, -1 for an error */
int st_claim_issue(Store* s, long report);
/* report's issue: its number and page, or (number 0, url NULL) none again after a failure. 0, or -1 */
int st_set_issue(Store* s, long report, long number, const char* url);
int st_set_facts(Store* s, const StReport* r);
/* report id's game named again, as a later log names it: 0, or -1 */
int st_set_game(Store* s, long id, const char* key, const char* title, const char* title_id, int named);
/* the report sent as message, or 0 */
long st_by_message(Store* s, u64 message);
/* report id, and its files (*files owned, NULL when not wanted): 1, 0 when there is none, -1 said in the log */
int st_get(Store* s, long id, StReport* r, StFile** files, int* nfiles);
/* report id and its files gone from the database: 0, or -1 */
int st_remove(Store* s, long id);

/* what was said of commit (a is zeroed first): 1, 0 when nothing was, -1 said in the log */
int st_age(Store* s, const char* commit, StAge* a);
/* what GitHub said of commit, kept in place of what it said before: 0, or -1 said in the log */
int st_set_age(Store* s, const char* commit, const StAge* a);
void st_age_free(StAge* a);

/* every game, the newest report first; the runs of edenprobe (key PROBE_KEY) are no game's */
int st_games(Store* s, StGame** games, int* n);
/* the game's reports, or every report when key is NULL (the runs of edenprobe too), the newest first; key PROBE_KEY
   gives the runs' reports */
int st_reports(Store* s, const char* key, StReport** reports, int* n);

/* ---- the runs of edenprobe (probe.h): each a report of key PROBE_KEY, its machine and each case's result ---- */

/* report's run, its cases with it, kept, in place of one it had: 0, or -1 said in the log, nothing kept */
int st_probe_set(Store* s, long report, const Probe* p, const char* digest);
/* report's run, its cases with it when cases: 1 (*p owned), 0 when it has none, -1 said in the log */
int st_probe_get(Store* s, long report, Probe* p, int cases);
/* the report poster sent of a run with this digest (probe_digest), 0 for none */
long st_probe_seen(Store* s, u64 poster, const char* digest);

typedef struct {
    long report;
    u64 poster; /* never shown: the rules count people by it */
    double when;
    Probe p; /* without its cases */
} StProbeRun;
/* every run, the newest first */
int st_probe_runs(Store* s, StProbeRun** runs, int* n);
void st_probe_runs_free(StProbeRun* runs, int n);

typedef struct {
    int id;
    char *suite, *name; /* owned */
    char key[17];       /* probe_case_key's: the case's page */
} StProbeName;
/* every case a run named, by id */
int st_probe_names(Store* s, StProbeName** names, int* n);
void st_probe_names_free(StProbeName* names, int n);

enum { ST_PASS, ST_FAIL, ST_SKIP, ST_CRASHED }; /* a case's result as kept: a skip whose reason is crashed is crashed */
typedef struct {
    long report;
    int name;   /* StProbeName's id */
    int result; /* ST_* */
} StProbeResult;
/* every result of every run, by case then report */
int st_probe_results(Store* s, StProbeResult** results, int* n);

typedef struct {
    long report;
    int result;
    char *tier, *reason, *detail; /* owned; "" for none */
    double us;                    /* -1 for none */
} StProbeDetail;
/* each run's result of case name, the newest report first */
int st_probe_details(Store* s, int name, StProbeDetail** details, int* n);
void st_probe_details_free(StProbeDetail* details, int n);

void st_report_free(StReport* r);
void st_files_free(StFile* f, int n);
void st_notes_free(StNote* notes, int n);
void st_games_free(StGame* g, int n);
void st_reports_free(StReport* r, int n);
#endif
