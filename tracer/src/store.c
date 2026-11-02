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
    "ALTER TABLE reports ADD COLUMN console TEXT;"};

/* a report's columns, in the order row_report reads them */
#define REPORT_COLUMNS                                                                                                 \
    "id, created, guild, channel, message, poster, game, title, named, title_id, note, version, build, commit_id, "    \
    "branch, system, gpu, driver, outcome, signature, missed, console"

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
                    " version, build, commit_id, branch, system, gpu, driver, outcome, signature, missed, console)"
                    " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
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

int st_count_since(Store* s, u64 poster, double since) {
    sqlite3_stmt* q = prepared(s, "SELECT count(*) FROM reports WHERE poster = ? AND created >= ?");
    int n = 0;
    if (!q) return 0;
    sqlite3_bind_int64(q, 1, (sqlite3_int64)poster);
    sqlite3_bind_double(q, 2, since);
    if (sqlite3_step(q) == SQLITE_ROW) n = sqlite3_column_int(q, 0);
    sqlite3_finalize(q);
    return n;
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
           " count(*) AS n, max(id) AS last FROM reports GROUP BY game) c ON r.id = c.last ORDER BY r.id DESC");
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
    memset(r, 0, sizeof *r);
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
