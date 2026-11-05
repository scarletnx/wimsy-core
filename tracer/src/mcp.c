/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "mcp.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define VERSION_LATEST "2025-06-18"
static const char* const VERSIONS[] = {"2025-06-18", "2025-03-26", "2024-11-05"};

static const char INSTRUCTIONS[] =
    "ScarletNX game reports: the logs and screenshots that players sent to Tracer on Discord, one report each, grouped "
    "by game, for each console ScarletNX runs: the Switch, the 3DS, the Vita and the GBA (a report's console field). A "
    "game's key is its title ID (the GBA's: gba- and its game code), or n- and its name for a game whose logs name no "
    "ID. list_games and "
    "search_reports find reports; signatures groups the failures that happened in the same place, across games; "
    "get_report gives a report's facts and each log's summary (how the run ended and where, its frames, the service "
    "commands and what else the runtime missed, the last lines); read_log reads or searches the log itself; "
    "get_screenshot shows a report's screenshot. Home folders in the logs are ~. A Switch log that failed has a symbolize "
    "object in get_report: its title ID, game version and game addresses. ScarletNX's own MCP server (scarletnx "
    "--mcp), where a copy of the game is, names them: its find_game with that title_id, the game whose version is "
    "game_version, then its symbolize (or disassemble) with that game's path and the addresses. This server has no "
    "game code, and never will: the games stay on the machines of those who own them.";

static const char TOOLS[] =
    "[{\"name\":\"list_games\",\"title\":\"List the games\",\"description\":\"The games that have reports: each "
    "one's key, title, title ID, report count, the latest report's date and outcome, and its page. Filtered by words "
    "of the title or title ID, and by the latest outcome.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"query\":{\"type\":\"string\",\"description\":\"words the title or title ID holds, in any case\"},"
    "\"console\":{\"type\":\"string\",\"description\":\"Switch, 3DS, Vita or GBA\"},"
    "\"outcome\":{\"type\":\"string\",\"description\":\"the latest report's outcome: crash, svcBreak, exception, brk, "
    "unsupported instruction, thread stopped, host abort, fatal, or no failure logged\"}}},\"annotations\":{"
    "\"readOnlyHint\":true,\"openWorldHint\":false}},"
    "{\"name\":\"search_reports\",\"title\":\"Search the reports\",\"description\":\"Reports, the newest first: each "
    "one's id, date, game, outcome, signature, build, system, GPU, driver, the player's note and its page. Every "
    "filter given must match.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"query\":{\"type\":\"string\",\"description\":\"words found anywhere in the report's fields, in any case\"},"
    "\"game\":{\"type\":\"string\",\"description\":\"a game's key or title ID, or words of its title\"},"
    "\"console\":{\"type\":\"string\",\"description\":\"Switch, 3DS, Vita or GBA\"},"
    "\"outcome\":{\"type\":\"string\",\"description\":\"how the run ended, as list_games names it\"},"
    "\"signature\":{\"type\":\"string\",\"description\":\"text the failure's signature holds\"},"
    "\"branch\":{\"type\":\"string\",\"description\":\"the ScarletNX branch of the build\"},"
    "\"gpu\":{\"type\":\"string\",\"description\":\"text the GPU's name holds\"},"
    "\"limit\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":500,\"description\":\"50 when not given\"}}},"
    "\"annotations\":{\"readOnlyHint\":true,\"openWorldHint\":false}},"
    "{\"name\":\"get_report\",\"title\":\"Get a report\",\"description\":\"A report's facts, and each of its files: "
    "a log with its summary (how the run ended, where, its frames, the service commands and what else the runtime "
    "missed, warnings, the game's output, the last lines), a screenshot with its address.\",\"inputSchema\":{"
    "\"type\":\"object\",\"properties\":{\"id\":{\"type\":\"integer\",\"minimum\":1,\"description\":\"the report's "
    "number\"}},\"required\":[\"id\"]},\"annotations\":{\"readOnlyHint\":true,\"openWorldHint\":false}},"
    "{\"name\":\"get_screenshot\",\"title\":\"Show a screenshot\",\"description\":\"One of a report's "
    "screenshots, as an image, with its name and address.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"id\":{\"type\":\"integer\",\"minimum\":1,\"description\":\"the report's number\"},"
    "\"n\":{\"type\":\"integer\",\"minimum\":1,\"description\":\"which of its screenshots, 1 when not given\"}},"
    "\"required\":[\"id\"]},\"annotations\":{\"readOnlyHint\":true,\"openWorldHint\":false}},"
    "{\"name\":\"read_log\",\"title\":\"Read a report's log\",\"description\":\"Lines of a report's log, each with its "
    "number: from a line on, or the lines that hold some text. The answer says how many lines the log has, and where "
    "to go on.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"id\":{\"type\":\"integer\",\"minimum\":1,\"description\":\"the report's number\"},"
    "\"log\":{\"type\":\"integer\",\"minimum\":1,\"description\":\"which of its logs, 1 when not given\"},"
    "\"grep\":{\"type\":\"string\",\"description\":\"only the lines that hold this text, in any case\"},"
    "\"start\":{\"type\":\"integer\",\"minimum\":1,\"description\":\"the first line, 1 when not given\"},"
    "\"lines\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":2000,\"description\":\"how many lines, 200 when not "
    "given\"}},\"required\":[\"id\"]},\"annotations\":{\"readOnlyHint\":true,\"openWorldHint\":false}},"
    "{\"name\":\"signatures\",\"title\":\"Group the failures\",\"description\":\"The failures' signatures, the most "
    "reported first: each one's outcome, report count, the games it happened in and its newest reports. A signature "
    "is the same for runs that failed in the same place.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"game\":{\"type\":\"string\",\"description\":\"only this game's: its key or title ID, or words of its title\"},"
    "\"outcome\":{\"type\":\"string\",\"description\":\"only failures of this outcome\"},"
    "\"limit\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":200,\"description\":\"30 when not given\"}}},"
    "\"annotations\":{\"readOnlyHint\":true,\"openWorldHint\":false}}]";

