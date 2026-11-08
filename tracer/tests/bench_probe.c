/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* What the runs of edenprobe cost where Tracer runs: RUNS runs of a run like edenprobe 0.9.0's graphics run (1069
   cases, a few with a detail) on fifteen machines (ScarletNX on twelve drivers and systems, Eden on two, the console),
   each machine failing cases of its own and a few that come and go, kept in a new database in FOLDER; at 1, 10, 100,
   250, 500, 1000, 2000... runs it says the database's size, the Probe pages' and their JSON's, the time the rules take
   and the time every Probe page takes, as a new run makes them drawn again: the processor's seconds, and in brackets
   the clock's, which a busy disk lengthens. No log is written: a run's log costs what
   its file weighs (Tracer keeps it as it came, less what it cuts).

    make bench [RUNS=1000] [FOLDER=/tmp/tracer-bench]    or    tests/bench_probe RUNS FOLDER

   FOLDER must not be there; it is left for a look, and removed by hand */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "probe.h"
#include "rules.h"
#include "site.h"
#include "store.h"

static double clock_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}
/* the processor's seconds this program has taken, its own and the system's for it */
static double cpu_s(void) {
    struct rusage u;
    getrusage(RUSAGE_SELF, &u);
    return (double)u.ru_utime.tv_sec + (double)u.ru_utime.tv_usec / 1e6 + (double)u.ru_stime.tv_sec +
           (double)u.ru_stime.tv_usec / 1e6;
}
static double day0(void) { return 1793491200.0 + 86400.0 * 5; } /* the reports' clock: 2026-11-06 */
/* the bytes and the files under path */
static long long weigh(const char* path, long* files) {
    struct stat st;
    long long n = 0;
    DIR* d;
    struct dirent* e;
    if (lstat(path, &st)) return 0;
    if (!S_ISDIR(st.st_mode)) {
        ++*files;
        return (long long)st.st_size;
    }
    d = opendir(path);
    while (d && (e = readdir(d))) {
        char* sub;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        sub = rei_format("%s/%s", path, e->d_name);
        n += weigh(sub, files);
        free(sub);
    }
    if (d) closedir(d);
    return n;
}
static unsigned long g_bench = 12345; /* the pseudo-random state: the same runs every time */
static unsigned long next(void) {
    g_bench = g_bench * 6364136223846793005UL + 1442695040888963407UL;
    return g_bench >> 33;
}

/* the machines, as a run names them */
static const struct {
    const char *emulator, *os, *name, *info;
    int id;
    unsigned long version;
    int own; /* the cases that fail on it alone, of 1069 */
} MACHINES[] = {
    {"ScarletNX", "linux", "radv", "Mesa 26.1.8", 3, (26UL << 22) | (1UL << 12) | 8, 5},
    {"ScarletNX", "linux", "radv", "Mesa 26.2.6", 3, (26UL << 22) | (2UL << 12) | 6, 3},
    {"ScarletNX", "linux", "radv", "Mesa 25.3.5", 3, (25UL << 22) | (3UL << 12) | 5, 8},
    {"ScarletNX", "windows", "NVIDIA", "621.14", 4, 0x9b404000UL, 12},
    {"ScarletNX", "windows", "NVIDIA", "580.95", 4, 0x9117c000UL, 15},
    {"ScarletNX", "linux", "NVIDIA", "580.95.05", 4, 0x9117c140UL, 14},
    {"ScarletNX", "android", "Mali-G52", "v1.r48p0", 9, 48UL << 22, 40},
    {"ScarletNX", "android", "Qualcomm", "512.800.48", 8, (512UL << 22) | (800UL << 12) | 48, 30},
    {"ScarletNX", "android", "turnip", "Mesa 26.1.0", 18, (26UL << 22) | (1UL << 12), 20},
    {"ScarletNX", "linux", "Intel open-source Mesa driver", "Mesa 25.3.5", 6, (25UL << 22) | (3UL << 12) | 5, 10},
    {"ScarletNX", "android", "Samsung Xclipse", "2.0.3", 21, (2UL << 22) | 3, 25},
    {"ScarletNX", "windows", "AMD proprietary driver", "2.0.302", 1, (2UL << 22) | 302, 18},
    {"Eden", "unknown", "radv", "26.1.8", 3, (26UL << 22) | (1UL << 12) | 8, 6},
    {"Eden", "unknown", "NVIDIA", "580.95.5.0", 4, 0x9117c140UL, 9},
    {"console", "unknown", NULL, NULL, 0, 0, 0},
};

