<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# The forge

A copy of the GitHub organization on [Forgejo](https://forgejo.org): code, issues and pull requests, kept the same
every 10 minutes by [AccordWimsy](./ACCORDWIMSY.md); releases stay on GitHub. Three isolated jails, each doing one
thing, so the one that holds the tokens to GitHub is not the one that answers the internet:

| Jail | Runs | Reaches |
|---|---|---|
| `gitscarlet` | Forgejo, on 3000 | DNS only; reached by `gitedge` and `gitsync` on 3000 |
| `gitedge` | cloudflared, the forge's tunnel | Cloudflare's edge (7844, TCP and UDP), `gitscarlet` on 3000 |
| `gitsync` | AccordWimsy, from cron | DNS, GitHub on 443 (pf table `github`), `gitscarlet` on 3000 |

The host and its tools, and the other jails: [Install](./INSTALL.md). The commands below run as root on the host.

## The jails

    noirjail.sh jail create --isolated --name gitsync --release 15.1-RELEASE
    noirjail.sh firewall jail gitsync egress allow --to table:github --port 443
    noirjail.sh firewall jail gitsync egress allow --to jail:gitscarlet --port 3000

`gitscarlet` and `gitedge` are made the same way, with `--name gitscarlet` and `--name gitedge`. Forgejo goes into
`gitscarlet` with `noirjail.sh forgejo install --url gitscarlet --fqdn <the forge's name> --db sqlite`, which leaves
it no egress but DNS, and lets in port 3000 from `gitedge` and `gitsync` alone. cloudflared goes into `gitedge` with
`noirjail.sh cloudflared install --url gitedge`, its tunnel's token read from a file (`--token-file`), and the tunnel's
public name routes to `http://<gitscarlet's address>:3000`.

## The guard

    noirjail.sh guard add --group <the forge's name> gitscarlet gitedge gitsync
    noirjail.sh guard learn <the forge's name>

The guard learns what the three jails run, listen on and hold in `/usr/local`, `/etc` and `/var/cron/tabs`. A process,
a socket or a file it never learned trips the whole group: the three jails stopped, the evidence kept, the name taken
off Cloudflare, a mail sent. Any change to them therefore goes:

    noirjail.sh guard pause <the forge's name> --minutes 15
    # the change: a package, a file under /usr/local, a restart
    noirjail.sh guard learn <the forge's name>
    noirjail.sh guard pause <the forge's name> --minutes 1

Nothing starts a process in them otherwise: no `jexec`, no `bastille cmd`, no `pkg -j`. They are read from the host,
under `/usr/local/bastille/jails/<jail>/root`, and `pkg -c <that root> query` reads their packages.

## Forgejo

What `noirjail.sh forgejo install` sets in `app.ini`: `ROOT_URL` and `DOMAIN` from its `--fqdn`; `HTTP_ADDR` the
jail's own address, port 3000; no SSH; offline mode; registration closed, and repositories made only by an admin; no
push mirrors; `[git] PULL_REQUEST_PUSH_MESSAGE = false`; migrations only from github.com and api.github.com; Actions,
packages, federation and the update check off; `[security] DISABLE_WEBHOOKS = true`; mail off until
`FORGEJO_MAIL_FROM` is set. Its three secrets (`SECRET_KEY`, `INTERNAL_TOKEN`, `JWT_SECRET`) are made in the jail by
`forgejo generate secret` and never leave it; `app.ini` is root:git 640 in a root:git 750 directory. Cloudflare
challenges every client that is no browser on the forge's name, so repositories come in through `gitsync`, never by a
clone from outside, and the [status page](./STATUS.md) does not check the forge.

## The start page and the themes

`dist/forge/`: Forgejo's start page (`templates/home.tmpl`: getting started, the bots, the repositories), its
images (`public/assets/img`: the logo, the favicons, Wimsy, each bot's avatar) and its themes (`public/assets/css`:
SWS's, each `theme-<id>.css` made by wimsy-dist's `themes/theme.py`). Forgejo offers only those, Sepia to whoever
has no account or picked none, through `app.ini`:

    [ui]
    DEFAULT_THEME = sepia-auto
    THEMES = sepia-auto,sepia-light,sepia-dark,wimsy-auto,wimsy-light,wimsy-dark,fuddles-auto,fuddles-light,fuddles-dark,default

With the group paused, from the host:

    sh dist/forge/install.sh
    service -j gitscarlet forgejo restart

`dist/forge/install.sh` copies them into Forgejo's custom directory in the jail, `/usr/local/etc/forgejo`, from the
host, starting no process in the jail; Forgejo reads them when it starts again. A file it no longer installs (an image
taken out) stays there until it is removed by hand. The jail's name is `gitscarlet` in the script.

## AccordWimsy's jail

AccordWimsy's package, from the host's poudriere repository (its port `devel/accordwimsy` in noirjail.sh's ports
overlay), with the group paused:

    noirjail.sh jail pkg --name gitsync accordwimsy

Its tokens, env file, first pass, cron line and log rotation: [Install](./INSTALL.md#accordwimsy). Its cron runs with
`cron_flags="-J 60"`. From the host, noirjail.sh's own cron mails the new lines of its `accordwimsy.err`
(`noirjail.sh config set JAIL_LOG_MAIL "gitsync:/var/log/accordwimsy/accordwimsy.err"`) and says when a mirror falls
behind (`JAIL_FRESH`, `"gitsync:/var/log/accordwimsy/accordwimsy.fresh:60"`, read by `noirjail.sh jail fresh`).
