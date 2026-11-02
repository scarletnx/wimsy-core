/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The games' names by ID (titles.tsv, by scripts/make-titles.py): the Switch's by title ID from blawar/titledb, the
   GBA's by game code ("AGB-BPEE") from No-Intro. What names a report's game when its logs name none; a log's own name
   comes first, as it is what the player's dump says, but for a GBA cartridge's 12-letter title */
#ifndef TR_TITLES_H
#define TR_TITLES_H
#include "tracer.h"

typedef struct {
    char *id, *name; /* owned: "0100F2C0115B6000", "AGB-BPEE" */
} TrTitle;

typedef struct {
    TrTitle* list; /* by id */
    int n;
    char version[17]; /* the file's FNV-1a in hex, "" when none was read: the reports are named again when it changes */
} Titles;

/* path's lines, "<id>\t<name>", '#' for a comment: 0, or -1 when it can't be read (said in the log, and t left
   empty) */
int tr_titles_load(Titles* t, const char* path);
/* the name of title_id's game on console (the log summary's name: "Switch", "GBA"; NULL is the Switch), or NULL; t may
   be NULL. A Switch title ID finds its base application (an update's or an add-on's id), a GBA's is its game code */
const char* tr_titles_name(const Titles* t, const char* console, const char* title_id);
void tr_titles_free(Titles* t);
#endif
