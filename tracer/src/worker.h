/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The worker that takes a report's files: the bot runs itself again (tracer --report) for each message, so that the
   downloads and the readers of archives, images and logs, which read files strangers send, run in a process without
   the bot's token, within limits of memory, time and file size, and a crash there costs that report alone. The bot
   asks on the worker's standard input and reads its answer on its standard output, both JSON; the files wait in the
   message's folder (Taken's dir) for the bot to move them into the site */
#ifndef TR_WORKER_H
#define TR_WORKER_H
#include <stdio.h>

#include "report.h"

#define WK_SECONDS 600        /* a worker's time at most, its downloads with it */
#define WK_CPU     120        /* its processor's seconds at most */
#define WK_MEMORY  (1u << 30) /* its address space at most, but under AddressSanitizer, which maps far more */
#define WK_ANSWER  (64 << 20) /* the bytes of its answer at most: the logs' summaries */

/* the environment a worker gets: environ without the bot's settings and its token (TRACER_*); owned, its strings
   environ's */
char** wk_env(char* const* environ_now);
/* m's files taken into k (its dir made, its base and counts set) by the worker argv names (argv[0] the program, run
   without a shell), which may take seconds at most: 0, or -1 with why (owned): it could not start, it stopped on a
   signal or a limit, it took too long, or its answer was not one */
int wk_take(char* const argv[], int seconds, const TrMessage* m, Taken* k, char** why);
/* the worker's side: the question on in, files taken through get into the folder it names, the answer on out. 0, or 1
   for a question that is not one (said on stderr) */
int wk_serve(FILE* in, FILE* out, TrGet get, void* ctx);
/* the worker's limits on itself, before it reads anything: memory, processor time, the size of a file, no core */
void wk_limit(void);
#endif
