/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* Tracer's limits, beside ReiWimsy's common pieces (rei.h: strings that grow, cJSON's lookups, the log, the clock) */
#ifndef TRACER_H
#define TRACER_H
#include "rei.h"

#define TR_FILES     10                 /* the attachments a message has at most, Discord's own limit */
#define TR_LOGS      3                  /* the logs one report keeps */
#define TR_IMAGES    8                  /* the screenshots one report keeps */
#define TR_VIDEOS    2                  /* the videos one report keeps */
#define TR_LOG_MAX   ((size_t)64 << 20) /* a log's bytes at most, downloaded and unpacked alike */
#define TR_IMAGE_MAX ((size_t)25 << 20) /* a screenshot's */
#define TR_VIDEO_MAX ((size_t)100 << 20) /* a video's */
#define TR_NOTE      200                /* the bytes kept of what the message said */
#define TR_HOURLY    10                 /* the reports one person sends in an hour */
#define TR_ROLES     256                /* the roles of a member that a message names, at most */
#define TR_KEY       72                 /* a game's key: its title ID, or n- and its name's letters and digits */

/* a download: url's body into out (cleared first), max bytes at most. 0, or -1 with why (owned) */
typedef int (*TrGet)(void* ctx, const char* url, size_t max, Buf* out, char** why);
#endif
