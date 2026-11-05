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

static struct {
    pthread_mutex_t lock;
    long long ready;    /* when the session was last seen ready, or the watch started: ms of the monotonic clock */
    int seen;           /* whether it has been seen ready */
    long long interval; /* the heartbeat interval the last HELLO gave, in ms */
    void (*end)(void);
} watch = {PTHREAD_MUTEX_INITIALIZER, 0, 0, WD_INTERVAL, NULL};

static long long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* each second, on the thread of discord_run, which alone changes the session */
static void look(struct discord* client, struct discord_timer* timer) {
    (void)timer;
    pthread_mutex_lock(&watch.lock);
    if (client->gw.session->is_ready) {
        watch.ready = now_ms();
        watch.seen = 1;
    }
    if (client->gw.timer->hbeat_interval > 0) watch.interval = client->gw.timer->hbeat_interval;
    pthread_mutex_unlock(&watch.lock);
}

/* a thread of its own: discord_run's may be the one waiting */
static void* guard(void* unused) {
    (void)unused;
    for (;;) {
        long long since, interval;
        int seen;
        sleep(1);
        pthread_mutex_lock(&watch.lock);
        since = now_ms() - watch.ready;
        interval = watch.interval;
        seen = watch.seen;
        pthread_mutex_unlock(&watch.lock);
        if (since > WD_INTERVALS * interval) {
            rei_log(
                "the gateway stopped: its session was %s %.1f s ago, longer than the %.1f s (%d heartbeat intervals "
                "of %.2f s) a connection may take to bring READY or RESUMED",
                seen ? "last seen ready" : "never ready since the bot started,", (double)since / 1000,
                (double)(WD_INTERVALS * interval) / 1000, WD_INTERVALS, (double)interval / 1000);
            watch.end();
        }
    }
    return NULL;
}

void wd_start(struct discord* client, void (*end)(void)) {
    pthread_t thread;
    int code;
    watch.end = end;
    watch.ready = now_ms();
    if (!discord_timer_interval(client, look, NULL, NULL, 1000, 1000, -1)) {
        rei_log("the gateway's watch did not start: Concord made no timer for it");
        end();
    }
    if ((code = pthread_create(&thread, NULL, guard, NULL))) {
        rei_log("the gateway's watch did not start: its thread did not: %s", strerror(code));
        end();
    }
    pthread_detach(thread);
}
