<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# Testing

    make -C rei test && make -C accordwimsy test && make -C tracer test && sh scripts/test-check-docs.sh &&
        sh scripts/check-docs.sh

What CI runs on each pull request and each push to main. No network and no Discord:

- ReiWimsy (`rei/tests/test_rei.c`, no Concord): the parsing, the gateway's events (read from their JSON: Concord
  decodes none of them, as its decoder crashes on messages of 2 to 3 KB) and the messages waiting for an answer, the
  replies and their edits, GitHub's ETags and pages, Discord's waits and answers, the cards (drawn, posted, edited,
  moved to the end at their activity, deleted a day after they close, posted again when deleted by hand), the state
  file.
- AccordWimsy (`accordwimsy/tests/test_aw.c`), against a fake GitHub and a fake Forgejo and git on bare repositories:
  the env file and its refusals, the first pass (repositories, branches and tags, labels, every number, a missing one,
  a pull request), a pass with nothing new (GitHub asked from where the last stopped, the Forgejo written to only for
  its Releases tab), edits and new ones (an issue, a comment, a branch deleted), a Forgejo whose numbers differ, and
  the program's lock and usage.
- Tracer (`tracer/tests/test_tracer.c`, no Concord, no libcurl), through a fake download into a database and a site in a
  temporary folder: the events and what a message asks, which files are logs and screenshots, the logs unpacked from a
  `.gz` and a `.zip`, the screenshots' metadata taken out (PNG, JPEG, WebP) and broken ones refused, text made valid
  UTF-8, the game a log names (and the older builds' titles, a garbled one, a name in another script), the crash
  report's dumps left out of a log as the log summary leaves them out, and whole reports: kept with their home folders
  as `~`, drawn with everything escaped, the same message again, refusals, updates (by a reply or `add N`, refused to
  others, within the limits, taken back with their message, a later log naming the game), deletion by the sender and the
  guild's administrators and by no one else, a database of the first schema upgraded, a deleted message, a report's
  number never reused, the hourly limit, the JSON with no Discord id, the games' names from the list (an update's and an
  add-on's ID, a report named at once, one named at the next start, a log's name first, a GBA game by its code), each
  console's report, what the runtime missed counted beside the outcome; the channels it answers in; the issues an
  approval files, through a fake GitHub (an administrator's reaction files one, a second files none, anyone else's
  reaction or another emoji nothing, a refusal said and tried again, the settings and their refusals); how old a
  report's build is, through a fake GitHub (behind main, the plurals, a commit asked once an hour, -dirty and a commit
  the repository lacks, no line at main or the newest nightly, GitHub silent or refusing, no token, no git); the
  worker (the test program run as one: a report through it, its environment without the token, one that crashes, one
  that never answers, one gone before it reads, one that names a host file); and the MCP server over that site: its
  handshake and protocol versions, notifications, broken and unknown requests, and each tool's filters, limits and
  errors.
- The docs (`scripts/check-docs.sh`): no date in `README.md` and `docs/*.md` outside code, as a doc says what is and
  the commit message says when; `scripts/test-check-docs.sh` holds the check to dates in text, in code and none.

And in a job of its own, on each pull request and each push to main, the bots built with Concord as
`dist/freebsd/build.sh` pins it, against a fake Discord (`rei/tests/test_gateway.sh` and `rei/tests/fake_discord.py`):

    make -C rei test-gateway CONCORD=/opt/concord

ReiWimsy comes up, its rounds wait inside a GitHub request held open, then the gateway closes and Discord's
`GET /gateway/bot` gets no answer: Concord's `discord_run` returns, and the bot has to end with status 1 (daemon(8)
starts it again), not on a signal, saying the gateway stopped. Linux only, with no network: the bot runs in a user,
mount and network namespace of its own (`unshare -rnm`, as root where the system keeps user namespaces from users),
whose `/etc/hosts` sends Discord and GitHub to the fake and whose CA bundle, libcurl's (`curl-config --ca`), holds only
the CA the test makes. It needs util-linux, iproute2, python3, openssl and libcurl's headers.

Beside them:

- `dist/freebsd/build.sh` runs the programs' tests, and Concord's `unit-gateway-lifecycle`, before it packages.
- `dist/status/check.sh <ScarletNX's dist/wimsy.svg>` checks the status page without Gatus and ends with `ok`: Wimsy's
  colours, `gatus.yaml` and `push.sh` rendered from their templates, the YAML Gatus reads, and each endpoint's answer
  now (this one goes to the network).
