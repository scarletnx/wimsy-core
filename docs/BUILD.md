<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# Build

The programs are C99, built with BSD make or GNU make, a C compiler and pkg-config's flags for libcurl (and for
SQLite, libarchive, zlib and libmicrohttpd, Tracer's). Run from the checkout's top:

| System | What to install |
|---|---|
| FreeBSD | `pkg install git gmake pkgconf curl ca_root_nss sqlite3 libmicrohttpd` (libarchive and zlib are in base) |
| Debian, Ubuntu | `apt install build-essential pkg-config libcurl4-openssl-dev libsqlite3-dev libarchive-dev zlib1g-dev libmicrohttpd-dev git` |

ReiWimsy and Tracer link [Concord](https://github.com/Cogmasters/concord); their tests, AccordWimsy and tracer-mcp
build without it.

## Concord

Concord 3.0.1, linked statically, with a fix it does not have (`dist/freebsd/concord-heartbeat-send.patch`: a
heartbeat that cannot be sent leaves its gateway retrying every millisecond, never reconnecting), at the commit
`dist/freebsd/build.sh` pins (`CONCORD_COMMIT`, and `REFLECTC_COMMIT` for its reflect-c submodule), installed into a
folder of its own. GNU make builds it (`gmake`, which is `make` on Linux):

    git clone --depth 1 --branch v3.0.1 --recurse-submodules --shallow-submodules \
        https://github.com/Cogmasters/concord.git /tmp/concord
    git -C /tmp/concord apply "$PWD/dist/freebsd/concord-heartbeat-send.patch"
    gmake -C /tmp/concord && gmake -C /tmp/concord install PREFIX="$HOME/concord" SHAREDIR="$HOME/concord/share"

## ReiWimsy

    make -C rei CONCORD="$HOME/concord"                # rei/reiwimsy
    make -C rei test                                   # its tests: no Discord, no network, no Concord
    make -C rei test-gateway CONCORD="$HOME/concord"   # reiwimsy against a fake Discord: Linux (Testing)

## Tracer

Concord as above, then:

    make -C tracer CONCORD="$HOME/concord"             # tracer/tracer and tracer/tracer-mcp
    make -C tracer test                                # its tests: no Discord, no network, no Concord, no libcurl
    make -C tracer test-gateway CONCORD="$HOME/concord"   # tracer against a fake Discord: Linux (Testing)

On FreeBSD add `CXXRT=-lc++` to each: the log summary demangles through the C++ runtime, libc++ in base
(`-lstdc++` elsewhere). `make -C tracer tracer-mcp` builds the MCP server alone, without Concord. Tracer builds
ReiWimsy's `util.c`, `http.c` and `rest.c` again as its own (`REI_NAME` names it in the log and the user agent), and
ScarletNX's log summary from `tracer/third_party/logscan`.

## AccordWimsy

    make -C accordwimsy          # accordwimsy/accordwimsy
    make -C accordwimsy test     # its tests: a fake GitHub and Forgejo, git on local bare repositories

It uses ReiWimsy's copy of cJSON (`rei/third_party/cjson`), so it builds from a whole checkout. At run time it needs
`git` (or FreeBSD's `git-lite`) and the system's CA certificates.

`make clean` in each folder takes its objects and programs out; `CC`, `CFLAGS` and the `*_CFLAGS` and `*_LIBS` of each
Makefile's head may be given on make's command line.

## The FreeBSD packages

On the FreeBSD release the jails run (15.1 here), with the network, from a checkout:

    sh dist/freebsd/build.sh [OUT]        # OUT: ./packages when not given

It clones Concord at its pinned commits and refuses others, applies the fix, runs Concord's `unit-gateway-lifecycle`
test, builds the programs from a copy of the checkout with their own tests first, and writes with `pkg create`:

| Package | Its files | Depends on |
|---|---|---|
| `reiwimsy-<VERSION>.pkg` | `/usr/local/libexec/reiwimsy`, `/usr/local/etc/rc.d/reiwimsy`, `/usr/local/etc/reiwimsy.env.sample` | curl, ca_root_nss |
| `tracer-<VERSION>.pkg` | `/usr/local/libexec/tracer`, `/usr/local/libexec/tracer-mcp`, `/usr/local/etc/rc.d/tracer`, `/usr/local/etc/rc.d/tracer_mcp`, `/usr/local/etc/tracer.env.sample`, `/usr/local/share/tracer/{style.css,site.js,tracer.svg}`, `/usr/local/share/examples/tracer/nginx.conf` | curl, ca_root_nss, sqlite3, libmicrohttpd |
| `accordwimsy-<VERSION>.pkg` | `/usr/local/bin/accordwimsy`, `/usr/local/etc/accordwimsy/accordwimsy.env.sample`, `/usr/local/share/examples/accordwimsy/{accordwimsy.cron,accordwimsy.newsyslog}` | curl, git-lite or git, ca_root_nss |

`VERSION`, at the top of the checkout, is every package's version; a dependency is recorded at the version the building
machine has installed. On install each one's post-install script makes its user, and its env file from the sample
when there is none (mode 600); AccordWimsy's also makes `/var/db/accordwimsy` (700, its user's) and
`/var/log/accordwimsy` with its log files. A package changes nothing it did not install: an env file filled in stays,
and so do the users when it is deleted. What a package holds: `pkg info -F <file> -l`, its scripts `pkg info -F
<file> -D`. Each package's files are listed in `dist/freebsd/<name>.plist`, its description in `<name>.desc` and its
post-install script in `<name>.post-install`.

A FreeBSD port of AccordWimsy, `devel/accordwimsy`, builds from a commit of this repository in noirjail.sh's ports
overlay, for a poudriere repository: what an isolated jail installs from.