/* ---- reading ---- */

static char* read_text(const char* root, const char* rel, size_t* n) {
    char* path = rei_format("%s/%s", root, rel);
    FILE* f = fopen(path, "rb");
    Buf b = {0};
    char piece[16384];
    size_t got;
    free(path);
    if (!f) return NULL;
    while ((got = fread(piece, 1, sizeof piece, f))) buf_addn(&b, piece, got);
    fclose(f);
    if (n) *n = b.len;
    return buf_take(&b);
}
static cJSON* read_json(const char* root, const char* rel) {
    size_t n;
    char* text = read_text(root, rel, &n);
    cJSON* o = text ? cJSON_ParseWithLength(text, n) : NULL;
    free(text);
    return o;
}

/* a text found in lines in any case (ASCII letters), in time linear in each line: Knuth, Morris and Pratt's search, as
   read_log's lines are a log's of up to 64 MiB and a request's texts 1 MiB, and a plain search costs a line's length
   times the text's */
typedef struct {
    char* low;    /* the text, lowered; owned */
    size_t* fail; /* the longest border of each prefix of low; owned */
    size_t n;
} Finder;
static void finder_init(Finder* f, const char* text) {
    size_t i, k = 0;
    f->n = strlen(text);
    f->low = rei_strdup(text);
    f->fail = malloc((f->n ? f->n : 1) * sizeof *f->fail);
    if (!f->fail) {
        fputs(REI_NAME ": out of memory\n", stderr);
        abort();
    }
    for (i = 0; i < f->n; ++i) f->low[i] = (char)tolower((unsigned char)f->low[i]);
    if (f->n) f->fail[0] = 0;
    for (i = 1; i < f->n; ++i) {
        while (k && f->low[i] != f->low[k]) k = f->fail[k - 1];
        if (f->low[i] == f->low[k]) ++k;
        f->fail[i] = k;
    }
}
static int finder_in(const Finder* f, const char* line) {
    size_t k = 0;
    if (!f->n) return 1;
    for (; *line; ++line) {
        char c = (char)tolower((unsigned char)*line);
        while (k && c != f->low[k]) k = f->fail[k - 1];
        if (c == f->low[k] && ++k == f->n) return 1;
    }
    return 0;
}
static void finder_free(Finder* f) {
    free(f->low);
    free(f->fail);
}
/* whether text holds what f finds; f's empty text is in everything, a NULL text holds nothing else */
static int found(const Finder* f, const char* text) { return !f->n || (text && finder_in(f, text)); }
static int same(const char* a, const char* b) {
    if (!a || !b) return 0;
    for (; *a && tolower((unsigned char)*a) == tolower((unsigned char)*b); ++a, ++b) continue;
    return !*a && !*b;
}
/* a query's words, each searched for in time linear in what it is searched in: a request is 1 MiB, and each report's
   fields are searched for each word */
