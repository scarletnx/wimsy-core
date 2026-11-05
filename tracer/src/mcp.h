/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* tracer-mcp's MCP server: the reports as tools for agents, read from the site's folder as Tracer draws it (the JSON in
   api/, and the logs), never from its database. Each POST holds one JSON-RPC message (MCP's Streamable HTTP transport,
   answered as JSON, without a stream); mcp_main.c is the HTTP around it */
#ifndef TR_MCP_H
#define TR_MCP_H
#include "tracer.h"

#define MCP_LINES     2000        /* the lines read_log returns at most */
#define MCP_TEXT_MAX  (128 << 10) /* the bytes of a tool's text at most */
#define MCP_IMAGE_MAX (8 << 20)   /* the bytes of a screenshot that get_screenshot returns at most */
#define MCP_WORDS     16          /* the words of a query at most */

typedef struct {
    const char* root; /* the site's folder */
    const char* url;  /* the site's address, without its last /, for the links in the answers */
} Mcp;

/* the answer to one JSON-RPC message, owned, and *status 200; NULL and *status 202 for a notification or an answer
   from the client; a JSON-RPC error and *status 400 for a body that is no JSON-RPC message */
char* mcp_handle(const Mcp* m, const char* body, size_t n, int* status);
/* the first TRACER_ entry (NAME=value) of env that is neither one of tracer-mcp's keys (TRACER_URL, TRACER_SITE,
   TRACER_MCP_LISTEN) nor one of the bot's (TR_KEYS), which it reads none of; NULL when each is */
const char* mcp_unknown_key(char* const* env);
#endif
