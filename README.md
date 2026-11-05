<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# wimsy-core

The services around [ScarletNX](https://github.com/scarletnx/ScarletNX), as FreeBSD packages for jails:

| Program | What it does |
|---|---|
| [ReiWimsy](docs/REIWIMSY.md) (`rei/`) | ScarletNX's Discord bot: a message that names an issue or a pull request gets a link to each, and a card is kept for each open pull request, each open issue and each push to main, edited as GitHub changes |
| [Tracer](docs/TRACER.md) (`tracer/`) | the game reports people send it on Discord, a log and screenshots each, kept and drawn as a static site grouped by game |
| [tracer-mcp](docs/TRACER-MCP.md) (`tracer/`) | those reports for agents, as an MCP server beside the site |
| [AccordWimsy](docs/ACCORDWIMSY.md) (`accordwimsy/`) | the GitHub organization kept the same on a Forgejo, every 10 minutes |

Beside them, the pages they show on: the [status page](docs/STATUS.md) (`dist/status`, Gatus) and the
[forge](docs/FORGE.md)'s start page (`dist/forge`, Forgejo).

## Quick start

On FreeBSD 15.1, as root on the jails' host, from a checkout:

    pkg install git gmake pkgconf curl ca_root_nss sqlite3 libmicrohttpd
    sh dist/freebsd/build.sh             # packages/reiwimsy-<VERSION>.pkg, tracer-<VERSION>.pkg, accordwimsy-<VERSION>.pkg
    sh scripts/install.sh discordbots packages/reiwimsy-$(cat VERSION).pkg

then fill in `/usr/local/etc/reiwimsy.env` in the jail and start the service ([Install](docs/INSTALL.md#reiwimsy)).
The tests, on Linux or FreeBSD: `make -C rei test`, and the rest in [Testing](docs/TESTING.md).

## Documentation

- [Install](docs/INSTALL.md): the host, the jails, the packages and each service, from nothing; upgrades and removal
- [ReiWimsy](docs/REIWIMSY.md), [Tracer](docs/TRACER.md), [tracer-mcp](docs/TRACER-MCP.md) and
  [AccordWimsy](docs/ACCORDWIMSY.md): what each does, its settings, its service and what it keeps
- [The forge](docs/FORGE.md), [the status page](docs/STATUS.md) and [their themes](docs/THEMES.md)
- [Security](docs/SECURITY.md): the secrets and where each lives, what each token and jail reaches, what the bots keep
  of people
- [Build](docs/BUILD.md), [Development](docs/DEVELOPMENT.md) and [Testing](docs/TESTING.md)
- [Every document](docs/README.md), by subject

## License and credits

wimsy-core is [GPL-3.0-or-later](LICENSE); what it uses and their licenses: [Licenses and credits](docs/CREDITS.md).