typedef struct {
    Finder find[MCP_WORDS];
    int n;
} Words;
/* query's words into w: 0, or -1 with why (owned) for more than MCP_WORDS */
static int words_of(Words* w, const char* query, char** why) {
    const char* s = query ? query : "";
    int count = 0;
    w->n = 0;
    *why = NULL;
    for (;;) {
        size_t len;
        while (isspace((unsigned char)*s)) ++s;
        if (!(len = strcspn(s, " \t\r\n"))) break;
        if (count < MCP_WORDS) {
            char* word = rei_format("%.*s", (int)len, s);
            finder_init(&w->find[w->n++], word);
            free(word);
        }
        ++count;
        s += len;
    }
    if (count <= MCP_WORDS) return 0;
    *why = rei_format("query has %d words: %d at most", count, MCP_WORDS);
    return -1;
}
static void words_free(Words* w) {
    while (w->n) finder_free(&w->find[--w->n]);
}
/* whether each word of w is in one of o's string fields */
static int words_in(const cJSON* o, const Words* w) {
    Buf all = {0};
    const cJSON* v;
    int ok = 1, i;
    if (!w->n) return 1;
    cJSON_ArrayForEach(v, o) if (cJSON_IsString(v)) {
        buf_add(&all, v->valuestring);
        buf_add(&all, "\n");
    }
    else if (cJSON_IsNumber(v)) buf_printf(&all, "#%.0f\n", v->valuedouble);
    for (i = 0; ok && i < w->n; ++i) ok = found(&w->find[i], all.text);
    buf_free(&all);
    return ok;
}
/* a game given as its key, its title ID, or words of its title (title, game's Finder) */
static int is_game(const cJSON* o, const char* game, const Finder* title) {
    if (!game || !*game) return 1;
    return same(jstr(o, "game"), game) || same(jstr(o, "title_id"), game) ||
           (jstr(o, "title") && found(title, jstr(o, "title")));
}
static int integer(const cJSON* args, const char* key, int otherwise, int lo, int hi) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(args, key);
    double d = cJSON_IsNumber(v) ? v->valuedouble : otherwise;
    return d < lo ? lo : d > hi ? hi : (int)d;
}

/* ---- the tools: each a text, *error when it is the tool's failure ---- */

static char* printed(cJSON* o) {
    char* text = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return text ? text : rei_strdup("{}");
}

static char* list_games(const Mcp* m, const cJSON* args, int* error) {
    cJSON *all, *out, *list, *g;
    const char *outcome = jstr(args, "outcome"), *console = jstr(args, "console");
    int count = 0;
    Words words;
    char* why;
    if (words_of(&words, jstr(args, "query"), &why)) {
        words_free(&words);
        *error = 1;
        return why;
    }
    if (!(all = read_json(m->root, "api/games.json"))) {
        words_free(&words);
        *error = 1;
        return rei_strdup("the site has no api/games.json yet");
    }
    out = cJSON_CreateObject();
    list = cJSON_AddArrayToObject(out, "games");
    cJSON_ArrayForEach(g, cJSON_GetObjectItemCaseSensitive(all, "games")) {
        char* url;
        cJSON* copy;
        if (!words_in(g, &words) || (outcome && *outcome && !same(jstr(g, "outcome"), outcome)) ||
            (console && *console && !same(jstr(g, "console"), console)))
            continue;
        url = rei_format("%s/g/%s.html", m->url, jstr(g, "game") ? jstr(g, "game") : "");
        copy = cJSON_Duplicate(g, 1);
        jset(copy, "url", url);
        free(url);
        cJSON_AddItemToArray(list, copy);
        ++count;
    }
    cJSON_AddNumberToObject(out, "count", count);
    cJSON_Delete(all);
    words_free(&words);
    return printed(out);
}

