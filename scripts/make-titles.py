#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
"""Writes tracer/titles.tsv, each game's title ID and name, from blawar/titledb (MIT,
https://github.com/blawar/titledb): Tracer names a report's game with it when the log names none.

    python3 scripts/make-titles.py [--from DIR] [--out FILE]

The region files are downloaded from titledb's main branch, or read from DIR when it is given (the files as
titledb names them). Only base applications are kept (a title ID whose low 13 bits are 0: no update, no add-on), each
once, named by the first region that has it in REGIONS' order, English first. A name keeps its letters; trademark
signs and line breaks go. The file is sorted by title ID and names titledb's commit in its first line."""

import argparse
import json
import os
import re
import sys
import urllib.request

REPO = 'blawar/titledb'
REGIONS = ['US.en', 'GB.en', 'AU.en', 'JP.ja', 'HK.zh', 'KR.ko', 'DE.de', 'FR.fr', 'ES.es', 'IT.it', 'NL.nl']
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'tracer', 'titles.tsv')
ID = re.compile(r'^[0-9A-Fa-f]{16}$')


def commit():
    """titledb's newest commit, or 'unknown' without the network"""
    try:
        with urllib.request.urlopen(f'https://api.github.com/repos/{REPO}/commits/master', timeout=30) as r:
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
    with open(args.out, 'w', encoding='utf-8', newline='\n') as f:
        f.write(f'# title ID, name: base applications from {REPO} (MIT) at {commit()}, by scripts/make-titles.py\n')
        for tid in sorted(names):
            f.write(f'{tid}\t{names[tid]}\n')
    print(f'{args.out}: {len(names)} titles', file=sys.stderr)


if __name__ == '__main__':
    main()
