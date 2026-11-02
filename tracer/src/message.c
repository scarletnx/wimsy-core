/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "message.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* a snowflake, a string of digits; 0 for anything else */
static u64 id_of(const cJSON* o, const char* key) {
    const char* s = jstr(o, key);
    char* end;
    u64 v;
    if (!s || *s < '0' || *s > '9') return 0;
    v = strtoull(s, &end, 10);
    return *end ? 0 : v;
}

int tr_message(const char* json, size_t len, u64 self, TrMessage* m) {
    cJSON* d = cJSON_ParseWithLength(json, len);
    const cJSON *author = cJSON_GetObjectItemCaseSensitive(d, "author"), *each;
    const char* content = jstr(d, "content");
    int ok;
    memset(m, 0, sizeof *m);
    m->id = id_of(d, "id");
    m->channel = id_of(d, "channel_id");
    m->guild = id_of(d, "guild_id");
    m->author = id_of(author, "id");
    m->reply_to = id_of(cJSON_GetObjectItemCaseSensitive(d, "message_reference"), "message_id");
    { /* a reply to the bot's answer: Discord sends that message with it, its embed linking the report */
        const cJSON* to = cJSON_GetObjectItemCaseSensitive(d, "referenced_message");
        const char* url = jstr(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(to, "embeds"), 0), "url");
        const char* at = url ? strstr(url, "/r/") : NULL;
        char* end;
        long v = at && isdigit((unsigned char)at[3]) ? strtol(at + 3, &end, 10) : 0;
        if (self && id_of(cJSON_GetObjectItemCaseSensitive(to, "author"), "id") == self && v > 0 && !strcmp(end, "/"))
            m->reply_report = v;
    }
    ok = cJSON_IsObject(author) && !jbool(author, "bot") && !jstr(d, "webhook_id") && m->id && m->channel && m->author;
    if (!ok) {
        memset(m, 0, sizeof *m);
        cJSON_Delete(d);
        return 0;
    }
    cJSON_ArrayForEach(each, cJSON_GetObjectItemCaseSensitive(d, "mentions")) m->mentioned |=
        self && id_of(each, "id") == self;
    m->content = rei_strdup(content ? content : "");
    cJSON_ArrayForEach(each, jpath(d, "member.roles")) {
        u64 role = cJSON_IsString(each) ? strtoull(each->valuestring, NULL, 10) : 0;
        if (role && m->nroles < TR_ROLES) m->roles[m->nroles++] = role;
    }
    cJSON_ArrayForEach(each, cJSON_GetObjectItemCaseSensitive(d, "attachments")) {
        TrAttachment* a = &m->files[m->nfiles];
        const char *name = jstr(each, "filename"), *url = jstr(each, "url"), *type = jstr(each, "content_type");
        if (m->nfiles == TR_FILES) break;
        if (!name || !url) continue;
        a->name = rei_strdup(name);
        a->url = rei_strdup(url);
        a->type = rei_strdup(type ? type : "");
        a->size = jnum(each, "size");
        ++m->nfiles;
    }
    cJSON_Delete(d);
    return 1;
}

void tr_message_free(TrMessage* m) {
    int i;
    for (i = 0; i < m->nfiles; ++i) {
        free(m->files[i].name);
        free(m->files[i].url);
        free(m->files[i].type);
    }
    free(m->content);
    memset(m, 0, sizeof *m);
}

u64 tr_deleted(const char* json, size_t len) {
    cJSON* d = cJSON_ParseWithLength(json, len);
    u64 id = id_of(d, "id");
    cJSON_Delete(d);
    return id;
}

u64 tr_ready(const char* json, size_t len, char** name) {
    cJSON* d = cJSON_ParseWithLength(json, len);
    const cJSON* user = cJSON_GetObjectItemCaseSensitive(d, "user");
    u64 id = id_of(user, "id");
    if (name) *name = jstr(user, "username") ? rei_strdup(jstr(user, "username")) : NULL;
    cJSON_Delete(d);
    return id;
}

/* the length of a mention at s ("<@1>", "<@!1>", "<@&1>", "<#1>"), its id in *id; 0 when s starts none */
static size_t mention(const char* s, u64* id) {
    size_t i = 1;
    if (s[0] != '<') return 0;
    if (s[i] == '@') {
        ++i;
        if (s[i] == '!' || s[i] == '&') ++i;
    } else if (s[i] == '#')
        ++i;
    else
        return 0;
    if (!isdigit((unsigned char)s[i])) return 0;
    *id = strtoull(s + i, NULL, 10);
    while (isdigit((unsigned char)s[i])) ++i;
    return s[i] == '>' ? i + 1 : 0;
}

TrVerb tr_command(const char* content, char** note, long* number) {
    Buf text = {0};
    const char* s;
    char *t, *end;
    size_t n;
    int space = 0;
    TrVerb verb = TR_REPORT;
    *note = NULL;
    *number = 0;
    for (s = content; *s;) { /* the mentions left out, the lines and spaces made one space each */
        u64 id;
        if ((n = mention(s, &id))) {
            s += n;
            space = 1;
        } else if (isspace((unsigned char)*s)) {
            ++s;
            space = 1;
        } else {
            if (space && text.len) buf_add(&text, " ");
            space = 0;
            buf_addn(&text, s++, 1);
        }
    }
    t = buf_take(&text);
    if (!strcasecmp(t, "help")) {
        free(t);
        return TR_HELP;
    }
    if (!strcasecmp(t, "delete")) {
        free(t);
        return TR_DELETE;
    }
    if (!strncasecmp(t, "delete ", 7)) {
        const char* at = t + 7 + (t[7] == '#');
        long v = isdigit((unsigned char)*at) ? strtol(at, &end, 10) : 0;
        if (v > 0 && !*end) {
            free(t);
            *number = v;
            return TR_DELETE;
        }
    }
    if (!strncasecmp(t, "add ", 4)) { /* add 12, and the note after it */
        const char* at = t + 4 + (t[4] == '#');
        long v = isdigit((unsigned char)*at) ? strtol(at, &end, 10) : 0;
        if (v > 0 && (!*end || *end == ' ')) {
            const char* rest = end + (*end == ' ');
            memmove(t, rest, strlen(rest) + 1);
            *number = v;
            verb = TR_ADD;
        }
    }
    n = strlen(t);
    if (n > TR_NOTE) { /* cut where no UTF-8 sequence is split */
        n = TR_NOTE;
        while (n && ((unsigned char)t[n] & 0xc0) == 0x80) --n;
        t[n] = 0;
    }
    *note = t;
    return verb;
}
