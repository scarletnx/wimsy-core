<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# Tracer

`tracer/`, in C99 with [Concord](https://github.com/Cogmasters/concord) 3.0.1, libcurl, SQLite, libarchive, cJSON
(ReiWimsy's copy) and ScarletNX's own log summary (`tracer/third_party/logscan`, the runtime's `--scan-log`). It keeps
the game reports that people send it on Discord and draws them as a static site, grouped by game.

A message that mentions it with a ScarletNX log attached becomes a report. Its files are read by a worker: the bot runs
itself again (`tracer --report`) for each message, without its settings or its token in the environment, within 1 GiB
of memory, 120 s of processor, 600 s in all and 128 MiB a file, so that a file that crashes or stalls its reader costs
that report alone. The worker's answer is checked before any of its files moves from the work folder (`work/` beside
the database) into the site. A report keeps:

- Its logs (`.log`, `.txt`, or one packed in a `.gz` or a `.zip`; 3 at most, 64 MiB each) are downloaded from Discord,
  each home folder in them written as `~` (`/home/<user>`, `C:\Users\<user>`, Android's app folders), the crash report's
  code and memory dumps and registers left out (the game's own bytes, which the summary leaves out too), each IPv4
  address but the loopback's written as `[ip]` and each e-mail address as `[email]` (a network game's log names the
  player's address and the other players'), and summarized: the game, its title ID and version, the build, the system
  and GPU, how the run ended and where, the service commands and what else the runtime missed, the last lines. A file
  that is no ScarletNX log is named in the reply and not kept.
- Its screenshots (PNG, JPEG, WebP, GIF or PPM, read from their bytes; 8 at most, 25 MiB each) lose their metadata:
  EXIF and its place and camera, XMP, PNG's text and time, JPEG's comments, and what follows a JPEG's end (a phone's
  motion photo video, a maker's trailer). A PPM, the frames ScarletNX's runtimes save (P6, 8 bits a channel), is kept
  as a PNG, which a browser shows.
- Its videos (MP4 or WebM, read from their bytes; 2 at most, 100 MiB each) are played on the report's page. An MP4's
  place, time and camera (its udta and meta boxes whole, where a phone keeps them under its own keys, XMP's uuid box,
  and ©xyz, loci, ©mak and ©mod wherever else they are) are blanked where they are, as free boxes of zeros, so that
  nothing in it moves; a WebM is kept as it is. The web server must allow them (`media-src 'self'` in
  `tracer-nginx.conf`).
- The game is the first log's, of the console its runtime's header says: the Switch, the 3DS, the Vita or the GBA.
  Its key is its title ID (the GBA's: `gba-` and its game code), else `n-`, the console's name but the Switch's, and
  its name. A log that has the title ID alone takes the name other reports' logs gave the game, else the name the
  list of games' names (`TRACER_TITLES`) gives it: the Switch's base applications by title ID, from
  [titledb](https://github.com/blawar/titledb) (an update's or an add-on's ID finds its game), and the GBA's games by
  game code, from No-Intro's list in [libretro-database](https://github.com/libretro/libretro-database), which names a
  GBA game before its cartridge's 12-letter title. The list names games, so it is never in this repository:
  `scripts/make-titles.py --out FILE` makes it where the bot runs. One that has the name alone goes with the reports
  that have its title ID. The 3DS's and the Vita's headers have the game's own name. The message's text names the game
  only when the log says neither, and is kept as the report's note.
- The reply links the report's page, with how the run ended, its signature and build, how old that build is (below),
  and what was not kept.
- Beside how the run ended, what the runtime missed, counted from the first log's summary: "1116 shaders, 1 texture
  format, 1 service command" (shaders by the shaders, the rest by the different ones). A run with no failure that
  missed something is answered in the warning's colour. The site marks it "something missed" beside the outcome, the
  counts in its tooltip and on the report's page, with a filter for it.

A report grows: a reply to any of its messages (the one that sent it, an update, Tracer's answer, also one it posted
before it kept them, whose link names the report) that mentions Tracer
adds its logs, screenshots and words to it, as `@Tracer add 12` does, within the same limits (3 logs and 8
screenshots a report). Only the person who sent the report and the server's administrators add to it; anyone else is
asked for a report of their own, which goes with the same game. A report its logs did not name takes the name a later
log of the same title ID gives. Words added later are shown under the report's note, each with its date.

A log sent without the mention: a reply to that message that mentions Tracer and brings no file of its own makes a
report of the message it replies to, as if it had mentioned Tracer: its logs, screenshots and videos, its author as the
one who sent it (who deletes it and adds to it, and whose deleted message takes it back), its words as the note, else
the reply's. The answer is a reply to the one who asked, and the hourly limit counts them. Tracer asks Discord for that
message (`GET channels/<id>/messages/<id>`, with the Read Message History permission), and Discord shows a bot the
files of a message that does not mention it only with the Message Content intent (below); without it the reply is told
to send the log again with a mention, and the start's log line says which. A reply that brings files makes a report of
those, as any message does; a reply to a message that is part of a report already gets that report's link; a message of
another channel or server, a bot's or a webhook's, one that is gone and one with no log are refused, each with why.

`@Tracer help` says how; `@Tracer delete 12`, or a reply with `@Tracer delete`, deletes a report, for the person who
sent it, the owner and administrators (the Administrator permission) of the server it was sent in, and `TRACER_ADMINS`.
A message deleted on Discord takes back what it sent: the whole report, or what it added to one. Ten reports and updates
an hour from one person at most, counted as they are asked for: one whose files were no log counts, and so does one
deleted since (its files were downloaded); a message with no file and no note does not. A report's number is never
another's after it is deleted, as replies link to it.

When the log summary or the list of names changes (`scripts/sync-logscan.sh`, `scripts/make-titles.py`), the next
start summarizes each kept log again, draws each report's facts again from its first log, files a report whose first
log now names another game under that game, and names one its logs don't name from the list.

A report becomes a GitHub issue once a server's administrator approves it: with `TRACER_ISSUES_REPO`, the owner or an
administrator of the server it was sent in (or one of `TRACER_ADMINS`) reacts to one of its messages (the one that sent
it, an update, Tracer's answer) with the approval's emoji, `TRACER_ISSUES_EMOJI`, the check mark by default. Tracer
opens the issue in that repository with what the site shows of the report: its facts and its first log's summary (home
folders as `~`, addresses as `[ip]` and `[email]`, no dumps), each of a log's or a player's words in a code block,
where GitHub links no `@name` and no `#number`, and the report's page. The issue's number goes into the database, onto
the report's page and its JSON, and into a short reply on Discord. A report is filed once: a second approval opens no
other issue, and one GitHub refused (its status and message in the reply) is tried again by the next approval. Anyone
else's reaction, and another emoji, are left as they are. GitHub is asked from the bot's own process, with a 30 s
timeout; the worker that reads the files never holds this token either.

How old a new report's build is goes into the reply and onto the report's page and its JSON (`build_age`), as it was
when the report came: the commits of main it lacks and the days from its commit's day to main's last (UTC), with the
newest nightly to try instead; or, for a build with local changes (`-dirty`) or with a commit the repository does not
have, that it is not a published one:

    Build a52fd20 (main) is 37 commits and 2 days behind main; the newest nightly is 2026-11-04 (413fc2f): retry on it before we dig in.
    Build 89b2da3-dirty is not a published build (local changes): numbers from it may not match main.

The repository is
`TRACER_UPSTREAM_REPO`, and the nightlies are the releases of its owner's `nightly` repository (tagged
`nightly-<day>-<commit>`). GitHub is asked with `TRACER_ISSUES_TOKEN`, from the bot's own process: one GraphQL query a
commit (its day, main's, how far behind main it is, the newest nightly), its answer kept in the database for an hour,
as main and the nightlies move. There is no line for a build that has every commit of main, a build of the newest
nightly, one without git, or when GitHub does not answer (said in the log, and asked again at the next report). Without
the token nothing is asked, and only a `-dirty` build is said.

The site does not say who sent a report. The database keeps the sender's user id (for the deletion, the updates and
the hourly limit) and the ids of the messages that lead to a report, nothing else of them; it keeps nothing of
messages that do not mention it, but the one a reply asks it to make a report of. Its gateway asks for no privileged
intent: Discord sends it the text and the attachments of a message that mentions the bot, and of no other. The one
message a reply asks for is read through Discord's REST API, which shows its files when the application has the
Message Content intent: a switch in the Developer Portal (Bot, Privileged Gateway Intents) for a bot in fewer than 100
servers, which changes nothing else Tracer receives.

## The site

Static files, drawn again where a report changes them, and all of them at each start (a new version's pages):

| Path | What it is |
|---|---|
| `index.html` | each game, its latest outcome, its reports' count; a search and a filter by outcome |
| `all.html` | every report, the newest first |
| `g/<key>.html` | a game's reports: `<key>` its title ID, or `n-` and its name's letters for a game known by name |
| `r/<id>/` | a report: its facts, screenshots, and each log with its summary, to read or download |
| `api/` | the same as JSON: `games.json`, `reports.json`, `r/<id>.json` (a report with its logs' summaries and the notes added later); no Discord id |
| `style.css`, `site.js`, `tracer.svg` | copied from `TRACER_ASSETS` at each start |

The lists filter in the browser (`site.js`: by words, outcome, branch, GPU; kept in the address's `#`) and work
without it. Everything a log or a message says is escaped. In the Phosphor palette: cyan, blue, purple and rose on
navy, light as well as dark. A web server serves the folder as it is: `dist/freebsd/tracer-nginx.conf`.

## The MCP server

`tracer-mcp` gives the reports to agents as an [MCP](https://modelcontextprotocol.io) server at `<site>/mcp`, over
MCP's Streamable HTTP transport (one JSON-RPC message a POST, answered as JSON; no stream, no session). It reads only
the site's folder, `api/` and the logs, never the database, so it runs where the site is served; nginx sends `/mcp` to
it. Read-only and public, as the site is: it names no one either.

| Tool | What it gives |
|---|---|
| `list_games` | the games, their console, report counts and latest outcome; by words of the title or title ID, by console, by outcome |
| `search_reports` | reports, the newest first; by words, game, console, outcome, signature, branch, GPU |
| `get_report` | a report's facts and each log's summary (how it ended, where, its frames, what the runtime missed), and for a log that failed what ScarletNX's `symbolize` asks for |
| `read_log` | a log's lines with their numbers, from a line on or those that hold some text, and where to go on |
| `get_screenshot` | one of a report's screenshots, as an image (8 MiB at most; a larger one is named by its address) |
| `signatures` | the failures grouped by where they happened, across games, the most reported first |

An agent adds it by its address, Claude Code with `claude mcp add --transport http tracer https://<site>/mcp`. A log's
addresses match only the game version it names, and only a copy of that version names them: `get_report` gives each log
that failed a `symbolize` object (its title ID, game version and game addresses) for ScarletNX's own MCP server
(`scarletnx --mcp`) on a machine that has the game, its `find_game` then `symbolize` or `disassemble`. Tracer keeps no
game code, not even the bytes a crash report dumps: nothing of a game goes to a server that is not its owner's. A
browser's request whose Origin is another site's gets 403; a request over 1 MiB, 413.

Its settings, in the environment (`tracer_mcp_url`, `tracer_mcp_site` and `tracer_mcp_listen` in rc.conf):
`TRACER_URL` (needed), `TRACER_SITE` (the site's folder, `/var/db/tracer-site`), `TRACER_MCP_LISTEN` (an IPv4 address
and port, `127.0.0.1:8081`).

## Settings

A line each of its env file, `/usr/local/etc/tracer.env` (the package puts `dist/freebsd/tracer.env.sample` there to
fill in; an empty value stops the service):

| Key | Default | What it is |
|---|---|---|
| `TRACER_TOKEN` | none: needed | the Discord bot's token |
| `TRACER_URL` | none: needed | the site's address, which the replies link: `https://`, or `http://localhost:8080` to try it on one machine |
| `TRACER_CHANNELS` | none: no report is taken | `id,id`: the channels it answers in, with their threads and a forum's posts; the start's log line says them |
| `TRACER_ADMINS` | none | `id,id`: people who delete any report beside the server's administrators |
| `TRACER_DB` | `/var/db/tracer/tracer.db` | the database (`tracer_db` in rc.conf) |
| `TRACER_SITE` | `/var/db/tracer-site` | the site's folder (`tracer_site` in rc.conf) |
| `TRACER_ASSETS` | `/usr/local/share/tracer` | where `style.css`, `site.js` and `tracer.svg` are |
| `TRACER_TITLES` | none: no list | the games' names by title ID, made by `scripts/make-titles.py --out FILE`; without it a game its logs don't name keeps its title ID |
| `TRACER_ISSUES_REPO` | none: no issue is filed | `OWNER/NAME`: the repository an approved report becomes an issue of; the start's log line says it, or that it is not set |
| `TRACER_ISSUES_TOKEN` | none: needed with the repository | a fine-grained token of that repository alone, with Issues read and write and nothing else; never in the log |
| `TRACER_ISSUES_EMOJI` | the check mark (U+2705) | the approval's emoji: its character, or a server emoji's name |
| `TRACER_ISSUES_LABELS` | none | `label,label`: the labels each issue gets |
| `TRACER_UPSTREAM_REPO` | `scarletnx/ScarletNX` | `OWNER/NAME`: the repository whose main a report's build is compared with, its owner's `nightly` the nightlies'; asked with `TRACER_ISSUES_TOKEN`, not at all without it |

A `TRACER_` key it does not know stops it, naming the key; so does an id that is not one, and an address that is not
`https://` or `http://`.

    make -C tracer test                                 # the tests: no Discord, no network, no Concord
    make -C tracer CONCORD=/usr/local/reiwimsy-concord  # the bot and tracer-mcp (CXXRT=-lc++ on FreeBSD)
    sh scripts/sync-logscan.sh ~/ScarletNX              # the log summary as a ScarletNX checkout has it, then the tests

It runs as the service `tracer` (`dist/freebsd/tracer.rc`): daemon(8) runs `/usr/local/libexec/tracer` as the user
`tracer` and starts it again when it stops, its output in syslog (tag `tracer`, `/var/log/messages`). Installing it:
[Install](./INSTALL.md#tracer). Its last line, when its gateway stops, says why as ReiWimsy's does
([ReiWimsy](./REIWIMSY.md)).
