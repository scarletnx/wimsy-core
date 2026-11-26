/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* ReiWimsy's bot, for the program that runs it (main.c): its settings (the REIWIMSY_ keys and GITHUB_TOKEN of the
   environment, the env file), its replies and cards, its Concord client and its threads */
#ifndef REI_BOT_H
#define REI_BOT_H
#include <discord.h> /* Concord's */

#include "rei.h"

/* the settings read, the program stopped through end (with why on stderr) on one the bot refuses */
void rb_settings(void (*end)(void));
/* --fix-cards (cards.h's cd_fix) with these settings, applied or only said: the program's exit status */
int rb_fix_cards(int apply);
/* the bot made: its clients, what it kept (REIWIMSY_STATE) and its Concord client, which the caller runs
   (discord_run); end on what fails */
struct discord* rb_start(void);
/* user, another bot this program runs (Tracer): a message that mentions it is that bot's, and gets no reply here, as
   its commands may hold a #N that is no issue. Before discord_run */
void rb_leave_to(u64 user);
/* the line that says why its gateway stopped: discord_run's code */
void rb_stopped(struct discord* client, CCORDcode code);
#endif
