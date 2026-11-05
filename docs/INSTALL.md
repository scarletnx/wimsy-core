<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# Install

## Index

* [The host and its tools](#the-host-and-its-tools)
* [The jails](#the-jails)
* [The packages](#the-packages)
* [ReiWimsy](#reiwimsy)
  * [ReiWimsy's Discord application](#reiwimsys-discord-application)
* [Tracer](#tracer)
  * [Tracer's Discord application](#tracers-discord-application)
* [AccordWimsy](#accordwimsy)
* [Upgrade and removal](#upgrade-and-removal)
* [Another machine: what changes](#another-machine-what-changes)

How the services run on one FreeBSD host, set up from nothing: the host and its tools, the jails, the packages, and
each program's settings and service. The forge and the status page have pages of their own ([Forge](./FORGE.md),
[Status page](./STATUS.md)); each secret and where it lives: [Security](./SECURITY.md). The commands run as root on the
host, from a checkout of this repository.

## The host and its tools

One FreeBSD host runs every jail with [Bastille](https://github.com/BastilleBSD/bastille), thin jails of
15.1-RELEASE. The jails are made and kept by [noirjail.sh](https://accord.dravee.dev/DraVee/noirjail.sh)
(Apache-2.0): Bastille underneath, pf rules for what each jail may reach, Cloudflare tunnels, and a guard that stops a
group of jails when they run something they never ran before. Its settings sit in
`/usr/local/etc/noirjail/jail-inventory.json`, set with `noirjail.sh config set NAME VALUE`. The commands below are
Bastille's and noirjail.sh's; each one's own help says what Bastille and pf do under it.

## The jails

| Jail | Runs | Network | Reaches |
|---|---|---|---|
| `discordbots` | ReiWimsy | the host's LAN bridge | Discord, GitHub |
| `tracer` | Tracer | the host's LAN bridge | Discord and its CDN; GitHub's API with `TRACER_ISSUES_TOKEN` |
| `tracerweb` | nginx, tracer-mcp and cloudflared: Tracer's site, its MCP server and their tunnel | the host's LAN bridge | Cloudflare; Tracer's site mounted read-only |
| `gitsync` | AccordWimsy, from cron | isolated: a /30 of `ISOLATED_NET` with NAT | DNS, GitHub on 443 (pf table `github`), `gitscarlet` on 3000 |
| `gitscarlet` | Forgejo, on 3000 | isolated | DNS only; reached by `gitedge` and `gitsync` on 3000 |
| `gitedge` | cloudflared, the forge's tunnel | isolated | Cloudflare's edge (7844, TCP and UDP), `gitscarlet` on 3000 |
| `status` | Gatus and cloudflared, the status page's tunnel | the host's LAN bridge | the addresses it checks, Cloudflare |

An isolated jail asks `ISOLATED_DNS` for names and reaches no package repository: its packages come from the host's
(noirjail.sh's `jail pkg` mounts them in). The forge's three jails are made in [Forge](./FORGE.md#the-jails), the
status page's in [Status page](./STATUS.md#putting-it-up).

## The packages

Built on the jails' FreeBSD release ([Build](./BUILD.md#the-freebsd-packages)):

    pkg install git gmake pkgconf curl ca_root_nss sqlite3 libmicrohttpd
    sh dist/freebsd/build.sh             # packages/reiwimsy-<VERSION>.pkg, tracer-<VERSION>.pkg, accordwimsy-<VERSION>.pkg

then into a jail:

    sh scripts/install.sh discordbots packages/reiwimsy-$(cat VERSION).pkg

`scripts/install.sh JAIL PACKAGE...` installs each package's dependencies from the jail's package repository, adds
the package from its file, and restarts its service when it was running. It turns no service on. A jail that reaches no
package repository installs from one of the host's instead (`noirjail.sh jail pkg --name JAIL accordwimsy`, from its
poudriere repository). A jail of a guard group trips on any process started in it: the group is paused first and
learns after ([Forge](./FORGE.md#the-guard)).

## ReiWimsy

1. Its jail and package:

       bastille create discordbots 15.1-RELEASE <ip> <interface>
       sh scripts/install.sh discordbots packages/reiwimsy-$(cat VERSION).pkg

2. Its Discord application ([below](#reiwimsys-discord-application)) and its token.
3. The env file the package made, `/usr/local/etc/reiwimsy.env` (root's, 600): `REIWIMSY_TOKEN`, and the other keys
   [ReiWimsy](./REIWIMSY.md#settings) lists. A value left empty keeps the service from starting, naming the file.
4. On, and started:

       sysrc -j discordbots reiwimsy_enable=YES
       service -j discordbots reiwimsy start

Its log is syslog's, tag `reiwimsy`, in the jail's `/var/log/messages`; what it keeps of its replies and cards is in
`/var/db/reiwimsy/state.json` (`reiwimsy_state` in rc.conf moves it). Its service and its commands:
[ReiWimsy](./REIWIMSY.md#the-service).

### ReiWimsy's Discord application

In the Developer Portal:

1. Bot: reset the token, put it in the env file; turn on **Message Content Intent** (without it, the messages it reads
   come with no text).
2. About me: what it does and that it keeps nothing.
3. Invite (OAuth2, scope `bot`), with only what it needs: View Channels, Send Messages, Send Messages in Threads, Embed
   Links, Read Message History, and Manage Messages (its first round on a card channel deletes the webhooks' cards
   there):

   `https://discord.com/oauth2/authorize?client_id=<APPLICATION_ID>&scope=bot&permissions=274878000128`

A channel id for `REIWIMSY_CHANNELS` and the card channels: Developer Mode, right click, Copy Channel ID.

## Tracer

1. Its two jails: the bot listens on nothing, and its site is served from another jail, which can read it and write
   nothing of it:

       bastille create tracer 15.1-RELEASE <ip> <interface>
       bastille create tracerweb 15.1-RELEASE <ip> <interface>
       bastille cmd tracer mkdir -p /var/db/tracer-site
       bastille cmd tracerweb mkdir -p /usr/local/www/tracer
       bastille mount tracerweb /usr/local/bastille/jails/tracer/root/var/db/tracer-site /usr/local/www/tracer nullfs ro 0 0
       bastille pkg tracerweb install nginx cloudflared

2. The package into both (the bot in `tracer`, `tracer-mcp` in `tracerweb`):

       sh scripts/install.sh tracer packages/tracer-$(cat VERSION).pkg
       sh scripts/install.sh tracerweb packages/tracer-$(cat VERSION).pkg

3. Its Discord application ([below](#tracers-discord-application)) and its token.
4. The env file the package made in `tracer`, `/usr/local/etc/tracer.env` (root's, 600): `TRACER_TOKEN`, `TRACER_URL`
   (the site's address), `TRACER_CHANNELS` (channels' ids, or `all` for every channel it can read; without it no
   report is taken), and the other keys
   [Tracer](./TRACER.md#settings) lists. For the issues, `TRACER_ISSUES_REPO` and `TRACER_ISSUES_TOKEN`: on GitHub,
   Settings, Developer settings, Fine-grained tokens, the repository alone, the permission Issues: Read and write,
   nothing else. For the games' names, `TRACER_TITLES`, a file `python3 scripts/make-titles.py --out FILE` makes in
   the jail.
5. On, and started:

       sysrc -j tracer tracer_enable=YES
       service -j tracer tracer start

   The service makes `/var/db/tracer-site` its user's at start.
6. The site, in `tracerweb`: nginx with the package's `/usr/local/share/examples/tracer/nginx.conf`
   (`dist/freebsd/tracer-nginx.conf`) in its `http` block, and its tunnel, `noirjail.sh cloudflared install --url
   tracerweb` with its token from a file (`--token-file`), the tunnel's public name routed to `http://127.0.0.1:8080`.
7. The MCP server, in `tracerweb` too:

       sysrc -j tracerweb tracer_mcp_enable=YES tracer_mcp_url=https://<the site's name>
       service -j tracerweb tracer_mcp start

Its log is syslog's, tag `tracer`, in the jail's `/var/log/messages`; its database is `/var/db/tracer/tracer.db`
(`tracer_db` in rc.conf moves it), its site `/var/db/tracer-site` (`tracer_site`). Back up the database and the site's
`r/` together: the files are there, the database names them. Its services: [Tracer](./TRACER.md#the-service) and
[tracer-mcp](./TRACER-MCP.md#the-service).

### Tracer's Discord application

In the Developer Portal:

1. Bot: the name Tracer, its avatar (`tracer/site/tracer.svg` on a navy square, room around it for the round crop);
   reset the token, put it in the env file. Discord sends a message's text and attachments to a bot it mentions; the
   Message Content Intent lets Tracer read the one message that a reply mentioning it asks for (a log sent without the
   mention), and without it such a reply is told to send the log again ([Tracer](./TRACER.md#a-log-sent-without-the-mention)).
   Public Bot off.
2. About me: what it keeps, that the site shows no names, and `@Tracer help`.
3. Invite (OAuth2, scope `bot`), with only what it needs: View Channels, Send Messages, Send Messages in Threads, Embed
   Links, Read Message History (a reply needs it):

   `https://discord.com/oauth2/authorize?client_id=<APPLICATION_ID>&scope=bot&permissions=274877991936`

An id for `TRACER_CHANNELS` or `TRACER_ADMINS`: Developer Mode, right click, Copy Channel ID or Copy User ID. A forum
channel's id takes its posts too. `TRACER_CHANNELS=all` takes reports in every channel and thread the bot can read, as
its server's roles and channels' permissions let it.

## AccordWimsy

It runs in the forge's `gitsync` jail, made with its package in [Forge](./FORGE.md#accordwimsys-jail); the steps below
start processes in it, so its guard group is paused first.

1. On the Forgejo: a user for it (`accordwimsy`, `ACCORDWIMSY_FORGEJO_USER` when another) and its token with
   `write:repository`, `write:issue`, `write:organization` and `read:user`; the organization it fills, and, by an
   admin in that organization's settings, the number of repositories it may hold (`-1` there means the site's
   default, 0, and every repository made fails).
2. On GitHub: a fine-grained token of the organization, read-only: Contents, Issues, Pull requests, Metadata.
3. The env file the package made, `/usr/local/etc/accordwimsy/accordwimsy.env` (its user's, 600): the Forgejo's
   address, both tokens, and the other keys [AccordWimsy](./ACCORDWIMSY.md#settings) lists.
4. A first pass by hand, then what it says of each repository:

       jexec gitsync su -m accordwimsy -c '/usr/local/bin/accordwimsy sync'
       jexec gitsync /usr/local/bin/accordwimsy status

5. Its cron line and its log rotation, from the package's examples (inside the jail, or under its root from the host):

       cp /usr/local/share/examples/accordwimsy/accordwimsy.cron /usr/local/etc/cron.d/accordwimsy
       cp /usr/local/share/examples/accordwimsy/accordwimsy.newsyslog /usr/local/etc/newsyslog.conf.d/accordwimsy.conf

   A pass every 10 minutes, its output in `/var/log/accordwimsy/accordwimsy.log` and `.err`, with no mail from cron.

What someone has to see goes to `accordwimsy.err`; noirjail.sh's `JAIL_LOG_MAIL` mails its new lines, and its
`JAIL_FRESH` reads `ACCORDWIMSY_FRESH` to say when a mirror falls behind ([Forge](./FORGE.md#accordwimsys-jail)).

## Upgrade and removal

A new version: build the packages again and run `scripts/install.sh` again for each jail. It restarts the service
named as the package when it was running: ReiWimsy's and Tracer's start again on the new program. In `tracerweb` that
service, `tracer`, does not run, so tracer-mcp goes on with the old program until `service -j tracerweb tracer_mcp
restart`. AccordWimsy's next pass runs the new one (in `gitsync`, `noirjail.sh jail pkg --name gitsync accordwimsy`
with the group paused, from the host's repository). Removal, for each:

    service -j discordbots reiwimsy stop
    pkg -j discordbots delete reiwimsy
    service -j tracerweb tracer_mcp stop
    pkg -j tracerweb delete tracer
    service -j tracer tracer stop
    pkg -j tracer delete tracer

AccordWimsy's cron line goes first (`/usr/local/etc/cron.d/accordwimsy`), then its package, with the group paused. The
env files filled in, the state, the database and the users stay; `pw userdel` and the files by hand take them out.

## Another machine: what changes

- noirjail.sh's settings (`noirjail.sh config set`): `CLOUDFLARE_TOKEN_FILE`, `CLOUDFLARE_ACCOUNT_ID`,
  `CLOUDFLARE_ZONE`, `CLOUDFLARE_TUNNEL`, `CLOUDFLARE_NOTIFY_EMAIL`; `ISOLATED_NET`, `ISOLATED_DNS`;
  `GUARD_NOTIFY_EMAIL`, `GUARD_FROM`, `FORGEJO_MAIL_FROM`; `JAIL_LOG_MAIL`, `JAIL_FRESH`.
- The names: the forge's (Forgejo's `--fqdn`, the guard group, its Cloudflare route), the status page's (its tunnel and
  route, `STATUS_URL`) and Tracer's site (its tunnel's route, `TRACER_URL`, `tracer_mcp_url`).
- The addresses: `gitscarlet`'s in `ACCORDWIMSY_FORGEJO`, the forge's route and pf's anchors; the status page's, all
  of them in `dist/status/addresses.conf` (`GATUS_PUSH_URL` is the `status` jail's), then `dist/status/render.sh`.
- The jails' names, when they differ: `discordbots` in `dist/status/push.sh.in`, `gitscarlet` in
  `dist/forge/install.sh`, `tracer` in the `bastille mount` of `tracerweb` above.
