<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# ReiWimsy

## Index

* [Replies](#replies)
* [Cards](#cards)
  * [Cards left saying "Building"](#cards-left-saying-building)
* [Tracer beside it](#tracer-beside-it)
* [Settings](#settings)
* [What it keeps](#what-it-keeps)
* [The service](#the-service)
* [When it stops](#when-it-stops)

`rei/`, in C99 with [Concord](https://github.com/Cogmasters/concord) 3.0.1, libcurl and
[cJSON](https://github.com/DaveGamble/cJSON) 1.7.19 (`rei/third_party/cjson`), and Tracer's code beside it:
ScarletNX's Discord bot. It answers a
message that names an issue or a pull request with a link to each, and keeps a card on Discord for each open pull
request, each open issue and each nightly published with its packages, edited as GitHub changes. Installing it:
[Install](./INSTALL.md#reiwimsy).

## Replies

A message that names `#160` (`issue #160`, `PR #160` too; a bare number is talk) gets one reply: each linked, with its
title, issue or PR, its state and labels, in GitHub's colours; 5 numbers a message at most. A number after `report`,
`reports` or `Tracer` is a Tracer report's (`Tracer report #74`, `Tracer #12`), not an issue, and gets none. `!nightly`
links the newest nightly and its downloads, and above them the nightly being built while the newest scheduled Build of
main has not ended (its run and when it began; when GitHub does not list them, it says so), `!release` the newest
version (the nightly while there is none), `!release #355` a pull request's builds. With a GitHub token it edits a
reply of the last week when what it links changes, looking every 10 minutes.

It reads the text of messages only to find those numbers, and sends GitHub's API only the numbers. It keeps no message
and logs nothing said, and pings no one.

## Cards

A message for each open pull request, each open issue and each nightly published with its packages, in channels of
their own, drawn from what GitHub says now and edited as it changes: its state, its commits or changes, the latest
comment a person wrote, each package of its build (a pull request's leaves out those only a nightly builds, unless its
run made them). A push to main, its build, and a nightly with no package yet have none. Its
activity (a push, a person's comment, a change of state, a pull request's builds published or failed) moves it to the
end of its channel, posted again before the old one is deleted; a pull request or an issue closed keeps its card a
day, as it closed, then the card is deleted.

The bot asks GitHub's API every 30 s, with ETags (an unchanged answer costs nothing of GitHub's 5000 an hour): the
issues and pull requests updated, the builds that run and their jobs, and the nightly releases: a nightly's card
comes when its release has its first package. A card that could not be drawn whole waits for the next round, as it
was. It follows the 30 newest nightlies' cards; an older one stays as it was last drawn. A nightly's card whose
nightly the releases lack (one of a push, from when each push had one) is deleted. The first round on the pull requests' and the issues' channels deletes the cards a webhook
posted there before, and messages of its own no card keeps; people's messages stay.

### Cards left saying "Building"

A card that says "Building main at" a commit is one of a push, from when each push had one, that no round follows
any more. `fixcards` finds them:

    service -j discordbots reiwimsy fixcards          # says what it would do of each, and the counts; changes nothing
    service -j discordbots reiwimsy fixcards apply    # does it

It runs `reiwimsy --fix-cards [--apply]` as the bot's user with its env file, beside the running bot (it keeps no
state): among the nightly channel's 1000 newest messages, the bot's own cards that say "Building main at" a commit.
One of a commit a nightly was published from is drawn again and edited; the others are deleted, each said with why,
and the last line counts each kind. A second run finds nothing to do.

## Tracer beside it

With `TRACER_TOKEN` in its env file, `reiwimsy` runs [Tracer](./TRACER.md) in its own process: two bots, each with its
own Discord application, token, gateway and name, so each answers as itself, and one service, one env file and one
jail. Each bot's gateway is a Concord client of its own, Tracer's on a thread of its own; the first gateway that stops
ends the program with status 1, its line saying which bot's stopped, and daemon(8) starts both again. A message that
mentions Tracer is Tracer's alone: ReiWimsy gives it no reply, as Tracer's commands may hold a `#12` that is a report's
number (`@Tracer delete #12`), and it reads no number after `report`, `reports` or `Tracer` either. Tracer's keys,
`TRACER_*`, are [Tracer's](./TRACER.md#settings), in the same file; one without `TRACER_TOKEN` stops the program,
naming it, as a setting quietly not taken. Tracer's workers run `reiwimsy --report`, and their environment holds
neither bot's settings nor tokens ([Security](./SECURITY.md#tracers-workers)). The start's lines say whether Tracer
runs (`Tracer: beside ReiWimsy, with its own token; the messages that mention it (user N) are its own`, or `Tracer:
none, as TRACER_TOKEN is not set`); each bot's lines open with its name, `reiwimsy:` or `tracer:`.

## Settings

A line each of its env file, `/usr/local/etc/reiwimsy.env` (the package puts `dist/freebsd/reiwimsy.env.sample` there
to fill in; an empty value stops the service):

| Key | Default | What it is |
|---|---|---|
| `REIWIMSY_TOKEN` | none: needed | the Discord bot's token |
| `REIWIMSY_REPO` | none: needed | `OWNER/NAME`, the repository whose issues and pull requests it links |
| `REIWIMSY_NIGHTLY` | `OWNER/nightly` | where the nightlies and the pull requests' builds are released |
| `REIWIMSY_CHANNELS` | every channel it sees | `id,id`: the channels its replies answer in, 64 at most |
| `GITHUB_TOKEN` | none: 60 questions an hour | a fine-grained token with read-only access to public repositories: 5000 an hour; the cards need it, and so does the editing of the replies |
| `REIWIMSY_CARDS_PRS` | none: no pull request cards | the channel id of the pull requests' cards |
| `REIWIMSY_CARDS_ISSUES` | none: no issue cards | the channel id of the issues' cards |
| `REIWIMSY_CARDS_NIGHTLY` | none: no nightly cards | the channel id of the nightlies' cards |
| `TRACER_TOKEN` | none: no Tracer | Tracer's Discord bot's token, another application's: with it the program runs Tracer beside ReiWimsy, and reads Tracer's other keys ([Tracer beside it](#tracer-beside-it)) |
| `REIWIMSY_STATE` | none: nothing kept between runs | the file its replies' and cards' ids are kept in; the service sets it from `reiwimsy_state` |

A `REIWIMSY_` key it does not know stops it, naming the key; so does a channel id that is not one, and a card channel
without `GITHUB_TOKEN`. A channel's id: Developer Mode in Discord, right click, Copy Channel ID.

## What it keeps

`REIWIMSY_STATE`, `/var/db/reiwimsy/state.json` under the service: GitHub's answers it asks again with their ETags, and
its replies' and its cards' ids, with no text and no author. Written after each round and each reply, and read at the
start; a file it cannot read is said in the log, and the bot starts with no cards and no replies kept: its first round
then deletes its own messages no card keeps on the pull requests' and the issues' channels and posts their cards again,
while a nightly's card posted before stays as it was.

## The service

It runs as the service `reiwimsy` (`dist/freebsd/reiwimsy.rc`): daemon(8) runs `/usr/local/libexec/reiwimsy` as the
user `reiwimsy` and starts it again 30 s after it stops, its output in syslog (tag `reiwimsy`, the jail's
`/var/log/messages`), its env file exported to it by rc.subr. Before it starts, the service makes the state's folder
(the user's, 700).

| Variable | Default | What it is |
|---|---|---|
| `reiwimsy_enable` | `NO` | `YES` starts it with the jail |
| `reiwimsy_env_file` | `/usr/local/etc/reiwimsy.env` | its settings; root's, 600 |
| `reiwimsy_state` | `/var/db/reiwimsy/state.json` | what it keeps, exported as `REIWIMSY_STATE` |
| `reiwimsy_tracer_db` | `/var/db/tracer/tracer.db` | Tracer's database, exported as `TRACER_DB` when the env file has `TRACER_TOKEN`; its folder (the user's, 700) holds the workers' `work/` |
| `reiwimsy_tracer_site` | `/var/db/tracer-site` | Tracer's site (the user's, 755: the web server's jail mounts it read-only), exported as `TRACER_SITE` |
| `reiwimsy_tracer_assets` | `/usr/local/share/reiwimsy/tracer` | the site's files the package holds, exported as `TRACER_ASSETS` |

    service -j discordbots reiwimsy start|stop|restart|status
    service -j discordbots reiwimsy fixcards [apply]

## When it stops

It stops when its gateway does, with status 1, which daemon(8) starts it again from, and its last line says why.
Concord calls any failure of the `GET gateway/bot` that starts a session "Bad authentication token", a timeout too, so
the bot asks Discord again and ends that line with the answer: `asked again, Discord answers 200` (the network back, the
token good), `401: ...` (the token refused), or `0: ` and why no answer came (Discord still out of reach).

It also stops when its gateway's session has not been ready for three heartbeat intervals (Discord's are 41.25 s), a
wait Concord would never end on its own (a connection that closes with no CLOSE frame before READY, a `GET
gateway/bot` that never gets an answer). That line says when the session was last seen ready and what it waited for:
`the gateway stopped: its session was last seen ready 124.0 s ago, longer than the 123.8 s (3 heartbeat intervals of
41.25 s) a connection may take to bring READY or RESUMED`.

Building it: [Build](./BUILD.md#reiwimsy); its tests: [Testing](./TESTING.md).