static char* search_reports(const Mcp* m, const cJSON* args, int* error) {
    cJSON *all, *out, *list, *r;
    const char *game = jstr(args, "game"), *outcome = jstr(args, "outcome"), *branch = jstr(args, "branch"),
               *console = jstr(args, "console");
    int limit = integer(args, "limit", 50, 1, 500), matched = 0;
    Words words;
    Finder title, signature, gpu;
    char* why;
    if (words_of(&words, jstr(args, "query"), &why)) {
        words_free(&words);
        *error = 1;
        return why;
    }
    if (!(all = read_json(m->root, "api/reports.json"))) {
        words_free(&words);
        *error = 1;
        return rei_strdup("the site has no api/reports.json yet");
    }
    finder_init(&title, game ? game : "");
    finder_init(&signature, jstr(args, "signature") ? jstr(args, "signature") : "");
    finder_init(&gpu, jstr(args, "gpu") ? jstr(args, "gpu") : "");
    out = cJSON_CreateObject();
    list = cJSON_AddArrayToObject(out, "reports");
    cJSON_ArrayForEach(r, cJSON_GetObjectItemCaseSensitive(all, "reports")) {
        char* url;
        cJSON* copy;
        if (!words_in(r, &words) || !is_game(r, game, &title) ||
            (outcome && *outcome && !same(jstr(r, "outcome"), outcome)) || !found(&signature, jstr(r, "signature")) ||
            (branch && *branch && !same(jstr(r, "branch"), branch)) || !found(&gpu, jstr(r, "gpu")) ||
            (console && *console && !same(jstr(r, "console"), console)))
            continue;
        if (matched++ >= limit) continue;
        url = rei_format("%s/r/%.0f/", m->url, jnum(r, "id"));
        copy = cJSON_Duplicate(r, 1);
        jset(copy, "url", url);
        free(url);
        cJSON_AddItemToArray(list, copy);
    }
    cJSON_AddNumberToObject(out, "matched", matched);
    if (matched > limit) cJSON_AddNumberToObject(out, "shown", limit);
    cJSON_Delete(all);
    words_free(&words);
    finder_free(&title);
    finder_free(&signature);
    finder_free(&gpu);
    return printed(out);
}

/* report id's JSON, or NULL with the reason in *why */
static cJSON* report(const Mcp* m, const cJSON* args, char** why) {
    const cJSON* id = cJSON_GetObjectItemCaseSensitive(args, "id");
    char* rel;
    cJSON* r;
    *why = NULL;
    if (!cJSON_IsNumber(id) || id->valuedouble < 1) {
        *why = rei_strdup("id, a report's number, is needed");
        return NULL;
    }
    rel = rei_format("api/r/%.0f.json", id->valuedouble);
    r = read_json(m->root, rel);
    free(rel);
    if (!r) *why = rei_format("there is no report #%.0f", id->valuedouble);
    return r;
}

/* a hexadecimal address, as the logs print them */
static int hex_address(const char* s) {
    size_t n = s ? strspn(s, "0123456789abcdefABCDEF") : 0;
    return n && n <= 16 && !s[n];
}
/* a log's file that failed, given what ScarletNX's symbolize asks for: the title ID and version that find the game,
   and the game addresses of its frames and of the failure */
static void symbolizing(cJSON* f) {
    const cJSON *summary = cJSON_GetObjectItemCaseSensitive(f, "summary"), *frame;
    const cJSON* failure = cJSON_GetObjectItemCaseSensitive(summary, "failure");
    const char *id = jstr(summary, "title_id"), *console = jstr(summary, "console");
    cJSON *s, *list = cJSON_CreateArray(), *each;
    const char* at;
    cJSON_ArrayForEach(frame, cJSON_GetObjectItemCaseSensitive(failure, "frames")) {
        int seen = 0;
        at = jstr(frame, "address");
        if (!hex_address(at)) continue;
        cJSON_ArrayForEach(each, list) seen |= !strcmp(each->valuestring, at);
        if (!seen) cJSON_AddItemToArray(list, cJSON_CreateString(at));
    }
    at = jstr(failure, "game_address");
    if (hex_address(at)) cJSON_AddItemToArray(list, cJSON_CreateString(at));
    if (!id || !cJSON_GetArraySize(list) || (console && strcmp(console, "Switch"))) { /* find_game knows the Switch's */
        cJSON_Delete(list);
        return;
    }
    s = cJSON_AddObjectToObject(f, "symbolize");
    jset(s, "title_id", id);
    jset(s, "game_version", jstr(summary, "game_version"));
    cJSON_AddItemToObject(s, "addresses", list);
    jset(s, "with", "scarletnx --mcp: find_game, then symbolize or disassemble");
}

