#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
#
# check-theme.sh against copies of this checkout's theme files, each broken one way: every way fails, the copy as it is
# passes.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/.." && pwd)
t=$(mktemp -d)
trap 'rm -rf "$t"' EXIT
fails=0

# A fresh copy, <sed program> run on <file> of it (or the file removed, for "rm"); expects check-theme.sh to give
# <want> (0 or 1)
case_() {
	rm -rf "$t/c"
	mkdir -p "$t/c/tracer/site" "$t/c/tracer/src" "$t/c/dist/forge/public/assets" "$t/c/dist/forge/templates" \
		"$t/c/dist/status" "$t/c/dist/freebsd" "$t/c/docs"
	cp "$top/tracer/site/style.css" "$t/c/tracer/site/"
	cp -R "$top/tracer/site/sws" "$t/c/tracer/site/"
	cp "$top/tracer/src/site.h" "$t/c/tracer/src/"
	cp -R "$top/dist/forge/public/assets/css" "$t/c/dist/forge/public/assets/"
	cp -R "$top/dist/forge/templates/custom" "$t/c/dist/forge/templates/"
	cp "$top/dist/status/gatus.yaml.in" "$top/dist/status/nginx.conf.in" "$t/c/dist/status/"
	cp -R "$top/dist/status/sws" "$t/c/dist/status/"
	cp "$top/dist/freebsd/tracer-nginx.conf" "$t/c/dist/freebsd/"
	cp "$top/docs/FORGE.md" "$t/c/docs/"
	case "$3" in
	'') ;;
	rm) rm "$t/c/$2" ;;
	*) sed -i.orig -e "$3" "$t/c/$2" && rm "$t/c/$2.orig" ;;
	esac
	got=0
	sh "$here/check-theme.sh" "$t/c" > /dev/null 2>&1 || got=$?
	if [ "$got" -eq "$4" ]; then echo "ok   $1"; else echo "FAIL $1: exit $got, not $4"; fails=$((fails + 1)); fi
}

case_ "the checkout's files pass" tracer/site/style.css "" 0
case_ "a hex colour in Tracer's rules fails" tracer/site/style.css 's/^a{color:var(--accent);/a{color:#123456;/' 1
case_ "rgb() in Tracer's rules fails" tracer/site/style.css 's/^a{color:var(--accent);/a{color:rgb(1,2,3);/' 1
case_ "a named colour in Tracer's rules fails" tracer/site/style.css 's/^a{color:var(--accent);/a{color: white;/' 1
case_ "a role the block lacks fails" tracer/site/style.css 's/^a{color:var(--accent);/a{color:var(--link);/' 1
case_ "an empty theme block fails" tracer/site/style.css '/theme:begin/,/theme:end/{/theme:/!d;}' 1
case_ "two theme blocks fail" tracer/site/style.css 's|^/\* theme:end \*/|&\n/* theme:begin */\n:root{--bg:0}\n/* theme:end */|' 1
case_ "Tracer's stylesheet over its budget fails" tracer/site/style.css '$a /*'"$(printf '%09000d' 0)"'*/' 1
case_ "a theme's block without a role the rules take fails" tracer/site/sws/wimsy-dark.css 's/--soft:[^;]*;//g' 1
case_ "a Forgejo theme without its block in tracer/site/sws fails" tracer/site/sws/default.css rm 1
case_ "Tracer's picker missing a theme fails" tracer/src/site.h '/sws\/fuddles/d' 1
case_ "the forge's picker missing a theme fails" dist/forge/templates/custom/body_inner_pre.tmpl '/sws\/default/d' 1
case_ "the status page's picker with a theme too many fails" dist/status/nginx.conf.in 's#<a href="@SWS_URL@/sepia">#<a href="@SWS_URL@/neon">Neon</a>&#' 1
case_ "Tracer's nginx not mapping a theme fails" dist/freebsd/tracer-nginx.conf '/^\tfuddles fuddles;$/d' 1
case_ "the status nginx not mapping a theme fails" dist/status/nginx.conf.in '/^\twimsy wimsy;$/d' 1
case_ "a status theme with a hex of its own fails" dist/status/sws/fuddles.css.in 's/var(--warn-fill)}/#ffaa00}/' 1
case_ "a status theme gone fails" dist/status/sws/wimsy.css.in rm 1
case_ "a Forgejo theme not made by theme.py fails" dist/forge/public/assets/css/theme-default.css '1s/.*/:root{}/' 1
case_ "a THEMES line missing a theme fails" docs/FORGE.md 's/^\( *THEMES = .*\),default$/\1/' 1
case_ "a Gatus rule with a role of its own fails" dist/status/gatus.yaml.in 's/var(--warn-fill)}/var(--amber)}/' 1
case_ "a Gatus rule with a hex fails" dist/status/gatus.yaml.in 's/var(--warn-fill)}/#ffaa00}/' 1
case_ "a Gatus colour line gone fails" dist/status/gatus.yaml.in '/^    html:root.dark{--background:/d' 1
[ "$fails" -eq 0 ] || exit 1
