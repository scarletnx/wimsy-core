/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The watch on Concord's gateway: the bot ends, with status 1 (daemon(8) starts it again), once its session has not
   been ready for WD_INTERVALS heartbeat intervals. Concord's discord_run returns when the GET gateway/bot that starts a
   session fails, but goes on waiting, the process alive and quiet, when a connection ends with no CLOSE before READY,
   or that GET never gets an answer */
#ifndef REI_WATCH_H
#define REI_WATCH_H

struct discord;

/* the heartbeat intervals a session may go without being ready: Concord takes a connection for dead one interval after
   a heartbeat with no ACK, and a reconnection takes a second or two */
#define WD_INTERVALS 3
/* Discord's heartbeat interval, in ms, until a HELLO gives one */
#define WD_INTERVAL  41250

/* watches client's gateway from now on: before discord_run. end ends the bot */
void wd_start(struct discord* client, void (*end)(void));
#endif
