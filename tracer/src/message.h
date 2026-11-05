/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* What Tracer reads of the gateway's events, from their JSON as Discord sent it (the payload's "d"), and what a message
   that mentions it asks for. Without the Message Content intent, Discord sends a message's text and attachments only
   when the message mentions the bot (or is a direct message), which is all Tracer reads */
#ifndef TR_MESSAGE_H
#define TR_MESSAGE_H
#include "tracer.h"

typedef struct {
    char *name, *url, *type; /* owned; type is "" when Discord gave none */
    double size;
} TrAttachment;

typedef struct {
    u64 id, channel, guild, author; /* guild is 0 in a direct message */
    u64 roles[TR_ROLES];            /* the author's roles in the guild, as the message's member says */
    int nroles;
    u64 reply_to;                   /* the message it replies to, or 0 */
    u64 reply_channel, reply_guild; /* where that message is, as the reply's reference says (0 when it does not) */
    long reply_report;              /* the report that message's embed links (r/<id>/), when the bot wrote it; 0 else */
    int mentioned;                  /* the message mentions self */
    int bot;                        /* a bot or a webhook sent it (tr_message_of's; tr_message takes no such message) */
    char* content;                  /* owned; "" for a message of files alone */
    TrAttachment files[TR_FILES];
    int nfiles;
} TrMessage;

/* MESSAGE_CREATE's message, *m filled and 1 when a person wrote it; 0, nothing owned, for a bot's or a webhook's, and
   for one that is no JSON object with an author, an id and a channel id */
int tr_message(const char* json, size_t len, u64 self, TrMessage* m);
/* a message as Discord's REST API gives it (GET channels/C/messages/ID: no guild id, no member), *m filled and 1, its
   bot set for a bot's or a webhook's; 0, nothing owned, for one that is no JSON object with an author, an id and a
   channel id */
int tr_message_of(const char* json, size_t len, TrMessage* m);
void tr_message_free(TrMessage* m);
typedef struct {
    u64 user, channel, message, guild; /* who reacted, to which message, in which server (0 in a direct message) */
    u64 roles[TR_ROLES];               /* the user's roles in the guild, as the reaction's member says */
    int nroles;
    char* emoji; /* owned: its character, or a server emoji's name */
} TrReaction;
/* MESSAGE_REACTION_ADD's reaction, *r filled and 1 when a person added it; 0, nothing owned, for a bot's (self's too)
   and for one that names no user, message or emoji */
int tr_reaction(const char* json, size_t len, u64 self, TrReaction* r);
void tr_reaction_free(TrReaction* r);
/* MESSAGE_DELETE's message id, or 0 */
u64 tr_deleted(const char* json, size_t len);
/* READY's user id, and its name (owned, or NULL) when name is given */
u64 tr_ready(const char* json, size_t len, char** name);

typedef enum { TR_REPORT, TR_HELP, TR_DELETE, TR_ADD } TrVerb;
/* what content asks of the bot, its mentions left out: "help"; "delete 12" (or "delete #12", *number 12), or
   "delete" alone (*number 0: the report the message replies to); "add 12" (or "add #12") and a note after it; else a
   report. *note (owned) is the rest of the text on one line, cut to TR_NOTE bytes; "" for none */
TrVerb tr_command(const char* content, char** note, long* number);
/* content's words, its mentions left out, on one line and cut to TR_NOTE bytes, as a report's note; owned */
char* tr_words(const char* content);
#endif
