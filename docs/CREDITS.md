<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# Licenses and credits

wimsy-core is [GPL-3.0-or-later](../LICENSE).

| Component | Where | License |
| --- | --- | --- |
| [cJSON](https://github.com/DaveGamble/cJSON) 1.7.19 | `rei/third_party/cjson`, in both programs | MIT (`rei/third_party/cjson/LICENSE`) |
| [Concord](https://github.com/Cogmasters/concord) 3.0.1 | linked into ReiWimsy, from its repository at build time | MIT |
| [reflect-c](https://github.com/lcsmuller/reflect-c) | Concord's submodule | MIT |
| [libcurl](https://curl.se/libcurl/) | linked by the programs, the system's | curl |
| [SQLite](https://sqlite.org) | Tracer's database, the system's | public domain |
| [titledb](https://github.com/blawar/titledb) | the Switch games' names by title ID, `tracer/titles.tsv` (`scripts/make-titles.py`) | MIT |
| [libretro-database](https://github.com/libretro/libretro-database) (No-Intro's list) | the GBA games' names by game code, `tracer/titles.tsv` | CC-BY-SA-4.0, which GPL-3.0 takes |
| [libmicrohttpd](https://www.gnu.org/software/libmicrohttpd/) | the HTTP of `tracer-mcp`, the system's | LGPL-2.1-or-later |
| [libarchive](https://libarchive.org) | Tracer's `.gz` and `.zip` logs, FreeBSD's base | BSD-2-Clause |
| ScarletNX's log summary (`logscan.c`, `demangle.c`, `ascii.h`) | `tracer/third_party/logscan`, its commit in `COMMIT` | GPL-3.0-or-later |
| [Gatus](https://github.com/TwiN/gatus) | runs the status page, `dist/status` its config | Apache-2.0 |
| [Forgejo](https://forgejo.org) | runs the forge, `dist/forge` its start page | GPL-3.0-or-later |
| [cloudflared](https://github.com/cloudflare/cloudflared) | the tunnels | Apache-2.0 |
| [Bastille](https://github.com/BastilleBSD/bastille) and [noirjail.sh](https://accord.dravee.dev/DraVee/noirjail.sh) | the jails | BSD-3-Clause, Apache-2.0 |
| Wimsy, ScarletNX's mascot, drawn by Liz | `dist/forge/public/assets/img`, `dist/status` and the bots' avatars | GPL-3.0-or-later (ScarletNX's `dist/wimsy.svg`) |
| Wimsy with short tails and glowing circuits, by AO55 after Liz's Wimsy | `tracer/site/tracer.svg`, Tracer's icon and avatar | GPL-3.0-or-later (ScarletNX's splash, `dist/splash/`) |
