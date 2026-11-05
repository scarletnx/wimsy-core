#!/bin/sh
# Puts git.scarletnx.dev's start page (templates/home.tmpl), SWS's theme picker (templates/custom: the picked theme's
# stylesheet in each page's head, the picker in the navbar), its icons (public/assets/img: logo, favicon,
# apple-touch-icon, wimsy.svg, bots/: each bot's avatar, and sws/: each theme's Wimsy) and its themes
# (public/assets/css: theme-<id>.css, SWS's, made by wimsy-dist's themes/theme.py) into
# Forgejo's custom directory in gitscarlet (FORGEJO_CUSTOM, its rc.d:
# /usr/local/etc/forgejo), from the folder this script is in. Run as root
# on the host (no process in the jail), with the guard group paused:
# /usr/local changes. Forgejo reads them after a restart.
set -e
here=$(dirname "$0")
G=/usr/local/bastille/jails/gitscarlet/root/usr/local/etc/forgejo
[ -d "$G/conf" ] || { echo "no $G/conf: is gitscarlet's root elsewhere?" >&2; exit 1; }
install -d -o root -g wheel -m 0755 "$G/templates" "$G/templates/custom" "$G/public" "$G/public/assets" "$G/public/assets/img" \
    "$G/public/assets/img/bots" "$G/public/assets/img/sws" "$G/public/assets/css"
install -o root -g wheel -m 0644 "$here/templates/home.tmpl" "$G/templates/home.tmpl"
for f in "$here"/templates/custom/*.tmpl; do
    install -o root -g wheel -m 0644 "$f" "$G/templates/custom/"
done
# the picker's bar at the top of every page, as an older install drew it: it is in the navbar now
rm -f "$G/templates/custom/body_inner_pre.tmpl"
for f in "$here"/public/assets/img/*.png "$here"/public/assets/img/*.svg; do
    install -o root -g wheel -m 0644 "$f" "$G/public/assets/img/"
done
for f in "$here"/public/assets/img/bots/*.png "$here"/public/assets/img/bots/*.svg; do
    install -o root -g wheel -m 0644 "$f" "$G/public/assets/img/bots/"
done
for f in "$here"/public/assets/img/sws/*.svg; do
    install -o root -g wheel -m 0644 "$f" "$G/public/assets/img/sws/"
done
for f in "$here"/public/assets/css/theme-*.css; do
    install -o root -g wheel -m 0644 "$f" "$G/public/assets/css/"
done
ls -ln "$G/templates" "$G/templates/custom" "$G/public/assets/img" "$G/public/assets/img/bots" "$G/public/assets/img/sws" \
    "$G/public/assets/css"
