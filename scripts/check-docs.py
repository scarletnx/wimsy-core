#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
"""The documentation held to the checkout, each failure a line that names its file and line:

- dates: no date (YYYY-MM-DD) in README.md or docs/ outside code. A doc says what is; when and how it came to be goes
  in the commit message
- links: each relative link of README.md and docs/ (Markdown's [text](target) and [name]: target, HTML's src= and
  href=, outside code) names a file or folder that is there. Its anchor is a heading of the Markdown it names, as
  GitHub makes one (lowercase, punctuation dropped but - and _, each space a -, a repeated one numbered), or an
  <a name=> or id= in it; on another file, #L<n> or #L<n>-L<m>, lines the file has
- indexes: a document over 100 lines with 3 or more ## and ### sections has an ## Index of them, and any document's
  ## Index lists its sections as they are. --index writes them: an index where it stands, a new one under the title
- the docs' own index: docs/README.md links every document of docs/
- settings: each key a program reads (a literal given to setting, getenv, get, channel_id or ids, or a row of
  AccordWimsy's table of keys) has a row in a | Key | Default | table of the program's document, and each key a row
  names is one it reads; a default the code gives a key is in its row's Default cell, as `value`. Each variable of
  the program's rc script (: ${name:=default}) has a row in a | Variable | Default | table there, with the script's
  default as `value` (none or empty for an empty one), and each variable a row names is one the script has
- samples: each key an env file's sample (dist/freebsd/*.env.sample) names is one its program reads
- scripts: each script of scripts/ and dist/ (.sh, .py, .rc) is named by its path in README.md or docs/

    python3 scripts/check-docs.py [--root DIR] [--index]
"""

import argparse
import bisect
import re
import sys
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
DOCS = ('README.md', 'docs')
INDEX_LINES = 100  # a document longer than this carries an ## Index
INDEX_SECTIONS = 3  # of at least this many sections
# each program: its document, the sources it reads its keys in (a ! drops a file the patterns before it took), its rc
# scripts, and its env file's sample
PROGRAMS = (
    ('docs/REIWIMSY.md', ('rei/src/*.c',), ('dist/freebsd/reiwimsy.rc',), 'dist/freebsd/reiwimsy.env.sample'),
    (
        'docs/TRACER.md',
        ('tracer/src/*.c', '!tracer/src/mcp_main.c'),
        ('dist/freebsd/tracer.rc',),
        'dist/freebsd/tracer.env.sample',
    ),
    ('docs/TRACER-MCP.md', ('tracer/src/mcp_main.c',), ('dist/freebsd/tracer-mcp.rc',), None),
    ('docs/ACCORDWIMSY.md', ('accordwimsy/src/*.c',), (), 'dist/freebsd/accordwimsy.env.sample'),
)
SCRIPT_DIRS = ('scripts', 'dist')
SCRIPT_SUFFIXES = ('.sh', '.py', '.rc')

FENCE = re.compile(r'^\s*(```|~~~)')
HEADING = re.compile(r'^(#{1,6})\s+(.*?)\s*#*\s*$')
INDEX_LINK = re.compile(r'^\s*[*-] \[.*\]\(#[^)]*\)\s*$')
CODE_SPAN = re.compile(r'(`+)(.+?)\1')
REFERENCE = re.compile(r'^ {0,3}\[[^\]]+\]:\s*(<[^>]*>|\S+)', re.MULTILINE)
HTML_LINK = re.compile(r'\b(?:src|href)\s*=\s*"([^"]*)"')
HTML_ANCHOR = re.compile(r'<a\s[^>]*\bname\s*=\s*"([^"]+)"|\bid\s*=\s*"([^"]+)"')
SCHEME = re.compile(r'^[A-Za-z][A-Za-z0-9+.-]*:')
LINE_ANCHOR = re.compile(r'^L(\d+)(?:-L(\d+))?$')
DATE = re.compile(r'(?<![0-9])20[0-9][0-9]-[01][0-9]-[0-3][0-9](?![0-9])')
# a key read: its name and, from setting(NAME, default), its default ("..." or NULL)
READ = re.compile(r'(?<![\w.>])(?:setting|getenv|get|channel_id|ids)\(\s*"([A-Z][A-Z0-9_]*)"\s*(?:,\s*("[^"]*"|NULL))?')
# a row of AccordWimsy's table of keys: {"NAME", ROOM(field), "default"}
TABLE_KEY = re.compile(r'\{\s*"([A-Z][A-Z0-9_]*)",\s*ROOM\([^)]*\),\s*"([^"]*)"\s*\}')
RC_VARIABLE = re.compile(r'^:\s*\$\{(\w+):=([^}]*)\}\s*$', re.MULTILINE)
SAMPLE_KEY = re.compile(r'^#?\s*([A-Z][A-Z0-9_]*)=', re.MULTILINE)