/* a run like edenprobe 0.9.0's quick graphics run: 1069 cases over its judge, cpu and graphics suites */
static void base_run(Probe* p) {
    static const char* const suites[] = {"selfcheck", "support", "reference", "cpu",   "clear",   "format",
                                         "raster",    "intmath", "blend",     "depth", "texture", "coherence",
                                         "fmath",     "order",   "index",     "astc",  "etc2",    "bcn",
                                         "zeta",      "swizzle", "query",     "tex8k", "dma",     "blit2d"};
    int i;
    memset(p, 0, sizeof *p);
    p->version = rei_strdup("0.9.0");
    p->describe = rei_strdup("v0.9.0");
    p->words = rei_strdup("graphics");
    p->system = rei_strdup("'Some Linux' (Linux 6.18), Some CPU, 12 threads, 32 GB RAM");
    p->gpu = rei_strdup("Some GPU");
    p->vulkan = rei_strdup("1.4.354");
    p->emulator_version = rei_strdup("main aaaa111");
    p->passed = 1069;
    p->failed = p->skipped = p->crashed = 0;
    p->whole = 1;
    p->ncases = 1069;
    p->cases = calloc((size_t)p->ncases, sizeof *p->cases);
    if (!p->cases) abort();
    for (i = 0; i < p->ncases; ++i) {
        const char* suite = suites[i % (int)(sizeof suites / sizeof *suites)];
        p->cases[i].suite = rei_strdup(suite);
        p->cases[i].name = rei_format("case %d of the %s suite, drawn and read back at sixteen texels", i, suite);
        p->cases[i].tier = rei_strdup("exact");
        p->cases[i].result = rei_strdup("pass");
        p->cases[i].reason = rei_strdup("");
        p->cases[i].detail = rei_strdup(i % 18 ? "" : "the blit 505 us; the processor's reads 153 us");
        p->cases[i].us = i % 3 ? -1 : 120.0 + i;
    }
}

