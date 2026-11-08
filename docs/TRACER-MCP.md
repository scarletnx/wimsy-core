<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# tracer-mcp

`tracer/src/mcp_main.c` and `mcp.c`, in C99 with [libmicrohttpd](https://www.gnu.org/software/libmicrohttpd/) and
cJSON: [Tracer](./TRACER.md)'s reports given to agents as an [MCP](https://modelcontextprotocol.io) server at
`<site>/mcp`, over MCP's Streamable HTTP transport (one JSON-RPC message a POST, answered as JSON; no stream, no
session). It reads only the site's folder, `api/` and the logs, never the database, so it runs where the site is
served; nginx sends `/mcp` to it (`dist/freebsd/tracer-nginx.conf`). Read-only and public, as the site is: it names no
one either. It is built and packaged with Tracer ([Build](./BUILD.md#tracer)).

## Tools

| Tool | What it gives |
|---|---|
| `list_games` | the games, their console, report counts and latest outcome; by words of the title or title ID, by console, by outcome |
| `search_reports` | reports, the newest first; by words, game, console, outcome, signature, branch, GPU |
| `get_report` | a report's facts and each log's summary (how it ended, where, its frames, what the runtime missed), and for a log that failed what ScarletNX's `symbolize` asks for |
| `read_log` | a log's lines with their numbers, from a line on or those that hold some text, and where to go on |
| `get_screenshot` | one of a report's screenshots, as an image (8 MiB at most; a larger one is named by its address) |
| `signatures` | the failures grouped by where they happened, across games, the most reported first |
| `probe_matrix` | edenprobe's cases by machine (an emulator, a driver and its version, a system), each case's results on each as `[pass, fail, skip, crashed]`; by words, suite, family, driver, emulator, the failing cases alone (the default) or all |
| `probe_case` | one case of edenprobe, by its key or by words of its suite and name (words that find several list them): its results on each machine and its newest failures and skips, each with its report, machine and detail |
| `ban_candidates` | what the runs of edenprobe say together ([What the runs say together](./TRACER.md#what-the-runs-say-together)): ban candidates with their draft lines, ScarletNX's own failures and regressions; by driver and by kind |

An agent adds it by its address, Claude Code with:

    claude mcp add --transport http tracer https://<site>/mcp

A log's addresses match only the game version it names, and only a copy of that version names them: `get_report` gives
each log that failed a `symbolize` object (its title ID, game version and game addresses) for ScarletNX's own MCP
server (`scarletnx --mcp`) on a machine that has the game, its `find_game` then `symbolize` or `disassemble`. Tracer
keeps no game code, not even the bytes a crash report dumps: nothing of a game goes to a server that is not its owner's.

## Limits

A browser's request whose Origin is another site's gets 403; a request over 1 MiB, 413. A query of more than 16 words
is refused with their count, and each word and filter is searched for in time linear in what it is searched in. It
answers with 4 threads, 64 connections at most, each closed after 30 s idle. In front of it, nginx lets each client (by
the address Cloudflare names, as every request comes through the tunnel) 2 requests a second with a burst of 20, and 8
at once in all, answering 429 past them.

## Settings

In its environment, which the service sets from rc.conf:

| Key | Default | What it is |
|---|---|---|
| `TRACER_URL` | none: needed | the site's address, `https://` (or `http://`): the Origin it takes, and the links it gives |
| `TRACER_SITE` | `/var/db/tracer-site` | the site's folder; the service gives it `tracer_mcp_site`'s |
| `TRACER_MCP_LISTEN` | `127.0.0.1:8081` | the IPv4 address and port it listens on, where nginx sends `/mcp` |

A `TRACER_` key it does not know stops it, naming the key. It knows each of the bot's keys too and reads none of them,
so that both may be given one env file on one machine.

## The service

It runs as the service `tracer_mcp` (`dist/freebsd/tracer-mcp.rc`), in the jail that serves the site: daemon(8) runs
`/usr/local/libexec/tracer-mcp` as the user `tracer` and starts it again 30 s after it stops, its output in syslog (tag
`tracer-mcp`).

| Variable | Default | What it is |
|---|---|---|
| `tracer_mcp_enable` | `NO` | `YES` starts it with the jail |
| `tracer_mcp_url` | none: needed | the site's address, exported as `TRACER_URL`; the service does not start without it |
| `tracer_mcp_site` | `/usr/local/www/tracer` | the site's folder as this jail mounts it, exported as `TRACER_SITE` |
| `tracer_mcp_listen` | `127.0.0.1:8081` | exported as `TRACER_MCP_LISTEN` |

    sysrc -j tracerweb tracer_mcp_enable=YES tracer_mcp_url=https://<the site's name>
    service -j tracerweb tracer_mcp start|stop|restart|status

Its tests: [Testing](./TESTING.md).
