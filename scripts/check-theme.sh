#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
#
# The sites' theme files as wimsy-dist's themes/theme.py makes them, and the rules around them using only their roles:
#   - tracer/site/style.css: one theme block; outside it no colour of its own, and no var(--x) the block lacks; at most
#     BUDGET bytes;
#   - dist/forge/public/assets/css/theme-*.css: each made by theme.py, and docs/FORGE.md's THEMES line naming them all;
#   - dist/status/gatus.yaml.in: the two colour lines, and the state rules using only the roles they hold.
# The colours themselves and their contrast are wimsy-dist's to check (theme.py fails on a pair that reads under it).
#
#   sh scripts/check-theme.sh [DIR]      DIR: the checkout when not given
set -eu
cd "${1:-$(dirname "$0")/..}"
python3 -I - <<'PY'
import pathlib, re, sys
BUDGET = 7168
bad = []
colour = re.compile(r"#[0-9a-fA-F]{3,8}\b|\b(?:rgba?|hsla?)\(|:\s*(?:white|black|red|green|blue|yellow|gr[ae]y|orange|purple|pink)\b")

def outside_uses(where, text, roles):
    for m in colour.finditer(text):
        bad.append(f"{where}: a colour of its own, {m.group(0)!r}: take a role")
    for v in sorted(set(re.findall(r"var\(--([a-z0-9-]+)\)", text)) - roles):
        bad.append(f"{where}: var(--{v}) is no role of the theme's")

style = pathlib.Path("tracer/site/style.css")
text = style.read_text()
blocks = re.findall(r"/\* theme:begin \*/\n(.*?)/\* theme:end \*/", text, re.S)
if len(blocks) != 1:
    bad.append(f"{style}: {len(blocks)} theme blocks, not 1")
else:
    roles = set(re.findall(r"--([a-z0-9-]+):", blocks[0]))
    if not roles:
        bad.append(f"{style}: an empty theme block: run wimsy-dist's themes/theme.py --core")
    outside_uses(style, text.replace(blocks[0], ""), roles)
if len(text.encode()) > BUDGET:
    bad.append(f"{style}: {len(text.encode())} bytes, over its {BUDGET}")

made = "made by wimsy-dist's themes/theme.py"
files = sorted(pathlib.Path("dist/forge/public/assets/css").glob("theme-*.css"))
for f in files:
    t = f.read_text()
    if not t.startswith('@import "./theme-forgejo-') or made not in t:
        bad.append(f"{f}: not made by theme.py (no @import of Forgejo's own, or no {made!r} line)")
ids = [f.name[len("theme-"):-len(".css")] for f in files]
line = re.search(r"^\s*THEMES = (.*)$", pathlib.Path("docs/FORGE.md").read_text(), re.M)
listed = [x.strip() for x in line.group(1).split(",")] if line else []
if sorted(listed) != sorted(ids):
    bad.append(f"docs/FORGE.md's THEMES line names {listed}, the theme files are {ids}")

yml = pathlib.Path("dist/status/gatus.yaml.in").read_text()
lines = re.findall(r"^    html:root(?:\.dark)?\{(--background:.*)\}$", yml, re.M)
if len(lines) != 2:
    bad.append(f"dist/status/gatus.yaml.in: {len(lines)} html:root{{--background:...}} lines, not 2")
roles = {r for l in lines for r in re.findall(r"--([a-z0-9-]+):", l)}
css = yml.split("custom-css: |", 1)[-1].split("\nendpoints:", 1)[0]
for l in lines:
    css = css.replace(l, "")
outside_uses("dist/status/gatus.yaml.in", re.sub(r"url\(data:[^)]*\)", "", css), roles)

for b in bad:
    print(b, file=sys.stderr)
sys.exit(1 if bad else 0)
PY
