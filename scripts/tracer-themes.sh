#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
#
# Tracer's stylesheet in each theme and mode, and each theme's Wimsy, for the nginx to give by the reader's pick
# (docs/THEMES.md): OUT/<id>.css is tracer/site/style.css with tracer/site/sws/<id>.css in its theme block, and
# OUT/logo-<theme>.svg is the theme's Wimsy (Wimsy's own is the forge's wimsy.svg; Fuddles' is Tracer's tracer.svg).
#
#   sh scripts/tracer-themes.sh OUT [DIR]      DIR: the checkout when not given
set -eu
[ $# -ge 1 ] || { echo "usage: $0 OUT [DIR]" >&2; exit 64; }
out=$1
cd "${2:-$(dirname "$0")/..}"
mkdir -p "$out"
for block in tracer/site/sws/*.css; do
	id=$(basename "$block" .css)
	# awk: the stylesheet's lines, those between the theme lines swapped for the block
	awk -v block="$block" '
	/^\/\* theme:begin \*\/$/ { print; while ((getline line < block) > 0) print line; skip = 1; next }
	/^\/\* theme:end \*\/$/ { skip = 0 }
	!skip' tracer/site/style.css > "$out/$id.css"
	grep -q 'theme:end' "$out/$id.css" || { echo "tracer/site/style.css has no theme block" >&2; exit 1; }
done
for logo in tracer/site/sws/logo-*.svg; do
	cp "$logo" "$out/"
done
cp dist/forge/public/assets/img/wimsy.svg "$out/logo-wimsy.svg"
cp tracer/site/tracer.svg "$out/logo-fuddles.svg"