static char* get_report(const Mcp* m, const cJSON* args, int* error) {
    char *why, *url;
    cJSON *r = report(m, args, &why), *f;
    if (!r) {
        *error = 1;
        return why;
    }
    url = rei_format("%s/r/%.0f/", m->url, jnum(r, "id"));
    jset(r, "url", url);
    free(url);
    cJSON_ArrayForEach(f, cJSON_GetObjectItemCaseSensitive(r, "files")) {
        url = rei_format("%s/%s", m->url, jstr(f, "path") ? jstr(f, "path") : "");
        jset(f, "url", url);
        free(url);
        symbolizing(f);
    }
    return printed(r);
}

static char* read_log(const Mcp* m, const cJSON* args, int* error) {
    char *why, *path, *line = NULL;
    cJSON *r = report(m, args, &why), *f, *log = NULL;
    const char *grep = jstr(args, "grep"), *rel;
    int which = integer(args, "log", 1, 1, TR_LOGS), start = integer(args, "start", 1, 1, 1 << 30),
        want = integer(args, "lines", 200, 1, MCP_LINES), nlogs = 0, shown = 0, matches = 0;
    long number = 0, last = 0;
    size_t cap = 0;
    FILE* in;
    Buf out = {0}, body = {0};
    Finder find;
    if (!r) {
        *error = 1;
        return why;
    }
    cJSON_ArrayForEach(f, cJSON_GetObjectItemCaseSensitive(r, "files")) if (same(jstr(f, "kind"), "log") &&
                                                                            ++nlogs == which) log = f;
    rel = jstr(log, "path");
    if (!rel || strstr(rel, "..") || strncmp(rel, "r/", 2)) {
        why = rei_format("report #%.0f has %d %s, not a log %d", jnum(r, "id"), nlogs, nlogs == 1 ? "log" : "logs",
                         which);
        cJSON_Delete(r);
        *error = 1;
        return why;
    }
    path = rei_format("%s/%s", m->root, rel);
    in = fopen(path, "rb");
    free(path);
    if (!in) {
        cJSON_Delete(r);
        *error = 1;
        return rei_strdup("the log's file is missing");
    }
    finder_init(&find, grep ? grep : "");
    for (;;) {
        ssize_t got = getline(&line, &cap, in);
        int keep;
        if (got < 0) break;
        ++number;
        while (got && (line[got - 1] == '\n' || line[got - 1] == '\r')) line[--got] = 0;
        if (number < start) continue;
        keep = finder_in(&find, line);
        matches += keep && grep && *grep;
        if (!keep || shown == want || body.len >= MCP_TEXT_MAX) continue;
        buf_printf(&body, "%ld: %s\n", number, line);
        last = number;
        ++shown;
    }
    free(line);
    fclose(in);
    finder_free(&find);
    buf_printf(&out, "%s, report #%.0f, %s: %ld lines", jstr(r, "title") ? jstr(r, "title") : "?", jnum(r, "id"),
               jstr(log, "name") ? jstr(log, "name") : rel, number);
    if (grep && *grep)
        buf_printf(&out, "; %d from line %d on hold \"%s\", %d shown", matches, start, grep, shown);
    else
        buf_printf(&out, "; lines %d to %ld shown", start, shown ? last : (long)start - 1);
    if (shown && last < number && (!(grep && *grep) || shown < matches))
        buf_printf(&out, "; go on with start=%ld", last + 1);
    buf_add(&out, "\n\n");
    buf_add(&out, body.text ? body.text : "");
    buf_free(&body);
    cJSON_Delete(r);
    return buf_take(&out);
}

