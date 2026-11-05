#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
"""check-docs.py against checkouts made here, a temporary folder each: each rule fails where it should, naming the
file, and passes where it should.

    python3 scripts/test-check-docs.py
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

CHECK = Path(__file__).resolve().with_name('check-docs.py')
# a checkout that passes: a front page, the docs' index and one document
BASE = {
    'README.md': '# R\n\n[The docs](docs/README.md)\n',
    'docs/README.md': '# D\n\n- [A](./A.md)\n',
    'docs/A.md': '# A\n\nWhat it is.\n',
}
REI_DOC = '# ReiWimsy\n\n| Key | Default | What |\n|---|---|---|\n| `REIWIMSY_TOKEN` | none: needed | the token |\n'
REI_RC = ': ${reiwimsy_enable:=NO}\n: ${reiwimsy_url:=}\n'
REI_VARIABLES = (
    '\n| Variable | Default | What |\n|---|---|---|\n| `reiwimsy_enable` | `NO` | on |\n'
    '| `reiwimsy_url` | none: needed | where |\n'
)
LONG = '# L\n\n' + ''.join(f'## Part {i}\n\n' + 'Line.\n' * 30 + '\n' for i in range(1, 5))

failed = 0


def run(files, *args):
    """check-docs.py on a checkout of BASE and files (a None drops a file of BASE): its exit status, its stderr and
    the checkout's files after it"""
    root = Path(tempfile.mkdtemp())
    try:
        for name, text in {**BASE, **files}.items():
            if text is None:
                continue
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding='utf-8')
        done = subprocess.run(
            [sys.executable, '-I', str(CHECK), '--root', str(root), *args], check=False, capture_output=True, text=True
        )
        after = {p.relative_to(root).as_posix(): p.read_text(encoding='utf-8') for p in root.rglob('*') if p.is_file()}
        return done.returncode, done.stderr, after
    finally:
        shutil.rmtree(root)


def case(name, files, want, says=None, *args):
    """want: the exit status; says: a text its stderr holds (a failure names its file)"""
    global failed
    status, err, _ = run(files, *args)
    ok = status == want and (says is None or says in err)
    print(f'{"ok  " if ok else "FAIL"} {name}' + ('' if ok else f': exit {status}, not {want}; stderr:\n{err}'))
    failed += not ok


# dates
case('a date in the text fails', {'docs/A.md': '# A\n\nThe bots moved on 2026-10-30.\n'}, 1, 'docs/A.md:3:')
case('a date in a fenced block passes', {'docs/A.md': '# A\n\n```\nlog-2026-10-28.txt\n```\n'}, 0)
case('a date in an indented block passes', {'docs/A.md': '# A\n\n    ls log-2026-10-28.txt\n'}, 0)
case('a date in inline code passes', {'docs/A.md': '# A\n\nA log is `console-2026-10-28.log`.\n'}, 0)
case('a version is no date', {'docs/A.md': '# A\n\nConcord 3.0.1 and Gatus 5.38.0.\n'}, 0)
case('a date in a list line indented 4 after text fails', {'docs/A.md': '# A\n\n- one\n    on 2026-10-30\n'}, 1)

# links
case('a link to a file that is not there fails', {'docs/A.md': '# A\n\n[B](./B.md)\n'}, 1, 'docs/A.md:3: ./B.md')
case('a link to a heading passes', {'docs/A.md': "# A\n\n[j](#reiwimsys-jail)\n\n## ReiWimsy's jail\n"}, 0)
case('a link to a heading that is not there fails', {'docs/A.md': '# A\n\n[j](#nothing)\n'}, 1, '#nothing')
case("a link to another document's heading passes", {'README.md': '# R\n\n[x](docs/A.md#a)\n'}, 0)
case('a link in inline code is no link', {'docs/A.md': '# A\n\n`[B](./B.md)`\n'}, 0)
case('a web address is not followed', {'docs/A.md': '# A\n\n[x](https://example.org/nothing)\n'}, 0)
case("a line past a file's end fails", {'docs/A.md': '# A\n\n[x](../LICENSE#L5)\n', 'LICENSE': 'one\n'}, 1, 'line 5')
case('a link out of the checkout fails', {'docs/A.md': '# A\n\n[x](../../x.md)\n'}, 1, 'outside the checkout')

# indexes
case('a long document without an index fails', {'docs/A.md': LONG}, 1, 'no ## Index')
status, err, after = run({'docs/A.md': LONG}, '--index')
written = after.get('docs/A.md', '')
ok = '## Index\n\n* [Part 1](#part-1)\n' in written and run({'docs/A.md': written})[0] == 0
print(f'{"ok  " if ok else "FAIL"} --index writes the index, which then passes')
failed += not ok
case('an index that lists another section fails', {'docs/A.md': written.replace('* [Part 1](#part-1)', '* [P](#p)')}, 1)
case('a short document needs no index', {'docs/A.md': '# A\n\n## B\n\n## C\n\n## D\n'}, 0)

# the docs' index
case('a document docs/README.md does not link fails', {'docs/B.md': '# B\n'}, 1, 'does not link B.md')

