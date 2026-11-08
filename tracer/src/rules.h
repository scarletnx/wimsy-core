/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* What the runs of edenprobe say together, by fixed rules (no judgment): the drivers that break a case, as candidates
   for ScarletNX's ban table (src/nx/gpu/vk/extension_bans.txt), never applied but by a person; the cases ScarletNX
   gets wrong everywhere; and the cases a ScarletNX build broke on a driver. Each compares the same case of the same
   edenprobe version run with the same words, and counts ScarletNX's runs; Eden's are shown beside, as a second
   emulator failing a case on the same driver points at the driver.

   A ban candidate, for a case on a driver (VkDriverId) and a system's family, when all three hold:
   1. it fails in RULE_FAIL_REPORTS reports or more, from RULE_FAIL_PEOPLE people or more, in a range of that driver's
      versions: the versions seen in a row, sorted, where it fails and never passes;
   2. it passes in RULE_PASS_REPORTS reports or more of RULE_PASS_DRIVERS other drivers or more, whose builds are at
      most RULE_DAYS days from a failing report's (the day the build's commit was made, else the day the report came):
      without that, the failure is ScarletNX's;
   3. the range runs from its lowest version to the next version seen above it, where the case passes (or passes and
      fails: the range is then said to end at a mixed version), or has no end when none is seen.
   A case that is ScarletNX's own failure is no candidate.

   ScarletNX's own failure: a case that fails on RULE_GENERAL_DRIVERS drivers or more, or on every driver seen when two
   or more are; a driver counts as failing when its runs fail the case and none passes it.

   A regression: on one driver, one version of it and one system's family, a case that passed in every run of a build
   and fails in every run of each build after it, to the newest; builds of a published commit only (no -dirty), in the
   order of their commits' days */
#ifndef TR_RULES_H
#define TR_RULES_H
#include "store.h"

#define RULE_FAIL_REPORTS    3
#define RULE_FAIL_PEOPLE     2
#define RULE_PASS_REPORTS    3
#define RULE_PASS_DRIVERS    2
#define RULE_DAYS            7
#define RULE_GENERAL_DRIVERS 3

typedef struct {
    int name;                    /* the case, StProbeName's id */
    const char *version, *words; /* edenprobe's version and the words that ran, the runs' */
    int driver_id;
    const char* os;          /* the system's family, the runs' */
    unsigned long low, high; /* the range: from low, up to high, which it leaves out; high 0 for no end */
    int mixed;               /* high is a version where the case passes and fails */
    char* versions;          /* owned: the range as the ban table writes it, ">= 26.1.0 < 26.2.0" */
    char* broken;            /* owned: the highest version that fails, as its vendor numbers it */
    long* failing;           /* owned: the reports that fail, the oldest first */
    int nfailing, people;
    long* passing; /* owned: the other drivers' reports that pass, the oldest first */
    int npassing, pass_drivers;
    int eden_fail, eden_pass; /* Eden's runs (any emulator but ScarletNX with a driver) on this driver: fail, pass */
    char* extensions;         /* owned: the optional extensions on in every failing run, NULL where one names none */
    double newest;            /* the newest failing report's day */
} RuleCandidate;

typedef struct {
    int name;
    const char *version, *words;
    int* drivers; /* owned: the drivers it fails on */
    int ndrivers, seen;
    long* reports; /* owned: ScarletNX's reports that fail it, the oldest first */
    int nreports;
    int eden_runs, eden_pass; /* Eden's runs on those drivers, and of them those that pass */
} RuleGeneral;

typedef struct {
    int name;
    const char *version, *words;
    int driver_id;
    unsigned long driver_version;
    const char* os;
    const char *passed_commit, *failed_commit; /* the last build that passed, the first that failed after it */
    double passed_built, failed_built;
    long passed_report, failed_report; /* a report of each */
    int builds;                        /* the builds that fail, to the newest */
} RuleRegression;

typedef struct {
    RuleCandidate* candidates;
    int ncandidates;
    RuleGeneral* general;
    int ngeneral;
    RuleRegression* regressions;
    int nregressions;
} Rules;

/* the rules over runs (any order) and their results (st_probe_results' order: by case, then report), into out; the
   runs' strings are pointed at, not copied, so runs outlives out. 0, or -1 out of memory */
int rules_make(const StProbeRun* runs, int nruns, const StProbeResult* results, int nresults, Rules* out);
void rules_free(Rules* r);

/* candidate c's line in the ban table's format, its key "<key>" for a person to choose, as the runs cannot say which
   extension or feature it is: "<key> | off | radv | >= 26.1.0 < 26.2.0 | linux | * | fails edenprobe's astc case
   \"...\" | Tracer #12, #15, #18; edenprobe 0.9.0 \"...\" | edenprobe:astc: ... | 2026-11-07 broken on 26.1.8 | -".
   suite and name are the case's; owned */
char* rules_draft(const RuleCandidate* c, const char* suite, const char* name);
#endif
