<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# The status page

`dist/status/`: [Gatus](https://github.com/TwiN/gatus) 5.38.0 in the jail `status`, its port reached only through the
jail's own Cloudflare tunnel. It checks the site, the nightly release and the Discord invite itself, and the public bots
through what the host pushes to it every minute. The forge is not checked: Cloudflare's challenge stops any checker
that is no browser ([Forge](./FORGE.md#forgejo)).

## What it checks

| Endpoint | Every | Up when |
|---|---|---|
| scarletnx.dev | 5 min | it answers 302 (to the Pages site) |
| Site (scarletnx.github.io) | 5 min | 200, and the page says "ScarletNX is a static recompiler" |
| Nightly release | 10 min | GitHub's API has a latest release named `nightly-*` (6 of the 60 calls an hour it allows) |
| Discord invite | 30 min | Discord's API knows the invite the site links to |
| ReiWimsy | pushed | `reiwimsy_enable` is YES in `discordbots` and `/usr/local/libexec/reiwimsy` has run a minute or more |
| AccordWimsy | pushed | `noirjail.sh jail fresh` passes: each mirror had a good pass within its time |

No push for 5 minutes reads as down, so a push that stops shows too. A running process is no proof of a live Discord
connection: a bot whose gateway stays down while it runs still reads as up. Only public bots are on the page.

## Its files

| File | What it is |
|---|---|
| `dist/status/addresses.conf` | every address the page names or probes, the one place they are written |
| `dist/status/gatus.yaml.in` | the config, `@NAME@` for each address and `@ICON_<BOT>@` for each icon |
| `dist/status/push.sh.in` | the bots' state, pushed every minute from the host's cron |
| `dist/status/render.sh` | writes `gatus.yaml` and `push.sh` from the two; those are what runs, never edited by hand |
| `dist/status/gatus.yaml`, `dist/status/push.sh` | what `render.sh` wrote, kept beside the templates so a review sees what runs |
| `dist/status/gatus.yaml.in`'s two `html:root` lines | the Sepia theme's colours as Gatus's, made by wimsy-dist's `themes/theme.py` |
| `dist/status/icons/` | each bot's icon, 64 px, put into the page as data |
| `dist/status/check.sh` | the page checked without Gatus ([Testing](./TESTING.md#the-status-page)) |
| `dist/status/install.sh` | as root on the host: the push token, the config and the database directory into the jail, Gatus started again, `push.sh` and its cron line |

`dist/status/install.sh [JAIL]` (`status` when not given) makes the push token once (`openssl rand`), keeps it as the
header curl sends in `/usr/local/etc/status-scarletnx/push-header` (root's, 600), writes `gatus.yaml` into the jail
with the token in it (root:www, 640), makes `/var/db/gatus` (www's, 750), turns `gatus_enable` on and starts Gatus
again, installs `push.sh` into `/usr/local/libexec/status-scarletnx` with its cron line,
`/usr/local/etc/cron.d/status-scarletnx` (every minute, its output in `/var/log/status-scarletnx-push.log`), pushes
once, and shows the service's status and the end of Gatus's log.

## Putting it up

As root on the host, from a checkout:

    noirjail.sh jail create --name status --release 15.1-RELEASE --ip <its address>
    noirjail.sh jail pkg --name status gatus cloudflared
    sh dist/status/install.sh status
    noirjail.sh cloudflare tunnel --tunnel <the page's name>
    noirjail.sh cloudflare publish --tunnel <the page's name> --fqdn <the page's name> --public --service http://127.0.0.1:8080
    noirjail.sh cloudflared install --url status --tunnel <the page's name>

## Changing it

After a change to a template, an icon or an address:

    sh dist/status/render.sh
    sh dist/status/check.sh
    sh dist/status/install.sh status

After a theme's colours change, first wimsy-dist's `themes/theme.py --core <this checkout>`. A new bot on the page is an
external endpoint in `gatus.yaml.in` with its icon's rule in the `custom-css` there, its icon in `icons/`, and a `bot`
or `push` line in `push.sh.in`; `check.sh` fails until each is there.

Gatus is FreeBSD's `www/gatus`; it is pure Go (`CGO_ENABLED=0 go build`), so the same config runs on any device that
builds it, with `GATUS_LISTEN=127.0.0.1` there in `addresses.conf`.
