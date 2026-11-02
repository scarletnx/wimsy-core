/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* Tracer: the game reports that people send it on Discord, kept in a database and drawn as a static site.

    TRACER_TOKEN=... TRACER_URL=https://... [TRACER_CHANNELS=id,id] [TRACER_ADMINS=id,id] [TRACER_DB=file]
    [TRACER_SITE=folder] [TRACER_ASSETS=folder] [TRACER_TITLES=file] tracer

A report is deleted by the person who sent it, by the guild's owner and administrators, and by TRACER_ADMINS.

Concord's gateway brings the events, read here from their JSON, never decoded by Concord. A message that mentions the
bot in a channel it answers in, and each message deleted, wait for the thread that handles them, one at a time: a
report downloads its files, and the database is that thread's alone. The bot asks for no privileged intent: Discord
sends the text and the attachments of a message that mentions it */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <discord.h> /* Concord's */

#include "fetch.h"
#include "http.h"
#include "report.h"
#include "rest.h"
#include "site.h"
#include "titles.h"

#define LISTED  64   /* the channels and the admins named, at most */
#define WAITING 1024 /* the events that wait for the thread, at most */
#define PARENTS 256  /* the threads whose channel is remembered */

static struct {
    const char *token, *url, *db, *site, *assets, *titles;
    u64 channels[LISTED], admins[LISTED];
    int nchannels, nadmins;
} env;
static RhAuth discord_auth;
static Dc dc;
static Tracer tracer;
static Titles titles;
static u64 self;

static const char* const KNOWN[] = {"TRACER_TOKEN", "TRACER_URL",  "TRACER_CHANNELS", "TRACER_ADMINS",
                                    "TRACER_DB",    "TRACER_SITE", "TRACER_ASSETS",   "TRACER_TITLES"};

static void stop(const char* fmt, const char* value) {
    fputs("tracer: ", stderr);
    fprintf(stderr, fmt, value);
    fputc('\n', stderr);
    exit(1);
}
static const char* setting(const char* name, const char* otherwise) {
    const char* v = getenv(name);
    return v && *v ? v : otherwise;
}
/* name's ids, "id,id", into ids */
static int ids(const char* name, u64* to) {
    const char* list = setting(name, NULL);
    int n = 0;
    while (list && *list) {
        size_t len = strcspn(list, ","), k;
        char id[32];
        if (len && len < sizeof id) {
            memcpy(id, list, len);
            id[len] = 0;
            for (k = 0; k < len; ++k)
                if (!isdigit((unsigned char)id[k])) stop("%s is not an id (Developer Mode, Copy ID: its digits)", id);
            if (n == LISTED) stop("%s names more than 64 ids", name);
            to[n++] = strtoull(id, NULL, 10);
        }
        list += len + (list[len] == ',');
    }
    return n;
}
static void read_env(void) {
    extern char** environ;
    char** e;
    size_t k, n;
    for (e = environ; *e; ++e) { /* a key of ours misspelt would be a setting quietly not taken */
        int known = 0;
        if (strncmp(*e, "TRACER_", 7)) continue;
        for (k = 0; k < sizeof KNOWN / sizeof *KNOWN; ++k)
            known |= !strncmp(*e, KNOWN[k], strlen(KNOWN[k])) && (*e)[strlen(KNOWN[k])] == '=';
        if (!known) stop("%s: no such setting (TRACER_TOKEN, _URL, _CHANNELS, _ADMINS, _DB, _SITE, _ASSETS)", *e);
    }
    env.token = setting("TRACER_TOKEN", NULL);
    env.url = setting("TRACER_URL", NULL);
    if (!env.token || !env.url) stop("TRACER_TOKEN and TRACER_URL (the site's address) are needed%s", "");
    if (strncmp(env.url, "https://", 8) && strncmp(env.url, "http://", 7)) /* http:// for a site on this machine */
        stop("TRACER_URL=%s is not an https:// or http:// address", env.url);
    env.db = setting("TRACER_DB", "/var/db/tracer/tracer.db");
    env.site = setting("TRACER_SITE", "/var/db/tracer-site");
    env.assets = setting("TRACER_ASSETS", "/usr/local/share/tracer");
    env.titles = setting("TRACER_TITLES", NULL);
    env.nchannels = ids("TRACER_CHANNELS", env.channels);
    env.nadmins = ids("TRACER_ADMINS", env.admins);
    n = strlen(env.url);
    while (n && env.url[n - 1] == '/') --n;
    env.url = rei_format("%.*s", (int)n, env.url);
}

/* ---- the events waiting for the thread ---- */