static char* base64(const char* data, size_t n) {
    static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const unsigned char* d = (const unsigned char*)data;
    char* out = malloc((n + 2) / 3 * 4 + 1);
    size_t i, o = 0;
    if (!out) {
        fputs(REI_NAME ": out of memory\n", stderr);
        abort();
    }
    for (i = 0; i + 2 < n; i += 3) {
        unsigned long v = (unsigned long)d[i] << 16 | (unsigned long)d[i + 1] << 8 | d[i + 2];
        out[o++] = digits[v >> 18 & 63];
        out[o++] = digits[v >> 12 & 63];
        out[o++] = digits[v >> 6 & 63];
        out[o++] = digits[v & 63];
    }
    if (i < n) {
        unsigned long v = (unsigned long)d[i] << 16 | (i + 1 < n ? (unsigned long)d[i + 1] << 8 : 0);
        out[o++] = digits[v >> 18 & 63];
        out[o++] = digits[v >> 12 & 63];
        out[o++] = i + 1 < n ? digits[v >> 6 & 63] : '=';
        out[o++] = '=';
    }
    out[o] = 0;
    return out;
}

/* a report's screenshot as MCP's image content, and a line naming it; a text when it cannot be shown */
static cJSON* get_screenshot(const Mcp* m, const cJSON* args, int* error) {
    char *why, *bytes = NULL, *text;
    cJSON *r = report(m, args, &why), *f, *shot = NULL, *content, *item;
    int which = integer(args, "n", 1, 1, TR_IMAGES), nshots = 0;
    const char *rel, *ext, *type;
    size_t n = 0;
    if (!r) {
        *error = 1;
        f = cJSON_CreateString(why ? why : "no such report");
        free(why);
        return f;
    }
    cJSON_ArrayForEach(f, cJSON_GetObjectItemCaseSensitive(r, "files")) if (same(jstr(f, "kind"), "image") &&
                                                                            ++nshots == which) shot = f;
    rel = jstr(shot, "path");
    ext = rel ? strrchr(rel, '.') : NULL;
    type = !ext                    ? NULL
           : !strcmp(ext, ".png")  ? "image/png"
           : !strcmp(ext, ".jpg")  ? "image/jpeg"
           : !strcmp(ext, ".webp") ? "image/webp"
           : !strcmp(ext, ".gif")  ? "image/gif"
                                   : NULL;
    if (!rel || !type || strstr(rel, "..") || strncmp(rel, "r/", 2)) {
        text = rei_format("report #%.0f has %d %s, not a screenshot %d", jnum(r, "id"), nshots,
                          nshots == 1 ? "screenshot" : "screenshots", which);
        cJSON_Delete(r);
        *error = 1;
        f = cJSON_CreateString(text);
        free(text);
        return f;
    }
    if (!(bytes = read_text(m->root, rel, &n)) || n > MCP_IMAGE_MAX) {
        text = bytes ? rei_format("the screenshot is over %d MiB: %s/%s", MCP_IMAGE_MAX >> 20, m->url, rel)
                     : rei_strdup("the screenshot's file is missing");
        free(bytes);
        cJSON_Delete(r);
        *error = 1;
        f = cJSON_CreateString(text);
        free(text);
        return f;
    }
    content = cJSON_CreateArray();
    item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "type", "image");
    why = base64(bytes, n);
    cJSON_AddStringToObject(item, "data", why);
    cJSON_AddStringToObject(item, "mimeType", type);
    cJSON_AddItemToArray(content, item);
    free(why);
    free(bytes);
    item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "type", "text");
    text = rei_format("%s, report #%.0f, screenshot %d of %d: %s, %s/%s", jstr(r, "title") ? jstr(r, "title") : "?",
                      jnum(r, "id"), which, nshots, jstr(shot, "name") ? jstr(shot, "name") : rel, m->url, rel);
    cJSON_AddStringToObject(item, "text", text);
    cJSON_AddItemToArray(content, item);
    free(text);
    cJSON_Delete(r);
    return content;
}

