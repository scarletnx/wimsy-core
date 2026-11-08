/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "store.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Store {
    sqlite3* db;
};

/* AUTOINCREMENT: a deleted report's number is never another's, as Discord's replies link to it */
static const char SCHEMA[] =
    "PRAGMA journal_mode=WAL;"
    "PRAGMA foreign_keys=ON;"
    "CREATE TABLE IF NOT EXISTS reports(id INTEGER PRIMARY KEY AUTOINCREMENT, created REAL NOT NULL,"
    " guild INTEGER NOT NULL, channel INTEGER NOT NULL, message INTEGER NOT NULL UNIQUE, poster INTEGER NOT NULL,"
    " game TEXT NOT NULL, title TEXT NOT NULL, named INTEGER NOT NULL, title_id TEXT, note TEXT, version TEXT,"
    " build TEXT, commit_id TEXT, branch TEXT, system TEXT, gpu TEXT, driver TEXT, outcome TEXT, signature TEXT);"
    "CREATE INDEX IF NOT EXISTS reports_game ON reports(game, id);"
    "CREATE INDEX IF NOT EXISTS reports_poster ON reports(poster, created);"
    "CREATE TABLE IF NOT EXISTS files(report INTEGER NOT NULL REFERENCES reports(id) ON DELETE CASCADE,"
    " n INTEGER NOT NULL, kind TEXT NOT NULL, name TEXT NOT NULL, path TEXT NOT NULL, bytes REAL NOT NULL,"
    " summary TEXT, PRIMARY KEY(report, n));";

/* the schema's versions after the first (PRAGMA user_version), each in a transaction of its own: 1, the updates (each
   file's message, the notes added later, every message that leads to a report) */
static const char* const UPGRADES[] = {
    "ALTER TABLE files ADD COLUMN message INTEGER;"
    "UPDATE files SET message = (SELECT r.message FROM reports r WHERE r.id = files.report);"
    "CREATE TABLE notes(report INTEGER NOT NULL REFERENCES reports(id) ON DELETE CASCADE, message INTEGER NOT NULL,"
    " created REAL NOT NULL, text TEXT NOT NULL);"
    "CREATE INDEX notes_report ON notes(report, created);"
    "CREATE TABLE messages(message INTEGER PRIMARY KEY, report INTEGER NOT NULL REFERENCES reports(id) ON DELETE"
    " CASCADE);"
    "CREATE INDEX messages_report ON messages(report);"
    "INSERT OR IGNORE INTO messages(message, report) SELECT message, id FROM reports;",
    /* 2, a value each for what the program keeps of itself: the log summary its reports were made with */
    "CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);",
    /* 3, what the runtime missed, counted, beside the outcome */
    "ALTER TABLE reports ADD COLUMN missed TEXT;",
    /* 4, the console the game is of */
    "ALTER TABLE reports ADD COLUMN console TEXT;",
    /* 5, each person's asks of the last hour, for the hourly limit */
    "CREATE TABLE asks(poster INTEGER NOT NULL, created REAL NOT NULL);"
    "CREATE INDEX asks_poster ON asks(poster, created);",
    /* 6, the GitHub issue an administrator's approval opened */
    "ALTER TABLE reports ADD COLUMN issue INTEGER NOT NULL DEFAULT 0;"
    "ALTER TABLE reports ADD COLUMN issue_url TEXT;",
    /* 7, how far a report's build was behind main, and what GitHub said of each commit (age.h) */
    "ALTER TABLE reports ADD COLUMN age TEXT;"
    "CREATE TABLE ages(commit_id TEXT PRIMARY KEY, asked REAL NOT NULL, found INTEGER NOT NULL, behind INTEGER NOT"
    " NULL, days INTEGER NOT NULL, nightly TEXT);",
    /* 8, the runs of edenprobe: each report's machine and mode, each case named once, each run's result of each */
    "CREATE TABLE probe_names(id INTEGER PRIMARY KEY, suite TEXT NOT NULL, name TEXT NOT NULL, key TEXT NOT NULL"
    " UNIQUE, UNIQUE(suite, name));"
    "CREATE TABLE probe_runs(report INTEGER PRIMARY KEY REFERENCES reports(id) ON DELETE CASCADE, emulator TEXT NOT"
    " NULL, emulator_version TEXT, commit_id TEXT, built REAL NOT NULL, version TEXT NOT NULL, describe TEXT, words"
    " TEXT NOT NULL, system TEXT, os TEXT NOT NULL, gpu TEXT, device TEXT, driver_name TEXT, driver_info TEXT,"
    " driver_id INTEGER NOT NULL, inferred INTEGER NOT NULL, driver_version INTEGER NOT NULL, vulkan TEXT, extensions"
    " TEXT, passed INTEGER NOT NULL, failed INTEGER NOT NULL, skipped INTEGER NOT NULL, crashed INTEGER NOT NULL, whole"
    " INTEGER NOT NULL, cut_cases INTEGER NOT NULL, cut_notes INTEGER NOT NULL, unread INTEGER NOT NULL, over INTEGER"
    " NOT NULL, digest TEXT NOT NULL);"
    "CREATE INDEX probe_runs_digest ON probe_runs(digest);"
    "CREATE TABLE probe_cases(report INTEGER NOT NULL REFERENCES probe_runs(report) ON DELETE CASCADE, name INTEGER"
    " NOT NULL REFERENCES probe_names(id), result INTEGER NOT NULL, tier TEXT, reason TEXT, detail TEXT, us REAL,"
    " PRIMARY KEY(report, name)) WITHOUT ROWID;"
    /* by case, with the result, so the rules and a case's page read the results from the index alone */
    "CREATE INDEX probe_cases_name ON probe_cases(name, report, result);"};

/* a report's columns, in the order row_report reads them */
#define REPORT_COLUMNS                                                                                                 \
    "id, created, guild, channel, message, poster, game, title, named, title_id, note, version, build, commit_id, "    \
    "branch, system, gpu, driver, outcome, signature, missed, console, issue, issue_url, age"

static void said(Store* s, const char* what) { rei_log("the database, %s: %s", what, sqlite3_errmsg(s->db)); }

