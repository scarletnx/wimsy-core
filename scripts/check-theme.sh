#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
#
# The sites' theme files as wimsy-dist's themes/theme.py makes them, and the rules around them using only their roles:
#   - tracer/site/style.css: one theme block; outside it no colour of its own, and no var(--x) the block lacks; at most
#     BUDGET bytes; tracer/site/sws/<id>.css, each theme's block, one for each Forgejo theme, each giving those roles;
#   - dist/forge/public/assets/css/theme-*.css: each made by theme.py, and docs/FORGE.md's THEMES line naming them all;
#   - dist/status/gatus.yaml.in and sws/<theme>.css.in: the two colour lines, and the rules using only their roles;
#   - the picker: Tracer's links (tracer/src/site.h), the forge's (dist/forge/templates/custom), the status page's
#     (dist/status/nginx.conf.in) and both nginx's cookie maps naming every theme, and no other.
# The colours themselves and their contrast are wimsy-dist's to check (theme.py fails on a pair that reads under it).
#
#   sh scripts/check-theme.sh [DIR]      DIR: the checkout when not given
set -eu
cd "${1:-$(dirname "$0")/..}"
python3 -I - <<'PY'
import pathlib, re, sys
BUDGET = 8192
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
    rules_vars = set(re.findall(r"var\(--([a-z0-9-]+)\)", text.replace(blocks[0], "")))
    for f in sorted(pathlib.Path("tracer/site/sws").glob("*.css")):
        lacks = rules_vars - set(re.findall(r"--([a-z0-9-]+):", f.read_text()))
        if lacks:
            bad.append(f"{f}: no {', '.join(sorted(lacks))}, which tracer/site/style.css's rules take")
if len(text.encode()) > BUDGET:
    bad.append(f"{style}: {len(text.encode())} bytes, over its {BUDGET}")

made = "made by wimsy-dist's themes/theme.py"
files = sorted(pathlib.Path("dist/forge/public/assets/css").glob("theme-*.css"))
for f in files:
    t = f.read_text()
    if not t.startswith('@import "./theme-forgejo-') or made not in t:
        bad.append(f"{f}: not made by theme.py (no @import of Forgejo's own, or no {made!r} line)")
ids = [f.name[len("theme-"):-len(".css")] for f in files]
blocks_ids = sorted(f.stem for f in pathlib.Path("tracer/site/sws").glob("*.css"))
if blocks_ids != sorted(ids):
    bad.append(f"tracer/site/sws has {blocks_ids}, the Forgejo themes are {sorted(ids)}")
themes = sorted({i.split("-")[0] for i in ids})
picks = {"tracer/src/site.h": r'href=\\"/sws/([a-z]+)\\"', "dist/forge/templates/custom/body_inner_pre.tmpl": r"/sws/([a-z]+)\"",
         "dist/status/nginx.conf.in": r"@SWS_URL@/([a-z]+)\""}
for path, pat in picks.items():
    named = sorted(set(re.findall(pat, pathlib.Path(path).read_text())) - {"auto", "light", "dark"})
    if named != themes:
        bad.append(f"{path}: the picker names {named}, the themes are {themes}")
for path in ("dist/freebsd/tracer-nginx.conf", "dist/status/nginx.conf.in"):
    m = re.search(r"map \$cookie_sws_theme \$sws_theme \{(.*?)\}", pathlib.Path(path).read_text(), re.S)
    # each value's line; nginx's own "default" line is the fallback, a theme named default is written \default
    keys = sorted("".join(k) for k in re.findall(r"^\s*(?:\\(default)|(?!default )([a-z]+)) [a-z]+;$", m.group(1), re.M)) if m else []
    if keys != themes:
        bad.append(f"{path}: the cookie's map takes {keys}, the themes are {themes}")
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
status = sorted(f.name[:-len(".css.in")] for f in pathlib.Path("dist/status/sws").glob("*.css.in"))
if status != themes:
    bad.append(f"dist/status/sws has {status}, the themes are {themes}")
for f in sorted(pathlib.Path("dist/status/sws").glob("*.css.in")):
    t = f.read_text()
    lines = re.findall(r"^html:root(?:\.dark)?\{(--background:.*)\}$", t, re.M)
    if len(lines) != 2:
        bad.append(f"{f}: {len(lines)} html:root{{--background:...}} lines, not 2")
    roles = {r for l in lines for r in re.findall(r"--([a-z0-9-]+):", l)}
    for l in lines:
        t = t.replace(l, "")
    outside_uses(str(f), re.sub(r"url\(data:[^)]*\)", "", t), roles)

for b in bad:
    print(b, file=sys.stderr)
sys.exit(1 if bad else 0)
PY
