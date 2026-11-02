/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The games' names by title ID (titles.tsv, from blawar/titledb by scripts/make-titles.py): what names a report's
   game when its logs name none. A log's own name comes first, as it is what the player's dump says */
#ifndef TR_TITLES_H
#define TR_TITLES_H
#include "tracer.h"

typedef struct {
    u64 id;
    char* name; /* owned */
} TrTitle;

typedef struct {
    TrTitle* list; /* by id */
    int n;
    char version[17]; /* the file's FNV-1a in hex, "" when none was read: the reports are named again when it changes */
} Titles;

/* path's lines, "<16 hex digits>\t<name>", '#' for a comment: 0, or -1 when it can't be read (said in the log, and
   t left empty) */
int tr_titles_load(Titles* t, const char* path);
/* the name of title_id's game (its base application: an update's or an add-on's id finds it), or NULL; t may be NULL */
const char* tr_titles_name(const Titles* t, const char* title_id);
void tr_titles_free(Titles* t);
#endif
