<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# Themes

## Index

* [Where each piece lives](#where-each-piece-lives)
* [The picker](#the-picker)
* [Adding a theme](#adding-a-theme)
* [Putting it in place](#putting-it-in-place)

The Tracer site (issues.scarletnx.dev), the forge (git.scarletnx.dev) and the status page (status.scarletnx.dev) wear
SWS's themes: Sepia, Wimsy and Fuddles, each in Auto (the system's light or dark), Light and Dark, and Default, dark
only. SWS, the Simple Wimsy Style, and the colours are wimsy-dist's (`docs/SWS.md`, `themes/palettes.toml`); this
repository takes the files its `themes/theme.py` makes. Sepia in Auto is what a reader sees until they pick.

## Where each piece lives

| Piece | What it is | Made by |
|---|---|---|
| `tracer/site/style.css` | Tracer's stylesheet, Sepia's roles between its theme lines | theme.py (the block); the rules by hand |
| `tracer/site/sws/<id>.css` | each theme and mode's roles, for that block | theme.py |
| `tracer/site/sws/logo-<theme>.svg` | Sepia's (AccordWimsy) and Default's Wimsy, which Tracer's nginx gives at `/sws/logo-<theme>.svg` to the three pickers | theme.py (wimsy-dist's drawings) |
| `scripts/tracer-themes.sh` | the stylesheet in each theme and each theme's Wimsy, into the package's `share/tracer/sws` | `dist/freebsd/build.sh` runs it |
| `tracer/src/site.h` (`SITE_THEMES`) | the picker's links on every Tracer page | by hand |
| `dist/freebsd/tracer-nginx.conf` | the picker's answer, and `style.css` and `tracer.svg` as the reader picked | by hand |
| `dist/forge/public/assets/css/theme-<id>.css` | Forgejo's themes, SWS's alone | theme.py |
| `dist/forge/templates/custom/header.tmpl` | the picked theme's stylesheet in every forge page's head | by hand |
| `dist/forge/templates/custom/extra_links.tmpl` | the picker in the forge's navbar, beside Sign In | by hand |
| `dist/forge/public/assets/img/sws/logo-<theme>.svg` | the forge's copies of each theme's Wimsy | copied; `check-theme.sh` holds them to Tracer's |
| `dist/status/gatus.yaml.in` | Sepia's two colour lines and the selector's filled link | theme.py |
| `dist/status/sws/<theme>.css.in` | the status page's custom.css in each theme; `render.sh` makes `.css` | theme.py |
| `dist/status/nginx.conf.in` | the status jail's nginx: Gatus behind it, custom.css as the reader picked, the picker | by hand; `render.sh` makes `nginx.conf` |
| `scripts/check-theme.sh` | every piece above naming the same themes, the rules taking only roles | CI |

## The picker

A pick is two cookies of all of scarletnx.dev (`Domain=scarletnx.dev; Path=/; Max-Age=31536000; SameSite=Lax;
Secure`): `sws_theme` (`sepia`, `wimsy`, `fuddles` or `default`) and `sws_mode` (`auto`, `light` or `dark`). A value
that is none of those reads as Sepia and Auto. Default has no light, so it ignores the mode.

- **Setting it**: every picker's link goes to `https://issues.scarletnx.dev/sws/<theme>` or `/sws/<mode>`. The Tracer
  site's nginx sets the cookie and sends the reader back to the page the link was on (its `Referer`) when that page is
  on git., status. or issues.scarletnx.dev, else to the site's first page. The links ask for that `Referer`
  (`referrerpolicy`); a theme or mode it does not know is a 404. No script runs anywhere.
- **Tracer**: `style.css` and `tracer.svg` are the same addresses for every reader, and nginx gives each the file of
  their pick from `/usr/local/share/tracer/sws`, marked `Cache-Control: private, no-cache` and `Vary: Cookie` so
  Cloudflare keeps none of them for everyone; each page links them as `style.css?sws=<pick>` and `tracer.svg?sws=<theme>`
  (nginx's `sub_filter`), so a cache that kept one never gives it for another pick. Each stylesheet fills the picker's
  links to its own theme and mode.
- **Forge**: Forgejo links its own theme first (the account's, else `DEFAULT_THEME`); `header.tmpl` then links the
  picked one, so a pick wins over the account's. `extra_links.tmpl` draws the picker: a `<details>` drop-down as
  Forgejo's own menus are (it opens without a script), each theme with its Wimsy, the pick in effect marked. Forgejo 17
  has no hook on the navbar's right, so it is the left menu's (`custom/extra_links`), and `header.tmpl`'s style lets
  that menu take the room between and puts the picker at its end, beside Sign In; overriding `base/head_navbar.tmpl`
  whole would do the same and break on each upgrade that changes that file. Both templates read the cookie through
  `.Context.GetSiteCookie`, which Forgejo 17 gives every page; look at it, and at the navbar's classes, before an
  upgrade. Settings > Appearance still lists the themes by their ids.
- **Status page**: Gatus reads no cookie. In the status jail nginx stands in front of it (127.0.0.1:8090 to Gatus's
  8080): it gives `/css/custom.css` from `/usr/local/www/status-sws/<theme>.css`, the page linking it as
  `custom.css?sws=<theme>` for the same reason, and puts the picker right after the
  page's `<body>` (nginx's `sub_filter`), each theme with its Wimsy from the Tracer site. The page has no mode links:
  Gatus's own button switches light and dark.

## Adding a theme

1. In wimsy-dist: its colours in `themes/palettes.toml`, then `python3 themes/theme.py --core <this checkout>`.
2. Here: its name in `tracer/src/site.h`'s `SITE_THEMES`, `dist/forge/templates/custom/` (both files) with its Wimsy in `dist/forge/public/assets/img/sws/`,
   `dist/status/nginx.conf.in` (the map and a link), `dist/freebsd/tracer-nginx.conf` (the map),
   `dist/freebsd/tracer.plist` and the `THEMES` line in [Forge](./FORGE.md#the-start-page-and-the-themes).
3. `sh dist/status/render.sh`, then `sh scripts/check-theme.sh`: it fails until every piece names the theme.

## Putting it in place

From the host, as root. The Tracer site and the picker's answer first, since the other two link to it.

**Tracer**: the new package (`sh dist/freebsd/build.sh`) in both jails, then the web jail's nginx config again:

    pkg -c /usr/local/bastille/jails/tracer/root add -f tracer-<VERSION>.pkg
    pkg -c /usr/local/bastille/jails/tracerweb/root add -f tracer-<VERSION>.pkg
    cp /usr/local/bastille/jails/tracerweb/root/usr/local/share/examples/tracer/nginx.conf \
        /usr/local/bastille/jails/tracerweb/root/usr/local/etc/nginx/tracer.conf
    service -j tracerweb nginx configtest && service -j tracerweb nginx reload
    service -j tracer tracer restart

**Forge**: the themes, the templates and the `[ui]` lines ([Forge](./FORGE.md#the-start-page-and-the-themes)) in
`app.ini`, with the group paused:

    sh dist/forge/install.sh
    service -j gitscarlet forgejo restart

**Status page**: nginx in the jail, its config included, the tunnel moved to it:

    noirjail.sh jail pkg --name status nginx
    # in the jail's /usr/local/etc/nginx/nginx.conf, inside http { }: include status-sws.conf;
    sh dist/status/install.sh status
    noirjail.sh cloudflare publish --tunnel <the page's name> --fqdn <the page's name> --public --service http://127.0.0.1:8090
