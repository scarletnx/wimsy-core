/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* reiwimsy: ReiWimsy (bot.h), and Tracer beside it (tracer/src/bot.h) when its env file has TRACER_TOKEN. One process,
   two bots: each its own token, gateway and Concord client, so each answers on Discord as itself; a message that
   mentions Tracer is Tracer's alone.

    REIWIMSY_TOKEN=... REIWIMSY_REPO=OWNER/NAME [REIWIMSY_NIGHTLY=OWNER/NAME] [REIWIMSY_CHANNELS=id,id]
    [GITHUB_TOKEN=...] [REIWIMSY_CARDS_PRS=id] [REIWIMSY_CARDS_ISSUES=id] [REIWIMSY_CARDS_NIGHTLY=id]
    [REIWIMSY_STATE=file] [TRACER_TOKEN=... TRACER_URL=https://... and Tracer's other keys] reiwimsy

"--report" is a Tracer's worker (the program runs itself again so, for each message with files) */
#define _POSIX_C_SOURCE 200809L
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bot.h"
#include "tracer/src/bot.h"

/* the program's end, with 1, from which daemon(8) starts it again. _exit, not exit: the bots' threads may be inside
   their requests, which libcurl's cleanup and exit's handlers (OpenSSL's) must not run under */
static void end(void) {
    fflush(NULL);
    _exit(1);
}
static void stop(const char* fmt, const char* value) {
    fputs("reiwimsy: ", stderr);
    fprintf(stderr, fmt, value);
    fputc('\n', stderr);
    end();
}

/* Tracer's gateway, on a thread of its own: the program ends when it stops, as when ReiWimsy's does */
static void* tracer_gateway(void* client) {
    tb_stopped(client, discord_run(client));
    end();
    return NULL;
}

/* the first TRACER_ key of the environment, which Tracer reads only with TRACER_TOKEN: NULL for none */
static const char* tracer_key(void) {
    extern char** environ;
    char** e;
    for (e = environ; *e; ++e)
        if (!strncmp(*e, "TRACER_", 7)) return *e;
    return NULL;
}

int main(int argc, char** argv) {
    const char* token = getenv("TRACER_TOKEN");
    int tracer = token && *token;
    struct discord *rei, *tr = NULL;
    if (argc == 2 && !strcmp(argv[1], "--report")) return tb_worker(); /* a worker: no settings, no token */
    if (argc > 1) stop("%s: not an option of reiwimsy, which takes none", argv[1]);
    if (!tracer && tracer_key()) /* a Tracer's key with no Tracer would be a setting quietly not taken */
        stop("%s: Tracer's keys are read only with TRACER_TOKEN, and it is not set", tracer_key());
    if (tracer) { /* before anything else: its workers, which read what strangers send, never read the tokens */
        char* why;
        if (tb_untraceable(&why)) stop("the tokens could not be kept from Tracer's workers: %s", why);
    }
    rb_settings(end);
    if (tracer) tb_settings(end);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    rei = rb_start();
    if (!tracer)
        rei_log("Tracer: none, as TRACER_TOKEN is not set");
    else {
        pthread_t thread;
        u64 user;
        int code;
        tr = tb_start(argv[0]);
        if (!(user = tb_user()))
            stop("Tracer's user is not known (GET users/@me with TRACER_TOKEN did not say it), so ReiWimsy could not "
                 "leave the messages that mention it to it%s",
                 "");
        rb_leave_to(user);
        rei_log("Tracer: beside ReiWimsy, with its own token; the messages that mention it (user %llu) are its own",
                (unsigned long long)user);
        if ((code = pthread_create(&thread, NULL, tracer_gateway, tr)))
            stop("Tracer's gateway did not start: no thread for it: %s", strerror(code));
        pthread_detach(thread);
    }
    rb_stopped(rei, discord_run(rei));
    end();
    return 1;
}
