/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* A run of edenprobe (github.com/scarletnx/edenprobe, the probe program that checks an emulator against the console)
   found in a file a report brought, in any of the three forms one comes in:
   - ScarletNX's log of the run: the machine in its header lines, each line of the report as "[game] {...}";
   - Eden's log (rEden's too): the machine in its "Driver:", "Device:", "Host OS:" lines, each line of the report as
     "Debug.Emulated <Info> ...: {...}" (the debug string service, svc_debug_string.cpp);
   - the report itself, report.jsonl, a JSON object a line, which names no machine: the console's reference.
   A run begins at its note "edenprobe 0.9.0 (v0.9.0) starting" and ends at its summary. What is read of it: the
   machine (system, GPU, the driver's VkDriverId, name and version), the emulator and its build, edenprobe's version and
   mode, and each case's suite, name, tier, result, reason and detail.
   The detail of the cases of edenprobe's service, net and pad families, and the net family's notes, can name the
   player (an account's id and nickname, a network's addresses, a session); probe_cut leaves them out of the text before
   it is kept or read. The driver's names and version numbering are ScarletNX's ban table's (extension_bans.c) */
#ifndef TR_PROBE_H
#define TR_PROBE_H
#include "tracer.h"

#define PROBE_CASES  10000   /* the case lines one run keeps at most: a full run has about 2000 */
#define PROBE_DETAIL 300     /* the bytes of a case's detail kept */
#define PROBE_KEY    "probe" /* the reports' key (store.h's game) of a report that is a run of edenprobe */

/* edenprobe's families, as its ui.c groups the suites (FAMILIES, edenprobe 86e1d11) */
typedef enum {
    PROBE_JUDGE,
    PROBE_CPU,
    PROBE_PAD,
    PROBE_GRAPHICS,
    PROBE_FS,
    PROBE_KERNEL,
    PROBE_DISPLAY,
    PROBE_VIDEO,
    PROBE_AUDIO,
    PROBE_SERVICE,
    PROBE_NET,
    PROBE_GAME,
    PROBE_OTHER
} ProbeFamily;
ProbeFamily probe_family(const char* suite);
/* the family's name: "judge", "graphics", ... "other" */
const char* probe_family_name(ProbeFamily f);
/* whether a case of suite keeps its detail: the families whose cases name nothing of the player (the judge, cpu,
   graphics, fs, kernel, display, video, audio, game). service, net and pad lose it, and so does a suite edenprobe's
   table does not have, as nothing says what it holds */
int probe_keeps_detail(const char* suite);

/* the ban table's name of a VkDriverId ("radv", "nvidia"), NULL for one it has none for; its label ("RADV", "NVIDIA's
   driver"); the VkDriverId of a table's name, 0 for none */
const char* probe_driver_name(int id);
const char* probe_driver_label(int id);
int probe_driver_id(const char* name);
/* the VkDriverId a driver's own name (VkPhysicalDeviceDriverProperties.driverName, as Eden's log says it) points at:
   "radv" 3, "NVIDIA" 4, "Qualcomm Technologies Inc. Adreno Vulkan Driver" 8, ... 0 for one it knows nothing of. An
   inference: Eden logs no driver ID */
int probe_driver_of_name(const char* driver_name);
/* driverVersion v of a driver of VkDriverId id as its vendor numbers it, into out: NVIDIA's 580.119.02, Intel's Windows
   101.4502, MoltenVK's 1.2.11, any other's as a Vulkan version (Mesa's 26.1.8) */
void probe_version_text(int id, unsigned long v, char* out, size_t n);
/* text as a version of a driver of VkDriverId id, in that numbering, into *v: 1, or 0 for text that is none */
int probe_version_parse(int id, const char* text, unsigned long* v);

/* the system's family from a log's system line or Eden's Host OS: "android", "windows", "macos", "ios", "freebsd",
   "openbsd", "netbsd", "linux" (the ban table's names), or "unknown" */
const char* probe_os(const char* system);

/* the n bytes of text with the detail of each case of a family probe_keeps_detail refuses emptied, and the text of
   each note of the net family ("net: ...") said to be left out: owned, its length in *size, and how many of each were
   cut in *cases and *notes. Every other byte stays as it is */
char* probe_cut(const char* text, size_t n, size_t* size, int* cases, int* notes);

/* what text holds of a run: 0 nothing, 1 a run's start, 2 a run's start and a summary after it */
int probe_marks(const char* text, size_t n);

typedef struct {
    char *suite, *name, *tier, *result, *reason, *detail; /* owned; reason and detail "" for none */
    double us;                                            /* the time it took, -1 for none */
} ProbeCase;

typedef struct {
    char* emulator;           /* "ScarletNX", "Eden", "rEden", or "console" for a report.jsonl alone */
    char* emulator_version;   /* its build: "main 3e7ffeb", "Eden Development Build | master-abc"; NULL for none */
    char* commit;             /* ScarletNX's commit, NULL for none */
    double built;             /* the day that commit was made (UTC, seconds since the epoch), 0 for none */
    char *version, *describe; /* edenprobe's: "0.9.0", "v0.9.0" */
    char* words;  /* what ran, from its mode note: the families, then fatal, hang, notime, retry, stress, frames=N */
    char* system; /* the machine's system line; NULL for none */
    char* os;     /* probe_os's */
    char* gpu;    /* the GPU's name, NULL for none */
    char* device; /* vendorID:deviceID ("1002:73df"), NULL where the log does not say */
    char* driver_name;            /* driverName, NULL for none */
    char* driver_info;            /* driverInfo ("Mesa 26.1.8"), NULL for none */
    int driver_id;                /* VkDriverId, 0 for none */
    int inferred;                 /* the driver ID is probe_driver_of_name's, not the log's */
    unsigned long driver_version; /* driverVersion, 0 for none */
    char* driver_text;            /* it as its vendor numbers it ("26.1.8"), NULL for none */
    char* vulkan;                 /* the device's Vulkan version, NULL for none */
    char* extensions; /* the optional extensions on, as the log's line lists them (comma separated), NULL for none */
    long passed, failed, skipped, crashed; /* the summary's counts, -1 each without one */
    int whole;                             /* the run has its summary */
    int cut_cases, cut_notes;              /* the details and the net notes probe_cut left out */
    int unread;                            /* report lines that were no case, note or summary edenprobe writes */
    int over;                              /* case lines past PROBE_CASES, not kept */
    ProbeCase* cases;
    int ncases;
} Probe;

/* the last whole run in the n bytes of text (else the last run begun), as probe_cut left it, into *p, with the
   machine its log names before the run begins (the last of each line). 1 for a run, 0 for none, -1 for report lines
   with no run's start in front of them (a launch that went on with a run after a crash, which says so in its first
   note): *why (owned) says which */
int probe_parse(const char* text, size_t n, Probe* p, char** why);
void probe_free(Probe* p);

/* p as JSON, for the worker's answer and the site; owned */
cJSON* probe_json(const Probe* p);
/* p from JSON a worker answered, each field checked as what strangers send is: 0, or -1 with why (owned) */
int probe_of_json(const cJSON* o, Probe* p, char** why);

/* the run's mark: FNV-1a over its machine, emulator, version, words and every case as kept, 16 hex digits (out holds
   17 bytes). Two runs with the same lines have the same */
void probe_digest(const Probe* p, char out[17]);
/* a case's mark, FNV-1a of its suite and name, 16 hex digits: its page's name (out holds 17 bytes) */
void probe_case_key(const char* suite, const char* name, char out[17]);
/* the counts as the reply says them: "1034 passed, 26 failed (astc 15, astcimage 9, tex8k 2), 6 skipped, 3 crashed",
   the failed suites most first, eight at most and how many more; owned */
char* probe_counts(const Probe* p);
/* the machine as the reply says it: "radv, Mesa 26.1.8 (Linux)", "Nvidia 580.95.05, driver ID inferred (Windows)";
   owned */
char* probe_machine(const Probe* p);
#endif