int main(int argc, char** argv) {
    int runs = argc > 1 ? atoi(argv[1]) : 1000, i, k, nm = (int)(sizeof MACHINES / sizeof *MACHINES), mark = 1;
    const char* root = argc > 2 ? argv[2] : "/tmp/tracer-bench";
    char *path, fail[] = "fail", pass[] = "pass", detail[] = "texel 0 reads 0000ffff, it holds ffff0000";
    Probe base;
    Store* s;
    if (runs < 1 || access(root, F_OK) == 0) {
        fprintf(stderr, "usage: %s RUNS FOLDER, RUNS 1 or more and FOLDER not there (%s is)\n", argv[0], root);
        return 2;
    }
    rei_now = day0;
    mkdir(root, 0755);
    path = rei_format("%s/tracer.db", root);
    if (!(s = st_open(path))) return 1;
    base_run(&base);
    printf("%d runs of %d cases on %d machines, in %s\n", runs, base.ncases, nm, root);
    for (i = 1; i <= runs; ++i) {
        int m = (int)(next() % (unsigned long)nm);
        Probe p = base;
        ProbeCase* cases = malloc((size_t)base.ncases * sizeof *cases);
        StReport r;
        char digest[17];
        double kept, kept_cpu;
        if (!cases) abort();
        memcpy(cases, base.cases, (size_t)base.ncases * sizeof *cases);
        p.cases = cases;
        p.emulator = (char*)MACHINES[m].emulator;
        p.os = (char*)MACHINES[m].os;
        p.driver_name = (char*)MACHINES[m].name;
        p.driver_info = (char*)MACHINES[m].info;
        p.driver_id = MACHINES[m].id;
        p.inferred = strcmp(MACHINES[m].emulator, "ScarletNX") != 0;
        p.driver_version = MACHINES[m].version;
        p.commit = (char*)(i % 3 ? "aaaa111" : "bbbb222");
        p.built = 1793491200.0 + 86400.0 * (i % 5);
        for (k = 0; k < MACHINES[m].own; ++k) { /* the machine's own failures */
            ProbeCase* c = &cases[(m * 7919 + k * 104729) % base.ncases];
            c->result = fail;
            c->detail = detail;
        }
        if (next() % 4 == 0) cases[next() % (unsigned long)base.ncases].result = fail; /* one that comes and goes */
        if (next() % 9 == 0) cases[next() % (unsigned long)base.ncases].result = pass;
        memset(&r, 0, sizeof r);
        r.key = PROBE_KEY;
        r.title = "edenprobe 0.9.0";
        r.named = 1;
        r.when = day0() - (runs - i) * 600.0;
        r.message = 1000000 + (u64)i;
        r.poster = 1 + next() % 300;
        probe_digest(&p, digest);
        kept = clock_s();
        kept_cpu = cpu_s();
        if (st_add(s, &r, NULL, 0) || st_probe_set(s, r.id, &p, digest)) {
            fprintf(stderr, "run %d was not kept\n", i);
            return 1;
        }
        kept = clock_s() - kept;
        kept_cpu = cpu_s() - kept_cpu;
        free(cases);
        if (i == mark || i == runs) { /* a mark: 1, 10, 100, 250, 500, 1000, 2000... */
            double rules_s = clock_s(), rules_cpu = cpu_s(), pages_s, pages_cpu;
            StProbeRun* all;
            StProbeResult* results;
            int nall, nresults;
            Rules rules;
            long db_files = 0, page_files = 0, api_files = 0;
            long long db, pages, api;
            char *pages_path = rei_format("%s/probe", root), *api_path = rei_format("%s/api/probe", root);
            st_probe_runs(s, &all, &nall);
            st_probe_results(s, &results, &nresults);
            rules_make(all, nall, results, nresults, &rules);
            rules_s = clock_s() - rules_s;
            rules_cpu = cpu_s() - rules_cpu;
            pages_s = clock_s();
            pages_cpu = cpu_s();
            site_probe(s, root);
            pages_s = clock_s() - pages_s;
            pages_cpu = cpu_s() - pages_cpu;
            st_close(s); /* the database's write-ahead log folded in, then weighed */
            db = weigh(path, &db_files);
            s = st_open(path);
            pages = weigh(pages_path, &page_files);
            api = weigh(api_path, &api_files);
            printf("%5d runs: database %6.1f MB (%4.1f KB a run); probe/ %5.1f MB in %ld files, api/probe/ %5.1f MB; "
                   "the rules %.2f s [%.2f s] (%d candidates, %d own failures); every Probe page %.2f s [%.2f s]; the "
                   "last run kept in %.0f ms [%.0f ms]\n",
                   i, db / 1048576.0, db / 1024.0 / i, pages / 1048576.0, page_files, api / 1048576.0, rules_cpu,
                   rules_s, rules.ncandidates, rules.ngeneral, pages_cpu, pages_s, kept_cpu * 1000, kept * 1000);
            fflush(stdout);
            rules_free(&rules);
            free(results);
            st_probe_runs_free(all, nall);
            free(pages_path);
            free(api_path);
            mark = mark == 1 ? 10 : mark == 10 ? 100 : mark == 100 ? 250 : mark == 250 ? 500 : mark * 2;
        }
    }
    st_close(s);
    probe_free(&base);
    free(path);
    return 0;
}