# settings
case(
    'a key read with no row fails',
    {'rei/src/main.c': 'setting("REIWIMSY_TOKEN");', 'docs/REIWIMSY.md': '# R\n'},
    1,
    'rei/src/main.c:1: `REIWIMSY_TOKEN`',
)
case(
    'a key read with its row passes',
    {
        'rei/src/main.c': 'x = setting("REIWIMSY_TOKEN");',
        'docs/REIWIMSY.md': REI_DOC,
        'docs/README.md': BASE['docs/README.md'] + '- [R](./REIWIMSY.md)\n',
    },
    0,
)
case(
    'a program without its document fails',
    {'rei/src/main.c': 'setting("REIWIMSY_TOKEN");'},
    1,
    'docs/REIWIMSY.md: is not there',
)
case(
    'a row of a key not read fails',
    {
        'rei/src/main.c': 'setting("REIWIMSY_TOKEN");',
        'docs/REIWIMSY.md': REI_DOC + '| `REIWIMSY_GONE` | none | was |\n',
        'docs/README.md': BASE['docs/README.md'] + '- [R](./REIWIMSY.md)\n',
    },
    1,
    '`REIWIMSY_GONE` is no key',
)
TRACER = {'docs/README.md': BASE['docs/README.md'] + '- [T](./TRACER.md)\n'}
case(
    'a default the code gives, missing from its cell, fails',
    {
        **TRACER,
        'tracer/src/main.c': 'setting("TRACER_DB", "/var/db/t.db");',
        'docs/TRACER.md': '# T\n\n| Key | Default |\n|---|---|\n| `TRACER_DB` | the database |\n',
    },
    1,
    'does not say `/var/db/t.db`',
)
case(
    'a default the code gives, in its cell, passes',
    {
        **TRACER,
        'tracer/src/main.c': 'setting("TRACER_DB", "/var/db/t.db");',
        'docs/TRACER.md': '# T\n\n| Key | Default |\n|---|---|\n| `TRACER_DB` | `/var/db/t.db` |\n',
    },
    0,
)
case(
    "tracer-mcp's keys are its own document's",
    {
        **TRACER,
        'tracer/src/main.c': '',
        'tracer/src/mcp_main.c': 'setting("TRACER_MCP_LISTEN", "127.0.0.1:1");',
        'docs/TRACER.md': '# T\n',
    },
    1,
    'docs/TRACER-MCP.md',
)
case(
    "AccordWimsy's table of keys is read",
    {
        'accordwimsy/src/config.c': '{"ACCORDWIMSY_GIT", ROOM(git), "/usr/local/bin/git"},',
        'docs/ACCORDWIMSY.md': '# A\n',
        'docs/README.md': BASE['docs/README.md'] + '- [AW](./ACCORDWIMSY.md)\n',
    },
    1,
    '`ACCORDWIMSY_GIT` (default',
)
REI = {
    'rei/src/main.c': 'setting("REIWIMSY_TOKEN");',
    'docs/README.md': BASE['docs/README.md'] + '- [R](./REIWIMSY.md)\n',
}
case(
    'an rc variable with no row fails',
    {
        **REI,
        'dist/freebsd/reiwimsy.rc': REI_RC,
        'docs/REIWIMSY.md': REI_DOC + 'Its service: `dist/freebsd/reiwimsy.rc`.\n',
    },
    1,
    'dist/freebsd/reiwimsy.rc:1: `reiwimsy_enable`',
)
case(
    'rc variables with their defaults pass',
    {
        **REI,
        'dist/freebsd/reiwimsy.rc': REI_RC,
        'docs/REIWIMSY.md': REI_DOC + REI_VARIABLES + '\nIts service: `dist/freebsd/reiwimsy.rc`.\n',
    },
    0,
)
case(
    "an rc variable's other default fails",
    {
        **REI,
        'dist/freebsd/reiwimsy.rc': REI_RC,
        'docs/REIWIMSY.md': REI_DOC + REI_VARIABLES.replace('`NO`', '`YES`') + '\n`dist/freebsd/reiwimsy.rc`\n',
    },
    1,
    'does not say `NO`',
)
case(
    "an rc variable's empty default said as a value fails",
    {
        **REI,
        'dist/freebsd/reiwimsy.rc': REI_RC,
        'docs/REIWIMSY.md': REI_DOC + REI_VARIABLES.replace('none: needed', '`x`') + '\n`dist/freebsd/reiwimsy.rc`\n',
    },
    1,
    'does not say none or empty',
)
case(
    'a key an env sample names, not read, fails',
    {
        **REI,
        'docs/REIWIMSY.md': REI_DOC,
        'dist/freebsd/reiwimsy.env.sample': 'REIWIMSY_TOKEN=\n# REIWIMSY_BOGUS=x   what\n',
    },
    1,
    'dist/freebsd/reiwimsy.env.sample:2: REIWIMSY_BOGUS',
)

# scripts
case('a script no document names fails', {'scripts/x.sh': 'true\n'}, 1, 'scripts/x.sh: is named in no document')
case('a script a document names passes', {'scripts/x.sh': 'true\n', 'docs/A.md': '# A\n\n`sh scripts/x.sh`.\n'}, 0)
case(
    'a script named only as a longer name fails',
    {'dist/s/push.sh': 'true\n', 'docs/A.md': '# A\n\n`dist/s/push.sh.in`\n'},
    1,
    'dist/s/push.sh: is named',
)
case(
    "a script named at a sentence's end passes",
    {'dist/s/push.sh': 'true\n', 'docs/A.md': '# A\n\nRun dist/s/push.sh.\n'},
    0,
)

sys.exit(1 if failed else 0)
