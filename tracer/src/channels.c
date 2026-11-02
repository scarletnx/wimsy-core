/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "channels.h"

static int listed(const u64* channels, int n, u64 channel) {
    int i;
    for (i = 0; channel && i < n; ++i)
        if (channels[i] == channel) return 1;
    return 0;
}

int tr_answers_in(const u64* channels, int n, TrParents* cache, u64 channel, u64 (*parent)(void* ctx, u64 channel),
                  void* ctx) {
    int i, yes;
    if (!n) return 0;
    if (listed(channels, n, channel)) return 1;
    for (i = 0; i < TR_PARENTS; ++i)
        if (cache->known[i] == channel) return cache->answer[i];
    yes = listed(channels, n, parent(ctx, channel));
    cache->known[cache->next] = channel;
    cache->answer[cache->next] = yes;
    cache->next = (cache->next + 1) % TR_PARENTS;
    return yes;
}

char* tr_channels_said(const u64* channels, int n) {
    Buf b = {0};
    int i;
    if (!n) return rei_strdup("none, as TRACER_CHANNELS is not set: no report is taken");
    for (i = 0; i < n; ++i) buf_printf(&b, "%s%llu", i ? "," : "", (unsigned long long)channels[i]);
    return buf_take(&b);
}
