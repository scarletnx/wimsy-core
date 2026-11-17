/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* Tracer's bot, for a program that runs it: its settings (the TRACER_ keys of the environment, the env file), its
   database and site, its Concord client and the thread that answers its events. tracer (main.c) runs it alone;
   reiwimsy runs it beside ReiWimsy in one process, each bot with its own token, gateway and name on Discord */
#ifndef TR_BOT_H
#define TR_BOT_H
#include <discord.h> /* Concord's */

#include "rei.h"

/* the program's memory and environment, where the tokens are, kept from its workers' debuggers (worker.h's
   wk_untraceable): 0, or -1 with why (owned). Before the settings are read */
int tb_untraceable(char** why);
/* a worker, "PROGRAM --report": one message's files read from stdin's question, answered on stdout (worker.h), within
   its limits and without the program's settings; never returns */
int tb_worker(void);
/* the settings read, the program stopped through end (with why on stderr) on one the bot refuses */
void tb_settings(void (*end)(void));
/* the bot made: its database, site, workers (self, the program's full path, which they run) and Concord client, which
   the caller runs (discord_run); end on what fails */
struct discord* tb_start(const char* self);
/* the bot's user id, asked of Discord with its token after tb_start: 0 when Discord did not say */
u64 tb_user(void);
/* the line that says why its gateway stopped: discord_run's code */
void tb_stopped(struct discord* client, CCORDcode code);
#endif