static int by_count(const void* a, const void* b) {
    const cJSON *x = *(const cJSON* const*)a, *y = *(const cJSON* const*)b;
    double d = jnum(y, "reports") - jnum(x, "reports");
    return d > 0 ? 1 : d < 0 ? -1 : 0;
}
static char* signatures(const Mcp* m, const cJSON* args, int* error) {
    cJSON *all = read_json(m->root, "api/reports.json"), *groups = cJSON_CreateObject(), *r, *g, *out, *list;
    cJSON** sorted;
    const char *game = jstr(args, "game"), *outcome = jstr(args, "outcome");
    int limit = integer(args, "limit", 30, 1, 200), n = 0, i;
    Finder title;
    if (!all) {
        cJSON_Delete(groups);
        *error = 1;
        return rei_strdup("the site has no api/reports.json yet");
    }
    finder_init(&title, game ? game : "");
    cJSON_ArrayForEach(r, cJSON_GetObjectItemCaseSensitive(all, "reports")) { /* the newest first */
        const char* sig = jstr(r, "signature");
        cJSON *games, *ids;
        if (!sig || !is_game(r, game, &title) || (outcome && *outcome && !same(jstr(r, "outcome"), outcome))) continue;
        if (!(g = cJSON_GetObjectItemCaseSensitive(groups, sig))) {
            g = cJSON_CreateObject();
            jset(g, "signature", sig);
            jset(g, "outcome", jstr(r, "outcome"));
            cJSON_AddNumberToObject(g, "reports", 0);
            cJSON_AddArrayToObject(g, "games");
            cJSON_AddArrayToObject(g, "newest");
            cJSON_AddItemToObject(groups, sig, g);
        }
        cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(g, "reports"), jnum(g, "reports") + 1);
        games = cJSON_GetObjectItemCaseSensitive(g, "games");
        ids = cJSON_GetObjectItemCaseSensitive(g, "newest");
        {
            const cJSON* known;
            int seen = 0;
            cJSON_ArrayForEach(known, games) seen |= same(known->valuestring, jstr(r, "title"));
            if (!seen && cJSON_GetArraySize(games) < 10 && jstr(r, "title"))
                cJSON_AddItemToArray(games, cJSON_CreateString(jstr(r, "title")));
        }
        if (cJSON_GetArraySize(ids) < 5) cJSON_AddItemToArray(ids, cJSON_CreateNumber(jnum(r, "id")));
    }
    sorted = malloc((size_t)(cJSON_GetArraySize(groups) + 1) * sizeof *sorted);
    if (!sorted) {
        fputs(REI_NAME ": out of memory\n", stderr);
        abort();
    }
    cJSON_ArrayForEach(g, groups) sorted[n++] = g;
    qsort(sorted, (size_t)n, sizeof *sorted, by_count);
    out = cJSON_CreateObject();
    list = cJSON_AddArrayToObject(out, "signatures");
    for (i = 0; i < n && i < limit; ++i) cJSON_AddItemToArray(list, cJSON_Duplicate(sorted[i], 1));
    cJSON_AddNumberToObject(out, "count", n);
    free(sorted);
    cJSON_Delete(groups);
    cJSON_Delete(all);
    finder_free(&title);
    return printed(out);
}

/* ---- JSON-RPC ---- */

static char* answer(const cJSON* id, cJSON* result) {
    cJSON* a = cJSON_CreateObject();
    cJSON_AddStringToObject(a, "jsonrpc", "2.0");
    cJSON_AddItemToObject(a, "id", id ? cJSON_Duplicate(id, 1) : cJSON_CreateNull());
    cJSON_AddItemToObject(a, "result", result);
    return printed(a);
}
static char* failed(const cJSON* id, int code, const char* message) {
    cJSON *a = cJSON_CreateObject(), *e;
    cJSON_AddStringToObject(a, "jsonrpc", "2.0");
    cJSON_AddItemToObject(a, "id", id ? cJSON_Duplicate(id, 1) : cJSON_CreateNull());
    e = cJSON_AddObjectToObject(a, "error");
    cJSON_AddNumberToObject(e, "code", code);
    cJSON_AddStringToObject(e, "message", message);
    return printed(a);
}

