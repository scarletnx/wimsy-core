/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* Tracer: the game reports that people send it on Discord, kept in a database and drawn as a static site.

    TRACER_TOKEN=... TRACER_URL=https://... [TRACER_CHANNELS=id,id] [TRACER_ADMINS=id,id] [TRACER_DB=file]
    [TRACER_SITE=folder] [TRACER_ASSETS=folder] [TRACER_TITLES=file] [TRACER_UPSTREAM_REPO=owner/name] tracer

A report is deleted by the person who sent it, by the guild's owner and administrators, and by TRACER_ADMINS.

Concord's gateway brings the events, read here from their JSON, never decoded by Concord. A message that mentions the
bot in a channel it answers in, and each message deleted, wait for the thread that handles them, one at a time: a
report's files are downloaded and read by a worker (worker.h, this program run again as "tracer --report", without the
token), and the database is that thread's alone. The bot asks for no privileged intent: Discord
sends the text and the attachments of a message that mentions it */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <signal.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <discord.h> /* Concord's */

#include "age.h"
#include "channels.h"
#include "fetch.h"
#include "issues.h"
#include "http.h"
#include "report.h"
#include "rest.h"
#include "site.h"
#include "titles.h"
#include "worker.h"

#define LISTED  64   /* the channels and the admins named, at most */
#define WAITING 1024 /* the events that wait for the thread, at most */

static struct {
    const char *token, *url, *db, *site, *assets, *titles;
    u64 channels[LISTED], admins[LISTED];
    int nchannels, nadmins;
} env;
static RhAuth discord_auth, github_auth;
static Dc dc;
static TrIssues issues; /* issues.repo NULL: no report is filed as an issue */
static TrAge age;       /* age.fetch NULL: no token, nothing asked of GitHub */
static Tracer tracer;
static Titles titles;
static u64 self;

static const char* const KNOWN[] = {
    "TRACER_TOKEN",        "TRACER_URL",           "TRACER_CHANNELS",     "TRACER_ADMINS",      "TRACER_DB",
    "TRACER_SITE",         "TRACER_ASSETS",        "TRACER_TITLES",       "TRACER_ISSUES_REPO", "TRACER_ISSUES_TOKEN",
    "TRACER_ISSUES_EMOJI", "TRACER_ISSUES_LABELS", "TRACER_UPSTREAM_REPO"};

/* the bot's end, with 1, from which daemon(8) starts it again. _exit, not exit: the thread of the events may be inside
   a request, which libcurl's cleanup and exit's handlers (OpenSSL's) must not run under */
