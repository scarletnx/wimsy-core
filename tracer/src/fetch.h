/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The downloads of the attachments, through libcurl: a TrGet, over HTTPS alone, max bytes at most */
#ifndef TR_FETCH_H
#define TR_FETCH_H
#include "tracer.h"

int tr_fetch(void* ctx, const char* url, size_t max, Buf* out, char** why);
#endif
