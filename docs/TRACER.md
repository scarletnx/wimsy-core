<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# Tracer

`tracer/`, in C99 with [Concord](https://github.com/Cogmasters/concord) 3.0.1, libcurl, SQLite, libarchive, cJSON
(ReiWimsy's copy) and ScarletNX's own log summary (`tracer/third_party/logscan`, the runtime's `--scan-log`). It keeps
the game reports that people send it on Discord and draws them as a static site, grouped by game.

A message that mentions it with a ScarletNX log attached becomes a report:

- Its logs (`.log`, `.txt`, or one packed in a `.gz` or a `.zip`; 3 at most, 64 MiB each) are downloaded from
  Discord, each home folder in them written as `~` (`/home/<user>`, `C:\Users\<user>`, Android's app folders), and
  summarized: the game, its title ID and version, the build, the system and GPU, how the run ended and where, the
  service commands and what else the runtime missed, the last lines. A file that is no ScarletNX log is named in the
  reply and not kept.
- Its screenshots (PNG, JPEG, WebP or GIF, read from their bytes; 8 at most, 25 MiB each) lose their metadata: EXIF
  and its place and camera, XMP, PNG's text and time, JPEG's comments.
- The game is the first log's: its title ID, else its name. A log that has the title ID alone takes the name other
  reports gave the game; one that has the name alone goes with the reports that have its title ID. The message's text
  names the game only when the log says neither, and is kept as the report's note.
- The reply links the report's page, with how the run ended, its signature and build, and what was not kept.

A report grows: a reply to any of its messages (the one that sent it, an update, Tracer's answer, also one it posted
before it kept them, whose link names the report) that mentions Tracer
adds its logs, screenshots and words to it, as `@Tracer add 12` does, within the same limits (3 logs and 8
screenshots a report). Only the person who sent the report and the server's administrators add to it; anyone else is
asked for a report of their own, which goes with the same game. A report its logs did not name takes the name a later
log of the same title ID gives. Words added later are shown under the report's note, each with its date.

`@Tracer help` says how; `@Tracer delete 12`, or a reply with `@Tracer delete`, deletes a report, for the person who
sent it, the server's owner and administrators (the Administrator permission), and `TRACER_ADMINS`. A message deleted
on Discord takes back what it sent: the whole report, or what it added to one. Ten reports an hour from one person at
most. A report's number is never another's after it is deleted, as replies link to it.

The site does not say who sent a report. The database keeps the sender's user id (for the deletion, the updates and
the hourly limit) and the ids of the messages that lead to a report, nothing else of them; it keeps nothing of
messages that do not mention it. It asks for no privileged intent: Discord sends the text and the attachments of a
message that mentions the bot, and of no other.

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
| `list_games` | the games, their report counts and latest outcome; by words of the title or title ID, by outcome |
| `search_reports` | reports, the newest first; by words, game, outcome, signature, branch, GPU |
| `get_report` | a report's facts and each log's summary (how it ended, where, its frames, what the runtime missed), and for a log that failed what ScarletNX's `symbolize` asks for |
| `read_log` | a log's lines with their numbers, from a line on or those that hold some text, and where to go on |
| `get_screenshot` | one of a report's screenshots, as an image (8 MiB at most; a larger one is named by its address) |
| `signatures` | the failures grouped by where they happened, across games, the most reported first |

An agent adds it by its address, Claude Code with `claude mcp add --transport http tracer https://<site>/mcp`. A log's
addresses match only the game version it names, and only a copy of that version names them: `get_report` gives each log
that failed a `symbolize` object (its title ID, game version and game addresses) for ScarletNX's own MCP server
(`scarletnx --mcp`) on a machine that has the game, its `find_game` then `symbolize` or `disassemble`. Tracer keeps no
game code: nothing of a game goes to a server that is not its owner's. A browser's request whose Origin is another
site's gets 403; a request over 1 MiB, 413.

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
| `TRACER_CHANNELS` | every channel it sees | `id,id`: the channels it answers in, with their threads and a forum's posts |
| `TRACER_ADMINS` | none | `id,id`: people who delete any report beside the server's administrators |
| `TRACER_DB` | `/var/db/tracer/tracer.db` | the database (`tracer_db` in rc.conf) |
| `TRACER_SITE` | `/var/db/tracer-site` | the site's folder (`tracer_site` in rc.conf) |
| `TRACER_ASSETS` | `/usr/local/share/tracer` | where `style.css`, `site.js` and `tracer.svg` are |

A `TRACER_` key it does not know stops it, naming the key; so does an id that is not one, and an address that is not
`https://` or `http://`.

    make -C tracer test                                 # the tests: no Discord, no network, no Concord
    make -C tracer CONCORD=/usr/local/reiwimsy-concord  # the bot and tracer-mcp (CXXRT=-lc++ on FreeBSD)
    sh scripts/sync-logscan.sh ~/ScarletNX              # the log summary as a ScarletNX checkout has it, then the tests

It runs as the service `tracer` (`dist/freebsd/tracer.rc`): daemon(8) runs `/usr/local/libexec/tracer` as the user
`tracer` and starts it again when it stops, its output in syslog (tag `tracer`, `/var/log/messages`). Installing it:
[Install](./INSTALL.md#tracer).
