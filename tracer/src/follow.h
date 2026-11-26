/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The issues and pull requests reports are linked to (by an approval or "link"), followed on GitHub every TR_FOLLOW
   seconds from the bot's own process with TRACER_ISSUES_TOKEN: one GraphQL query for TR_FOLLOW_BATCH of them at a time,
   with the newest nightly of TRACER_UPSTREAM_REPO's owner. Each report keeps its issue's state (open, closed and why,
   merged), the pull request that fixed it (a merged pull request linked, or the one whose merge closed the issue as
   completed) and its merge commit, and the first nightly that has that commit (GitHub's compare of the commit and the
   nightly's); with TRACER_NOTIFY, a reply to the report's own message says it once that nightly is out */
#ifndef TR_FOLLOW_H
#define TR_FOLLOW_H
#include "report.h"

#define TR_FOLLOW       600 /* the seconds between two rounds */
#define TR_FOLLOW_BATCH 50  /* the issues one query asks for */

/* TRACER_NOTIFY's value into *notify: off (none, the default), reply, ping. 0, or -1 with why (owned) for another
   value or for reply and ping without an issues' repository (issues true when there is one) */
int tr_notify_of(const char* value, int issues, TrNotify* notify, char** why);
/* the start's line saying how the issues are followed; owned */
char* tr_follow_said(const Tracer* t);
/* one round: each followed report's issue asked of GitHub, kept and drawn again where it changed, and its sender told
   when its fix is in a nightly (t->notify). The reports changed or told, or -1 when GitHub did not answer (said) */
int tr_follow(Tracer* t);
#endif