enum { EV_CREATE, EV_DELETE };
static struct {
    int kind[WAITING];
    char* json[WAITING]; /* owned: a MESSAGE_CREATE's data, or NULL with id for a deletion */
    u64 id[WAITING];
    int first, n;
    pthread_mutex_t lock;
    pthread_cond_t more;
} waiting = {{0}, {0}, {0}, 0, 0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER};

static void push(int kind, const char* json, size_t len, u64 id) {
    pthread_mutex_lock(&waiting.lock);
    if (waiting.n < WAITING) {
        int at = (waiting.first + waiting.n++) % WAITING;
        waiting.kind[at] = kind;
        waiting.json[at] = json ? rei_format("%.*s", (int)len, json) : NULL;
        waiting.id[at] = id;
        pthread_cond_signal(&waiting.more);
    } else
        rei_log("%d events wait already: one is dropped", WAITING);
    pthread_mutex_unlock(&waiting.lock);
}

/* ---- the channels it answers in ---- */

static int listed(u64 channel) {
    int i;
    for (i = 0; i < env.nchannels; ++i)
        if (env.channels[i] == channel) return 1;
    return 0;
}
/* a channel listed, or a thread of one (a forum's post too): the thread's parent asked of Discord once */
static int answers_in(u64 channel) {
    static u64 known[PARENTS];
    static int answer[PARENTS], next;
    char* url;
    RgAnswer a;
    cJSON* c;
    int i, yes = 0;
    if (!env.nchannels || listed(channel)) return 1;
    for (i = 0; i < PARENTS; ++i)
        if (known[i] == channel) return answer[i];
    url = rei_format(DC_API "channels/%llu", (unsigned long long)channel);
    rh_fetch(&discord_auth, "GET", url, NULL, NULL, &a);
    if (a.status == 200 && (c = cJSON_Parse(a.body))) {
        const char* parent = jstr(c, "parent_id");
        yes = parent && listed(strtoull(parent, NULL, 10));
        cJSON_Delete(c);
    } else
        rei_log("channel %llu could not be read: %s", (unsigned long long)channel, a.error ? a.error : "no access");
    free(url);
    free(a.body);
    free(a.etag);
    free(a.error);
    known[next] = channel;
    answer[next] = yes;
    next = (next + 1) % PARENTS;
    return yes;
}
#define ADMINISTRATOR 0x8ULL /* the permission's bit */
/* guild's owner, or a member whose roles (and @everyone, the role whose id is the guild's) give Administrator: the
   guild's roles asked of Discord, as a message names its author's roles and not their permissions */
static int administers(void* ctx, u64 guild, u64 user, const u64* roles, int nroles) {
    char* url = rei_format(DC_API "guilds/%llu", (unsigned long long)guild);
    RgAnswer a;
    cJSON* g;
    const cJSON* role;
    unsigned long long permissions = 0;
    int i, yes = 0;
    (void)ctx;
    rh_fetch(&discord_auth, "GET", url, NULL, NULL, &a);
    if (a.status == 200 && (g = cJSON_Parse(a.body))) {
        const char* owner = jstr(g, "owner_id");
        yes = owner && strtoull(owner, NULL, 10) == user;
        cJSON_ArrayForEach(role, cJSON_GetObjectItemCaseSensitive(g, "roles")) {
            const char *id = jstr(role, "id"), *bits = jstr(role, "permissions");
            u64 rid = id ? strtoull(id, NULL, 10) : 0;
            int has = rid == guild;
            for (i = 0; i < nroles && !has; ++i) has = roles[i] == rid;
            if (has && bits) permissions |= strtoull(bits, NULL, 10);
        }
        yes |= (permissions & ADMINISTRATOR) != 0;
        cJSON_Delete(g);
    } else
        rei_log("guild %llu could not be read: %s", (unsigned long long)guild, a.error ? a.error : "no access");
    free(url);
    free(a.body);
    free(a.etag);
    free(a.error);
    return yes;
}

static void typing(u64 channel) {
    char* url = rei_format(DC_API "channels/%llu/typing", (unsigned long long)channel);
    RgAnswer a;
    rh_fetch(&discord_auth, "POST", url, "{}", NULL, &a);
    free(url);
    free(a.body);
    free(a.etag);
    free(a.error);
}

