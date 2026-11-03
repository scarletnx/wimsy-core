/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "titles.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int by_id(const void* a, const void* b) { return strcmp(((const TrTitle*)a)->id, ((const TrTitle*)b)->id); }

int tr_titles_load(Titles* t, const char* path) {
    FILE* f = fopen(path, "rb");
    char* line = NULL;
    size_t cap = 0;
    ssize_t got;
    int room = 0;
    u64 hash = 14695981039346656037ULL;
    memset(t, 0, sizeof *t);
    if (!f) {
        rei_log("the games' names %s could not be read: %s", path, strerror(errno));
        return -1;
    }
    while ((got = getline(&line, &cap, f)) >= 0) {
        char *tab = memchr(line, '\t', (size_t)got), *p;
        ssize_t i;
        for (i = 0; i < got; ++i) hash = (hash ^ (unsigned char)line[i]) * 1099511628211ULL;
        while (got && (line[got - 1] == '\n' || line[got - 1] == '\r')) line[--got] = 0;
        if (!got || line[0] == '#' || !tab || tab == line || !tab[1]) continue;
        *tab = 0;
        for (p = line; *p; ++p) *p = (char)toupper((unsigned char)*p);
        if (t->n == room) {
            room = room ? room * 2 : 4096;
            t->list = realloc(t->list, (size_t)room * sizeof *t->list);
            if (!t->list) {
                fputs(REI_NAME ": out of memory\n", stderr);
                abort();
            }
        }
        t->list[t->n].id = rei_strdup(line);
        t->list[t->n++].name = rei_strdup(tab + 1);
    }
    free(line);
    fclose(f);
    qsort(t->list, (size_t)t->n, sizeof *t->list, by_id);
    snprintf(t->version, sizeof t->version, "%016llx", (unsigned long long)hash);
    return 0;
}

static const char* found(const Titles* t, const char* id) {
    TrTitle key, *at;
    key.id = (char*)id;
    at = bsearch(&key, t->list, (size_t)t->n, sizeof *t->list, by_id);
    return at ? at->name : NULL;
}

const char* tr_titles_name(const Titles* t, const char* console, const char* title_id) {
    char id[24], *end;
    unsigned long long v;
    size_t i, n = title_id ? strlen(title_id) : 0;
    const char* name;
    if (!t || !t->n || !n || n > 16) return NULL;
    if (console && !strcmp(console, "GBA")) { /* its game code */
        snprintf(id, sizeof id, "AGB-%s", title_id);
        for (i = 4; id[i]; ++i) id[i] = (char)toupper((unsigned char)id[i]);
        return found(t, id);
    }
    if (console && strcmp(console, "Switch")) return NULL; /* the list has the Switch's and the GBA's */
    v = strtoull(title_id, &end, 16);
    if (n != 16 || *end) return NULL;
    snprintf(id, sizeof id, "%016llX", v & ~0xfffULL); /* an update's id (base | 0x800) finds its game */
    if ((name = found(t, id))) return name;
    snprintf(id, sizeof id, "%016llX", v & ~0x1fffULL); /* an add-on's: its game is 0x1000 below its base */
    return found(t, id);
}

void tr_titles_free(Titles* t) {
    int i;
    for (i = 0; i < t->n; ++i) {
        free(t->list[i].id);
        free(t->list[i].name);
    }
    free(t->list);
    memset(t, 0, sizeof *t);
}
