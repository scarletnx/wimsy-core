/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* tracer-mcp: Tracer's reports as an MCP server for agents, over HTTP (libmicrohttpd), read-only.

    TRACER_URL=https://... [TRACER_SITE=folder] [TRACER_MCP_LISTEN=127.0.0.1:8081] tracer-mcp

It reads only the site's folder that Tracer draws (mcp.h), so it runs where the site is served, a web server sending
it the requests to /mcp. A POST to /mcp is one JSON-RPC message, answered as JSON; GET and DELETE get 405, as it opens
no stream and keeps no session. A browser's request from another site (its Origin header not TRACER_URL's) gets 403,
as MCP's transport asks */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <microhttpd.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mcp.h"

#define BODY_MAX (1 << 20) /* a request's bytes at most */
#define ENDPOINT "/mcp"

static Mcp mcp;
static char origin[512]; /* TRACER_URL's scheme, host and port */

/* the settings of tracer's env file too, which this program may share */
static const char* const KNOWN[] = {"TRACER_TOKEN", "TRACER_URL",    "TRACER_CHANNELS",   "TRACER_ADMINS", "TRACER_DB",
                                    "TRACER_SITE",  "TRACER_ASSETS", "TRACER_MCP_LISTEN", "TRACER_TITLES"};

static void stop(const char* fmt, const char* value) {
    fputs("tracer-mcp: ", stderr);
    fprintf(stderr, fmt, value);
    fputc('\n', stderr);
    exit(1);
}
static const char* setting(const char* name, const char* otherwise) {
    const char* v = getenv(name);
    return v && *v ? v : otherwise;
}

typedef struct {
    Buf body;
    int over;
} Upload;

static enum MHD_Result reply(struct MHD_Connection* c, unsigned status, const char* type, const char* text) {
    struct MHD_Response* r = MHD_create_response_from_buffer_copy(text ? strlen(text) : 0, text ? text : "");
    enum MHD_Result done;
    if (!r) return MHD_NO;
    if (type) MHD_add_response_header(r, "Content-Type", type);
    MHD_add_response_header(r, "Cache-Control", "no-store");
    MHD_add_response_header(r, "X-Content-Type-Options", "nosniff");
    if (status == 405) MHD_add_response_header(r, "Allow", "POST");
    done = MHD_queue_response(c, status, r);
    MHD_destroy_response(r);
    return done;
}

/* each request: its headers first (*state NULL), then its body in pieces, then once with none left */
static enum MHD_Result handle(void* cls, struct MHD_Connection* c, const char* url, const char* method,
                              const char* version, const char* data, size_t* size, void** state) {
    Upload* u = *state;
    (void)cls;
    (void)version;
    if (!u) {
        const char* from = MHD_lookup_connection_value(c, MHD_HEADER_KIND, "Origin");
        if (strcmp(url, ENDPOINT)) return reply(c, 404, "text/plain", "Tracer's MCP server is at " ENDPOINT "\n");
        if (strcmp(method, "POST"))
            return reply(c, 405, "text/plain", "MCP's Streamable HTTP: POST a JSON-RPC message; no stream is opened\n");
        if (from && strcmp(from, origin)) return reply(c, 403, "text/plain", "another site's page may not call this\n");
        if (!(u = calloc(1, sizeof *u))) return MHD_NO;
        *state = u;
        return MHD_YES;
    }
    if (*size) {
        if (*size > BODY_MAX - u->body.len)
            u->over = 1;
        else
            buf_addn(&u->body, data, *size);
        *size = 0;
        return MHD_YES;
    }
    if (u->over) return reply(c, 413, "text/plain", "a request is 1 MiB at most\n");
    {
        int status;
        char* text = mcp_handle(&mcp, u->body.text ? u->body.text : "", u->body.len, &status);
        enum MHD_Result done = reply(c, (unsigned)status, text ? "application/json" : NULL, text);
        free(text);
        return done;
    }
}
static void completed(void* cls, struct MHD_Connection* c, void** state, enum MHD_RequestTerminationCode why) {
    Upload* u = *state;
    (void)cls;
    (void)c;
    (void)why;
    if (u) {
        buf_free(&u->body);
        free(u);
        *state = NULL;
    }
}

int main(void) {
    extern char** environ;
    char** e;
    const char *url, *listen, *colon;
    char host[64];
    struct sockaddr_in addr;
    struct MHD_Daemon* d;
    size_t k, n;
    long port;
    for (e = environ; *e; ++e) {
        int known = 0;
        if (strncmp(*e, "TRACER_", 7)) continue;
        for (k = 0; k < sizeof KNOWN / sizeof *KNOWN; ++k)
            known |= !strncmp(*e, KNOWN[k], strlen(KNOWN[k])) && (*e)[strlen(KNOWN[k])] == '=';
        if (!known) stop("%s: no such setting (TRACER_URL, TRACER_SITE, TRACER_MCP_LISTEN)", *e);
    }
    url = setting("TRACER_URL", NULL);
    if (!url || (strncmp(url, "https://", 8) && strncmp(url, "http://", 7)))
        stop("TRACER_URL, the site's https:// (or http://) address, is needed%s", "");
    n = strlen(url);
    while (n && url[n - 1] == '/') --n;
    mcp.url = rei_format("%.*s", (int)n, url);
    n = strcspn(strstr(mcp.url, "://") + 3, "/") + (size_t)(strstr(mcp.url, "://") + 3 - mcp.url);
    if (n >= sizeof origin) stop("TRACER_URL=%s is too long", url);
    memcpy(origin, mcp.url, n);
    origin[n] = 0;
    mcp.root = setting("TRACER_SITE", "/var/db/tracer-site");
    listen = setting("TRACER_MCP_LISTEN", "127.0.0.1:8081");
    colon = strrchr(listen, ':');
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    port = colon ? strtol(colon + 1, NULL, 10) : 0;
    if (!colon || (size_t)(colon - listen) >= sizeof host || port < 1 || port > 65535)
        stop("TRACER_MCP_LISTEN=%s is not ADDRESS:PORT", listen);
    memcpy(host, listen, (size_t)(colon - listen));
    host[colon - listen] = 0;
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) stop("TRACER_MCP_LISTEN=%s: not an IPv4 address", listen);
    addr.sin_port = htons((unsigned short)port);
    d = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_ERROR_LOG, (unsigned short)port, NULL, NULL, handle,
                         NULL, MHD_OPTION_SOCK_ADDR, (struct sockaddr*)&addr, MHD_OPTION_THREAD_POOL_SIZE,
                         (unsigned int)4, MHD_OPTION_CONNECTION_LIMIT, (unsigned int)64, MHD_OPTION_CONNECTION_TIMEOUT,
                         (unsigned int)30, MHD_OPTION_NOTIFY_COMPLETED, completed, NULL, MHD_OPTION_END);
    if (!d) stop("could not listen on %s", listen);
    rei_log("MCP on http://%s" ENDPOINT ", the site %s at %s", listen, mcp.root, mcp.url);
    for (;;) pause(); /* the daemon's threads answer; a signal ends it */
}
