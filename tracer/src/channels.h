/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The channels Tracer takes reports in: those TRACER_CHANNELS lists, with their threads and forum posts, and none when
   it lists none, so that a bot added to a server answers nowhere until it is told where */
#ifndef TR_CHANNELS_H
#define TR_CHANNELS_H
#include "tracer.h"

#define TR_PARENTS 256 /* the threads whose channel is remembered */

/* the threads' channels asked once */
typedef struct {
    u64 known[TR_PARENTS];
    int answer[TR_PARENTS], next;
} TrParents;

/* whether a report is taken in channel: one of the n channels, or a thread or a forum post of one (its parent asked
   through parent, once, the answer kept in cache); never when n is 0 */
int tr_answers_in(const u64* channels, int n, TrParents* cache, u64 channel, u64 (*parent)(void* ctx, u64 channel),
                  void* ctx);
/* the channels as the start's log says them: "1,2", or that none is listed; owned */
char* tr_channels_said(const u64* channels, int n);
#endif