class Failures(list):
    def add(self, path, line, text):
        self.append(f'{path}:{line}: {text}' if line else f'{path}: {text}')


# ---- Markdown ----
def code_lines(text):
    """for each line of text, whether it is code: inside a ``` or ~~~ fence (the fence itself too), or indented by
    4 spaces or a tab after a blank line or another such line, as an indented code block is"""
    out, fenced, indented, blank = [], False, False, True
    for line in text.split('\n'):
        if FENCE.match(line):
            fenced = not fenced
            out.append(True)
            indented, blank = False, False
            continue
        if fenced:
            out.append(True)
            continue
        if not line.strip():
            out.append(indented)
            blank = True
            continue
        indented = (blank or indented) and line.startswith(('    ', '\t'))
        out.append(indented)
        blank = False
    return out


def prose_lines(text):
    """the lines of text with code blocks and inline code blanked, so neither links nor makes headings"""
    return [
        '' if code else CODE_SPAN.sub(lambda m: ' ' * len(m.group(0)), line)
        for line, code in zip(text.split('\n'), code_lines(text))
    ]


def headings(text):
    """(line index, level, text) of each heading outside fenced code"""
    out, fenced = [], False
    for i, line in enumerate(text.split('\n')):
        if FENCE.match(line):
            fenced = not fenced
            continue
        m = None if fenced else HEADING.match(line)
        if m:
            out.append((i, len(m.group(1)), m.group(2)))
    return out


def slug(text, seen):
    """the anchor GitHub gives a heading: its text as shown (links' words, no code marks or emphasis), lowercase,
    letters, digits, - and _ kept, each space a -, numbered from -1 when an earlier heading had it"""
    text = re.sub(r'!?\[([^\]]*)\]\([^)]*\)', r'\1', text)
    text = re.sub(r'<[^>]+>', '', text)
    text = re.sub(r'[`*]', '', text)
    text = re.sub(r'(?<![A-Za-z0-9])_+|_+(?![A-Za-z0-9])', '', text)  # emphasis; a_b keeps its _
    base = re.sub(r'[^\w\- ]', '', text.lower()).replace(' ', '-')
    n = seen.get(base, 0)
    seen[base] = n + 1
    return base if n == 0 else f'{base}-{n}'


def anchors(text):
    """every anchor a Markdown document has: its headings' and its HTML names and ids"""
    seen, out = {}, set()
    for _, _, name in headings(text):
        out.add(slug(name, seen))
    for line in prose_lines(text):
        for m in HTML_ANCHOR.finditer(line):
            out.add(m.group(1) or m.group(2))
    return out


def links(text):
    """(line number, target) of each link of a Markdown document, outside code"""
    lines = prose_lines(text)
    prose = '\n'.join(lines)
    starts = [0]  # where each line starts in prose
    for line in lines[:-1]:
        starts.append(starts[-1] + len(line) + 1)

    def number(offset):
        return bisect.bisect_right(starts, offset)

    out = []
    for m in re.finditer(r'\]\(', prose):
        # the target runs to the ) that closes the (, a <target> or a "title" aside
        i, depth = m.end(), 1
        while i < len(prose) and depth:
            depth += {'(': 1, ')': -1}.get(prose[i], 0)
            i += 1
        inner = prose[m.end() : i - 1].strip()
        if inner.startswith('<'):
            target = inner[1 : inner.find('>')] if '>' in inner else inner[1:]
        else:
            target = inner.split()[0] if inner.split() else ''
        out.append((number(m.start()), target))
    for m in REFERENCE.finditer(prose):
        out.append((number(m.start()), m.group(1).strip('<>')))
    for m in HTML_LINK.finditer(prose):
        out.append((number(m.start()), m.group(1)))
    return sorted(out)


