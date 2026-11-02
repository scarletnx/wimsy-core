/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "titles.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int by_id(const void* a, const void* b) {
    u64 x = ((const TrTitle*)a)->id, y = ((const TrTitle*)b)->id;
    return x < y ? -1 : x > y;
}

/* 16 hex digits as a title ID; 0 for anything else */
static u64 title_id(const char* s, size_t n) {
    u64 v = 0;
    size_t i;
    if (n != 16) return 0;
    for (i = 0; i < 16; ++i) {
        int c = s[i], d = c >= '0' && c <= '9'   ? c - '0'
                          : c >= 'a' && c <= 'f' ? c - 'a' + 10
                          : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                 : -1;
        if (d < 0) return 0;
        v = v << 4 | (u64)d;
    }
    return v;
}

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
        char* tab = memchr(line, '\t', (size_t)got);
        ssize_t i;
        u64 id;
        for (i = 0; i < got; ++i) hash = (hash ^ (unsigned char)line[i]) * 1099511628211ULL;
        while (got && (line[got - 1] == '\n' || line[got - 1] == '\r')) line[--got] = 0;
        if (!got || line[0] == '#' || !tab || !(id = title_id(line, (size_t)(tab - line))) || !tab[1]) continue;
        if (t->n == room) {
            room = room ? room * 2 : 4096;
            t->list = realloc(t->list, (size_t)room * sizeof *t->list);
            if (!t->list) {
                fputs(REI_NAME ": out of memory\n", stderr);
                abort();
            }
        }
        t->list[t->n].id = id;
        t->list[t->n++].name = rei_strdup(tab + 1);
    }
    free(line);
    fclose(f);
    qsort(t->list, (size_t)t->n, sizeof *t->list, by_id);
    snprintf(t->version, sizeof t->version, "%016llx", (unsigned long long)hash);
    return 0;
}

const char* tr_titles_name(const Titles* t, const char* id) {
    TrTitle key, *found;
    if (!t || !t->n || !id || !(key.id = title_id(id, strlen(id)))) return NULL;
    key.id &= ~(u64)0xfff; /* an update's id (base | 0x800) finds its game */
    found = bsearch(&key, t->list, (size_t)t->n, sizeof *t->list, by_id);
    if (!found) { /* an add-on's: its game is base + 0x1000 below it */
        key.id = (key.id & ~(u64)0x1fff);
        found = bsearch(&key, t->list, (size_t)t->n, sizeof *t->list, by_id);
    }
    return found ? found->name : NULL;
}

void tr_titles_free(Titles* t) {
    int i;
    for (i = 0; i < t->n; ++i) free(t->list[i].name);
    free(t->list);
    memset(t, 0, sizeof *t);
}