static void* handle(void* unused) {
    (void)unused;
    for (;;) {
        int kind;
        char* json;
        u64 id;
        pthread_mutex_lock(&waiting.lock);
        while (!waiting.n) pthread_cond_wait(&waiting.more, &waiting.lock);
        kind = waiting.kind[waiting.first];
        json = waiting.json[waiting.first];
        id = waiting.id[waiting.first];
        waiting.first = (waiting.first + 1) % WAITING;
        --waiting.n;
        pthread_mutex_unlock(&waiting.lock);
        if (kind == EV_CREATE) {
            TrMessage m;
            if (tr_message(json, strlen(json), self, &m)) {
                if (answers_in(m.channel)) {
                    cJSON* e;
                    typing(m.channel);
                    long report;
                    if ((e = tr_answer(&tracer, &m, &report)))
                        tr_replied(&tracer, report, dc_post(&dc, m.channel, e, m.id));
                    cJSON_Delete(e);
                }
                tr_message_free(&m);
            }
        } else
            tr_forget(&tracer, id);
        free(json);
    }
    return NULL;
}

/* ---- the gateway ---- */

static void ready(const char* json, size_t size) {
    static int started;
    char* name;
    self = tr_ready(json, size, &name);
    rei_log("up as %s, the site at %s; channels: %s", name ? name : "a user READY did not name", env.url,
            env.nchannels ? "those named" : "every channel it sees");
    free(name);
    if (!started) {
        pthread_t thread;
        int code = pthread_create(&thread, NULL, handle, NULL);
        if (code) stop("the thread of the reports did not start: %s", strerror(code));
        pthread_detach(thread);
        started = 1;
    }
}
/* the gateway's thread: a message that mentions the bot in a server waits for the thread, the others are dropped here;
   each deletion waits, the thread knows which made a report */
static enum discord_event_scheduler scheduler(struct discord* client, const char data[], size_t size,
                                              enum discord_gateway_events event) {
    (void)client;
    if (event == DISCORD_EV_READY) ready(data, size);
    if (event == DISCORD_EV_MESSAGE_CREATE) {
        TrMessage m;
        if (tr_message(data, size, self, &m)) {
            if (m.mentioned && m.guild) push(EV_CREATE, data, size, 0);
            tr_message_free(&m);
        }
    }
    if (event == DISCORD_EV_MESSAGE_DELETE) {
        u64 id = tr_deleted(data, size);
        if (id) push(EV_DELETE, NULL, 0, id);
    }
    if (event == DISCORD_EV_MESSAGE_DELETE_BULK) {
        cJSON *d = cJSON_ParseWithLength(data, size), *each;
        cJSON_ArrayForEach(each, cJSON_GetObjectItemCaseSensitive(d, "ids")) if (cJSON_IsString(each))
            push(EV_DELETE, NULL, 0, strtoull(each->valuestring, NULL, 10));
        cJSON_Delete(d);
    }
    return DISCORD_EVENT_IGNORE;
}

int main(void) {
    struct discord* client;
    struct discord_config config;
    CCORDcode code;
    read_env();
    curl_global_init(CURL_GLOBAL_DEFAULT);
    snprintf(discord_auth.authorization, sizeof discord_auth.authorization, "Bot %s", env.token);
    dc_init(&dc, rh_fetch, &discord_auth);
    tracer.store = st_open(env.db);
    if (!tracer.store) stop("the database %s could not be opened", env.db);
    tracer.root = env.site;
    tracer.url = env.url;
    tracer.get = tr_fetch;
    tracer.admins = env.admins;
    tracer.nadmins = env.nadmins;
    tracer.administers = administers;
    if (site_assets(env.assets, env.site)) stop("the site's files could not be copied from %s", env.assets);
    if (!env.titles)
        rei_log("TRACER_TITLES is not set: no list of games' names, a game its logs don't name keeps its title ID");
    else if (!tr_titles_load(&titles, env.titles)) {
        tracer.titles = &titles;
        rei_log("%d games' names from %s", titles.n, env.titles);
    }
    { /* a new log summary or list of names: the reports summarized and named again with them */
        char* version = rei_format("%s %s %s", TR_LOGSCAN, titles.version, TR_FACTS);
        tr_rescan(&tracer, version);
        free(version);
    }
    if (site_rebuild(tracer.store, env.site)) rei_log("some pages could not be drawn again");
    memset(&config, 0, sizeof config);
    config.token = (char*)env.token;
    config.log.level = LOGMOD_LEVEL_WARN; /* Concord's traces name every event: only what goes wrong is logged */
    client = discord_from_config(&config);
    if (!client) stop("Concord could not start a client with TRACER_TOKEN%s", "");
    discord_add_intents(client, DISCORD_GATEWAY_GUILDS | DISCORD_GATEWAY_GUILD_MESSAGES);
    discord_set_event_scheduler(client, scheduler);
    code = discord_run(client);
    rei_log("the gateway stopped: %s", discord_strerror(code, client));
    discord_cleanup(client);
    ccord_global_cleanup();
    curl_global_cleanup();
    st_close(tracer.store);
    return 1; /* daemon(8) starts it again */
}