static int version(Store* s) {
    sqlite3_stmt* q = NULL;
    int v = -1;
    if (sqlite3_prepare_v2(s->db, "PRAGMA user_version", -1, &q, NULL) == SQLITE_OK && sqlite3_step(q) == SQLITE_ROW)
        v = sqlite3_column_int(q, 0);
    sqlite3_finalize(q);
    return v;
}
/* the schema brought to the last version, a version at a time */
static int upgrade(Store* s, const char* path) {
    int v = version(s);
    char* error = NULL;
    for (; v >= 0 && v < (int)(sizeof UPGRADES / sizeof *UPGRADES); ++v) {
        char* sql = rei_format("BEGIN;%sPRAGMA user_version = %d;COMMIT;", UPGRADES[v], v + 1);
        int ok = sqlite3_exec(s->db, sql, NULL, NULL, &error) == SQLITE_OK;
        free(sql);
        if (!ok) {
            rei_log("the database %s, its version %d made %d: %s", path, v, v + 1, error ? error : "failed");
            sqlite3_free(error);
            sqlite3_exec(s->db, "ROLLBACK", NULL, NULL, NULL);
            return -1;
        }
        rei_log("the database %s is at version %d", path, v + 1);
    }
    return v < 0 ? -1 : 0;
}

Store* st_open(const char* path) {
    Store* s = calloc(1, sizeof *s);
    char* error = NULL;
    if (!s) return NULL;
    if (sqlite3_open(path, &s->db) != SQLITE_OK) {
        said(s, path);
        sqlite3_close(s->db);
        free(s);
        return NULL;
    }
    sqlite3_busy_timeout(s->db, 5000);
    if (sqlite3_exec(s->db, SCHEMA, NULL, NULL, &error) != SQLITE_OK) {
        rei_log("the database %s: %s", path, error ? error : "its tables could not be made");
        sqlite3_free(error);
        sqlite3_close(s->db);
        free(s);
        return NULL;
    }
    if (upgrade(s, path)) {
        sqlite3_close(s->db);
        free(s);
        return NULL;
    }
    return s;
}

void st_close(Store* s) {
    if (!s) return;
    sqlite3_close(s->db);
    free(s);
}

