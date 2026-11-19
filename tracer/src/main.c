/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* Tracer alone (bot.h): the game reports that people send it on Discord, kept in a database and drawn as a static site.

    TRACER_TOKEN=... TRACER_URL=https://... [TRACER_CHANNELS=id,id|all] [TRACER_ADMINS=id,id] [TRACER_DB=file]
    [TRACER_SITE=folder] [TRACER_ASSETS=folder] [TRACER_TITLES=file] [TRACER_UPSTREAM_REPO=owner/name] tracer

reiwimsy runs the same bot beside ReiWimsy, in one process */
#define _POSIX_C_SOURCE 200809L
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bot.h"

/* the program's end, with 1, from which daemon(8) starts it again. _exit, not exit: the thread of the events may be
   inside a request, which libcurl's cleanup and exit's handlers (OpenSSL's) must not run under */
static void end(void) {
    fflush(NULL);
    _exit(1);
}

int main(int argc, char** argv) {
    struct discord* client;
    if (argc == 2 && !strcmp(argv[1], "--report")) return tb_worker(); /* a worker: no settings, no token */
    { /* before anything else: its workers, which read what strangers send, never read its tokens */
        char* why;
        if (tb_untraceable(&why)) {
            fprintf(stderr, "tracer: its tokens could not be kept from its workers: %s\n", why);
            end();
        }
    }
    if (argc != 1) {
        fprintf(stderr, "tracer: %s takes no argument: its settings are in its environment (the env file)\n", argv[0]);
        end();
    }
    tb_settings(end);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    client = tb_start(argv[0]);
    tb_stopped(client, discord_run(client));
    end();
    return 1;
}