static cJSON* initialized(const cJSON* params) {
    cJSON *r = cJSON_CreateObject(), *caps, *info;
    const char* asked = jstr(params, "protocolVersion");
    const char* version = VERSION_LATEST;
    size_t i;
    for (i = 0; asked && i < sizeof VERSIONS / sizeof *VERSIONS; ++i)
        if (!strcmp(asked, VERSIONS[i])) version = VERSIONS[i];
    cJSON_AddStringToObject(r, "protocolVersion", version);
    caps = cJSON_AddObjectToObject(r, "capabilities");
    cJSON_AddFalseToObject(cJSON_AddObjectToObject(caps, "tools"), "listChanged");
    info = cJSON_AddObjectToObject(r, "serverInfo");
    cJSON_AddStringToObject(info, "name", "tracer");
    cJSON_AddStringToObject(info, "title", "Tracer: ScarletNX game reports");
    cJSON_AddStringToObject(info, "version", "1.0.0");
    cJSON_AddStringToObject(r, "instructions", INSTRUCTIONS);
    return r;
}

static char* called(const Mcp* m, const cJSON* id, const cJSON* params) {
    static const struct {
        const char* name;
        char* (*run)(const Mcp*, const cJSON*, int*);
    } tools[] = {{"list_games", list_games},
                 {"search_reports", search_reports},
                 {"get_report", get_report},
                 {"read_log", read_log},
                 {"signatures", signatures}};
    const char* name = jstr(params, "name");
    const cJSON* args = cJSON_GetObjectItemCaseSensitive(params, "arguments");
    size_t i;
    if (name && !strcmp(name, "get_screenshot")) { /* content of its own: an image, or a text when it fails */
        int error = 0;
        cJSON *got = get_screenshot(m, args, &error), *r = cJSON_CreateObject();
        if (cJSON_IsString(got)) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "type", "text");
            cJSON_AddStringToObject(item, "text", got->valuestring);
            cJSON_Delete(got);
            got = cJSON_CreateArray();
            cJSON_AddItemToArray(got, item);
        }
        cJSON_AddItemToObject(r, "content", got);
        if (error) cJSON_AddTrueToObject(r, "isError");
        return answer(id, r);
    }
    for (i = 0; i < sizeof tools / sizeof *tools; ++i)
        if (name && !strcmp(name, tools[i].name)) {
            int error = 0;
            char* text = tools[i].run(m, args, &error);
            cJSON *r = cJSON_CreateObject(), *item = cJSON_CreateObject();
            if (strlen(text) > MCP_TEXT_MAX) { /* cut on a whole UTF-8 sequence */
                size_t n = MCP_TEXT_MAX;
                char* cut;
                while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
                cut = rei_format("%.*s\n[cut: ask for less]", (int)n, text);
                free(text);
                text = cut;
            }
            cJSON_AddStringToObject(item, "type", "text");
            cJSON_AddStringToObject(item, "text", text);
            cJSON_AddItemToArray(cJSON_AddArrayToObject(r, "content"), item);
            if (error) cJSON_AddTrueToObject(r, "isError");
            free(text);
            return answer(id, r);
        }
    return failed(id, -32602, "no such tool");
}

char* mcp_handle(const Mcp* m, const char* body, size_t n, int* status) {
    cJSON* msg = cJSON_ParseWithLength(body, n);
    const cJSON* id = cJSON_GetObjectItemCaseSensitive(msg, "id");
    const cJSON* params = cJSON_GetObjectItemCaseSensitive(msg, "params");
    const char* method = jstr(msg, "method");
    char* text;
    *status = 200;
    if (!cJSON_IsObject(msg)) {
        int json = msg != NULL;
        cJSON_Delete(msg);
        *status = 400;
        return failed(NULL, json ? -32600 : -32700, json ? "not a JSON-RPC message" : "not JSON");
    }
    if (!method || !id) { /* a notification, or an answer to a request of ours (we make none) */
        cJSON_Delete(msg);
        *status = 202;
        return NULL;
    }
    if (!strcmp(method, "initialize"))
        text = answer(id, initialized(params));
    else if (!strcmp(method, "ping"))
        text = answer(id, cJSON_CreateObject());
    else if (!strcmp(method, "tools/list")) {
        cJSON* r = cJSON_CreateObject();
        cJSON_AddItemToObject(r, "tools", cJSON_Parse(TOOLS));
        text = answer(id, r);
    } else if (!strcmp(method, "tools/call"))
        text = called(m, id, params);
    else
        text = failed(id, -32601, "no such method");
    cJSON_Delete(msg);
    return text;
}