static void end(void) {
    fflush(NULL);
    _exit(1);
}
static void stop(const char* fmt, const char* value) {
    fputs("tracer: ", stderr);
    fprintf(stderr, fmt, value);
    fputc('\n', stderr);
    end();
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
static const char* environment(const char* name) { return getenv(name); }
static void read_env(void) {
    extern char** environ;
    char** e;
    size_t k, n;
    for (e = environ; *e; ++e) { /* a key of ours misspelt would be a setting quietly not taken */
        int known = 0;
        if (strncmp(*e, "TRACER_", 7)) continue;
        for (k = 0; k < sizeof KNOWN / sizeof *KNOWN; ++k)
            known |= !strncmp(*e, KNOWN[k], strlen(KNOWN[k])) && (*e)[strlen(KNOWN[k])] == '=';
        if (!known)
            stop("%s: no such setting (TRACER_TOKEN, _URL, _CHANNELS, _ADMINS, _DB, _SITE, _ASSETS, _TITLES, "
                 "_ISSUES_REPO, _ISSUES_TOKEN, _ISSUES_EMOJI, _ISSUES_LABELS, _UPSTREAM_REPO)",
                 *e);
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
    { /* the issues: their token goes into the GitHub requests' header alone, never into the log */
        const char* token;
        char* why;
        if (tr_issues_settings(&issues, environment, &token, &why)) stop("%s", why);
        if (token) snprintf(github_auth.authorization, sizeof github_auth.authorization, "Bearer %s", token);
        github_auth.accept = "application/vnd.github+json";
        issues.fetch = rh_fetch;
        issues.ctx = &github_auth;
        /* a build's age: asked with the same token, once a commit */
        if (tr_age_settings(&age, environment, &why)) stop("%s", why);
        age.fetch = token ? rh_fetch : NULL;
        age.ctx = &github_auth;
    }
}

/* ---- the events waiting for the thread ---- */

enum { EV_CREATE, EV_DELETE, EV_REACTION };
static struct {
    int kind[WAITING];
    char* json[WAITING]; /* owned: a MESSAGE_CREATE's or MESSAGE_REACTION_ADD's data, or NULL with id for a deletion */
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

/* channel's parent (a thread's channel, a forum post's forum), asked of Discord; 0 for none or no answer (said) */
static u64 parent_of(void* ctx, u64 channel) {
    char* url = rei_format(DC_API "channels/%llu", (unsigned long long)channel);
    RgAnswer a;
    cJSON* c;
    u64 parent = 0;
    (void)ctx;
    rh_fetch(&discord_auth, "GET", url, NULL, NULL, &a);
    if (a.status == 200 && (c = cJSON_Parse(a.body))) {
        const char* id = jstr(c, "parent_id");
        parent = id ? strtoull(id, NULL, 10) : 0;
        cJSON_Delete(c);
    } else
        rei_log("channel %llu could not be read: %s", (unsigned long long)channel, a.error ? a.error : "no access");
    free(url);
    free(a.body);
    free(a.etag);
    free(a.error);
    return parent;
}
static TrParents parents;
static int answers_in(u64 channel) {
    return tr_answers_in(env.channels, env.nchannels, &parents, channel, parent_of, NULL);
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

/* GET channels/C/messages/ID, for a reply that asks for a report of the message it replies to: the message's JSON
   (owned) and Discord's status; NULL, 0 and why when Discord did not answer */
static char* message_got(void* ctx, u64 channel, u64 id, int* status, char** why) {
    char* url = rei_format(DC_API "channels/%llu/messages/%llu", (unsigned long long)channel, (unsigned long long)id);
    RgAnswer a;
    (void)ctx;
    rh_fetch(&discord_auth, "GET", url, NULL, NULL, &a);
    free(url);
    *status = a.status;
    *why = a.error ? rei_strdup(a.error) : a.status != 200 && a.body ? rei_format("%.200s", a.body) : NULL;
    free(a.etag);
    free(a.error);
    return a.body;
}
#define CONTENT_INTENT 0xc0000UL /* the application's flags of the Message Content intent: approved, or limited */
/* whether Discord shows the bot the files of the messages that do not mention it, by its application's flags: 1, 0,
   or -1 with why when Discord did not say */
static int content_intent(void* ctx, char** why) {
    RgAnswer a;
    cJSON* app;
    int yes = -1;
    (void)ctx;
    *why = NULL;
    rh_fetch(&discord_auth, "GET", DC_API "applications/@me", NULL, NULL, &a);
    if (a.status == 200 && (app = cJSON_Parse(a.body))) {
        const cJSON* flags = cJSON_GetObjectItemCaseSensitive(app, "flags");
        if (cJSON_IsNumber(flags))
            yes = ((unsigned long)flags->valuedouble & CONTENT_INTENT) != 0;
        else
            *why = rei_strdup("GET applications/@me answered no flags");
        cJSON_Delete(app);
    } else
        *why = a.status ? rei_format("GET applications/@me answered %d: %.200s", a.status, a.body ? a.body : "nothing")
                        : rei_format("GET applications/@me: %s", a.error ? a.error : "no answer");
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
        } else if (kind == EV_REACTION) {
            TrReaction r;
            if (tr_reaction(json, strlen(json), self, &r)) {
                cJSON* e = tr_approved(&tracer, &r);
                if (e) dc_post(&dc, r.channel, e, r.message);
                cJSON_Delete(e);
                tr_reaction_free(&r);
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
    {
        char* channels = tr_channels_said(env.channels, env.nchannels);
        rei_log("up as %s, the site at %s; channels: %s", name ? name : "a user READY did not name", env.url, channels);
        free(channels);
    }
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
    if (event == DISCORD_EV_MESSAGE_REACTION_ADD && issues.repo) { /* the approval's emoji alone waits */
        TrReaction r;
        if (tr_reaction(data, size, self, &r)) {
            if (r.guild && !strcmp(r.emoji, issues.emoji)) push(EV_REACTION, data, size, 0);
            tr_reaction_free(&r);
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

/* a report's files taken by this program run again as "--report" (ctx its argv) */
static int worker(void* ctx, Tracer* t, const TrMessage* m, Taken* k, char** why) {
    (void)t;
    return wk_take(ctx, WK_SECONDS, m, k, why);
}

int main(int argc, char** argv) {
    static char* again[3] = {NULL, "--report", NULL};
    struct discord* client;
    struct discord_config config;
    CCORDcode code;
    if (argc == 2 && !strcmp(argv[1], "--report")) { /* a worker: no settings, no token, its own limits */
        int failed;
        wk_limit();
        curl_global_init(CURL_GLOBAL_DEFAULT);
        failed = wk_serve(stdin, stdout, tr_fetch, NULL);
        curl_global_cleanup();
        return failed;
    }
    if (argc != 1) stop("%s takes no argument: its settings are in its environment (the env file)", argv[0]);
    if (argv[0][0] != '/') stop("%s is not run by its full path, which its workers need", argv[0]);
    again[0] = argv[0];
    signal(SIGPIPE, SIG_IGN); /* a worker gone before it read its question is an error said, not the bot's end */
    read_env();
    curl_global_init(CURL_GLOBAL_DEFAULT);
    snprintf(discord_auth.authorization, sizeof discord_auth.authorization, "Bot %s", env.token);
    dc_init(&dc, rh_fetch, &discord_auth);
    tracer.store = st_open(env.db);
    if (!tracer.store) stop("the database %s could not be opened", env.db);
    tracer.root = env.site;
    tracer.url = env.url;
    tracer.get = tr_fetch;
    { /* the files wait beside the database, never in the site */
        char* db = rei_strdup(env.db);
        /* (dirname): FreeBSD's libgen.h makes dirname a C11 _Generic macro, which -std=c99 -Wpedantic refuses; the
           function itself, which may write into its argument, gets a copy */
        tracer.work = rei_format("%s/work", (dirname)(db));
        free(db);
        if (mkdir(tracer.work, 0700) && errno != EEXIST)
            stop("%s", rei_format("the work folder %s could not be made: %s", tracer.work, strerror(errno)));
    }
    tracer.take = worker;
    tracer.take_ctx = again;
    tracer.admins = env.admins;
    tracer.nadmins = env.nadmins;
    tracer.administers = administers;
    tracer.issues = &issues;
    tracer.age = &age;
    tracer.message = message_got;
    tracer.content_intent = content_intent;
    { /* what a reply alone to a message with a log gets, said once at the start; asked again at each such reply */
        char* why;
        int sees = content_intent(NULL, &why);
        rei_log("replies: a reply that mentions me with no file makes a report of the message it replies to; %s",
                sees == 1 ? "Discord shows me that message's files (the Message Content intent is on)"
                : !sees   ? "Discord does not show me that message's files, as the Message Content intent is off: "
                            "such a reply is told to send the log with a mention"
                : why     ? why
                          : "Discord did not say whether it shows me that message's files");
        free(why);
    }
    {
        char* said = tr_issues_said(&issues);
        rei_log("%s", said);
        free(said);
        said = tr_age_said(&age);
        rei_log("%s", said);
        free(said);
    }
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
    /* a copy for Concord to own: discord_from_config keeps the pointer it is given, and discord_cleanup frees it */
    config.token = rei_strdup(env.token);
    config.log.level = LOGMOD_LEVEL_WARN; /* Concord's traces name every event: only what goes wrong is logged */
    client = discord_from_config(&config);
    if (!client) stop("Concord could not start a client with TRACER_TOKEN%s", "");
    discord_add_intents(client, DISCORD_GATEWAY_GUILDS | DISCORD_GATEWAY_GUILD_MESSAGES |
                                    (issues.repo ? DISCORD_GATEWAY_GUILD_MESSAGE_REACTIONS : 0));
    discord_set_event_scheduler(client, scheduler);
    code = discord_run(client);
    if (code == CCORD_DISCORD_BAD_AUTH) { /* what Concord's discord_run returns for any failure of GET gateway/bot */
        char* now = dc_said(&dc, "gateway/bot");
        rei_log("the gateway stopped: Concord's GET gateway/bot failed, which it calls \"%s\" whatever the failure, a "
                "timeout too; asked again, Discord answers %s",
                discord_strerror(code, client), now);
        free(now);
    } else
        rei_log("the gateway stopped: %s", discord_strerror(code, client));
    end();
    return 1;
}
