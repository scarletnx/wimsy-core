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

# A fresh copy, <sed program> run on <file> of it; expects check-theme.sh to give <want> (0 or 1)
case_() {
	rm -rf "$t/c"
	mkdir -p "$t/c/tracer/site" "$t/c/dist/forge/public/assets" "$t/c/dist/status" "$t/c/docs"
	cp "$top/tracer/site/style.css" "$t/c/tracer/site/"
	cp -R "$top/dist/forge/public/assets/css" "$t/c/dist/forge/public/assets/"
	cp "$top/dist/status/gatus.yaml.in" "$t/c/dist/status/"
	cp "$top/docs/FORGE.md" "$t/c/docs/"
	[ -z "$3" ] || sed -i.orig -e "$3" "$t/c/$2"
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
case_ "Tracer's stylesheet over its budget fails" tracer/site/style.css '$a /*'"$(printf '%08000d' 0)"'*/' 1
case_ "a Forgejo theme not made by theme.py fails" dist/forge/public/assets/css/theme-default.css '1s/.*/:root{}/' 1
case_ "a THEMES line missing a theme fails" docs/FORGE.md 's/^\( *THEMES = .*\),default$/\1/' 1
case_ "a Gatus rule with a role of its own fails" dist/status/gatus.yaml.in 's/var(--warn-fill)}/var(--amber)}/' 1
case_ "a Gatus rule with a hex fails" dist/status/gatus.yaml.in 's/var(--warn-fill)}/#ffaa00}/' 1
case_ "a Gatus colour line gone fails" dist/status/gatus.yaml.in '/^    html:root.dark{--background:/d' 1
[ "$fails" -eq 0 ] || exit 1