static sqlite3_stmt* prepared(Store* s, const char* sql) {
    sqlite3_stmt* q = NULL;
    if (sqlite3_prepare_v2(s->db, sql, -1, &q, NULL) != SQLITE_OK) {
        said(s, sql);
        return NULL;
    }
    return q;
}
static void text_at(sqlite3_stmt* q, int at, const char* text) {
    if (text)
        sqlite3_bind_text(q, at, text, -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(q, at);
}
static char* text_of(sqlite3_stmt* q, int at) {
    const unsigned char* t = sqlite3_column_text(q, at);
    return t ? rei_strdup((const char*)t) : NULL;
}

int st_add(Store* s, StReport* r, const StFile* files, int nfiles) {
    sqlite3_stmt *q, *f;
    int i, ok;
    if (sqlite3_exec(s->db, "BEGIN", NULL, NULL, NULL) != SQLITE_OK) {
        said(s, "BEGIN");
        return -1;
    }
    q = prepared(s, "INSERT INTO reports(created, guild, channel, message, poster, game, title, named, title_id, note,"
                    " version, build, commit_id, branch, system, gpu, driver, outcome, signature, missed, console, age)"
                    " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    f = prepared(s, "INSERT INTO files(report, n, kind, name, path, bytes, summary, message) VALUES(?,?,?,?,?,?,?,?)");
    ok = q && f;
    if (ok) {
        sqlite3_bind_double(q, 1, r->when);
        sqlite3_bind_int64(q, 2, (sqlite3_int64)r->guild);
        sqlite3_bind_int64(q, 3, (sqlite3_int64)r->channel);
        sqlite3_bind_int64(q, 4, (sqlite3_int64)r->message);
        sqlite3_bind_int64(q, 5, (sqlite3_int64)r->poster);
        text_at(q, 6, r->key);
        text_at(q, 7, r->title);
        sqlite3_bind_int(q, 8, r->named);
        text_at(q, 9, r->title_id);
        text_at(q, 10, r->note);
        text_at(q, 11, r->version);
        text_at(q, 12, r->build);
        text_at(q, 13, r->commit);
        text_at(q, 14, r->branch);
        text_at(q, 15, r->system);
        text_at(q, 16, r->gpu);
        text_at(q, 17, r->driver);
        text_at(q, 18, r->outcome);
        text_at(q, 19, r->signature);
        text_at(q, 20, r->missed);
        text_at(q, 21, r->console);
        text_at(q, 22, r->age);
        ok = sqlite3_step(q) == SQLITE_DONE;
        if (!ok) said(s, "a report added");
    }
    if (ok) r->id = (long)sqlite3_last_insert_rowid(s->db);
    for (i = 0; ok && i < nfiles; ++i) {
        sqlite3_reset(f);
        sqlite3_bind_int64(f, 1, r->id);
        sqlite3_bind_int(f, 2, files[i].n);
        text_at(f, 3, files[i].kind);
        text_at(f, 4, files[i].name);
        text_at(f, 5, files[i].path);
        sqlite3_bind_double(f, 6, files[i].bytes);
        text_at(f, 7, files[i].summary);
        sqlite3_bind_int64(f, 8, (sqlite3_int64)r->message);
        ok = sqlite3_step(f) == SQLITE_DONE;
        if (!ok) said(s, "a report's file added");
    }
    sqlite3_finalize(q);
    sqlite3_finalize(f);
    ok = ok && !st_link(s, r->message, r->id);
    if (sqlite3_exec(s->db, ok ? "COMMIT" : "ROLLBACK", NULL, NULL, NULL) != SQLITE_OK) {
        said(s, ok ? "COMMIT" : "ROLLBACK");
        ok = 0;
    }
    return ok ? 0 : -1;
}

int st_ask(Store* s, u64 poster, double when, double since, int max) {
    sqlite3_stmt* q;
    int n = -1, ok;
    if (sqlite3_exec(s->db, "BEGIN", NULL, NULL, NULL) != SQLITE_OK) {
        said(s, "an ask counted");
        return -1;
    }
    q = prepared(s, "DELETE FROM asks WHERE created < ?");
    ok = q != NULL;
    if (ok) {
        sqlite3_bind_double(q, 1, since);
        ok = sqlite3_step(q) == SQLITE_DONE;
        sqlite3_finalize(q);
    }
    q = ok ? prepared(s, "SELECT count(*) FROM asks WHERE poster = ? AND created >= ?") : NULL;
    if (q) {
        sqlite3_bind_int64(q, 1, (sqlite3_int64)poster);
        sqlite3_bind_double(q, 2, since);
        if (sqlite3_step(q) == SQLITE_ROW) n = sqlite3_column_int(q, 0);
        sqlite3_finalize(q);
    }
    ok = n >= 0;
    if (ok && n < max && (ok = (q = prepared(s, "INSERT INTO asks(poster, created) VALUES(?, ?)")) != NULL)) {
        sqlite3_bind_int64(q, 1, (sqlite3_int64)poster);
        sqlite3_bind_double(q, 2, when);
        ok = sqlite3_step(q) == SQLITE_DONE;
        sqlite3_finalize(q);
    }
    if (!ok) said(s, "an ask counted");
    if (sqlite3_exec(s->db, ok ? "COMMIT" : "ROLLBACK", NULL, NULL, NULL) != SQLITE_OK) ok = 0;
    return !ok ? -1 : n >= max ? 1 : 0;
}

int st_link(Store* s, u64 message, long report) {
    sqlite3_stmt* q = prepared(s, "INSERT OR REPLACE INTO messages(message, report) VALUES(?, ?)");
    int ok;
    if (!q) return -1;
    sqlite3_bind_int64(q, 1, (sqlite3_int64)message);
    sqlite3_bind_int64(q, 2, report);
    ok = sqlite3_step(q) == SQLITE_DONE;
    if (!ok) said(s, "a message linked");
    sqlite3_finalize(q);
    return ok ? 0 : -1;
}

long st_report_of(Store* s, u64 message) {
    sqlite3_stmt* q = prepared(s, "SELECT report FROM messages WHERE message = ?");
    long id = 0;
    if (!q) return 0;
    sqlite3_bind_int64(q, 1, (sqlite3_int64)message);
    if (sqlite3_step(q) == SQLITE_ROW) id = (long)sqlite3_column_int64(q, 0);
    sqlite3_finalize(q);
    return id;
}

int st_counts(Store* s, long report, int* logs, int* images, int* videos) {
    sqlite3_stmt* q = prepared(s, "SELECT coalesce(max(n), 0), coalesce(sum(kind = 'log'), 0),"
                                  " coalesce(sum(kind = 'image'), 0), coalesce(sum(kind = 'video'), 0) FROM files"
                                  " WHERE report = ?");
    int last = 0;
    *logs = *images = *videos = 0;
    if (!q) return 0;
    sqlite3_bind_int64(q, 1, report);
    if (sqlite3_step(q) == SQLITE_ROW) {
        last = sqlite3_column_int(q, 0);
        *logs = sqlite3_column_int(q, 1);
        *images = sqlite3_column_int(q, 2);
        *videos = sqlite3_column_int(q, 3);
    }
    sqlite3_finalize(q);
    return last;
}

int st_update(Store* s, long report, u64 message, double when, const StFile* files, int nfiles, const char* note) {
    sqlite3_stmt *f = NULL, *q = NULL;
    int i, ok;
    if (sqlite3_exec(s->db, "BEGIN", NULL, NULL, NULL) != SQLITE_OK) {
        said(s, "BEGIN");
        return -1;
    }
    f = prepared(s, "INSERT INTO files(report, n, kind, name, path, bytes, summary, message) VALUES(?,?,?,?,?,?,?,?)");
    ok = f != NULL;
    for (i = 0; ok && i < nfiles; ++i) {
        sqlite3_reset(f);
        sqlite3_bind_int64(f, 1, report);
        sqlite3_bind_int(f, 2, files[i].n);
        text_at(f, 3, files[i].kind);
        text_at(f, 4, files[i].name);
        text_at(f, 5, files[i].path);
        sqlite3_bind_double(f, 6, files[i].bytes);
        text_at(f, 7, files[i].summary);
        sqlite3_bind_int64(f, 8, (sqlite3_int64)message);
        ok = sqlite3_step(f) == SQLITE_DONE;
        if (!ok) said(s, "a report's file added");
    }
    if (ok && note && *note) {
        ok = (q = prepared(s, "INSERT INTO notes(report, message, created, text) VALUES(?,?,?,?)")) != NULL;
        if (ok) {
            sqlite3_bind_int64(q, 1, report);
            sqlite3_bind_int64(q, 2, (sqlite3_int64)message);
            sqlite3_bind_double(q, 3, when);
            text_at(q, 4, note);
            ok = sqlite3_step(q) == SQLITE_DONE;
            if (!ok) said(s, "a note added");
        }
    }
    sqlite3_finalize(f);
    sqlite3_finalize(q);
    ok = ok && !st_link(s, message, report);
    if (sqlite3_exec(s->db, ok ? "COMMIT" : "ROLLBACK", NULL, NULL, NULL) != SQLITE_OK) {
        said(s, ok ? "COMMIT" : "ROLLBACK");
        ok = 0;
    }
    return ok ? 0 : -1;
}

int st_notes(Store* s, long report, StNote** notes, int* n) {
    sqlite3_stmt* q = prepared(s, "SELECT created, text FROM notes WHERE report = ? ORDER BY created, rowid");
    int cap = 0;
    *notes = NULL;
    *n = 0;
    if (!q) return -1;
    sqlite3_bind_int64(q, 1, report);
    while (sqlite3_step(q) == SQLITE_ROW) {
        if (*n == cap) {
            cap = cap ? cap * 2 : 8;
            *notes = realloc(*notes, (size_t)cap * sizeof **notes);
            if (!*notes) {
                fputs(REI_NAME ": out of memory\n", stderr);
                abort();
            }
        }
        (*notes)[*n].when = sqlite3_column_double(q, 0);
        (*notes)[(*n)++].text = text_of(q, 1);
    }
    sqlite3_finalize(q);
    return 0;
}

long st_take_back(Store* s, u64 message, StFile** files, int* nfiles) {
    long report = st_report_of(s, message);
    sqlite3_stmt* q;
    int cap = 0, ok;
    *files = NULL;
    *nfiles = 0;
    if (!report || st_by_message(s, message) == report) return 0; /* a report's own message deletes it all */
    if (sqlite3_exec(s->db, "BEGIN", NULL, NULL, NULL) != SQLITE_OK) return 0;
    q = prepared(s, "SELECT n, kind, name, path FROM files WHERE report = ? AND message = ?");
    ok = q != NULL;
    if (ok) {
        sqlite3_bind_int64(q, 1, report);
        sqlite3_bind_int64(q, 2, (sqlite3_int64)message);
        while (sqlite3_step(q) == SQLITE_ROW) {
            StFile* f;
            const unsigned char* kind = sqlite3_column_text(q, 1);
            if (*nfiles == cap) {
                cap = cap ? cap * 2 : 8;
                *files = realloc(*files, (size_t)cap * sizeof **files);
                if (!*files) {
                    fputs(REI_NAME ": out of memory\n", stderr);
                    abort();
                }
            }
            f = &(*files)[(*nfiles)++];
            memset(f, 0, sizeof *f);
            f->n = sqlite3_column_int(q, 0);
            strncpy(f->kind, kind ? (const char*)kind : "", sizeof f->kind - 1);
            f->name = text_of(q, 2);
            f->path = text_of(q, 3);
        }
        sqlite3_finalize(q);
    }
    if (ok) {
        char* sql = rei_format("DELETE FROM files WHERE report = %ld AND message = %lld;"
                               "DELETE FROM notes WHERE report = %ld AND message = %lld;"
                               "DELETE FROM messages WHERE message = %lld;",
                               report, (long long)message, report, (long long)message, (long long)message);
        ok = sqlite3_exec(s->db, sql, NULL, NULL, NULL) == SQLITE_OK;
        if (!ok) said(s, "an update taken back");
        free(sql);
    }
    if (sqlite3_exec(s->db, ok ? "COMMIT" : "ROLLBACK", NULL, NULL, NULL) != SQLITE_OK) ok = 0;
    if (!ok) {
        st_files_free(*files, *nfiles);
        *files = NULL;
        *nfiles = 0;
        return 0;
    }
    return report;
}

char* st_meta(Store* s, const char* key) {
    sqlite3_stmt* q = prepared(s, "SELECT value FROM meta WHERE key = ?");
    char* value = NULL;
    if (!q) return NULL;
    text_at(q, 1, key);
    if (sqlite3_step(q) == SQLITE_ROW) value = text_of(q, 0);
    sqlite3_finalize(q);
    return value;
}

int st_set_meta(Store* s, const char* key, const char* value) {
    sqlite3_stmt* q = prepared(s, "INSERT OR REPLACE INTO meta(key, value) VALUES(?, ?)");
    int ok;
    if (!q) return -1;
    text_at(q, 1, key);
    text_at(q, 2, value);
    ok = sqlite3_step(q) == SQLITE_DONE;
    if (!ok) said(s, "a value kept");
    sqlite3_finalize(q);
    return ok ? 0 : -1;
}

int st_set_summary(Store* s, long report, int n, const char* summary) {
    sqlite3_stmt* q = prepared(s, "UPDATE files SET summary = ? WHERE report = ? AND n = ?");
    int ok;
    if (!q) return -1;
    text_at(q, 1, summary);
    sqlite3_bind_int64(q, 2, report);
    sqlite3_bind_int(q, 3, n);
    ok = sqlite3_step(q) == SQLITE_DONE;
    if (!ok) said(s, "a summary made again");
    sqlite3_finalize(q);
    return ok ? 0 : -1;
}

int st_claim_issue(Store* s, long report) {
    sqlite3_stmt* q = prepared(s, "UPDATE reports SET issue = -1 WHERE id = ? AND issue = 0");
    int done;
    if (!q) return -1;
    sqlite3_bind_int64(q, 1, report);
    done = sqlite3_step(q) == SQLITE_DONE;
    if (!done) said(s, "an issue claimed");
    sqlite3_finalize(q);
    return done ? sqlite3_changes(s->db) == 1 : -1;
}

int st_set_issue(Store* s, long report, long number, const char* url) {
    sqlite3_stmt* q = prepared(s, "UPDATE reports SET issue = ?, issue_url = ? WHERE id = ?");
    int ok;
    if (!q) return -1;
    sqlite3_bind_int64(q, 1, number);
    text_at(q, 2, url);
    sqlite3_bind_int64(q, 3, report);
    ok = sqlite3_step(q) == SQLITE_DONE;
    if (!ok) said(s, "an issue kept");
    sqlite3_finalize(q);
    return ok ? 0 : -1;
}

int st_set_bytes(Store* s, long report, int n, double bytes) {
    sqlite3_stmt* q = prepared(s, "UPDATE files SET bytes = ? WHERE report = ? AND n = ?");
    int ok;
    if (!q) return -1;
    sqlite3_bind_double(q, 1, bytes);
    sqlite3_bind_int64(q, 2, report);
    sqlite3_bind_int(q, 3, n);
    ok = sqlite3_step(q) == SQLITE_DONE;
    if (!ok) said(s, "a file's size written again");
    sqlite3_finalize(q);
    return ok ? 0 : -1;
}

int st_set_facts(Store* s, const StReport* r) {
    sqlite3_stmt* q = prepared(s, "UPDATE reports SET version = ?, build = ?, commit_id = ?, branch = ?, system = ?,"
                                  " gpu = ?, driver = ?, outcome = ?, signature = ?, missed = ?, console = ?"
                                  " WHERE id = ?");
    int ok;
    if (!q) return -1;
    text_at(q, 1, r->version);
    text_at(q, 2, r->build);
    text_at(q, 3, r->commit);
    text_at(q, 4, r->branch);
    text_at(q, 5, r->system);
    text_at(q, 6, r->gpu);
    text_at(q, 7, r->driver);
    text_at(q, 8, r->outcome);
    text_at(q, 9, r->signature);
    text_at(q, 10, r->missed);
    text_at(q, 11, r->console);
    sqlite3_bind_int64(q, 12, r->id);
    ok = sqlite3_step(q) == SQLITE_DONE;
    if (!ok) said(s, "a report's facts made again");
    sqlite3_finalize(q);
    return ok ? 0 : -1;
}

int st_set_game(Store* s, long id, const char* key, const char* title, const char* title_id, int named) {
    sqlite3_stmt* q = prepared(s, "UPDATE reports SET game = ?, title = ?, title_id = ?, named = ? WHERE id = ?");
    int ok;
    if (!q) return -1;
    text_at(q, 1, key);
    text_at(q, 2, title);
    text_at(q, 3, title_id);
    sqlite3_bind_int(q, 4, named);
    sqlite3_bind_int64(q, 5, id);
    ok = sqlite3_step(q) == SQLITE_DONE;
    if (!ok) said(s, "a report's game named again");
    sqlite3_finalize(q);
    return ok ? 0 : -1;
}

char* st_key_of_title(Store* s, const char* title) {
    sqlite3_stmt* q =
        prepared(s, "SELECT game FROM reports WHERE title = ? AND title_id IS NOT NULL ORDER BY id DESC LIMIT 1");
    char* key = NULL;
    if (!q) return NULL;
    sqlite3_bind_text(q, 1, title, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(q) == SQLITE_ROW) key = text_of(q, 0);
    sqlite3_finalize(q);
    return key;
}

char* st_title_of_key(Store* s, const char* key) {
    sqlite3_stmt* q = prepared(s, "SELECT title FROM reports WHERE game = ? AND named ORDER BY id DESC LIMIT 1");
    char* title = NULL;
    if (!q) return NULL;
    sqlite3_bind_text(q, 1, key, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(q) == SQLITE_ROW) title = text_of(q, 0);
    sqlite3_finalize(q);
    return title;
}

long st_by_message(Store* s, u64 message) {
    sqlite3_stmt* q = prepared(s, "SELECT id FROM reports WHERE message = ?");
    long id = 0;
    if (!q) return 0;
    sqlite3_bind_int64(q, 1, (sqlite3_int64)message);
    if (sqlite3_step(q) == SQLITE_ROW) id = (long)sqlite3_column_int64(q, 0);
    sqlite3_finalize(q);
    return id;
}

static void row_report(sqlite3_stmt* q, StReport* r) {
    r->id = (long)sqlite3_column_int64(q, 0);
    r->when = sqlite3_column_double(q, 1);
    r->guild = (u64)sqlite3_column_int64(q, 2);
    r->channel = (u64)sqlite3_column_int64(q, 3);
    r->message = (u64)sqlite3_column_int64(q, 4);
    r->poster = (u64)sqlite3_column_int64(q, 5);
    r->key = text_of(q, 6);
    r->title = text_of(q, 7);
    r->named = sqlite3_column_int(q, 8);
    r->title_id = text_of(q, 9);
    r->note = text_of(q, 10);
    r->version = text_of(q, 11);
    r->build = text_of(q, 12);
    r->commit = text_of(q, 13);
    r->branch = text_of(q, 14);
    r->system = text_of(q, 15);
    r->gpu = text_of(q, 16);
    r->driver = text_of(q, 17);
    r->outcome = text_of(q, 18);
    r->signature = text_of(q, 19);
    r->missed = text_of(q, 20);
    r->console = text_of(q, 21);
    r->issue = (long)sqlite3_column_int64(q, 22);
    r->issue_url = text_of(q, 23);
    r->age = text_of(q, 24);
}

int st_get(Store* s, long id, StReport* r, StFile** files, int* nfiles) {
    sqlite3_stmt* q = prepared(s, "SELECT " REPORT_COLUMNS " FROM reports WHERE id = ?");
    int found, cap = 0;
    memset(r, 0, sizeof *r);
    if (files) *files = NULL;
    if (nfiles) *nfiles = 0;
    if (!q) return -1;
    sqlite3_bind_int64(q, 1, id);
    found = sqlite3_step(q) == SQLITE_ROW;
    if (found) row_report(q, r);
    sqlite3_finalize(q);
    if (!found || !files) return found;
    if (!(q = prepared(s, "SELECT n, kind, name, path, bytes, summary FROM files WHERE report = ? ORDER BY n"))) {
        st_report_free(r);
        return -1;
    }
    sqlite3_bind_int64(q, 1, id);
    while (sqlite3_step(q) == SQLITE_ROW) {
        StFile* f;
        const unsigned char* kind = sqlite3_column_text(q, 1);
        if (*nfiles == cap) {
            cap = cap ? cap * 2 : 8;
            *files = realloc(*files, (size_t)cap * sizeof **files);
            if (!*files) {
                fputs(REI_NAME ": out of memory\n", stderr);
                abort();
            }
        }
        f = &(*files)[(*nfiles)++];
        memset(f, 0, sizeof *f);
        f->n = sqlite3_column_int(q, 0);
        strncpy(f->kind, kind ? (const char*)kind : "", sizeof f->kind - 1);
        f->name = text_of(q, 2);
        f->path = text_of(q, 3);
        f->bytes = sqlite3_column_double(q, 4);
        f->summary = text_of(q, 5);
    }
    sqlite3_finalize(q);
    return 1;
}

int st_remove(Store* s, long id) {
    sqlite3_stmt* q = prepared(s, "DELETE FROM reports WHERE id = ?");
    int ok;
    if (!q) return -1;
    sqlite3_bind_int64(q, 1, id);
    ok = sqlite3_step(q) == SQLITE_DONE;
    if (!ok) said(s, "a report deleted");
    sqlite3_finalize(q);
    return ok ? 0 : -1;
}

/* rows of q, each read into an element of size bytes by row: their count in *n, the array in *out */
static int rows(sqlite3_stmt* q, void** out, int* n, size_t size, void (*row)(sqlite3_stmt*, void*)) {
    int cap = 0;
    *out = NULL;
    *n = 0;
    if (!q) return -1;
    while (sqlite3_step(q) == SQLITE_ROW) {
        if (*n == cap) {
            cap = cap ? cap * 2 : 64;
            *out = realloc(*out, (size_t)cap * size);
            if (!*out) {
                fputs(REI_NAME ": out of memory\n", stderr);
                abort();
            }
        }
        memset((char*)*out + (size_t)*n * size, 0, size);
        row(q, (char*)*out + (size_t)(*n)++ * size);
    }
    sqlite3_finalize(q);
    return 0;
}
static void row_game(sqlite3_stmt* q, void* to) {
    StGame* g = to;
    g->key = text_of(q, 0);
    g->title = text_of(q, 1);
    g->title_id = text_of(q, 2);
    g->reports = sqlite3_column_int(q, 3);
    g->last = sqlite3_column_double(q, 4);
    g->outcome = text_of(q, 5);
    g->missed = text_of(q, 6);
    g->console = text_of(q, 7);
}
static void row_any_report(sqlite3_stmt* q, void* to) { row_report(q, to); }

int st_games(Store* s, StGame** games, int* n) {
    sqlite3_stmt* q = prepared(
        s, "SELECT r.game, (SELECT t.title FROM reports t WHERE t.game = r.game ORDER BY t.named DESC, t.id"
           " DESC LIMIT 1), (SELECT t.title_id FROM reports t WHERE t.game = r.game AND t.title_id IS NOT NULL ORDER BY"
           " t.id DESC LIMIT 1), c.n, r.created, r.outcome, r.missed, r.console FROM reports r JOIN (SELECT game,"
           " count(*) AS n, max(id) AS last FROM reports WHERE game != '" PROBE_KEY "' GROUP BY game) c ON r.id ="
           " c.last ORDER BY r.id DESC");
    return rows(q, (void**)games, n, sizeof **games, row_game);
}

int st_reports(Store* s, const char* key, StReport** reports, int* n) {
    sqlite3_stmt* q = prepared(s, key ? "SELECT " REPORT_COLUMNS " FROM reports WHERE game = ? ORDER BY id DESC"
                                      : "SELECT " REPORT_COLUMNS " FROM reports ORDER BY id DESC");
    if (q && key) sqlite3_bind_text(q, 1, key, -1, SQLITE_TRANSIENT);
    return rows(q, (void**)reports, n, sizeof **reports, row_any_report);
}

void st_report_free(StReport* r) {
    free(r->key);
    free(r->title);
    free(r->title_id);
    free(r->note);
    free(r->version);
    free(r->build);
    free(r->commit);
    free(r->branch);
    free(r->system);
    free(r->gpu);
    free(r->driver);
    free(r->outcome);
    free(r->signature);
    free(r->missed);
    free(r->console);
    free(r->issue_url);
    free(r->age);
    memset(r, 0, sizeof *r);
}

int st_age(Store* s, const char* commit, StAge* a) {
    sqlite3_stmt* q = prepared(s, "SELECT asked, found, behind, days, nightly FROM ages WHERE commit_id = ?");
    int step;
    memset(a, 0, sizeof *a);
    if (!q) return -1;
    sqlite3_bind_text(q, 1, commit, -1, SQLITE_TRANSIENT);
    step = sqlite3_step(q);
    if (step == SQLITE_ROW) {
        a->asked = sqlite3_column_double(q, 0);
        a->found = sqlite3_column_int(q, 1);
        a->behind = (long)sqlite3_column_int64(q, 2);
        a->days = (long)sqlite3_column_int64(q, 3);
        a->nightly = text_of(q, 4);
    } else if (step != SQLITE_DONE)
        said(s, "a commit's age read");
    sqlite3_finalize(q);
    return step == SQLITE_ROW ? 1 : step == SQLITE_DONE ? 0 : -1;
}

int st_set_age(Store* s, const char* commit, const StAge* a) {
    sqlite3_stmt* q = prepared(s, "INSERT OR REPLACE INTO ages(commit_id, asked, found, behind, days, nightly)"
                                  " VALUES(?,?,?,?,?,?)");
    int ok;
    if (!q) return -1;
    sqlite3_bind_text(q, 1, commit, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(q, 2, a->asked);
    sqlite3_bind_int(q, 3, a->found);
    sqlite3_bind_int64(q, 4, a->behind);
    sqlite3_bind_int64(q, 5, a->days);
    text_at(q, 6, a->nightly);
    ok = sqlite3_step(q) == SQLITE_DONE;
    if (!ok) said(s, "a commit's age kept");
    sqlite3_finalize(q);
    return ok ? 0 : -1;
}

void st_age_free(StAge* a) {
    free(a->nightly);
    memset(a, 0, sizeof *a);
}
void st_files_free(StFile* f, int n) {
    int i;
    for (i = 0; i < n; ++i) {
        free(f[i].name);
        free(f[i].path);
        free(f[i].summary);
    }
    free(f);
}
void st_notes_free(StNote* notes, int n) {
    int i;
    for (i = 0; i < n; ++i) free(notes[i].text);
    free(notes);
}
void st_games_free(StGame* g, int n) {
    int i;
    for (i = 0; i < n; ++i) {
        free(g[i].key);
        free(g[i].title);
        free(g[i].title_id);
        free(g[i].outcome);
        free(g[i].missed);
        free(g[i].console);
    }
    free(g);
}
void st_reports_free(StReport* r, int n) {
    int i;
    for (i = 0; i < n; ++i) st_report_free(&r[i]);
    free(r);
}

/* ---- the runs of edenprobe ---- */

/* name's id in probe_names, found, else added: the id, or 0 said in the log */
static long name_id(Store* s, sqlite3_stmt* add, sqlite3_stmt* find, const char* suite, const char* name) {
    char key[17];
    long id = 0;
    int added;
    for (added = 0; !id && added < 2; ++added) {
        if (added) { /* not there yet */
            probe_case_key(suite, name, key);
            sqlite3_reset(add);
            text_at(add, 1, suite);
            text_at(add, 2, name);
            text_at(add, 3, key);
            if (sqlite3_step(add) != SQLITE_DONE) {
                said(s, "a case's name added");
                return 0;
            }
        }
        sqlite3_reset(find);
        text_at(find, 1, suite);
        text_at(find, 2, name);
        if (sqlite3_step(find) == SQLITE_ROW) id = (long)sqlite3_column_int64(find, 0);
    }
    if (!id) said(s, "a case's name found");
    return id;
}
static int result_code(const ProbeCase* c) {
    if (!strcmp(c->result, "pass")) return ST_PASS;
    if (!strcmp(c->result, "fail")) return ST_FAIL;
    return !strcmp(c->reason, "crashed") ? ST_CRASHED : ST_SKIP;
}
static const char* result_word(int code) { return code == ST_PASS ? "pass" : code == ST_FAIL ? "fail" : "skip"; }

int st_probe_set(Store* s, long report, const Probe* p, const char* digest) {
    sqlite3_stmt *q = NULL, *add = NULL, *find = NULL, *c = NULL;
    int i, ok;
    if (sqlite3_exec(s->db, "BEGIN", NULL, NULL, NULL) != SQLITE_OK) {
        said(s, "BEGIN");
        return -1;
    }
    q = prepared(s, "DELETE FROM probe_runs WHERE report = ?");
    ok = q != NULL;
    if (ok) {
        sqlite3_bind_int64(q, 1, report);
        ok = sqlite3_step(q) == SQLITE_DONE;
        if (!ok) said(s, "a run taken out");
        sqlite3_finalize(q);
    }
    q = ok ? prepared(s,
                      "INSERT INTO probe_runs(report, emulator, emulator_version, commit_id, built, version,"
                      " describe, words, system, os, gpu, device, driver_name, driver_info, driver_id, inferred,"
                      " driver_version, vulkan, extensions, passed, failed, skipped, crashed, whole, cut_cases,"
                      " cut_notes, unread, over, digest) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,"
                      "?,?)")
           : NULL;
    ok = q != NULL;
    if (ok) {
        sqlite3_bind_int64(q, 1, report);
        text_at(q, 2, p->emulator);
        text_at(q, 3, p->emulator_version);
        text_at(q, 4, p->commit);
        sqlite3_bind_double(q, 5, p->built);
        text_at(q, 6, p->version);
        text_at(q, 7, p->describe);
        text_at(q, 8, p->words);
        text_at(q, 9, p->system);
        text_at(q, 10, p->os ? p->os : "unknown");
        text_at(q, 11, p->gpu);
        text_at(q, 12, p->device);
        text_at(q, 13, p->driver_name);
        text_at(q, 14, p->driver_info);
        sqlite3_bind_int(q, 15, p->driver_id);
        sqlite3_bind_int(q, 16, p->inferred);
        sqlite3_bind_int64(q, 17, (sqlite3_int64)p->driver_version);
        text_at(q, 18, p->vulkan);
        text_at(q, 19, p->extensions);
        sqlite3_bind_int64(q, 20, p->passed);
        sqlite3_bind_int64(q, 21, p->failed);
        sqlite3_bind_int64(q, 22, p->skipped);
        sqlite3_bind_int64(q, 23, p->crashed);
        sqlite3_bind_int(q, 24, p->whole);
        sqlite3_bind_int(q, 25, p->cut_cases);
        sqlite3_bind_int(q, 26, p->cut_notes);
        sqlite3_bind_int(q, 27, p->unread);
        sqlite3_bind_int(q, 28, p->over);
        text_at(q, 29, digest);
        ok = sqlite3_step(q) == SQLITE_DONE;
        if (!ok) said(s, "a run added");
    }
    sqlite3_finalize(q);
    add = ok ? prepared(s, "INSERT OR IGNORE INTO probe_names(suite, name, key) VALUES(?,?,?)") : NULL;
    find = add ? prepared(s, "SELECT id FROM probe_names WHERE suite = ? AND name = ?") : NULL;
    c = find ? prepared(s, "INSERT INTO probe_cases(report, name, result, tier, reason, detail, us)"
                           " VALUES(?,?,?,?,?,?,?)")
             : NULL;
    ok = c != NULL;
    for (i = 0; ok && i < p->ncases; ++i) {
        const ProbeCase* k = &p->cases[i];
        long id = name_id(s, add, find, k->suite, k->name);
        ok = id != 0;
        if (!ok) break;
        sqlite3_reset(c);
        sqlite3_bind_int64(c, 1, report);
        sqlite3_bind_int64(c, 2, id);
        sqlite3_bind_int(c, 3, result_code(k));
        text_at(c, 4, k->tier);
        text_at(c, 5, *k->reason ? k->reason : NULL);
        text_at(c, 6, *k->detail ? k->detail : NULL);
        if (k->us >= 0)
            sqlite3_bind_double(c, 7, k->us);
        else
            sqlite3_bind_null(c, 7);
        ok = sqlite3_step(c) == SQLITE_DONE;
        if (!ok) said(s, "a case's result added");
    }
    sqlite3_finalize(add);
    sqlite3_finalize(find);
    sqlite3_finalize(c);
    if (sqlite3_exec(s->db, ok ? "COMMIT" : "ROLLBACK", NULL, NULL, NULL) != SQLITE_OK) {
        said(s, ok ? "COMMIT" : "ROLLBACK");
        ok = 0;
    }
    return ok ? 0 : -1;
}

#define RUN_COLUMNS                                                                                                    \
    "r.report, r.emulator, r.emulator_version, r.commit_id, r.built, r.version, r.describe, r.words, r.system, r.os, " \
    "r.gpu, r.device, r.driver_name, r.driver_info, r.driver_id, r.inferred, r.driver_version, r.vulkan, "             \
    "r.extensions, r.passed, r.failed, r.skipped, r.crashed, r.whole, r.cut_cases, r.cut_notes, r.unread, r.over, "    \
    "p.poster, p.created"
static void row_run(sqlite3_stmt* q, void* to) {
    StProbeRun* r = to;
    Probe* p = &r->p;
    char text[32];
    r->report = (long)sqlite3_column_int64(q, 0);
    p->emulator = text_of(q, 1);
    p->emulator_version = text_of(q, 2);
    p->commit = text_of(q, 3);
    p->built = sqlite3_column_double(q, 4);
    p->version = text_of(q, 5);
    p->describe = text_of(q, 6);
    p->words = text_of(q, 7);
    p->system = text_of(q, 8);
    p->os = text_of(q, 9);
    p->gpu = text_of(q, 10);
    p->device = text_of(q, 11);
    p->driver_name = text_of(q, 12);
    p->driver_info = text_of(q, 13);
    p->driver_id = sqlite3_column_int(q, 14);
    p->inferred = sqlite3_column_int(q, 15);
    p->driver_version = (unsigned long)sqlite3_column_int64(q, 16);
    p->vulkan = text_of(q, 17);
    p->extensions = text_of(q, 18);
    p->passed = (long)sqlite3_column_int64(q, 19);
    p->failed = (long)sqlite3_column_int64(q, 20);
    p->skipped = (long)sqlite3_column_int64(q, 21);
    p->crashed = (long)sqlite3_column_int64(q, 22);
    p->whole = sqlite3_column_int(q, 23);
    p->cut_cases = sqlite3_column_int(q, 24);
    p->cut_notes = sqlite3_column_int(q, 25);
    p->unread = sqlite3_column_int(q, 26);
    p->over = sqlite3_column_int(q, 27);
    r->poster = (u64)sqlite3_column_int64(q, 28);
    r->when = sqlite3_column_double(q, 29);
    if (p->driver_id && p->driver_version) {
        probe_version_text(p->driver_id, p->driver_version, text, sizeof text);
        p->driver_text = rei_strdup(text);
    }
}

int st_probe_get(Store* s, long report, Probe* p, int cases) {
    sqlite3_stmt* q = prepared(s, "SELECT " RUN_COLUMNS " FROM probe_runs r JOIN reports p ON p.id = r.report"
                                  " WHERE r.report = ?");
    StProbeRun run;
    int found, cap = 0;
    memset(p, 0, sizeof *p);
    if (!q) return -1;
    memset(&run, 0, sizeof run);
    sqlite3_bind_int64(q, 1, report);
    found = sqlite3_step(q) == SQLITE_ROW;
    if (found) row_run(q, &run);
    sqlite3_finalize(q);
    if (!found) return 0;
    *p = run.p;
    if (!cases) return 1;
    q = prepared(s, "SELECT n.suite, n.name, c.result, c.tier, c.reason, c.detail, c.us FROM probe_cases c JOIN"
                    " probe_names n ON n.id = c.name WHERE c.report = ? ORDER BY n.id");
    if (!q) {
        probe_free(p);
        return -1;
    }
    sqlite3_bind_int64(q, 1, report);
    while (sqlite3_step(q) == SQLITE_ROW) {
        ProbeCase* c;
        int code = sqlite3_column_int(q, 2);
        char* reason = text_of(q, 4);
        char* detail = text_of(q, 5);
        if (p->ncases == cap) {
            cap = cap ? cap * 2 : 256;
            p->cases = realloc(p->cases, (size_t)cap * sizeof *p->cases);
            if (!p->cases) {
                fputs(REI_NAME ": out of memory\n", stderr);
                abort();
            }
        }
        c = &p->cases[p->ncases++];
        c->suite = text_of(q, 0);
        c->name = text_of(q, 1);
        c->result = rei_strdup(result_word(code));
        c->tier = text_of(q, 3);
        if (!c->tier) c->tier = rei_strdup("none");
        c->reason = reason ? reason : rei_strdup(code == ST_CRASHED ? "crashed" : "");
        c->detail = detail ? detail : rei_strdup("");
        c->us = sqlite3_column_type(q, 6) == SQLITE_NULL ? -1 : sqlite3_column_double(q, 6);
    }
    sqlite3_finalize(q);
    return 1;
}

long st_probe_seen(Store* s, u64 poster, const char* digest) {
    sqlite3_stmt* q = prepared(s, "SELECT r.report FROM probe_runs r JOIN reports p ON p.id = r.report WHERE"
                                  " r.digest = ? AND p.poster = ? ORDER BY r.report LIMIT 1");
    long id = 0;
    if (!q) return 0;
    text_at(q, 1, digest);
    sqlite3_bind_int64(q, 2, (sqlite3_int64)poster);
    if (sqlite3_step(q) == SQLITE_ROW) id = (long)sqlite3_column_int64(q, 0);
    sqlite3_finalize(q);
    return id;
}

int st_probe_runs(Store* s, StProbeRun** runs, int* n) {
    sqlite3_stmt* q = prepared(s, "SELECT " RUN_COLUMNS " FROM probe_runs r JOIN reports p ON p.id = r.report"
                                  " ORDER BY r.report DESC");
    return rows(q, (void**)runs, n, sizeof **runs, row_run);
}
void st_probe_runs_free(StProbeRun* runs, int n) {
    int i;
    for (i = 0; i < n; ++i) probe_free(&runs[i].p);
    free(runs);
}

static void row_name(sqlite3_stmt* q, void* to) {
    StProbeName* name = to;
    const unsigned char* key = sqlite3_column_text(q, 3);
    name->id = sqlite3_column_int(q, 0);
    name->suite = text_of(q, 1);
    name->name = text_of(q, 2);
    snprintf(name->key, sizeof name->key, "%s", key ? (const char*)key : "");
}
int st_probe_names(Store* s, StProbeName** names, int* n) {
    sqlite3_stmt* q = prepared(s, "SELECT id, suite, name, key FROM probe_names WHERE id IN (SELECT DISTINCT name FROM"
                                  " probe_cases) ORDER BY id");
    return rows(q, (void**)names, n, sizeof **names, row_name);
}
void st_probe_names_free(StProbeName* names, int n) {
    int i;
    for (i = 0; i < n; ++i) {
        free(names[i].suite);
        free(names[i].name);
    }
    free(names);
}

static void row_result(sqlite3_stmt* q, void* to) {
    StProbeResult* r = to;
    r->report = (long)sqlite3_column_int64(q, 0);
    r->name = sqlite3_column_int(q, 1);
    r->result = sqlite3_column_int(q, 2);
}
int st_probe_results(Store* s, StProbeResult** results, int* n) {
    sqlite3_stmt* q = prepared(s, "SELECT report, name, result FROM probe_cases ORDER BY name, report");
    return rows(q, (void**)results, n, sizeof **results, row_result);
}

static void row_detail(sqlite3_stmt* q, void* to) {
    StProbeDetail* d = to;
    char* text;
    d->report = (long)sqlite3_column_int64(q, 0);
    d->result = sqlite3_column_int(q, 1);
    d->tier = (text = text_of(q, 2)) ? text : rei_strdup("");
    d->reason = (text = text_of(q, 3)) ? text : rei_strdup(d->result == ST_CRASHED ? "crashed" : "");
    d->detail = (text = text_of(q, 4)) ? text : rei_strdup("");
    d->us = sqlite3_column_type(q, 5) == SQLITE_NULL ? -1 : sqlite3_column_double(q, 5);
}
int st_probe_details(Store* s, int name, int limit, StProbeDetail** details, int* n) {
    sqlite3_stmt* q = prepared(s, "SELECT report, result, tier, reason, detail, us FROM probe_cases WHERE name = ? AND"
                                  " result != 0 ORDER BY report DESC LIMIT ?");
    if (q) {
        sqlite3_bind_int(q, 1, name);
        sqlite3_bind_int(q, 2, limit);
    }
    return rows(q, (void**)details, n, sizeof **details, row_detail);
}
void st_probe_details_free(StProbeDetail* details, int n) {
    int i;
    for (i = 0; i < n; ++i) {
        free(details[i].tier);
        free(details[i].reason);
        free(details[i].detail);
    }
    free(details);
}
