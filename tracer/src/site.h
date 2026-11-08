/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The site: static pages drawn from the database into a folder a web server serves as it is. index.html lists the
   games, all.html every report, g/<key>.html a game's reports, r/<id>/ a report with its screenshots and logs, probe/
   the runs of edenprobe, api/ the same as JSON.
   Everything a log or a message says is escaped; site.js filters the lists in the browser, and the pages work without
   it */
#ifndef TR_SITE_H
#define TR_SITE_H
#include "store.h"

#define SITE_REPO "https://github.com/scarletnx/ScarletNX" /* where a log's commit is linked to */
/* the theme picker at the top of every page, each theme with its Wimsy: links the site's nginx answers by setting the
   reader's theme or mode in a cookie of all scarletnx.dev and sending them back (dist/freebsd/tracer-nginx.conf,
   docs/THEMES.md) */
#define SITE_THEMES                                                                                                                    \
    "<nav class=\"sws\" aria-label=\"Theme\"><span>Theme</span>"                                                                       \
    "<a href=\"/sws/sepia\" referrerpolicy=\"same-origin\"><img src=\"/sws/logo-sepia.svg\" alt=\"\">Sepia</a>"                        \
    "<a href=\"/sws/wimsy\" referrerpolicy=\"same-origin\"><img src=\"/sws/logo-wimsy.svg\" alt=\"\">Wimsy</a>"                        \
    "<a href=\"/sws/fuddles\" referrerpolicy=\"same-origin\"><img src=\"/sws/logo-fuddles.svg\" alt=\"\">Fuddles</a>"                  \
    "<a href=\"/sws/default\" referrerpolicy=\"same-origin\"><img src=\"/sws/logo-default.svg\" alt=\"\">Default</a><span>Mode</span>" \
    "<a href=\"/sws/auto\" referrerpolicy=\"same-origin\">Auto</a>"                                                                    \
    "<a href=\"/sws/light\" referrerpolicy=\"same-origin\">Light</a>"                                                                  \
    "<a href=\"/sws/dark\" referrerpolicy=\"same-origin\">Dark</a></nav>\n"

/* the pages, each a string owned by the caller */
char* site_index_html(const StGame* games, int n);
char* site_all_html(const StReport* reports, int n);
char* site_game_html(const StGame* game, const StReport* reports, int n);
/* a run of edenprobe's report is given its run (with its cases), NULL for any other */
char* site_report_html(const StReport* r, const StFile* files, int nfiles, const StNote* notes, int nnotes,
                       const Probe* probe);
/* the same as JSON, for programs (tracer-mcp, scripts): api/games.json, api/reports.json and api/r/<id>.json. No
   Discord id is in them, as none is on the pages */
char* site_games_json(const StGame* games, int n);
char* site_reports_json(const StReport* reports, int n);
char* site_report_json(const StReport* r, const StFile* files, int nfiles, const StNote* notes, int nnotes,
                       const Probe* probe);

/* text written to root/rel through rel.new, its folders made: 0, or -1 said in the log */
int site_write(const char* root, const char* rel, const char* text, size_t n);
/* the file at from moved to root/rel, its folders made: a rename, or a copy across file systems. 0, or -1 said in the
   log */
int site_move(const char* root, const char* rel, const char* from);
/* style.css, site.js and tracer.svg copied from the folder from into root: 0, or -1 said in the log */
int site_assets(const char* from, const char* root);
/* a report's page, its game's page (gone when the game has no report left) and the lists drawn again, and every page
   of the runs of edenprobe when key is PROBE_KEY: 0, or -1 */
int site_update(Store* s, const char* root, long id, const char* key);
/* the pages of the runs of edenprobe, every one drawn again from the database: probe/index.html (each case that fails
   somewhere, by machine: the emulator, the driver and its version, the system; each suite; each run),
   probe/c/<key>.html (a case on each machine, and its failures and skips with their detail, the newest 200),
   probe/d/<driver>.html (a driver's machines, the cases that fail on it, its candidates, its runs),
   probe/candidates.html (rules.h's ban candidates with their draft lines, ScarletNX's own failures and the
   regressions), and the same as JSON in api/probe/ (matrix.json, c/<key>.json, d/<driver>.json, candidates.json); a
   case's or a driver's page that no run names any more removed. 0, or -1 */
int site_probe(Store* s, const char* root);
/* a report's folder gone: its files and its page */
void site_forget(const char* root, long id, const StFile* files, int nfiles);
/* some files of report id gone from its folder */
void site_forget_files(const char* root, long id, const StFile* files, int nfiles);
/* every page drawn again, as after an upgrade: 0, or -1 */
int site_rebuild(Store* s, const char* root);
#endif