def in_order(paths):
    """paths in the code point order of their names"""
    return sorted(paths, key=lambda p: p.parts)


def documents(root):
    out = []
    for name in DOCS:
        path = root / name
        if path.is_dir():
            out += in_order(p for p in path.rglob('*.md') if p.is_file())
        elif path.is_file():
            out.append(path)
    return out


def read(path):
    return path.read_text(encoding='utf-8', errors='replace')


# ---- dates ----
def check_dates(root, docs, failures):
    for doc in docs:
        name = doc.relative_to(root).as_posix()
        for number, line in enumerate(prose_lines(read(doc)), 1):
            m = DATE.search(line)
            if m:
                failures.add(
                    name,
                    number,
                    f'{m.group(0).strip()}: a doc says what is, not when it came to be; the date and the story go in '
                    'the commit message',
                )


# ---- links ----
def check_links(root, docs, failures):
    texts = {}

    def text_of(path):
        if path not in texts:
            texts[path] = read(path)
        return texts[path]

    for doc in docs:
        name = doc.relative_to(root).as_posix()
        for line, target in links(text_of(doc)):
            if not target or SCHEME.match(target) or target.startswith('//'):
                continue
            path, _, anchor = target.partition('#')
            path = unquote(path.split('?')[0])
            if path:
                dest = (root / path.lstrip('/')) if path.startswith('/') else (doc.parent / path)
                try:
                    dest.resolve().relative_to(root.resolve())
                except ValueError:
                    failures.add(name, line, f'{target}: outside the checkout')
                    continue
                if not dest.exists():
                    failures.add(name, line, f'{target}: {path} is not there')
                    continue
            else:
                dest = doc
            if not anchor:
                continue
            if dest.is_dir():
                failures.add(name, line, f'{target}: #{anchor} on a folder, which has no anchors')
            elif dest.suffix.lower() == '.md':
                if anchor not in anchors(text_of(dest)):
                    where = 'this document' if dest == doc else dest.resolve().relative_to(root.resolve()).as_posix()
                    failures.add(name, line, f'{target}: {where} has no heading or name with the anchor #{anchor}')
            else:
                m = LINE_ANCHOR.match(anchor)
                if not m:
                    failures.add(name, line, f'{target}: #{anchor} on a file that is not Markdown takes #L<n> alone')
                    continue
                count = len(text_of(dest).splitlines())
                last = int(m.group(2) or m.group(1))
                if last > count:
                    failures.add(name, line, f'{target}: line {last} is past the end of {path} ({count} lines)')


# ---- indexes ----
def index_block(lines):
    """(start, end) of the ## Index heading and the links under it, blank lines around them taken; None for none. A
    section named Index whose text opens with anything but a link is a section, not the index"""
    for i, level, name in headings('\n'.join(lines)):
        if level == 2 and name.strip().lower() == 'index':
            first = next((line for line in lines[i + 1 :] if line.strip()), '')
            if not INDEX_LINK.match(first):
                continue
            end = i + 1
            while end < len(lines) and (not lines[end].strip() or INDEX_LINK.match(lines[end])):
                end += 1
            return i, end
    return None


def indexed(text):
    """text with its ## Index made again: the ## and ### headings after the title, ### under their ##. An index
    already there is replaced where it is, a new one goes right under the title, and a document of fewer than
    INDEX_SECTIONS sections has none"""
    lines = text.split('\n')
    block = index_block(lines)
    if block:
        at, end = block
        rest = lines[:at] + lines[end:]
    else:
        rest = lines
    heads = headings('\n'.join(rest))
    title = next((i for i, level, _ in heads if level == 1), None)
    listed = [(i, level, name) for i, level, name in heads if level in (2, 3) and title is not None and i > title]
    if title is None or len(listed) < INDEX_SECTIONS:
        return '\n'.join(rest)
    if not block:
        at = title + 1
        while at < len(rest) and not rest[at].strip():
            rest = rest[:at] + rest[at + 1 :]
    # anchors in document order, the index's own among them where it stands
    seen, entries, mine = {}, [], False
    for i, level, name in heads:
        if not mine and i >= at:
            slug('Index', seen)
            mine = True
        a = slug(name, seen)
        if (i, level, name) in listed:
            entries.append(('  ' if level == 3 else '') + f'* [{name}](#{a})')
    if not mine:
        slug('Index', seen)
    tail = rest[at:]
    while tail and not tail[0].strip():
        tail = tail[1:]
    head = rest[:at]
    while head and not head[-1].strip():
        head = head[:-1]
    return '\n'.join(head + ['', '## Index', ''] + entries + [''] + tail)


