#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
"""Tracer's code that decides, changed one way at a time, each change a mutant that its tests must fail: the rules over
the runs of edenprobe (tracer/src/rules.c, tests run as test_tracer --rules: a threshold one off, a comparison that
lets one more in, a condition left out), and which files a report keeps and what of them (tracer/src/files.c,
report.c and issues.c, tests run as test_tracer --files: a zip's screenshots, a picture never read as a log, the
paths outside a home cut, the limits said with their values; the worker's locale for a zip's names, by the whole
suite). A mutant the tests pass is a rule nothing holds. Each runs in a copy of the tree in a temporary folder; the
checkout is never written.

    python3 scripts/test-mutants.py [--root DIR] [--cc CC]

Exit 0 when every mutant was caught; 1 when one survived, or one's text is no longer in its file once (said with
which)."""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# each mutant of rules.c: what it does, the text it changes (found once), and what it becomes
RULES = (
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

# each mutant of what a report keeps: what it does, the file under tracer/src, its text (found once), what it becomes
FILES = (
    (
        'a picture in a zip can be its log',
        'files.c',
        'if (inner && (named(inner) == TR_IMAGE || named(inner) == TR_VIDEO)) continue;',
        'if (inner && named(inner) == TR_VIDEO) continue;',
    ),
    (
        "a Mac's fork can be a zip's log",
        'files.c',
        'if (archive_entry_filetype(e) != AE_IFREG || (inner && mac_fork(inner))) continue;',
        'if (archive_entry_filetype(e) != AE_IFREG) continue;',
    ),
    (
        "a Mac's fork is a screenshot",
        'files.c',
        'if (mac_fork(path) || named(path) != TR_IMAGE) continue;',
        'if (named(path) != TR_IMAGE) continue;',
    ),
    ('a PPM is no picture by its name', 'files.c', '".webp", ".gif", ".ppm"};', '".webp", ".gif"};'),
    (
        "a screenshot keeps its zip's folders",
        'files.c',
        "for (leaf = path + strlen(path); leaf > path && leaf[-1] != '/' && leaf[-1] != '\\\\'; --leaf) continue;",
        'leaf = path;',
    ),
    (
        "a zip's names are read in the worker's locale, C",
        'files.c',
        'return utf8 ? uselocale(utf8) : (locale_t)0;',
        'return utf8 ? (locale_t)0 : (locale_t)0;',
    ),
    (
        'a name libarchive could not convert ends the walk',
        'files.c',
        'static int entry(int got) { return got == ARCHIVE_OK || got == ARCHIVE_WARN; }',
        'static int entry(int got) { return got == ARCHIVE_OK; }',
    ),
    (
        'a picture of its max exactly is refused',
        'files.c',
        '        if ((size_t)got > max - out->len) {\n            buf_free(out);\n            return 1;',
        '        if ((size_t)got >= max - out->len) {\n            buf_free(out);\n            return 1;',
    ),
    (
        'a picture one byte past its max is read',
        'files.c',
        '        if ((size_t)got > max - out->len) {\n            buf_free(out);\n            return 1;',
        '        if ((size_t)got > max - out->len + 1) {\n            buf_free(out);\n            return 1;',
    ),
    (
        'no path outside a home is cut',
        'files.c',
        '(len = outside_home(s + i, n - i, &keep))) {',
        '(len = 0 * outside_home(s + i, n - i, &keep))) {',
    ),
    ('/mnt is no root', 'files.c', '{"/mnt/", 1}', '{"/nomnt/", 1}'),
    (
        "the first byte's look passes a drive by",
        'files.c',
        "if ((s[i] == '/' || (i + 1 < n && s[i + 1] == ':' && letter(s[i]))) && path_start(s, i) &&",
        "if ((s[i] == '/') && path_start(s, i) &&",
    ),
    (
        "a mount's own name stays",
        'files.c',
        'if (folders >= names && file_name(s + start, i - start)) {',
        'if (file_name(s + start, i - start)) {',
    ),
    (
        "a folder's name stays as a file's",
        'files.c',
        'if (folders >= names && file_name(s + start, i - start)) {',
        'if (folders >= names) {',
    ),
    ("a drive's Users folder is cut", 'files.c', '{"users", "windows", ', '{"windows", '),
    ('Windows is cut', 'files.c', '{"users", "windows", ', '{"users", '),
    ("WSL's drive is a mount", 'files.c', 'return left_on_drive(s + 7, n - 7) ? 0 : 7;', 'return 0;'),
    (
        'a folder named mnt inside a path is a root',
        'files.c',
        'return !(letter(c) || digit(c) || strchr("._-~/\\\\", c));',
        'return 1;',
    ),
    (
        'a blank before another path ends no name',
        'files.c',
        "if (s[i] == ' ' && path_next(s + i + 1, n - i - 1)) break;",
        'continue;',
    ),
    (
        "a JSON string's doubled backslash is halved",
        'files.c',
        'while (keep < len && sep > 0 && path_sep(s[i + sep - 1], 1)) --sep;',
        'if (keep < len) sep = keep - 1;',
    ),
    (
        "the line's blanks go with a name that goes",
        'files.c',
        "while (i > start && s[i - 1] == ' ') --i;",
        '',
    ),
    (
        "a zip's screenshots are not taken",
        'report.c',
        'if (zip_named(a->name)) take_zip_images(k, dirfd, a->name, &got, !logged);',
        'if (0 && zip_named(a->name)) take_zip_images(k, dirfd, a->name, &got, !logged);',
    ),
    (
        'a picture attached as a log is read as one',
        'report.c',
        'if (tr_image_ext(text.text, text.len)) { /* a picture, whatever its name says: never read as a log */',
        'if (0) {',
    ),
    (
        "a zip's screenshots go past the room",
        'report.c',
        '        if (full(k, TR_IMAGE)) {\n            if (more++ < 3)',
        '        if (0) {\n            if (more++ < 3)',
    ),
    ('the screenshots past the room are not said', 'report.c', '    if (more) {', '    if (0) {'),
    (
        'a zip is not downloaded once its logs are full',
        'report.c',
        'if (kind == TR_LOG && zip_named(a->name) && !full(k, TR_IMAGE)) return NULL;',
        'if (0 && kind == TR_LOG && zip_named(a->name) && !full(k, TR_IMAGE)) return NULL;',
    ),
    ("a zip's log is read once the logs are full", 'report.c', 'int logged = !full(k, TR_LOG);', 'int logged = 1;'),
    (
        "a screenshot's size is not said",
        'report.c',
        'return rei_format("it is %.1f MiB, over the %zu MiB a %s may be", bytes / (1 << 20), max_of(kind) >> 20,',
        'return rei_format("over %.0f%zu MiB%s", bytes * 0, max_of(kind) >> 20,',
    ),
    (
        "the homes' ~ comes before the paths' cut",
        'report.c',
        '    shown = tr_public_log(text.text, text.len, &n); '
        "/* before the homes' ~, which would keep /media/<user>'s folders */\n"
        '    buf_free(&text);\n'
        '    tilded = snx_logscan_tilde(shown, n, &n);\n'
        '    free(shown);',
        '    shown = snx_logscan_tilde(text.text, text.len, &n);\n'
        '    buf_free(&text);\n'
        '    tilded = tr_public_log(shown, n, &n);\n'
        '    free(shown);',
    ),
    (
        'the issue has no screenshots',
        'issues.c',
        '    shots(&b, r->id, files, nfiles, url);',
        '    if (0) shots(&b, r->id, files, nfiles, url);',
    ),
)

# what only the whole suite holds: the worker, a process of its own shut in its folder (test_worker)
WORKER = (
    (
        "the worker makes a zip's locale once shut in, where its files do not open",
        'worker.c',
        '    if (tr_names_ready()) rei_log(',
        '    if (0 && tr_names_ready()) rei_log(',
    ),
)

# each group: its mutants, as (what, file, old, new), and test_tracer's arguments for the tests that must catch them
GROUPS = (
    (tuple((what, 'rules.c', old, new) for what, old, new in RULES), ['--rules']),
    (FILES, ['--files']),
    (WORKER, []),
)


def run(cmd, cwd):
    # a failing check may print a picture's bytes: they are read as text all the same
    return subprocess.run(
        cmd, check=False, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace'
    )


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
        # without -Werror: a mutant often leaves a helper or a variable unused, which says nothing of what it tests
        make = ['make', '-s', 'tests/test_tracer', 'CC=' + args.cc, 'CFLAGS=-O0 -g', 'WARN=-std=c99']
        built = run(make, work)
        for _, tests in GROUPS:
            if built.returncode or run(['./tests/test_tracer', *tests], work).returncode:
                print(
                    f'the tree as it is does not build or pass test_tracer {" ".join(tests)}:\n' + built.stdout,
                    file=sys.stderr,
                )
                return 1
        failed = count = 0
        for mutants, tests in GROUPS:
            for what, name, old, new in mutants:
                source = work / 'src' / name
                original = source.read_text()
                count += 1
                if original.count(old) != 1:
                    print(f'stale: {what}: {name} has "{old}" {original.count(old)} times, not once')
                    failed = 1
                    continue
                source.write_text(original.replace(old, new))
                built = run(make, work)
                if built.returncode:
                    print(f'stale: {what}: the mutant does not build:\n{built.stdout}')
                    failed = 1
                elif run(['./tests/test_tracer', *tests], work).returncode:
                    print(f'killed: {what}')
                else:
                    print(f'SURVIVED: {what}')
                    failed = 1
                source.write_text(original)
        print(f'{count} mutants, {"one or more left" if failed else "every one caught"}')
        return failed


if __name__ == '__main__':
    sys.exit(main())
