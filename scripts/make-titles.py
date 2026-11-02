#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
"""Writes tracer/titles.tsv, each game's ID and name: the Switch's by title ID from blawar/titledb (MIT,
https://github.com/blawar/titledb), the GBA's by game code ("AGB-BPEE") from libretro-database's No-Intro list
(CC-BY-SA-4.0, https://github.com/libretro/libretro-database). Tracer names a report's game with it when the log
names none, and a GBA game by it before the cartridge's 12-letter title.

    python3 scripts/make-titles.py [--from DIR] [--out FILE]

The files are downloaded from their repositories' main branches, or read from DIR when it is given (titledb's region
files as titledb names them, and libretro's as "Nintendo - Game Boy Advance.dat"). Of the Switch only base
applications are kept (a title ID whose low 13 bits are 0: no update, no add-on), each once, named by the first region
that has it in REGIONS' order, English first. A GBA game is named as No-Intro names it, its tags cut. A name keeps its
letters; trademark signs and line breaks go. The file is sorted by ID and names both sources' commits in its first
line."""

import argparse
import json
import os
import re
import sys
import urllib.parse
import urllib.request

REPO = 'blawar/titledb'
LIBRETRO = 'libretro/libretro-database'
GBA = 'metadat/no-intro/Nintendo - Game Boy Advance.dat'
# a game of No-Intro's list: its name with No-Intro's tags, and its game code
NAME = re.compile(r'^\s*name "([^"]+)"', re.M)
CODE = re.compile(r'serial "([0-9A-Z]{4})"')
REGIONS = ['US.en', 'GB.en', 'AU.en', 'JP.ja', 'HK.zh', 'KR.ko', 'DE.de', 'FR.fr', 'ES.es', 'IT.it', 'NL.nl']
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'tracer', 'titles.tsv')
ID = re.compile(r'^[0-9A-Fa-f]{16}$')


def commit(repo=REPO):
    """the repository's newest commit, or 'unknown' without the network"""
    try:
        with urllib.request.urlopen(f'https://api.github.com/repos/{repo}/commits/master', timeout=30) as r:
            return json.load(r)['sha']
    except Exception:  # noqa: BLE001 - a name for the header only
        return 'unknown'


def region(name, source):
    """the region file's entries, or {} when it is missing"""
    if source:
        path = os.path.join(source, f'{name}.json')
        if not os.path.exists(path):
            return {}
        with open(path, 'rb') as f:
            return json.load(f)
    url = f'https://raw.githubusercontent.com/{REPO}/master/{name}.json'
    try:
        with urllib.request.urlopen(url, timeout=600) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return {}
        raise


def gba(source):
    """the GBA's games by code, from No-Intro's list: the first name of each, its tags cut"""
    if source:
        with open(os.path.join(source, os.path.basename(GBA)), encoding='utf-8') as f:
            text = f.read()
    else:
        url = f'https://raw.githubusercontent.com/{LIBRETRO}/master/' + urllib.parse.quote(GBA)
        with urllib.request.urlopen(url, timeout=120) as r:
            text = r.read().decode('utf-8')
    names = {}
    for game in text.split('\ngame (')[1:]:
        name, code = NAME.search(game), CODE.search(game)
        if not name or not code:
            continue
        name = clean(re.sub(r'(\s*[(\[][^()\[\]]*[)\]])+$', '', name.group(1)))
        if name:
            names.setdefault(f'AGB-{code.group(1)}', name)
    return names


def clean(name):
    name = re.sub(r'[™®©]', '', name)
    return re.sub(r'\s+', ' ', name).strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--from', dest='source', help='a folder of titledb region files, in place of the download')
    ap.add_argument('--out', default=OUT)
    args = ap.parse_args()
    names = {}
    for r in REGIONS:
        entries = region(r, args.source)
        kept = 0
        for e in entries.values():
            tid, name = (e or {}).get('id'), (e or {}).get('name')
            if not tid or not name or not ID.match(tid) or int(tid, 16) & 0x1FFF:
                continue
            tid = tid.upper()
            if tid not in names and clean(name):
                names[tid] = clean(name)
                kept += 1
        print(f'{r}: {kept} new of {len(entries)}', file=sys.stderr)
        del entries
    if len(names) < 1000:
        sys.exit(f'make-titles: only {len(names)} titles: titledb was not read whole')
    codes = gba(args.source)
    print(f'GBA: {len(codes)} games', file=sys.stderr)
    if len(codes) < 1000:
        sys.exit(f'make-titles: only {len(codes)} GBA games: libretro\'s serials were not read whole')
    names.update(codes)
    with open(args.out, 'w', encoding='utf-8', newline='\n') as f:
        f.write(f'# ID, name: the Switch\'s base applications from {REPO} (MIT) at {commit()}, the GBA\'s games by code'
                f' from {LIBRETRO} (CC-BY-SA-4.0) at {commit(LIBRETRO)}, by scripts/make-titles.py\n')
        for tid in sorted(names):
            f.write(f'{tid}\t{names[tid]}\n')
    print(f'{args.out}: {len(names)} titles', file=sys.stderr)


if __name__ == '__main__':
    main()
