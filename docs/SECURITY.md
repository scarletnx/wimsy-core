<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# Security

What the services hold that must not leak, where each secret lives and how little each token may do, what the files
strangers send can reach, and what the bots keep of people. How the jails are made: [Install](./INSTALL.md) and
[Forge](./FORGE.md).

## The secrets, and where each lives

None is in this repository; each is made or put in place on the host. `.gitignore` keeps any `*.env` out.

| Secret | Where |
|---|---|
| ReiWimsy's Discord token, its GitHub token | `discordbots`: `/usr/local/etc/reiwimsy.env` (root's, 600) |
| Tracer's Discord token, its issues token | `tracer`: `/usr/local/etc/tracer.env` (root's, 600) |
| AccordWimsy's Forgejo token, its GitHub token | `gitsync`: `/usr/local/etc/accordwimsy/accordwimsy.env` (its user's, 600) |
| Forgejo's `SECRET_KEY`, `INTERNAL_TOKEN`, `JWT_SECRET` | `gitscarlet`: `app.ini`, made in place, never leaving the jail |
| Each tunnel's connector token | the file `--token-file` names, in `gitedge`, `tracerweb` and `status` |
| The Cloudflare API token | the file `CLOUDFLARE_TOKEN_FILE` names, on the host |
| Gatus's push token | the host: `/usr/local/etc/status-scarletnx/push-header` (root's, 600), made by `dist/status/install.sh`; the jail's `gatus.yaml` (root:www, 640); `@PUSH_TOKEN@` in `dist/status` |

ReiWimsy's and Tracer's env files are read by rc.subr as root and exported to the program, which runs as its own user;
AccordWimsy reads its own, as its user. No token goes on a command line: AccordWimsy gives git its tokens in git's
environment (`GIT_CONFIG_*`), and `push.sh` gives curl the push token from its header file.

## How little each token may do

| Token | Of | What it may do |
|---|---|---|
| ReiWimsy's `GITHUB_TOKEN` | GitHub, fine-grained | public repositories, read-only |
| Tracer's `TRACER_ISSUES_TOKEN` | GitHub, fine-grained | the issues' repository alone: Issues, read and write |
| AccordWimsy's `GITHUB_TOKEN` | GitHub, fine-grained, of the organization | read-only: Contents, Issues, Pull requests, Metadata |
| AccordWimsy's `ACCORDWIMSY_FORGEJO_TOKEN` | the forge, its own user | `write:repository`, `write:issue`, `write:organization`, `read:user` |
| ReiWimsy's Discord invite | the server | View Channels, Send Messages, Send Messages in Threads, Embed Links, Read Message History, Manage Messages (its first round deletes the webhooks' cards) |
| Tracer's Discord invite | the server | View Channels, Send Messages, Send Messages in Threads, Embed Links, Read Message History |

ReiWimsy's gateway asks for the Message Content intent (it reads a message's text for the numbers it names); Tracer's
asks for no privileged intent, and the reactions only when it files issues
([Tracer](./TRACER.md#what-it-keeps-of-people)). Tracer's application is no Public Bot.

## What each jail reaches

Each program runs in a jail of its own, as its own user ([Install](./INSTALL.md#the-jails)). The bots listen on no
port. Tracer's bot writes its site into a folder that the web jail, `tracerweb`, mounts read-only: the jail that
answers the internet holds no token and cannot change the site. The forge's three jails are isolated: the one that
holds the GitHub token (`gitsync`) answers no one, the one that answers the internet (`gitedge`) holds no token, and
Forgejo (`gitscarlet`) reaches nothing but DNS; a guard stops the three at the first process, socket or file it never
learned ([Forge](./FORGE.md#the-guard)).

## Tracer's workers

Tracer reads the files strangers send. Each message's files are read by a worker, the bot run again as `tracer
--report` ([Tracer](./TRACER.md#the-worker)):

- its environment without the bot's settings and tokens (every `TRACER_` key taken out);
- within 1 GiB of memory, 120 s of processor, 600 s in all and 128 MiB a file, with no core file (a crash's core
  would hold what a stranger sent), and every descriptor it inherited but the standard three closed (the bot's
  gateway, its database);
- the bot itself not dumpable on Linux (`PR_SET_DUMPABLE`), out of reach of ptrace and the debugging sysctls on
  FreeBSD (`PROC_TRACE_CTL`), so that a worker a file took over, though it runs as the same user, cannot read the bot's
  memory or environment; the bot does not start without it;
- shut in its own folder before it reads any file (Capsicum's capability mode on FreeBSD, Landlock on Linux), so that
  it can write neither the site nor the database, nor read any other file; one that cannot reads nothing, and the bot
  takes no answer that does not say it was shut in;
- its answer checked before any file moves from the work folder into the site.

GitHub is asked from the bot's own process; the worker never holds the issues token.

## The site and the MCP server

The site is static: everything a log or a message says is escaped in the pages, and nginx
(`dist/freebsd/tracer-nginx.conf`) serves it with `X-Content-Type-Options: nosniff`, `Referrer-Policy: no-referrer`
and a Content-Security-Policy that lets a page load only the site's own images, videos, style and `site.js`, with no
form, no base and no other page framing it; a page the bot is writing (`*.new`) is not served. [tracer-mcp](./TRACER-MCP.md#limits) is
read-only, reads the site's folder and never the database, refuses another site's Origin and a request over 1 MiB, and
searches in time linear in what it reads; nginx limits its requests by client and in all.

## What the bots keep of people

- ReiWimsy reads a message's text only for the numbers it names, sends GitHub only the numbers, and keeps no message,
  no text and no author: its state holds its replies' and cards' ids ([ReiWimsy](./REIWIMSY.md#what-it-keeps)).
- Tracer keeps the sender's user id and the ids of the messages that lead to a report, for the deletion, the updates
  and the hourly limit; the site and its JSON name no one. A log's home folders become `~`, its addresses `[ip]` and
  `[email]`, and the crash report's dumps are left out; a picture loses its metadata and a video its place, time and
  camera ([Tracer](./TRACER.md#what-a-report-keeps)).
- AccordWimsy copies what GitHub shows publicly, each issue and comment opening with its GitHub author, day and link.
- The status page keeps nothing of anyone.
