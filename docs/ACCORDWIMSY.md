<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# AccordWimsy

## Index

* [What a pass mirrors](#what-a-pass-mirrors)
* [Releases](#releases)
* [A migration's copy](#a-migrations-copy)
* [A pass, its state and its logs](#a-pass-its-state-and-its-logs)
* [Commands](#commands)
* [Settings](#settings)
* [Its files](#its-files)

`accordwimsy/`: the repositories of a GitHub organization, the same on a Forgejo, a pass at a time (cron, every 10
minutes), in C99 with libcurl and ReiWimsy's cJSON (`rei/third_party/cjson`). It needs no Discord and listens on no
port. It runs in the forge's `gitsync` jail ([Forge](./FORGE.md)); installing it: [Install](./INSTALL.md#accordwimsy).

## What a pass mirrors

`accordwimsy sync [ENV FILE]`, for each repository of the organization, the new ones too:

- the repository on the Forgejo, made in its organization when it is not there, with GitHub's name, description and
  visibility, its default branch, and its Releases tab on only when a rule of `ACCORDWIMSY_RELEASES` names it;
- its branches and tags, through a bare mirror in `mirrors/`: fetched from GitHub (the mirror pruned of what GitHub has
  not, GitHub's pull requests' heads with them) and pushed to the Forgejo, which loses nothing GitHub loses. A branch or
  a tag GitHub has no more stays on the Forgejo `ACCORDWIMSY_GRACE_DAYS` (7) from the pass that first sees it gone,
  said then in its log with the day it goes, and is deleted after that; back on GitHub before, it just stays. A branch
  GitHub rewrote (a force-push: its new tip does not hold the old one), or a tag moved, keeps its old tip on the Forgejo
  as the branch `accordwimsy/kept/branch/<name>/<YYYYMMDD-HHMMSS>` (`.../tag/...` for a tag), said in its log, for the
  same grace. No `--prune` push: over HTTP, with the negative refspec that keeps the `accordwimsy/pull/<n>` branches
  out of it, git warns `helper reported unexpected status` for each of them on every pass. The tokens go to git in its
  environment (`GIT_CONFIG_*`), never on its command line, and no one's git settings are read;
- its labels, made or edited as GitHub has them;
- its issues and pull requests by their numbers, which the Forgejo then gives the same: a number GitHub has no more
  (deleted, moved to another repository) becomes a closed issue saying so, and a number the Forgejo gives otherwise
  stops the repository, said, since its issues must start empty. Each starts with its GitHub author, day and link; an
  edit on GitHub (its `updated_at`: title, text, state, labels) edits it;
- the comments, each with its GitHub author, day and link; one edited on GitHub is edited. One deleted there stays;
- its releases that `ACCORDWIMSY_RELEASES` picks, with their files ([Releases](#releases)).

A pull request is a pull request into the same branch, from its head, which GitHub keeps for every one, a fork's too
(`refs/pull/<n>/head`): pushed to the Forgejo as the branch `accordwimsy/pull/<n>` while it is open, moved when GitHub's
moves. One merged on GitHub is marked merged by GitHub's merge commit (the repository allows a commit to mark it,
`allow_manual_merge`), one closed is closed, one reopened is reopened; once it is not open its branch goes, the pull
request keeps its commits. One left open that the Forgejo closed under the sync gets its head back and is reopened on
the next pass, and one still open whose branch is gone gets the branch back. A merge commit the branch here does not
hold (pushed over since) leaves it closed, said. Only one whose head or base branch GitHub has no more is an issue,
saying why, its branches (`fork:branch` into `main`), its state and where its changes are.

An issue, a comment or a repository is never deleted on the Forgejo. A repository GitHub lists no more (deleted, moved,
or out of the token's reach) is said once on stderr and left as it is.

Not mirrored: milestones, a comment's deletion, a release no rule picks, a pull request's reviews and its comments on
lines of its changes.

## Releases

`ACCORDWIMSY_RELEASES` picks them, a rule per repository and tag: `nightly:nightly-*:3 edenprobe:*` brings the 3
newest releases of `nightly` whose tag starts with `nightly-`, and every release of `edenprobe`. A rule's pattern is
fnmatch's; its count, when it has one, keeps the newest that many of the releases it matches, by when GitHub published
each. A draft never comes. A repository no rule names keeps its Releases tab off, and its releases here, if any, are
left as they are.

Each release picked is made with GitHub's name (its tag when it has none), its pre-release mark, and its text after a
line with its tag, author, day and link; an edit on GitHub (its `updated_at`) edits it. Its files come one at a time:
fetched into `release.part` in `ACCORDWIMSY_STATE`, checked against GitHub's size and SHA-256 (the file's `digest`,
when GitHub gives one), sent to the Forgejo, and the part deleted. A file whose SHA-256 is not GitHub's is not sent,
said on stderr, and fetched again on the next pass. A file GitHub replaced is sent again, and one the release has no
more is deleted here. A release whose tag the pass has not fetched yet (published in between) waits for the next pass.

Once every release picked is here (none failed, none waits for its tag), each one made here that the rules pick no
more is deleted: with a count, the oldest goes when a new one comes in. One GitHub has no more stays
`ACCORDWIMSY_GRACE_DAYS`, as a branch does, said when first seen and by `status`. A release of the Forgejo's own, of a
tag no rule picks, is never touched. A `state.json` lost takes back each release here by its tag and each file of
GitHub's size by its name, without fetching it again.

## A migration's copy

The Forgejo's API makes an issue, a pull request or a comment as its caller: only its own migration from GitHub writes
them with their GitHub authors and days. `accordwimsy migrate REPOSITORY` has it do so for one repository: the copy
this program made is moved aside, renamed `<name>-bot-<YYYYMMDD-HHMMSS>`, archived and private, for someone to delete;
the Forgejo migrates the repository from GitHub with `GITHUB_TOKEN` (its git, issues, pull requests, comments, labels
and milestones; no release, no wiki); then the command adopts it, as below. A repository a migration made already is
left alone. The Forgejo answers once its migration is done, minutes for a thousand pull requests, and the command
waits up to 4 hours for it. The migration runs in the Forgejo, which then reaches GitHub on 443:
[Forge](./FORGE.md#a-repository-made-again-by-the-migration).

`accordwimsy adopt REPOSITORY` rebuilds the repository's entry of `state.json` from the migration's copy, in place of
what it had:

- each number up to the copy's last is the migration's: from then on its text, and its comments', is GitHub's alone,
  as the Forgejo shows their GitHub authors; a new one is the program's, saying its author, as before;
- GitHub's `updated_at` of each, so that the next pass, which reads all of GitHub's once, edits only what changed;
- each pull request's state, and the branch it is made from here: GitHub's own, which the mirror keeps, or, for a
  fork's pull request open when it came, the branch `<owner>/<branch>` the migration fetched its head into, which the
  program moves with GitHub's new commits, keeps out of the grace, and deletes once the pull request is closed. A
  fork's pull request closed when it came has no branch here: GitHub reopening it leaves it closed here, said;
- each of GitHub's comments, matched to the Forgejo's of the same issue, GitHub author and time; one the migration did
  not make (made while it went) is made by the next pass.

It refuses, writing nothing, when the Forgejo's copy was not made by a migration (it has no `original_url`), has an
issue with no GitHub author (a GitHub account linked to one of the Forgejo's users has the migration write that user
instead), misses a number GitHub has below its last, or has a comment that matches none of GitHub's: the matching gone
wrong, the next pass would make GitHub's comments again beside the migration's. What it found goes to its log: the
numbers, the pull requests open and their branches, the comments matched, and those left to the next pass.

## A pass, its state and its logs

A pass asks only for what changed since the last one (GitHub's `since`), keeps in `state.json` what it made (the next
number, each issue's and comment's `updated_at`, the Forgejo's id of each comment, the state each pull request was left
in, each release made and the GitHub id of each file sent, what waits out the grace and since when, when its last
pass went through, since when GitHub lists it no more, the numbers a migration made and the branches of its pull
requests), and
writes it after each repository: a pass that stops goes on from there. A `state.json` that is no JSON object stops the
pass, since starting from nothing would make every issue again. One pass at a time: a second one, while one goes, says
so and does nothing.

What it did goes to stdout, its log, and so do a repository's pass that went wrong (an HTTP error, a push refused: the
next pass tries again), what git itself says, and the everyday news (a branch or a tag gone from GitHub, a branch
rewritten, a pull request closed rather than merged). stderr, which the host mails (noirjail.sh's `JAIL_LOG_MAIL`),
holds only what someone has to see: the default branch gone from GitHub or rewritten, half or more of a repository's
branches and tags gone in one pass (3 at least), a repository GitHub lists no more, the Forgejo's issue numbers no
longer GitHub's (once, until that repository passes again), a release's file whose SHA-256 is not GitHub's, a rule of
`ACCORDWIMSY_RELEASES` naming a repository GitHub does not list (each pass), a migration or an adoption refused or
failed, a fork's open pull request whose head a migration did not fetch, a `state.json` or `ACCORDWIMSY_FRESH` it
cannot write, and what keeps a pass from starting. A repository that keeps failing shows in its last good pass, which
noirjail.sh's `jail fresh` mails about once it lags. The exit status is 1 when a repository stopped.

After each pass it writes `ACCORDWIMSY_FRESH`, a line per repository GitHub lists, `<name> <seconds since the epoch of
its last good pass>` (0: none yet), for the host to tell a copy that lags (noirjail.sh's `jail fresh`).

## Commands

    accordwimsy sync [ENV FILE]      # a pass; ENV FILE: /usr/local/etc/accordwimsy/accordwimsy.env when not given
    accordwimsy status [ENV FILE]    # each repository's last good pass, and what waits out the grace with the day it goes
    accordwimsy migrate REPOSITORY [ENV FILE]   # the repository made again by the Forgejo's migration, then adopted
    accordwimsy adopt REPOSITORY [ENV FILE]     # state.json's entry of the repository rebuilt from a migration's copy

`status` reads `state.json` as a pass going leaves it after each repository; what waits out the grace is a branch, a tag
or a release. `migrate` and `adopt` hold the lock a pass holds: one of them, while a pass goes, says so and does
nothing (exit 1). A pass, a migration or an adoption run by hand goes as the user `accordwimsy`, which owns its state
and mirrors ([Install](./INSTALL.md#accordwimsy)).

## Settings

A line each of its env file, `/usr/local/etc/accordwimsy/accordwimsy.env` (the package puts
`dist/freebsd/accordwimsy.env.sample` there to fill in):

| Key | Default | What it is |
|---|---|---|
| `ACCORDWIMSY_FORGEJO` | none: needed | the Forgejo's address, `http://host:3000`: its API under `/api/v1`, its repositories under it |
| `ACCORDWIMSY_FORGEJO_TOKEN` | none: needed | a token of the Forgejo's: `write:repository`, `write:issue`, `write:organization`, `read:user` |
| `ACCORDWIMSY_FORGEJO_USER` | `accordwimsy` | whose that token is, for git's push |
| `GITHUB_TOKEN` | none: needed | a fine-grained token, read-only, of the organization: Contents, Issues, Pull requests, Metadata |
| `ACCORDWIMSY_ORG` | `scarletnx` | the GitHub organization |
| `ACCORDWIMSY_FORGEJO_ORG` | `ACCORDWIMSY_ORG`'s | the Forgejo's organization |
| `ACCORDWIMSY_STATE` | `/var/db/accordwimsy` | `state.json`, the lock and `mirrors/` |
| `ACCORDWIMSY_GIT` | `/usr/local/bin/git` | the git it runs |
| `ACCORDWIMSY_GRACE_DAYS` | `7` | how long a branch, a tag or a release GitHub has no more, or a rewritten one's old tip, stays on the Forgejo |
| `ACCORDWIMSY_RELEASES` | none: no release comes | the releases that come ([Releases](#releases)): rules split by spaces, each `repository:tag pattern` or `repository:tag pattern:newest` (1 to 9999), 16 at most |
| `ACCORDWIMSY_FRESH` | `/var/log/accordwimsy/accordwimsy.fresh` | each repository's last good pass; made beforehand, owned by the user, in a directory only root writes, since root on the host reads it |
| `ACCORDWIMSY_GITHUB_API`, `ACCORDWIMSY_GITHUB` | `https://api.github.com`, `https://github.com` | where GitHub is (the tests' fakes) |

A key it does not know stops it, naming the key and its line; so does a value past the room it has, and a needed one
left empty, days that are no number, or a word of `ACCORDWIMSY_RELEASES` that is no rule. It goes out to `github.com`,
`api.github.com` and, for a release's files, the host GitHub sends them from (`release-assets.githubusercontent.com`,
in the pf table `github`) on 443, and to the Forgejo, nowhere else.

## Its files

As its package installs them: the program in `/usr/local/bin/accordwimsy`; the env file above (its user's, 600); its
state in `/var/db/accordwimsy` (700, its user's), a release's file on its way there too (`release.part`); its logs
`accordwimsy.log` and `accordwimsy.err` and `ACCORDWIMSY_FRESH` in `/var/log/accordwimsy` (a folder of root's); its cron
line and log rotation in `/usr/local/share/examples/accordwimsy` to copy into place (`dist/freebsd/accordwimsy.cron`: a
pass every 10 minutes, no mail from cron; `dist/freebsd/accordwimsy.newsyslog`: each log rotated at 1024 KB, 7 of the
log kept and 30 of the errors).

Building it: [Build](./BUILD.md#accordwimsy); its tests: [Testing](./TESTING.md).