def has_index(text):
    return any(level == 2 and name.strip().lower() == 'index' for _, level, name in headings(text))


def check_indexes(root, docs, failures, write):
    for doc in docs:
        if doc.name == 'README.md' and doc.parent == root:
            continue  # the project's front page
        text = doc.read_text(encoding='utf-8')
        made = indexed(text)
        long = len(text.rstrip('\n').split('\n')) > INDEX_LINES
        if not (long or has_index(text)) or made == text:
            continue
        name = doc.relative_to(root).as_posix()
        if write:
            doc.write_text(made, encoding='utf-8')
            print(f'{name}: index written')
        elif has_index(text):
            failures.add(name, 0, '## Index does not list its sections as they are: run scripts/check-docs.py --index')
        else:
            failures.add(
                name,
                0,
                f'over {INDEX_LINES} lines with {INDEX_SECTIONS} or more sections and no ## Index: '
                'run scripts/check-docs.py --index',
            )


def check_docs_index(root, failures):
    index = root / 'docs/README.md'
    if not index.is_file():
        failures.add('docs/README.md', 0, 'is not there: it lists every document of docs/')
        return
    linked = set()
    for _, target in links(read(index)):
        path = unquote(target.partition('#')[0])
        if path and not SCHEME.match(path):
            linked.add((index.parent / path).resolve())
    for doc in in_order((root / 'docs').rglob('*.md')):
        if doc != index and doc.resolve() not in linked:
            failures.add('docs/README.md', 0, f'does not link {doc.relative_to(root / "docs").as_posix()}')


# ---- settings ----
def sources(root, patterns):
    """the files the patterns name, in order; a pattern that starts with ! drops what it names"""
    out = []
    for pattern in patterns:
        if pattern.startswith('!'):
            drop = set(root.glob(pattern[1:]))
            out = [p for p in out if p not in drop]
        else:
            out += [p for p in in_order(root.glob(pattern)) if p.is_file() and p not in out]
    return out


def keys_read(files):
    """{key: (file, line, default or None)} of the keys these sources read, a default where the code gives one"""
    out = {}
    for path in files:
        text = read(path)
        for pattern in (READ, TABLE_KEY):
            for m in pattern.finditer(text):
                default = m.group(2)
                if default in (None, 'NULL', '""', ''):
                    default = None
                elif default.startswith('"'):
                    default = default[1:-1]
                line = text.count('\n', 0, m.start()) + 1
                key = m.group(1)
                if key not in out or (out[key][2] is None and default is not None):
                    out[key] = (path, line, default)
    return out


def rc_variables(files):
    """{variable: (file, line, default)} of rc scripts' : ${name:=default} lines"""
    out = {}
    for path in files:
        text = read(path)
        for m in RC_VARIABLE.finditer(text):
            out[m.group(1)] = (path, text.count('\n', 0, m.start()) + 1, m.group(2).strip('"\''))
    return out


def cells(line):
    """the cells of a Markdown table row, split as GitHub does: at each | but an escaped one, \\|"""
    row = line.strip()
    row = row.removeprefix('|')
    row = row[:-1] if row.endswith('|') and not row.endswith('\\|') else row
    return [cell.strip().replace('\\|', '|') for cell in re.split(r'(?<!\\)\|', row)]


def table_rows(text, first):
    """[(line number, [names], Default cell)] of the rows of every table whose header is | <first> | Default |; the
    names are the Key cell's `code`, but what a parenthesis says"""
    rows, in_table = [], False
    for number, (line, code) in enumerate(zip(text.split('\n'), code_lines(text)), 1):
        if code or not line.lstrip().startswith('|'):
            in_table = False
            continue
        row = cells(line)
        if not in_table:
            in_table = len(row) >= 2 and row[0] == first and row[1] == 'Default'
            continue
        if set(''.join(row)) <= set('-: '):
            continue  # the header's rule
        rows.append(
            (number, re.findall(r'`([^`]+)`', re.sub(r'\([^)]*\)', '', row[0])), row[1] if len(row) > 1 else '')
        )
    return rows


