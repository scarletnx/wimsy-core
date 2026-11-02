/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The site: static pages drawn from the database into a folder a web server serves as it is. index.html lists the
   games, all.html every report, g/<key>.html a game's reports, r/<id>/ a report with its screenshots and logs, api/
   the same as JSON.
   Everything a log or a message says is escaped; site.js filters the lists in the browser, and the pages work without
   it */
#ifndef TR_SITE_H
#define TR_SITE_H
#include "store.h"

#define SITE_REPO "https://github.com/scarletnx/ScarletNX" /* where a log's commit is linked to */

/* the pages, each a string owned by the caller */
char* site_index_html(const StGame* games, int n);
char* site_all_html(const StReport* reports, int n);
char* site_game_html(const StGame* game, const StReport* reports, int n);
char* site_report_html(const StReport* r, const StFile* files, int nfiles, const StNote* notes, int nnotes);
/* the same as JSON, for programs (tracer-mcp, scripts): api/games.json, api/reports.json and api/r/<id>.json. No
   Discord id is in them, as none is on the pages */
char* site_games_json(const StGame* games, int n);
char* site_reports_json(const StReport* reports, int n);
char* site_report_json(const StReport* r, const StFile* files, int nfiles, const StNote* notes, int nnotes);

/* text written to root/rel through rel.new, its folders made: 0, or -1 said in the log */
int site_write(const char* root, const char* rel, const char* text, size_t n);
/* style.css, site.js and tracer.svg copied from the folder from into root: 0, or -1 said in the log */
int site_assets(const char* from, const char* root);
/* a report's page, its game's page (gone when the game has no report left) and the lists drawn again: 0, or -1 */
int site_update(Store* s, const char* root, long id, const char* key);
/* a report's folder gone: its files and its page */
void site_forget(const char* root, long id, const StFile* files, int nfiles);
/* some files of report id gone from its folder */
void site_forget_files(const char* root, long id, const StFile* files, int nfiles);
/* every page drawn again, as after an upgrade: 0, or -1 */
int site_rebuild(Store* s, const char* root);
#endif
