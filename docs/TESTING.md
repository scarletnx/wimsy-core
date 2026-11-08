<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# Testing

## Index

* [ReiWimsy](#reiwimsy)
* [AccordWimsy](#accordwimsy)
* [Tracer](#tracer)
* [The docs](#the-docs)
* [The install script](#the-install-script)
* [The bots against a fake Discord](#the-bots-against-a-fake-discord)
* [The themes](#the-themes)
* [The status page](#the-status-page)
* [Before a package](#before-a-package)

What CI runs on each pull request and each push to main (`.github/workflows/test.yml`), with no network and no
Discord:

    make -C rei test && make -C accordwimsy test && make -C tracer test && python3 scripts/test-rule-mutants.py &&
        sh scripts/test-install.sh && python3 scripts/test-check-docs.py && python3 scripts/check-docs.py &&
        sh scripts/test-check-theme.sh && sh scripts/check-theme.sh

On FreeBSD, `make -C tracer test CXXRT=-lc++` ([Build](./BUILD.md#tracer)). The libraries each needs:
[Build](./BUILD.md).

## ReiWimsy

`rei/tests/test_rei.c`, no Concord: the parsing, the gateway's events (read from their JSON: Concord decodes none of
them, as its decoder crashes on messages of 2 to 3 KB) and the messages waiting for an answer, the replies and their
edits, GitHub's ETags and pages, Discord's waits and answers, and the state file. The cards: drawn, posted, edited,
moved to the end at their activity, deleted a day after they close, posted again when deleted by hand, a nightly's for
each push, the newest kept; and the fix of the cards left saying "Building" (a dry run, then each kind of card, then a
run with nothing to do).

## AccordWimsy

`accordwimsy/tests/test_aw.c`, against a fake GitHub and a fake Forgejo and git on bare repositories: the env file and
its refusals; the first pass (repositories, branches and tags, labels, every number, a missing one, a pull request); a
pass with nothing new (GitHub asked from where the last stopped, the Forgejo written to only for its Releases tab);
edits and new ones (an issue, a comment, a branch deleted); a Forgejo whose numbers differ; and the program's lock and
usage.

## Tracer

`tracer/tests/test_tracer.c`, no Concord and no libcurl, through a fake download into a database and a site in a
temporary folder:

- the events and what a message asks; which files are logs, screenshots and videos; the logs unpacked from a `.gz` and
  a `.zip`, and the name each downloads as from the site (a log's, never the archive's); the screenshots' metadata
  taken out (PNG, JPEG, WebP, GIF) and broken ones refused; an MP4's place and camera blanked; text made valid UTF-8;
- the game a log names (and the older builds' titles, a garbled one, a name in another script); the crash report's
  dumps left out of a log as the log summary leaves them out; the games' names from the list (an update's and an
  add-on's ID, a report named at once, one named at the next start, a log's name first, a GBA game by its code, a
  name with é or in Japanese kept to the reply, the pages and the JSON, one the launcher wrote as `?` taken from the
  list and one kept so named again); each
  console's report; what the runtime missed counted beside the outcome;
- whole reports: kept with their home folders as `~`, drawn with everything escaped, the same message again, refusals,
  updates (by a reply or `add N`, refused to others, within the limits, taken back with their message, a later log
  naming the game), deletion by the sender and the guild's administrators and by no one else; a reply alone that makes
  a report of the message it replies to (its files, its author, its words, the hourly limit of the one who asked) and
  its refusals (another channel or server, a bot's message, one gone or kept from the bot, one without files as the
  Message Content intent shows it, one that is a report already);
- a database of the first schema upgraded, a deleted message, a report's number never reused, the hourly limit, the
  JSON with no Discord id, the channels it answers in (a list, `all`, none, and other words refused);
- the issues an approval files, through a fake GitHub: an administrator's reaction files one, a second files none,
  anyone else's reaction or another emoji nothing, a refusal said and tried again, the settings and their refusals;
- how old a report's build is, through a fake GitHub: behind main, the plurals, a commit asked once an hour, `-dirty`
  and a commit the repository lacks, no line at main or the newest nightly, GitHub silent or refusing, no token, no git;
- the worker, the test program run as one: a report through it, its environment without the token, one that crashes,
  one that never answers, one gone before it reads, one that names a host file, one shut in its folder that tries the
  site, the database and /etc/passwd, one that answers without having shut itself in, one that reads the bot's
  environment before the bot keeps it from its user's debuggers and not after, the bot's descriptors closed in it, no
  worker for a message without files;
- the MCP server over that site: its handshake and protocol versions, notifications, broken and unknown requests, each
  tool's filters, limits and errors, and a query's 16 words at most;
- runs of edenprobe: edenprobe's families and which lose their detail, the drivers by the ban table's names and by
  their own, each driver's version numbering both ways, the system's family; the cut of a log (the service, net, pad
  and unknown suites' details and the net notes, every other byte as it was); ScarletNX's log, Eden's, rEden's and a
  report.jsonl read, two copies of a run, a run cut short, a launch that went on after a crash, lines edenprobe does
  not write; a run through JSON and the answers refused (another emulator, a detail the cut takes out, a case that is
  none); the run in a Share Log zip, a run begun, none; whole runs as reports (the reply's counts and what was left
  out, the database and the kept log without the player, no game's list, the same run again from one person and from
  another, Eden's and the console's, deleted with their message, a resumed launch kept as a log, one through the
  worker), and a report kept before made the run's at the next start;
- the rules over the runs ([What the runs say together](./TRACER.md#what-the-runs-say-together)), on fixtures that sit
  on each threshold: a candidate and its draft line, and none with one person, two failing reports, one other driver,
  two passing reports, a passing build eight days before or after (seven count), a failure Eden's or of another mode;
  a mixed end, no end, a range a passing version splits, another system's runs apart, the extensions every failing
  run has; ScarletNX's own failure on three drivers of five (no candidate then) and on both of two, and none on one or
  with a driver that passes; a regression, and none when the newest build passes once, the build before fails once,
  or a published build after passes, a `-dirty` one left out;
- the Probe pages over those runs: the matrix by machine, a case's page with its failures, a driver's, a run's report
  page and JSON without the player; a ban candidate made of seven whole reports on three drivers, drawn with its draft
  line on the candidates page and the driver's, given by the MCP server's `ban_candidates`, `probe_matrix` (by driver,
  words, suite, failing or not) and `probe_case` (by key, by words, several, none, a key that is none), and gone with
  its reports; a case no run names any more losing its page.

`python3 scripts/test-rule-mutants.py` then changes `tracer/src/rules.c` one way at a time in a copy of the tree (a
threshold one off, a comparison that lets one more in, a condition left out: 19 mutants) and expects `test_tracer
--rules` to fail on each; a mutant it passes, or one whose text `rules.c` no longer has, fails it. About 10 s.

## The docs

`scripts/check-docs.py` holds `README.md` and `docs/` to the checkout, each failure a line naming its file and line:

- no date in the text (a doc says what is; when it came to be goes in the commit message);
- every relative link names a file that is there, and its anchor a heading of it, as GitHub makes one;
- a document over 100 lines with 3 or more sections has an `## Index` of them, and every index lists its sections as
  they are (`python3 scripts/check-docs.py --index` writes them);
- `docs/README.md` links every document of `docs/`;
- each key a program reads has a row in its document's `| Key | Default |` table, with the default the code gives it,
  and each variable of its rc script a row in a `| Variable | Default |` table, with the script's default; no row names
  a key or a variable that is not there, and an env file's sample names only keys its program reads;
- each script of `scripts/` and `dist/` is named by its path in a document.

`scripts/test-check-docs.py` runs it on documents and sources made in a temporary folder, each rule failing where it
should and passing where it should.

## The install script

`sh scripts/test-install.sh` runs `scripts/install.sh` against fake jails (`jls`, `jexec` and `pkg` on `PATH`, a
package a text file of its name, dependencies and files): the services of a package restarted where they were running
and nowhere else (`tracer_mcp` in the site's jail, where `tracer` does not run; both where both run), a package with
none, and a jail that is not running. No FreeBSD needed.

## The bots against a fake Discord

In a job of its own, on each pull request and each push to main, the bots built with Concord as
`dist/freebsd/build.sh` pins it, against a fake Discord (`rei/tests/test_gateway.sh` and `rei/tests/fake_discord.py`):

    make -C rei test-gateway CONCORD="$HOME/concord"
    make -C tracer test-gateway CONCORD="$HOME/concord"

- ReiWimsy comes up, its rounds wait inside a GitHub request held open, then the gateway closes and Discord's
  `GET /gateway/bot` gets no answer: Concord's `discord_run` returns, and the bot has to end with status 1 (daemon(8)
  starts it again), not on a signal, saying that Concord's `GET gateway/bot` failed and what Discord answers it now.
- Tracer the same, its gateway closed once it has answered two replies that mention it and bring no file: the first
  makes report #1 of the message they reply to (Discord's REST API gives it, the fake CDN its log, which the site
  keeps with its home folder as `~`), the second is told that message is report #1 already. A mention asking for help
  in channel 7 goes unanswered with `TRACER_CHANNELS=6`, and is answered with `TRACER_CHANNELS=all` (`tracer-all`).
- `zombie` (ReiWimsy) and `tracer-zombie`: a heartbeat a second; the first connection sends READY and the first
  kilobyte of a 60000-byte frame, then nothing, its heartbeats unanswered. The bot has to leave it as a zombie, identify
  on the next connection (a Concord that reads that one's HELLO as the dead frame's tail waits forever) and keep it
  until the gateway closes, though its ACKs come 0.3 s after each heartbeat (a Concord whose heartbeat timer still
  fires every millisecond from the zombie leaves it a millisecond after READY).
- `unready` (ReiWimsy) and `tracer-unready`: a heartbeat a second, the first connection's unanswered, and each later
  one closed with no CLOSE frame once the bot sends IDENTIFY, its session never ready again, which Concord waits out
  forever: the bot has to end with status 1 on its own, saying its session was last seen ready longer ago than three
  heartbeat intervals. `stalled` (ReiWimsy) the same, once the first connection is left each `GET /gateway/bot` held
  with no answer, which Concord also waits on forever.
- `reiwimsy --fix-cards` on a nightly channel holding two cards that say "Building": its dry run changes nothing,
  `--apply` deletes the one of a commit inside a push and redraws the push's head with its failed build, and a second
  `--apply` finds nothing to do.

Linux only, with no network: the bot runs in a mount and network namespace of its own, and a user namespace when the
test is not run by root (as root where the system keeps user namespaces from users, as CI does), whose `/etc/hosts`
sends Discord, its CDN and GitHub to the fake and whose CA bundle, libcurl's (`curl-config --ca`), holds only the CA
the test makes. It needs util-linux, iproute2, python3, openssl and libcurl's headers. One by hand:

    sh rei/tests/test_gateway.sh PROGRAM BINARY    # one of reiwimsy, tracer, tracer-all, zombie, tracer-zombie,
                                                   # unready, tracer-unready, stalled, fix-cards

## The themes

`scripts/check-theme.sh` checks the theme files wimsy-dist's `themes/theme.py` makes (Tracer's stylesheet and its
blocks, Forgejo's themes, Gatus's colour lines and custom.css in each theme), that the rules around them take every
colour from a theme's role, and that every picker and nginx map names the same themes ([Themes](./THEMES.md));
`scripts/test-check-theme.sh` breaks a copy of each and expects the check to fail.

## The status page

    sh dist/status/check.sh

checks the status page without Gatus and ends with `ok`: its theme's colour lines, `gatus.yaml`, `push.sh`, `nginx.conf`
and each theme's custom.css rendered from their templates, the YAML Gatus reads, a bot with an icon for each pushed
endpoint and the reverse, and each endpoint's answer now (this one goes to the network). It needs python3 with PyYAML,
and runs shellcheck on `push.sh` where it is installed. CI does not run it.

## Before a package

`dist/freebsd/build.sh` runs each program's tests, and Concord's `unit-gateway-lifecycle`, before it packages them
([Build](./BUILD.md#the-freebsd-packages)).