def says(cell, default):
    """whether a Default cell says this default: `default`, or none or empty (its first word) for an empty one"""
    if default:
        return f'`{default}`' in cell
    return re.match(r'(none|empty)\b', cell) is not None


def check_table(root, doc, text, first, known, what, failures):
    """known: {name: (file, line, default or None for one the code does not give)}"""
    name = doc.relative_to(root).as_posix()
    rows = {}
    for number, names, cell in table_rows(text, first):
        if not names:
            failures.add(name, number, f'a row of the | {first} | Default | table with no `{first.lower()}`')
            continue
        for key in names:
            if key in rows:
                failures.add(name, number, f'`{key}` has a row already, at line {rows[key]}')
                continue
            rows[key] = number
            if key not in known:
                failures.add(name, number, f'`{key}` is no {what}')
                continue
            path, line, default = known[key]
            if (default is not None or first == 'Variable') and not says(cell, default or ''):
                shown = f'`{default}`' if default else 'none or empty'
                where = f'{path.relative_to(root).as_posix()}:{line}'
                failures.add(name, number, f'`{key}`: its Default cell does not say {shown}, which {where} gives it')
    for key, (path, line, default) in sorted(known.items()):
        if key not in rows:
            shown = f' (default `{default}`)' if default else ''
            failures.add(
                path.relative_to(root).as_posix(),
                line,
                f'`{key}`{shown} has no row in a | {first} | Default | table of {name}',
            )


def check_settings(root, failures):
    for doc_name, patterns, rc_names, sample_name in PROGRAMS:
        files = sources(root, patterns)
        rcs = [root / n for n in rc_names if (root / n).is_file()]
        if not files and not rcs:
            continue  # a checkout without this program
        doc = root / doc_name
        if not doc.is_file():
            failures.add(doc_name, 0, 'is not there: it has a row for each key and rc.conf variable of its program')
            continue
        text = read(doc)
        keys = keys_read(files)
        check_table(root, doc, text, 'Key', keys, 'key the program reads', failures)
        check_table(root, doc, text, 'Variable', rc_variables(rcs), "variable of its rc script's", failures)
        sample = root / sample_name if sample_name else None
        if sample and sample.is_file():
            sample_text = read(sample)
            for m in SAMPLE_KEY.finditer(sample_text):
                if m.group(1) not in keys:
                    failures.add(
                        sample_name,
                        sample_text.count('\n', 0, m.start()) + 1,
                        f'{m.group(1)} is no key its program reads ({doc_name} lists them)',
                    )


# ---- scripts ----
def check_scripts(root, docs, failures):
    said = '\n'.join(read(doc) for doc in docs)
    for folder in SCRIPT_DIRS:
        for path in in_order((root / folder).rglob('*')):
            if path.is_file() and path.suffix in SCRIPT_SUFFIXES:
                name = path.relative_to(root).as_posix()
                if not re.search(re.escape(name) + r'(?![\w-]|\.\w)', said):
                    failures.add(name, 0, 'is named in no document: README.md or docs/ says what it does')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--root', type=Path, default=ROOT, help="the checkout (default: this script's)")
    parser.add_argument('--index', action='store_true', help='write the ## Index of each document that needs one')
    args = parser.parse_args()
    root = args.root.resolve()
    docs = documents(root)
    failures = Failures()
    check_indexes(root, docs, failures, args.index)
    check_dates(root, docs, failures)
    check_links(root, docs, failures)
    check_docs_index(root, failures)
    check_settings(root, failures)
    check_scripts(root, docs, failures)
    for failure in failures:
        print(failure, file=sys.stderr)
    if failures:
        print(f'check-docs.py: {len(failures)} to fix', file=sys.stderr)
        return 1
    print(f'check-docs.py: {len(docs)} documents, their dates, links, indexes, settings and scripts hold')
    return 0


if __name__ == '__main__':
    sys.exit(main())
