#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
"""Tracer's rules over the runs of edenprobe (tracer/src/rules.c) changed one way at a time, each change a mutant that
its tests (tracer/tests/test_tracer.c's test_rules, run as test_tracer --rules) must fail: a threshold one off, a
comparison that lets one more in, a condition left out. A mutant the tests pass is a rule nothing holds. Each runs in
a copy of the tree in a temporary folder; the checkout is never written.

    python3 scripts/test-rule-mutants.py [--root DIR] [--cc CC]

Exit 0 when every mutant was caught; 1 when one survived, or one's text is no longer in rules.c (said with which)."""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# each mutant: what it does, the text of rules.c it changes (found once), and what it becomes
MUTANTS = (
    (
        'two failing reports make a candidate',
        'if (fails < RULE_FAIL_REPORTS || people < RULE_FAIL_PEOPLE)',
        'if (fails < RULE_FAIL_REPORTS - 1 || people < RULE_FAIL_PEOPLE)',
    ),
    (
        'one person makes a candidate',
        'if (fails < RULE_FAIL_REPORTS || people < RULE_FAIL_PEOPLE)',
        'if (fails < RULE_FAIL_REPORTS || people < RULE_FAIL_PEOPLE - 1)',
    ),
    (
        'each failing report is another person',
        'for (k = 0; k < people && posters[k] != who; ++k) continue;',
        'k = people;',
    ),
    (
        'two passing reports clear the driver',
        'if (npass < RULE_PASS_REPORTS || drivers < RULE_PASS_DRIVERS)',
        'if (npass < RULE_PASS_REPORTS - 1 || drivers < RULE_PASS_DRIVERS)',
    ),
    (
        'one other driver clears it',
        'if (npass < RULE_PASS_REPORTS || drivers < RULE_PASS_DRIVERS)',
        'if (npass < RULE_PASS_REPORTS || drivers < RULE_PASS_DRIVERS - 1)',
    ),
    (
        'a passing build eight days after counts',
        'near = gap <= RULE_DAYS * 86400.0 && gap >= -RULE_DAYS * 86400.0;',
        'near = gap <= (RULE_DAYS + 1) * 86400.0 && gap >= -RULE_DAYS * 86400.0;',
    ),
    (
        'a passing build eight days before counts',
        'near = gap <= RULE_DAYS * 86400.0 && gap >= -RULE_DAYS * 86400.0;',
        'near = gap <= RULE_DAYS * 86400.0 && gap >= -(RULE_DAYS + 1) * 86400.0;',
    ),
    ('the failing driver clears itself', 'run->p.driver_id == d || e[j].result != ST_PASS', 'e[j].result != ST_PASS'),
    ('a passing version does not end the range', 'if (vp) break;', 'if (vp && 0) break;'),
    (
        'another system joins the range',
        'if (scarletnx(p) && p->driver_id == d && !strcmp(p->os, os) &&',
        'if (scarletnx(p) && p->driver_id == d && os &&',
    ),
    (
        "Eden's failures count",
        'if (scarletnx(p) && p->driver_id == d && !strcmp(p->os, os) &&',
        'if (p->driver_id == d && !strcmp(p->os, os) &&',
    ),
    ('a mixed end is not said', 'c->mixed |= c->high && on[j].result == ST_FAIL;', 'c->mixed |= 0;'),
    (
        "four drivers make it ScarletNX's",
        'if (!(failing >= RULE_GENERAL_DRIVERS || (nd >= 2 && failing == nd)))',
        'if (!(failing >= RULE_GENERAL_DRIVERS + 1 || (nd >= 2 && failing == nd)))',
    ),
    (
        "one driver seen makes it ScarletNX's",
        'if (!(failing >= RULE_GENERAL_DRIVERS || (nd >= 2 && failing == nd)))',
        'if (!(failing >= RULE_GENERAL_DRIVERS || (nd >= 1 && failing == nd)))',
    ),
    (
        "Eden's drivers count as seen",
        'if (!scarletnx(p) || p->driver_id <= 0 || (e[i].result != ST_PASS',
        'if (p->driver_id <= 0 || (e[i].result != ST_PASS',
    ),
    (
        "ScarletNX's own failure is a candidate too",
        'if (!general(out, &gcap, results[i].name, e + a, b - a))',
        'if (general(out, &gcap, results[i].name, e + a, b - a) || 1)',
    ),
    (
        'a build that passes and fails passed',
        'if (last >= 0 && last < nb - 1 && b[last].passes && !b[last].fails)',
        'if (last >= 0 && last < nb - 1 && b[last].passes)',
    ),
    (
        'a newest build that passes once fails',
        'last >= 0 && b[last].fails && !b[last].passes; --last',
        'last >= 0 && b[last].fails; --last',
    ),
    (
        'a -dirty build is a build',
        'strcmp(q->os, p->os) || !published(q->commit) || q->built <= 0 ||',
        'strcmp(q->os, p->os) || !published(q->commit ? "0000000" : NULL) || q->built <= 0 ||',
    ),
)


def run(cmd, cwd):
    return subprocess.run(cmd, check=False, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--root', type=Path, default=ROOT, help='the tree (the checkout this script is in)')
    ap.add_argument('--cc', default=os.environ.get('CC', 'cc'), help='the compiler (CC, else cc)')
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='tracer-mutants-') as tmp:
        tree = Path(tmp)
        for part in ('rei', 'tracer'):
            shutil.copytree(
                args.root / part,
                tree / part,
                ignore=shutil.ignore_patterns('*.o', 'test_tracer', 'tracer', 'tracer-mcp'),
            )
        work = tree / 'tracer'
        rules = work / 'src' / 'rules.c'
        original = rules.read_text()
        make = ['make', '-s', 'tests/test_tracer', 'CC=' + args.cc, 'CFLAGS=-O0 -g']
        built = run(make, work)
        if built.returncode or run(['./tests/test_tracer', '--rules'], work).returncode:
            print('the tree as it is does not build or pass its rules tests:\n' + built.stdout, file=sys.stderr)
            return 1
        failed = 0
        for what, old, new in MUTANTS:
            if original.count(old) != 1:
                print(f'stale: {what}: rules.c has "{old}" {original.count(old)} times, not once')
                failed = 1
                continue
            rules.write_text(original.replace(old, new))
            built = run(make, work)
            if built.returncode:
                print(f'stale: {what}: the mutant does not build:\n{built.stdout}')
                failed = 1
            elif run(['./tests/test_tracer', '--rules'], work).returncode:
                print(f'killed: {what}')
            else:
                print(f'SURVIVED: {what}')
                failed = 1
        rules.write_text(original)
        print(f'{len(MUTANTS)} mutants, {"one or more left" if failed else "every one caught"}')
        return failed


if __name__ == '__main__':
    sys.exit(main())
