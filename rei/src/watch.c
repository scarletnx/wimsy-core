/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <discord.h> /* Concord's */
/* the session's state and its heartbeat interval, which no call of Concord's gives */
#include <discord-internal.h>

#include "rei.h"
#include "watch.h"

/* each watched client's: the session's state and its heartbeat interval, which no call of Concord's gives */
typedef struct {
    struct discord* client;
    const char* name;
    long long ready;    /* when the session was last seen ready, or the watch started: ms of the monotonic clock */
    int seen;           /* whether it has been seen ready */
    long long interval; /* the heartbeat interval the last HELLO gave, in ms */
} Watched;

static struct {
    pthread_mutex_t lock;
    Watched each[WD_CLIENTS];
    int n;
    void (*end)(void);
} watch = {PTHREAD_MUTEX_INITIALIZER, {{0}}, 0, NULL};

static long long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* each second, on the thread of client's discord_run, which alone changes its session */
static void look(struct discord* client, struct discord_timer* timer) {
    int i;
    (void)timer;
    pthread_mutex_lock(&watch.lock);
    for (i = 0; i < watch.n; ++i) {
        Watched* w = &watch.each[i];
        if (w->client != client) continue;
        if (client->gw.session->is_ready) {
            w->ready = now_ms();
            w->seen = 1;
        }
        if (client->gw.timer->hbeat_interval > 0) w->interval = client->gw.timer->hbeat_interval;
    }
    pthread_mutex_unlock(&watch.lock);
}

/* a thread of its own: a discord_run's may be the one waiting */
static void* guard(void* unused) {
    (void)unused;
    for (;;) {
        int i;
        sleep(1);
        for (i = 0; i < WD_CLIENTS; ++i) {
            long long since, interval;
            int seen, watched;
            const char* name;
            pthread_mutex_lock(&watch.lock);
            watched = i < watch.n;
            since = watched ? now_ms() - watch.each[i].ready : 0;
            interval = watched ? watch.each[i].interval : WD_INTERVAL;
            seen = watched && watch.each[i].seen;
            name = watched ? watch.each[i].name : REI_NAME;
            pthread_mutex_unlock(&watch.lock);
            if (watched && since > WD_INTERVALS * interval) {
                rei_log_as(name,
                           "the gateway stopped: its session was %s %.1f s ago, longer than the %.1f s (%d heartbeat "
                           "intervals of %.2f s) a connection may take to bring READY or RESUMED",
                           seen ? "last seen ready" : "never ready since the bot started,", (double)since / 1000,
                           (double)(WD_INTERVALS * interval) / 1000, WD_INTERVALS, (double)interval / 1000);
                watch.end();
            }
        }
    }
    return NULL;
}

void wd_start(struct discord* client, const char* name, void (*end)(void)) {
    pthread_t thread;
    int code, first;
    pthread_mutex_lock(&watch.lock);
    if (watch.n == WD_CLIENTS) {
        pthread_mutex_unlock(&watch.lock);
        rei_log_as(name, "the gateway's watch did not start: %d clients are watched already", WD_CLIENTS);
        end();
        return;
    }
    watch.each[watch.n].client = client;
    watch.each[watch.n].name = name;
    watch.each[watch.n].ready = now_ms();
    watch.each[watch.n].interval = WD_INTERVAL;
    first = !watch.n++;
    watch.end = end;
    pthread_mutex_unlock(&watch.lock);
    if (!discord_timer_interval(client, look, NULL, NULL, 1000, 1000, -1)) {
        rei_log_as(name, "the gateway's watch did not start: Concord made no timer for it");
        end();
        return;
    }
    if (!first) return; /* one guard watches them all */
    if ((code = pthread_create(&thread, NULL, guard, NULL))) {
        rei_log_as(name, "the gateway's watch did not start: no thread for it: %s", strerror(code));
        end();
        return;
    }
    pthread_detach(thread);
}
