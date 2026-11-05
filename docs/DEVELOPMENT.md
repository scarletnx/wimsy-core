<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# Development

Where the code is, the rules it is written to, the pieces that come from elsewhere and how each is updated, and what
CI runs. Building: [Build](./BUILD.md); the tests: [Testing](./TESTING.md).

## Layout

| Folder | What it holds |
|---|---|
| `rei/src/` | ReiWimsy: `main.c` (its settings, the gateway and its threads), `event.c` (the gateway's events, from their JSON), `watch.c` (the gateway's watch), `refs.c` (what a message asks for), `link.c` (the replies), `cards.c` (the cards), `look.c` (how an issue or a pull request reads), `github.c` (GitHub's REST and GraphQL APIs), `rest.c` (Discord's REST API), `http.c` (libcurl), `state.c` (the state file), `util.c` (strings, cJSON's lookups, the log, the clock) |
| `rei/tests/` | `test_rei.c`; `test_gateway.sh` and `fake_discord.py`, the bots against a fake Discord |
| `rei/third_party/cjson/` | cJSON 1.7.19, as its release has it, and the files' `SHA256SUMS` |
| `tracer/src/` | Tracer: `main.c` (its settings, the gateway), `message.c` (what a message asks), `report.c` (what it does with one), `files.c` (logs, pictures and videos), `worker.c` (the worker that reads them), `fetch.c` (the downloads), `store.c` (the SQLite database), `site.c` (the pages), `issues.c`, `age.c`, `titles.c`, `channels.c`; `mcp.c` and `mcp_main.c`, tracer-mcp |
| `tracer/site/` | the site's `style.css`, `site.js` and `tracer.svg` |
| `tracer/third_party/logscan/` | ScarletNX's log summary, its commit in `COMMIT` |
| `tracer/tests/` | `test_tracer.c` |
| `accordwimsy/src/` | AccordWimsy: `main.c`, `config.c` (the env file), `sync.c` (a pass), `git.c` (the bare mirrors), `http.c`, `util.c` |
| `accordwimsy/tests/` | `test_aw.c` |
| `dist/freebsd/` | the packages: `build.sh`, each one's rc script, env sample, plist, description and post-install script, AccordWimsy's cron line and log rotation, Tracer's nginx config, Concord's fixes |
| `dist/status/`, `dist/forge/` | the [status page](./STATUS.md) and the [forge](./FORGE.md)'s start page |
| `scripts/` | `install.sh`, `make-titles.py`, `sync-logscan.sh`, `check-docs.py`, and the tests of the first and the last |

Tracer builds ReiWimsy's `util.c`, `http.c`, `rest.c` and `watch.c` again as its own, and AccordWimsy builds
ReiWimsy's cJSON: a change there is a change to all of them, and every program's tests run it.

## Rules

- C99 (`-std=c99 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Werror`, `_POSIX_C_SOURCE=200809L`), built by
  BSD make and GNU make alike from Makefiles that list each object; the third-party code builds as its project builds
  it, with its own warnings.
- A program's settings are its env file's keys, each with a row in its document; a key it does not know stops it,
  naming the key, so a misspelt one is never a setting quietly not taken.
- Formatting isn't checked by CI: format the lines a change touches with `git clang-format` (clang-format 23). The
  style is `rei/.clang-format`, and `tracer/` holds the same file; `accordwimsy/` has none, so name it there:
  `git clang-format --style file:rei/.clang-format`.
- A doc says what is, not how it came to be; [the docs' check](./TESTING.md#the-docs) holds the rest.
- A commit's subject opens with the part it changes in brackets (`[rei]`, `[tracer]`), then says what that part does
  now.

## The pieces from elsewhere

| Piece | Where | How it is updated |
|---|---|---|
| Concord 3.0.1, with the fixes `CONCORD_PATCHES` names | cloned at build time by `dist/freebsd/build.sh` and CI | `CONCORD_VERSION`, `CONCORD_COMMIT` and `REFLECTC_COMMIT` in `build.sh` (CI reads them there); a fix is a patch in `dist/freebsd/` named in `CONCORD_PATCHES` |
| cJSON 1.7.19 | `rei/third_party/cjson` | a release's `cJSON.c`, `cJSON.h` and `LICENSE`, and `SHA256SUMS` made again (`sha256sum cJSON.c cJSON.h LICENSE`) |
| ScarletNX's log summary | `tracer/third_party/logscan` | `sh scripts/sync-logscan.sh SCARLETNX` from a checkout of ScarletNX: its files and commit copied, then Tracer's tests run. A summary that changed is a new version of it (`TR_LOGSCAN`, the files' checksum), and the bot's next start summarizes the kept logs again |
| The games' names | where Tracer runs, never here | `python3 scripts/make-titles.py --out FILE` downloads titledb's and libretro-database's lists from their main branches (`--from DIR` reads them from a folder), and the bot's next start names the reports again |

## CI

`.github/workflows/test.yml`, on each pull request and each push to main, on Ubuntu:

- **The bots' tests**: `make -C rei test`, AccordWimsy's and Tracer's with libcurl's, SQLite's, libarchive's and zlib's
  headers, `sh scripts/test-install.sh`, `python3 scripts/test-check-docs.py` and `python3 scripts/check-docs.py`.
- **The bots against a fake Discord**: Concord at the commits `build.sh` pins, with its fixes, and its tests
  `CONCORD_TESTS` names; `reiwimsy` and `tracer` built with it; `rei/tests/test_gateway.sh` for ReiWimsy, its
  `--fix-cards` and Tracer, as root.

What each covers: [Testing](./TESTING.md). The FreeBSD packages are built by hand on the jails' host
([Build](./BUILD.md#the-freebsd-packages)).
