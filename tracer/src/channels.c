/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "channels.h"
#include <stdlib.h>
#include <string.h>

static int listed(const u64* channels, int n, u64 channel) {
    int i;
    for (i = 0; channel && i < n; ++i)
        if (channels[i] == channel) return 1;
    return 0;
}

int tr_channels_of(const char* value, u64* channels, int max, char** why) {
    const char* s = value;
    int n = 0;
    *why = NULL;
    if (!value || !*value) return 0;
    if (!strcmp(value, "all")) return TR_CHANNELS_ALL;
    while (*s) {
        size_t len = strcspn(s, ","), digits = strspn(s, "0123456789");
        if (len && (digits != len || len > 20)) {
            *why =
                rei_format("TRACER_CHANNELS=%s: %.*s is neither all nor a channel's id (Developer Mode, Copy Channel "
                           "ID: its digits)",
                           value, (int)len, s);
            return -2;
        }
        if (len) {
            if (n == max) {
                *why = rei_format("TRACER_CHANNELS=%s names more than %d channels", value, max);
                return -2;
            }
            channels[n++] = strtoull(s, NULL, 10);
        }
        s += len + (s[len] == ',');
    }
    return n;
}

int tr_answers_in(const u64* channels, int n, TrParents* cache, u64 channel, u64 (*parent)(void* ctx, u64 channel),
                  void* ctx) {
    int i, yes;
    if (n == TR_CHANNELS_ALL) return 1;
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
    if (n == TR_CHANNELS_ALL) return rei_strdup("all, as TRACER_CHANNELS=all: every channel and thread it can read");
    if (!n) return rei_strdup("none, as TRACER_CHANNELS is not set: no report is taken");
    for (i = 0; i < n; ++i) buf_printf(&b, "%s%llu", i ? "," : "", (unsigned long long)channels[i]);
    return buf_take(&b);
}
