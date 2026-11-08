<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# Tracer

## Index

* [Reports](#reports)
  * [The worker](#the-worker)
  * [What a report keeps](#what-a-report-keeps)
  * [Adding to a report](#adding-to-a-report)
  * [A log sent without the mention](#a-log-sent-without-the-mention)
  * [A run of edenprobe](#a-run-of-edenprobe)
  * [Deleting, and the hourly limit](#deleting-and-the-hourly-limit)
  * [Summarized and named again](#summarized-and-named-again)
* [Commands](#commands)
* [Issues](#issues)
* [How old a build is](#how-old-a-build-is)
* [What the runs say together](#what-the-runs-say-together)
* [What it keeps of people](#what-it-keeps-of-people)
* [The site](#the-site)
* [Settings](#settings)
* [The service](#the-service)

`tracer/`, in C99 with [Concord](https://github.com/Cogmasters/concord) 3.0.1, libcurl, SQLite, libarchive, cJSON
(ReiWimsy's copy) and ScarletNX's own log summary (`tracer/third_party/logscan`, the runtime's `--scan-log`). It keeps
the game reports that people send it on Discord and draws them as a static site, grouped by game. Its MCP server,
which gives the same reports to agents, has a page of its own: [tracer-mcp](./TRACER-MCP.md). Installing both:
[Install](./INSTALL.md#tracer).

## Reports

A message that mentions Tracer with a ScarletNX log attached, in a channel `TRACER_CHANNELS` lists (or a thread or a
forum post of one), or in any channel and thread it can read with `TRACER_CHANNELS=all`, becomes a report. Its reply links the report's page, with how the run ended, its signature and
build, how old that build is ([below](#how-old-a-build-is)), and what was not kept.

### The worker

A report's files are read by a worker: the bot runs itself again (`tracer --report`) for each message that has files,
without its settings or its tokens in the environment, within 1 GiB of memory, 120 s of processor, 600 s in all and
128 MiB a file, so that a file that crashes or stalls its reader costs that report alone. A message without files
starts no worker. A worker runs as the bot's user, and the bot keeps its own memory and environment, where its tokens
are, from debuggers of that user (not dumpable on Linux, `PROC_TRACE_CTL` on FreeBSD: no ptrace, no
`/proc/<pid>/environ` or `kern.proc.env`), so that a worker a file took over cannot read them; it does not start
without that. A worker downloads the files into its own folder in the work folder (`work/` beside the database), then
shuts itself in that folder before it reads any of them: on FreeBSD Capsicum's capability mode, the folder's
descriptor allowed to look up, make, read, write and remove its files and nothing else; on Linux Landlock, files read
and written in that folder alone, and no TCP and no signal outside it where the kernel's Landlock has them (ABI 4 and
6). A worker a file took over can then write neither the site nor the database, nor read any other file. One that
cannot shut itself in reads nothing, and the bot takes no answer that does not say it was shut in (the system's name
for it). The worker's answer is checked before any of its files moves from the work folder into the site.

### What a report keeps

- Its logs (`.log`, `.txt`, `.jsonl`, or one packed in a `.gz` or a `.zip`; 3 at most, 64 MiB each) are downloaded from
  Discord, each home folder in them written as `~` (`/home/<user>`, `C:\Users\<user>`, Android's app folders), the crash
  report's code and memory dumps and registers left out (the game's own bytes, which the summary leaves out too), each
  IPv4 address but the loopback's written as `[ip]` and each e-mail address as `[email]` (a network game's log names the
  player's address and the other players'), and summarized: the game, its title ID and version, the build, the system
  and GPU, how the run ended and where, the service commands and what else the runtime missed, the last lines. A file
  that is no ScarletNX log, nor a run of edenprobe, is named in the reply and not kept. Of a `.zip`, the log is the
  entry [a run of edenprobe](#a-run-of-edenprobe) is in, else its first `.log`, `.txt` or `.jsonl`, else its first
  file that is no picture or video; never one of its pictures, nor a Mac's `__MACOSX/` fork. A zip's names are read as
  UTF-8 whatever the worker's locale (ScarletNX's Share Log flags each name so), in a locale it makes before it shuts
  itself in; an entry whose name it cannot read is passed by, and the walk goes on.
- Each absolute path outside a home folder in a log is written as `~` and its file's name, as where a player keeps
  games says how they arrange their disks, and a disk's label may be theirs: a path under `/mnt`, `/media`,
  `/run/media`, `/Volumes` (macOS), `/storage` or `/sdcard` (Android), or on a drive (`D:\`, `D:/`, WSL's `/mnt/d/`) but
  in its `\Users` (a home, written as one), `\Windows` and `\Program Files` (the system's and the programs', which name
  no one). `/mnt/Disk/Games/Some Game.nsp (9 MB mapped)` becomes `~/Some Game.nsp (9 MB mapped)`; its folders go, and so
  does the mount's own name (a disk's label, an account's under `/media`), as `/media/someone` becomes `~`. The last
  name stays when it is a file's (a dot, then a letter or a digit); one that is no file's goes with the words after it
  on the line, which nothing tells from it, as an account's name does.
- Its screenshots (PNG, JPEG, WebP, GIF or PPM, read from their bytes; 8 at most, 25 MiB each), attached or in a `.zip`
  beside its log (each entry named `.png`, `.jpg`, `.jpeg`, `.webp`, `.gif` or `.ppm`, kept under its file's name
  without the zip's folders; a Mac's `__MACOSX/` forks are none), lose their metadata: EXIF and its place and camera,
  XMP, PNG's text and time, JPEG's and GIF's comments, a GIF's applications' data but its loop, and what follows a
  JPEG's or a GIF's end (a phone's motion photo video, a maker's trailer). A PPM, the frames ScarletNX's runtimes save
  (P6, 8 bits a channel; the capture key's `captures/screenshot_<when>.ppm`), is kept as a PNG, which a browser shows.
  A file whose bytes are a picture is taken as a screenshot, whatever its name says: it is never read as a log. The
  reply names the screenshots past the 8 with that room (`shot_9.png, shot_10.png, shot_11.png and 2 more in
  logs.zip: a report keeps 8 screenshots`), and one too large with its size (`huge.png in logs.zip: it is 30.2 MiB, over
  the 25 MiB a screenshot may be`), as it does a log or a video; and, on a system with no UTF-8 locale, how many of a
  zip's files had names it could not read.
- Its videos (MP4 or WebM, read from their bytes; 2 at most, 100 MiB each) are played on the report's page. An MP4's
  place, time and camera (its udta and meta boxes whole, where a phone keeps them under its own keys, XMP's uuid box,
  and ©xyz, loci, ©mak and ©mod wherever else they are) are blanked where they are, as free boxes of zeros, so that
  nothing in it moves; a WebM is kept as it is. The web server must allow them (`media-src 'self'` in
  `dist/freebsd/tracer-nginx.conf`).
- The game is the first log's, of the console its runtime's header says: the Switch, the 3DS, the Vita or the GBA.
  Its key is its title ID (the GBA's: `gba-` and its game code), else `n-`, the console's name but the Switch's, and
  its name. A log that has the title ID alone takes the name other reports' logs gave the game, else the name the
  list of games' names (`TRACER_TITLES`) gives it: the Switch's base applications by title ID, from
  [titledb](https://github.com/blawar/titledb) (an update's or an add-on's ID finds its game), and the GBA's games by
  game code, from No-Intro's list in [libretro-database](https://github.com/libretro/libretro-database), which names a
  GBA game before its cartridge's 12-letter title. The list names games, so it is never in this repository:
  `python3 scripts/make-titles.py --out FILE` makes it where the bot runs. One that has the name alone goes with the
  reports that have its title ID. The 3DS's and the Vita's headers have the game's own name. A name with two `?` in a
  row is one ScarletNX's launcher wrote into a shared log, which turns each byte outside ASCII into a `?`
  ("Pok??mon"): it counts as no name, so other reports' and the list's come first, and a report kept with one is
  named again from them at the next start (its log line: `report #38 named <name>, which was <the ? name>`). The
  message's text names the game only when the log says neither; its first 200 bytes are kept as the report's note.
- Beside how the run ended, what the runtime missed, counted from the first log's summary: "1116 shaders, 1 texture
  format, 1 service command" (shaders by the shaders, the rest by the different ones). A run with no failure that
  missed something is answered in the warning's colour. The site marks it "something missed" beside the outcome, the
  counts in its tooltip and on the report's page, with a filter for it.

### Adding to a report

A reply to any of a report's messages (the one that sent it, an update, Tracer's answer, also one it posted before it
kept them, whose link names the report) that mentions Tracer adds its logs, screenshots, videos and words to it, as
`@Tracer add 12` does, within the same limits (3 logs and 8 screenshots a report; a `.zip` brings its screenshots while
the report has room for them, though its logs are full). Only the person who sent the report,
the owner and administrators of the server it was sent in and `TRACER_ADMINS` add to it; anyone else is asked for a
report of their own, which goes with the same game. A report its logs did not name takes the name a later log of the
same title ID gives. Words added later are shown under the report's note, each with its date.

### A log sent without the mention

A reply to that message that mentions Tracer and brings no file of its own makes a report of the message it replies
to, as if it had mentioned Tracer: its logs, screenshots and videos, its author as the one who sent it (who deletes it
and adds to it, and whose deleted message takes it back), its words as the note, else the reply's. The answer is a
reply to the one who asked, and the hourly limit counts them. Tracer asks Discord for that message
(`GET channels/<id>/messages/<id>`, with the Read Message History permission), and Discord shows a bot the files of a
message that does not mention it only with the Message Content intent ([What it keeps of
people](#what-it-keeps-of-people)); without it the reply is told to send the log again with a mention, and the start's
log line says which. A reply that brings files makes a report of those, as any message does; a reply to a message that
is part of a report already gets that report's link; a message of another channel or server, a bot's or a webhook's,
one that is gone and one with no log are refused, each with why.

### A run of edenprobe

[edenprobe](https://github.com/scarletnx/edenprobe) checks an emulator against the console, case by case. A report
whose first log holds a run of it is the run's, kept apart from the games: in no game's list, nor `all.html` and
`api/reports.json`, but on the site's Probe pages ([The site](#the-site)), by machine: the emulator, the driver and its
version, the system. A run comes in three forms:

- ScarletNX's log of the run, alone or in the launcher's Share Log zip, whose entry that holds a whole run is the one
  kept (else one a run begins in, else its first log, as for any zip): the machine from its header (`system:`, the
  driver's `vulkan: the driver that runs: radv, Mesa 26.1.8 (driver ID 3)`, and the GPU's line with the driver's
  version), the report's lines as `[game] {...}`;
- Eden's log of it, or rEden's: the machine from its `Driver:`, `Device:`, `Vulkan:` and `Host OS:` lines, the report's
  lines as `Debug.Emulated` ones. Eden logs no driver ID: it is read from the driver's name, and the reply says so;
- the report itself, `report.jsonl`, which names no machine: it is kept as the console's reference.

A run begins at its note `edenprobe 0.9.0 (v0.9.0) starting`; of several in one file (the Share Log's `summary.txt`
holds a run's output and its log), the last whole one is read, one with its summary, else the last one. Kept of it:
the machine (the system and its family, the GPU, the driver's VkDriverId, name and version as its vendor numbers it,
which are ScarletNX's ban table's), the emulator and its build, edenprobe's version, the words its mode note says ran
(`cpu graphics net notime`), and each case's suite, name, tier, result, reason, detail (its first 300 bytes) and time,
10000 cases at most.

The detail of the cases of edenprobe's service, net and pad families, and of a suite its table does not have, is left
out, and so is the text of the net family's notes: the account cases name a user's id and nickname, the network ones
the addresses and sessions the net file names. They are taken out of every log a report keeps, wherever such a line
is, before anything reads it, so neither the database nor the site has them.

The reply gives the counts and the suites that failed most, `edenprobe 0.9.0 on radv, Mesa 26.1.8 (Linux): 1034
passed, 26 failed (astc 15, astcimage 9, tex8k 2), 6 skipped, 3 crashed`, and what the run lacks: no summary line, a
summary that counts more cases than the file has (a launch that went on after a crash logs its own cases alone), lines
edenprobe does not write, a report.jsonl's machine. The same run sent again by the same person, line for line, is
answered with the first report's link and kept no more. A log whose lines of edenprobe have no run's start, a launch
that went on after a crash, is kept as the game's log, and the reply says why it is no run.

### Deleting, and the hourly limit

`@Tracer delete 12`, or a reply with `@Tracer delete`, deletes a report, for the person who sent it, the owner and
administrators (the Administrator permission) of the server it was sent in, and `TRACER_ADMINS`. A message deleted on
Discord takes back what it sent: the whole report, or what it added to one. A report's number is never another's after
it is deleted, as replies link to it.

Ten reports and updates an hour from one person at most, counted as they are asked for: one whose files were no log
counts, and so does one deleted since (its files were downloaded); a message with no file and no note does not.

### Summarized and named again

When the log summary or the list of names changes (`scripts/sync-logscan.sh`, `scripts/make-titles.py`), or what a
report keeps of its logs (`TR_FACTS`), the next start summarizes each kept log again, draws each report's facts again
from its first log, files a report whose first log now names another game under that game, names one its logs don't
name from the list, makes a report whose first log holds a run of edenprobe the run's, and takes out of each kept log
what a run's lines would say of the player and the paths outside a home. A report keeps no zip, only its log and
screenshots: a zip's screenshots sent before Tracer read them are not found again, and are added by a reply with them.

## Commands

Each is a message that mentions Tracer, in a channel it answers in:

| Message | What it does | Who |
|---|---|---|
| a log attached, and words | a report, the words its note | anyone, ten reports and updates an hour |
| `@Tracer help` | how to send a report | anyone |
| `@Tracer add 12`, and words; or a reply to one of report 12's messages | adds the message's files and words to report 12 | the one who sent it, the server's owner and administrators, `TRACER_ADMINS` |
| a reply with no file, to a message with a log | a report of that message | anyone |
| `@Tracer delete 12`, or `@Tracer delete` in a reply | deletes report 12 | the one who sent it, the server's owner and administrators, `TRACER_ADMINS` |

## Issues

A report becomes a GitHub issue once a server's administrator approves it: with `TRACER_ISSUES_REPO`, the owner or an
administrator of the server it was sent in (or one of `TRACER_ADMINS`) reacts to one of its messages (the one that sent
it, an update, Tracer's answer) with the approval's emoji, `TRACER_ISSUES_EMOJI`, the check mark by default. Tracer
opens the issue in that repository with what the site shows of the report: its facts and its first log's summary (home
folders and the folders of paths outside them as `~`, addresses as `[ip]` and `[email]`, no dumps), each of a log's or
a player's words in a code block, where GitHub links no `@name` and no `#number`, the report's page, and its
screenshots, each the site's picture 320 pixels wide linked to it and named by its number in the report (a file's
name is a player's words). GitHub's API takes no file for an issue: its image proxy fetches each picture from the site,
so the site must answer it. The issue's number goes into the database, onto
the report's page and its JSON, and into a short reply on Discord. A report is filed once: a second approval opens no
other issue, and one GitHub refused (its status and message in the reply) is tried again by the next approval. Anyone
else's reaction, and another emoji, are left as they are. GitHub is asked from the bot's own process, with a 30 s
timeout; the worker that reads the files never holds this token either. The gateway asks for the reactions only when
`TRACER_ISSUES_REPO` is set.

## How old a build is

How old a new report's build is goes into the reply and onto the report's page and its JSON (`build_age`), as it was
when the report came: the commits of main it lacks and the days from its commit's day to main's last (UTC), with the
newest nightly to try instead; or, for a build with local changes (`-dirty`) or with a commit the repository does not
have, that it is not a published one:

    Build a52fd20 (main) is 37 commits and 2 days behind main; the newest nightly is 2026-11-04 (413fc2f): retry on it before we dig in.
    Build 89b2da3-dirty is not a published build (local changes): numbers from it may not match main.

The repository is `TRACER_UPSTREAM_REPO`, and the nightlies are the releases of its owner's `nightly` repository
(tagged `nightly-<day>-<commit>`). GitHub is asked with `TRACER_ISSUES_TOKEN`, from the bot's own process: one GraphQL
query a commit (its day, main's, how far behind main it is, the newest nightly), its answer kept in the database for an
hour, as main and the nightlies move. There is no line for a build that has every commit of main, a build of the newest
nightly, one without git, or when GitHub does not answer (said in the log, and asked again at the next report).
Without the token nothing is asked, and only a `-dirty` build is said.

## What the runs say together

Fixed rules over the runs of edenprobe (`tracer/src/rules.c`), each comparing one case of one edenprobe version run
with the same words, and counting ScarletNX's runs; Eden's are shown beside them, as a second emulator failing a case
on the same driver points at the driver. A case fails where its result is `fail`; a skip, a crash among them, says
nothing. Nothing here is applied: a person reads it and decides.

- **A ban candidate**, for a case on a driver (its VkDriverId) and a system's family, when all three hold:
  1. it fails in 3 reports or more, from 2 people or more, in a range of that driver's versions: the versions seen in a
     row, sorted, where it fails and never passes;
  2. it passes in 3 reports or more of 2 other drivers or more, whose builds are at most 7 days from a failing
     report's (the day the build's commit was made, else the day the report came): without that, the failure is
     ScarletNX's;
  3. the range runs from its lowest version to the next version seen above it, where the case passes (or passes and
     fails: a mixed end, said so), or has no end when none is seen: failing on 26.1.0 to 26.1.8 and passing on 26.2.0
     is `>= 26.1.0 < 26.2.0`.

  With it come the reports, the people, Eden's runs of the case on that driver, the optional extensions every failing
  run's log lists as on (`vulkan: optional extensions on: ...`, where the runtime logs that line), and a draft line in
  the ban table's format (ScarletNX's `src/nx/gpu/vk/extension_bans.txt`), its key `<key>` for a person to choose, as
  the runs cannot say which extension or feature breaks:

      <key> | off | radv | >= 26.1.0 < 26.2.0 | linux | * | fails edenprobe's astc case "..." | Tracer #12, #15, #18; edenprobe 0.9.0 "..." | edenprobe:astc: ... | 2026-11-07 broken on 26.1.8 | -

- **ScarletNX's own failure**: a case that fails on 3 drivers or more, or on every driver seen when two or more are; a
  driver counts as failing when its runs fail the case and none passes it. Such a case is no ban candidate. Eden's
  runs on those drivers are counted beside it: where Eden passes it, the failure is ScarletNX's to fix.
- **A regression**: on one driver, one version of it and one system's family, a case that passed in every run of a
  build and fails in every run of each build after it, to the newest; builds of published commits only (no `-dirty`),
  in the order of their commits' days. The last build that passed and the first that failed bound the commits to look
  at.

## What it keeps of people

The site does not say who sent a report. The database keeps the sender's user id (for the deletion, the updates and
the hourly limit) and the ids of the messages that lead to a report, nothing else of them; it keeps nothing of
messages that do not mention it, but the one a reply asks it to make a report of. Its gateway asks for no privileged
intent: Discord sends it the text and the attachments of a message that mentions the bot, and of no other. The one
message a reply asks for is read through Discord's REST API, which shows its files when the application has the
Message Content intent: a switch in the Developer Portal (Bot, Privileged Gateway Intents) for a bot in fewer than 100
servers, which changes nothing else Tracer receives. How the logs and pictures lose what names a person: [What a
report keeps](#what-a-report-keeps); what keeps the bot's tokens and its site apart from the files strangers send:
[Security](./SECURITY.md#tracers-workers).

## The site

Static files, drawn again where a report changes them, and all of them at each start (a new version's pages):

| Path | What it is |
|---|---|
| `index.html` | each game, its latest outcome, its reports' count; a search and a filter by outcome |
| `all.html` | every report, the newest first |
| `g/<key>.html` | a game's reports: `<key>` its title ID, or `n-` and its name's letters for a game known by name |
| `r/<id>/` | a report: its facts, its screenshots (each shown small, and opened whole by a click), and each log with its summary, to read or download (a log sent in a `.zip` or a `.gz` is kept as its text and downloads as one: `logs.zip` as `logs.log`, `game.log.gz` as `game.log`); a run of edenprobe's has its counts and each case that did not pass |
| `probe/index.html` | the runs of edenprobe: the machines (an emulator, a driver and its version, a system), each case that fails somewhere with its results on each machine, each suite's, and each run |
| `probe/c/<key>.html` | a case of edenprobe: `<key>` 16 hex digits of its suite and name; its results on each machine, what the rules say of it, and its newest 200 failures and skips with their detail |
| `probe/d/<driver>.html` | a driver, by the ban table's name (`radv`, `nvidia`; `unknown` for the console's reports): its machines, the cases that fail on it, its ban candidates, its runs |
| `probe/candidates.html` | [what the runs say together](#what-the-runs-say-together): ban candidates with their draft lines, ScarletNX's own failures, regressions |
| `api/` | the same as JSON: `games.json`, `reports.json`, `r/<id>.json` (a report with its logs' summaries and the notes added later, a run of edenprobe's with its run); `probe/matrix.json`, `probe/c/<key>.json`, `probe/d/<driver>.json`, `probe/candidates.json`; no Discord id |
| `style.css`, `site.js`, `tracer.svg` | copied from `TRACER_ASSETS` at each start |

The lists filter in the browser (`site.js`: by words, outcome, branch, GPU; kept in the address's `#`) and work
without it. Everything a log or a message says is escaped. Its look is SWS's: Sepia until the reader picks another
theme at the top of the page, which the web server keeps for them ([Themes](./THEMES.md)). A web server serves the
folder as it is, `style.css` and `tracer.svg` as the reader picked: `dist/freebsd/tracer-nginx.conf`, which also sends
`/mcp` to [tracer-mcp](./TRACER-MCP.md).

## Settings

A line each of its env file, `/usr/local/etc/tracer.env` (the package puts `dist/freebsd/tracer.env.sample` there to
fill in; an empty value stops the service):

| Key | Default | What it is |
|---|---|---|
| `TRACER_TOKEN` | none: needed | the Discord bot's token |
| `TRACER_URL` | none: needed | the site's address, which the replies link: `https://`, or `http://localhost:8080` to try it on one machine |
| `TRACER_CHANNELS` | none: no report is taken | `id,id`: the channels it answers in, with their threads and a forum's posts; or `all`: every channel and thread it can read; the start's log line says them, or `all`; any other word stops it, naming the value |
| `TRACER_ADMINS` | none | `id,id`: people who delete, add to and approve any report beside the server's owner and administrators |
| `TRACER_DB` | `/var/db/tracer/tracer.db` | the database; the service sets it from `tracer_db` |
| `TRACER_SITE` | `/var/db/tracer-site` | the site's folder; the service sets it from `tracer_site` |
| `TRACER_ASSETS` | `/usr/local/share/tracer` | where `style.css`, `site.js` and `tracer.svg` are |
| `TRACER_TITLES` | none: no list | the games' names by title ID, made by `scripts/make-titles.py --out FILE`; without it a game its logs don't name keeps its title ID |
| `TRACER_ISSUES_REPO` | none: no issue is filed | `OWNER/NAME`: the repository an approved report becomes an issue of; the start's log line says it, or that it is not set |
| `TRACER_ISSUES_TOKEN` | none: needed with the repository | a fine-grained token of that repository alone, with Issues read and write and nothing else; never in the log |
| `TRACER_ISSUES_EMOJI` | the check mark (U+2705) | the approval's emoji: its character, or a server emoji's name |
| `TRACER_ISSUES_LABELS` | none | `label,label`: the labels each issue gets |
| `TRACER_UPSTREAM_REPO` | `scarletnx/ScarletNX` | `OWNER/NAME`: the repository whose main a report's build is compared with, its owner's `nightly` the nightlies'; asked with `TRACER_ISSUES_TOKEN`, not at all without it |

A `TRACER_` key it does not know stops it, naming the key; so does an id that is not one, an address that is not
`https://` or `http://`, and an issues key without `TRACER_ISSUES_REPO`. An id: Developer Mode in Discord, right click,
Copy Channel ID or Copy User ID; a forum channel's id takes its posts too.

## The service

It runs as the service `tracer` (`dist/freebsd/tracer.rc`): daemon(8) runs `/usr/local/libexec/tracer` as the user
`tracer` and starts it again 30 s after it stops, its output in syslog (tag `tracer`, the jail's `/var/log/messages`),
its env file exported to it by rc.subr. Before it starts, the service makes the database's folder (the user's, 700)
and the site's (the user's, 755, which anyone may read: the web server's jail mounts it read-only).

| Variable | Default | What it is |
|---|---|---|
| `tracer_enable` | `NO` | `YES` starts it with the jail |
| `tracer_env_file` | `/usr/local/etc/tracer.env` | its settings; root's, 600 |
| `tracer_db` | `/var/db/tracer/tracer.db` | the database, exported as `TRACER_DB`; its folder holds the workers' `work/` |
| `tracer_site` | `/var/db/tracer-site` | the site's folder, exported as `TRACER_SITE` |

    service -j tracer tracer start|stop|restart|status

Back up the database and the site's `r/` together: the files are there, the database names them. When its gateway
stops, its last line says why as ReiWimsy's does ([ReiWimsy](./REIWIMSY.md#when-it-stops)), and it ends with status 1,
which daemon(8) starts it again from.

Building it: [Build](./BUILD.md#tracer); its tests: [Testing](./TESTING.md).
