/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* AccordWimsy: the repositories of a GitHub organization, the same on a Forgejo, a pass at a time (cron)

     accordwimsy sync [ENV FILE]               default /usr/local/etc/accordwimsy/accordwimsy.env
     accordwimsy status [ENV FILE]             each repository's last good pass, and what waits out the grace
     accordwimsy migrate REPOSITORY [ENV FILE] the repository made again by the Forgejo's migration, then adopted
     accordwimsy adopt REPOSITORY [ENV FILE]   state.json's of the repository rebuilt from a migration's copy

   One at a time: a pass, a migration or an adoption, while another goes, says so and does nothing. */
#include "aw.h"
#include "config.h"
#include "http.h"
#include "sync.h"
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define ENV_FILE "/usr/local/etc/accordwimsy/accordwimsy.env"
/* how long a migration may take: the Forgejo answers once it is done */
#define MIGRATION_SECONDS (4L * 3600)

static cJSON* load(const char* path) {
    FILE* f = fopen(path, "rb");
    Buf b = {0};
    char chunk[4096];
    size_t n;
    cJSON* state;
    if (!f) {
        if (errno != ENOENT) aw_warn("%s: %s: the pass starts from nothing made", path, strerror(errno));
        return cJSON_CreateObject();
    }
    while ((n = fread(chunk, 1, sizeof chunk, f))) buf_addn(&b, chunk, n);
    fclose(f);
    state = cJSON_Parse(b.text ? b.text : "");
    if (!cJSON_IsObject(state)) {
        aw_warn("%s: its %zu bytes are no JSON object: stopping, as starting from nothing would make every issue again",
                path, b.len);
        exit(1);
    }
    buf_free(&b);
    return state;
}
int main(int argc, char** argv) {
    AwConfig config;
    Aw a;
    char why[1024], *lock_path, *mirrors;
    const char* command = argc > 1 ? argv[1] : "";
    int lock, rc, named = !strcmp(command, "migrate") || !strcmp(command, "adopt");
    static const char repository[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-";
    if ((!named && strcmp(command, "sync") && strcmp(command, "status")) || argc < 2 + named || argc > 3 + named ||
        (named && (!argv[2][0] || strspn(argv[2], repository) != strlen(argv[2])))) {
        fprintf(stderr,
                "usage: accordwimsy sync|status [ENV FILE]\n"
                "       accordwimsy migrate|adopt REPOSITORY [ENV FILE]   (ENV FILE: %s when not given)\n",
                ENV_FILE);
        return 2;
    }
    if (aw_config_read(&config, argc == 3 + named ? argv[2 + named] : ENV_FILE, why, sizeof why)) {
        aw_warn("%s", why);
        return 2;
    }
    if (!strcmp(command, "status")) { /* reads state.json, as a pass going leaves it after each repository */
        memset(&a, 0, sizeof a);
        a.config = &config;
        a.now = (long)time(NULL);
        a.state_path = aw_format("%s/state.json", config.state);
        a.state = load(a.state_path);
        aw_status(&a);
        cJSON_Delete(a.state);
        free(a.state_path);
        return 0;
    }
    mirrors = aw_format("%s/mirrors", config.state);
    if ((mkdir(config.state, 0700) && errno != EEXIST) || (mkdir(mirrors, 0700) && errno != EEXIST)) {
        aw_warn("%s: %s", mirrors, strerror(errno));
        return 1;
    }
    free(mirrors);
    lock_path = aw_format("%s/lock", config.state);
    lock = open(lock_path, O_RDWR | O_CREAT, 0600);
    if (lock < 0) {
        aw_warn("%s: %s", lock_path, strerror(errno));
        return 1;
    }
    struct flock whole;
    memset(&whole, 0, sizeof whole);
    whole.l_type = F_WRLCK;
    whole.l_whence = SEEK_SET;
    if (fcntl(lock, F_SETLK, &whole)) {
        aw_log("a pass is going already (%s held): this one does nothing", lock_path);
        return named; /* a migration or an adoption asked for and not done */
    }
    free(lock_path);
    if (curl_global_init(CURL_GLOBAL_DEFAULT)) {
        aw_warn("libcurl could not start");
        return 1;
    }
    memset(&a, 0, sizeof a);
    a.config = &config;
    a.now = (long)time(NULL);
    a.fetch = aw_http;
    a.download = aw_http_download;
    a.upload = aw_http_upload;
    a.state_path = aw_format("%s/state.json", config.state);
    a.state = load(a.state_path);
    if (!strcmp(command, "migrate")) {
        aw_http_timeout(MIGRATION_SECONDS);
        rc = aw_migrate(&a, argv[2]);
    } else if (!strcmp(command, "adopt"))
        rc = aw_adopt(&a, argv[2]);
    else
        rc = aw_sync(&a);
    cJSON_Delete(a.state);
    free(a.state_path);
    curl_global_cleanup();
    close(lock);
    return rc;
}
