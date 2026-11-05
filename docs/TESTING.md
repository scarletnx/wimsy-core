<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# Testing

## Index

* [ReiWimsy](#reiwimsy)
* [AccordWimsy](#accordwimsy)
* [Tracer](#tracer)
* [The docs](#the-docs)
* [The bots against a fake Discord](#the-bots-against-a-fake-discord)
* [The themes](#the-themes)
* [The status page](#the-status-page)
* [Before a package](#before-a-package)

What CI runs on each pull request and each push to main (`.github/workflows/test.yml`), with no network and no
Discord:

    make -C rei test && make -C accordwimsy test && make -C tracer test &&
        python3 scripts/test-check-docs.py && python3 scripts/check-docs.py && sh scripts/test-check-theme.sh &&
        sh scripts/check-theme.sh

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
  a `.zip`; the screenshots' metadata taken out (PNG, JPEG, WebP, GIF) and broken ones refused; an MP4's place and
  camera blanked; text made valid UTF-8;
- the game a log names (and the older builds' titles, a garbled one, a name in another script); the crash report's
  dumps left out of a log as the log summary leaves them out; the games' names from the list (an update's and an
  add-on's ID, a report named at once, one named at the next start, a log's name first, a GBA game by its code); each
  console's report; what the runtime missed counted beside the outcome;
- whole reports: kept with their home folders as `~`, drawn with everything escaped, the same message again, refusals,
  updates (by a reply or `add N`, refused to others, within the limits, taken back with their message, a later log
  naming the game), deletion by the sender and the guild's administrators and by no one else; a reply alone that makes
  a report of the message it replies to (its files, its author, its words, the hourly limit of the one who asked) and
  its refusals (another channel or server, a bot's message, one gone or kept from the bot, one without files as the
  Message Content intent shows it, one that is a report already);
- a database of the first schema upgraded, a deleted message, a report's number never reused, the hourly limit, the
  JSON with no Discord id, the channels it answers in;
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
  tool's filters, limits and errors, and a query's 16 words at most.

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
  keeps with its home folder as `~`), the second is told that message is report #1 already.
- `reiwimsy --fix-cards` on a nightly channel holding two cards that say "Building": its dry run changes nothing,
  `--apply` deletes the one of a commit inside a push and redraws the push's head with its failed build, and a second
  `--apply` finds nothing to do.

Linux only, with no network: the bot runs in a mount and network namespace of its own, and a user namespace when the
test is not run by root (as root where the system keeps user namespaces from users, as CI does), whose `/etc/hosts`
sends Discord, its CDN and GitHub to the fake and whose CA bundle, libcurl's (`curl-config --ca`), holds only the CA
the test makes. It needs util-linux, iproute2, python3, openssl and libcurl's headers. One by hand:

    sh rei/tests/test_gateway.sh reiwimsy|tracer|fix-cards BINARY

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
