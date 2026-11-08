/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "rules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* a case's result in one run: the run's index in the runs given, ST_* */
typedef struct {
    int run, result;
} Entry;

/* the runs the comparators read, for one rules_make at a time */
static const StProbeRun* g_rules;

static void* grown(void* p, int n, int* cap, size_t size) {
    if (n < *cap) return p;
    *cap = *cap ? *cap * 2 : 16;
    p = realloc(p, (size_t)*cap * size);
    if (!p) {
        fputs(REI_NAME ": out of memory\n", stderr);
        abort();
    }
    return p;
}
static int text_cmp(const char* a, const char* b) { return strcmp(a ? a : "", b ? b : ""); }
/* qsort, for n of 2 or more: an empty array may be NULL, which qsort may not be given */
static void sorted(void* p, int n, size_t size, int (*cmp)(const void*, const void*)) {
    if (n > 1) qsort(p, (size_t)n, size, cmp);
}
static int scarletnx(const Probe* p) { return !strcmp(p->emulator, "ScarletNX"); }
/* Eden, rEden: another emulator, on a machine its log names */
static int other(const Probe* p) { return !scarletnx(p) && strcmp(p->emulator, "console") && p->driver_id > 0; }
/* the day a run's build was made, else the day it came */
static double build_day(const StProbeRun* r) { return r->p.built > 0 ? r->p.built : r->when; }
static long report_of(const Entry* e) { return g_rules[e->run].report; }

/* by edenprobe's version and words, then by report */
static int by_mode(const void* a, const void* b) {
    const Entry *x = a, *y = b;
    const Probe *p = &g_rules[x->run].p, *q = &g_rules[y->run].p;
    int c = text_cmp(p->version, q->version);
    if (!c) c = text_cmp(p->words, q->words);
    if (!c) c = report_of(x) < report_of(y) ? -1 : report_of(x) > report_of(y);
    return c;
}
static int same_mode(const Entry* x, const Entry* y) {
    const Probe *p = &g_rules[x->run].p, *q = &g_rules[y->run].p;
    return !text_cmp(p->version, q->version) && !text_cmp(p->words, q->words);
}
/* by the driver's version, then by report */
static int by_version(const void* a, const void* b) {
    const Entry *x = a, *y = b;
    unsigned long v = g_rules[x->run].p.driver_version, w = g_rules[y->run].p.driver_version;
    if (v != w) return v < w ? -1 : 1;
    return report_of(x) < report_of(y) ? -1 : report_of(x) > report_of(y);
}
static int by_long(const void* a, const void* b) {
    long x = *(const long*)a, y = *(const long*)b;
    return x < y ? -1 : x > y;
}

/* ---- ScarletNX's own failures ---- */

/* the drivers of ScarletNX's runs among e[0..n): each one's id, and how many of its runs fail and pass */
typedef struct {
    int id, fails, passes;
} Driver;
static int drivers_of(const Entry* e, int n, Driver** out) {
    Driver* d = NULL;
    int nd = 0, cap = 0, i, k;
    for (i = 0; i < n; ++i) {
        const Probe* p = &g_rules[e[i].run].p;
        if (!scarletnx(p) || p->driver_id <= 0 || (e[i].result != ST_PASS && e[i].result != ST_FAIL)) continue;
        for (k = 0; k < nd && d[k].id != p->driver_id; ++k) continue;
        if (k == nd) {
            d = grown(d, nd, &cap, sizeof *d);
            d[nd].id = p->driver_id;
            d[nd].fails = d[nd].passes = 0;
            ++nd;
        }
        d[k].fails += e[i].result == ST_FAIL;
        d[k].passes += e[i].result == ST_PASS;
    }
    *out = d;
    return nd;
}
static int failing_driver(const Driver* d) { return d->fails > 0 && d->passes == 0; }

/* e[0..n), one case of one version and words: its failure, when it is ScarletNX's own, added to r. 1 when it is */
static int general(Rules* r, int* cap, int name, const Entry* e, int n) {
    Driver* d;
    int nd = drivers_of(e, n, &d), failing = 0, i, k, rcap = 0;
    RuleGeneral* g;
    for (k = 0; k < nd; ++k) failing += failing_driver(&d[k]);
    if (!(failing >= RULE_GENERAL_DRIVERS || (nd >= 2 && failing == nd))) {
        free(d);
        return 0;
    }
    r->general = grown(r->general, r->ngeneral, cap, sizeof *r->general);
    g = &r->general[r->ngeneral++];
    memset(g, 0, sizeof *g);
    g->name = name;
    g->version = g_rules[e[0].run].p.version;
    g->words = g_rules[e[0].run].p.words;
    g->seen = nd;
    g->drivers = malloc((size_t)failing * sizeof *g->drivers);
    if (!g->drivers) abort();
    for (k = 0; k < nd; ++k)
        if (failing_driver(&d[k])) g->drivers[g->ndrivers++] = d[k].id;
    for (i = 0; i < n; ++i) {
        const Probe* p = &g_rules[e[i].run].p;
        int on = 0;
        for (k = 0; k < g->ndrivers; ++k) on |= g->drivers[k] == p->driver_id;
        if (!on) continue;
        if (scarletnx(p) && e[i].result == ST_FAIL) {
            g->reports = grown(g->reports, g->nreports, &rcap, sizeof *g->reports);
            g->reports[g->nreports++] = report_of(&e[i]);
        } else if (other(p) && (e[i].result == ST_PASS || e[i].result == ST_FAIL)) {
            ++g->eden_runs;
            g->eden_pass += e[i].result == ST_PASS;
        }
    }
    sorted(g->reports, g->nreports, sizeof *g->reports, by_long);
    free(d);
    return 1;
}

/* ---- ban candidates ---- */

/* whether list (comma separated, spaces around each name) has the name of n bytes at name */
static int listed(const char* list, const char* name, size_t n) {
    while (*list) {
        size_t len;
        while (*list == ' ') ++list;
        len = strcspn(list, ",");
        while (len && list[len - 1] == ' ') --len;
        if (len == n && !memcmp(list, name, n)) return 1;
        list += strcspn(list, ",");
        if (*list == ',') ++list;
    }
    return 0;
}
/* the extensions every one of the runs names as on, comma separated; NULL when one names none */
static char* common_extensions(const Entry* e, int n) {
    Buf b = {0};
    const char* s;
    int i;
    if (!n) return NULL;
    for (i = 0; i < n; ++i)
        if (!g_rules[e[i].run].p.extensions) return NULL;
    for (s = g_rules[e[0].run].p.extensions; *s;) {
        size_t len;
        int all = 1;
        while (*s == ' ') ++s;
        len = strcspn(s, ",");
        while (len && s[len - 1] == ' ') --len;
        for (i = 1; i < n && all && len; ++i) all = listed(g_rules[e[i].run].p.extensions, s, len);
        if (all && len) buf_printf(&b, "%s%.*s", b.len ? ", " : "", (int)len, s);
        s += strcspn(s, ",");
        if (*s == ',') ++s;
    }
    return buf_take(&b);
}

static int by_double(const void* a, const void* b) {
    double x = *(const double*)a, y = *(const double*)b;
    return x < y ? -1 : x > y;
}
/* whether day is RULE_DAYS days or less from one of the n days (sorted): the nearest are the two on either side */
static int near_day(double day, const double* days, int n) {
    int lo = 0, hi = n, k, near = 0;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (days[mid] < day)
            lo = mid + 1;
        else
            hi = mid;
    }
    for (k = lo - 1; k <= lo && !near; ++k) {
        double gap;
        if (k < 0 || k >= n) continue;
        gap = day - days[k];
        near = gap <= RULE_DAYS * 86400.0 && gap >= -RULE_DAYS * 86400.0;
    }
    return near;
}

/* e[0..n), one case of one version and words, ScarletNX's failure of it on driver d and system's family os: its
   candidates, a range of versions each, added to r */
static void candidates_on(Rules* r, int* cap, int name, const Entry* e, int n, int d, const char* os) {
    Entry* on = NULL; /* ScarletNX's runs on the driver and system that passed or failed, by version */
    int non = 0, oncap = 0, i, k;
    for (i = 0; i < n; ++i) {
        const Probe* p = &g_rules[e[i].run].p;
        if (scarletnx(p) && p->driver_id == d && !strcmp(p->os, os) &&
            (e[i].result == ST_PASS || e[i].result == ST_FAIL)) {
            on = grown(on, non, &oncap, sizeof *on);
            on[non++] = e[i];
        }
    }
    sorted(on, non, sizeof *on, by_version);
    for (i = 0; i < non;) {
        int start = i, end, fails = 0, j, people = 0, npass = 0, pcap = 0, fcap = 0, drivers = 0;
        unsigned long v = g_rules[on[i].run].p.driver_version;
        long *pass = NULL, *fail = NULL;
        int *pass_driver = NULL, dcap = 0, wcap = 0;
        u64* posters = NULL;
        double* days;
        RuleCandidate* c;
        double newest = 0;
        char low[32], high[32], top[32];
        /* the range: the versions in a row where it fails and never passes */
        for (end = i; end < non;) {
            int vf = 0, vp = 0, at = end;
            unsigned long w = g_rules[on[end].run].p.driver_version;
            while (at < non && g_rules[on[at].run].p.driver_version == w) {
                vf += on[at].result == ST_FAIL;
                vp += on[at].result == ST_PASS;
                ++at;
            }
            if (vp) break;
            fails += vf;
            end = at;
        }
        if (end == start) { /* a version where it passes: past it */
            while (i < non && g_rules[on[i].run].p.driver_version == v) ++i;
            continue;
        }
        i = end;
        for (j = start; j < end; ++j) { /* the people its failures come from */
            u64 who = g_rules[on[j].run].poster;
            for (k = 0; k < people && posters[k] != who; ++k) continue;
            if (k == people) {
                posters = grown(posters, people, &wcap, sizeof *posters);
                posters[people++] = who;
            }
            fail = grown(fail, j - start, &fcap, sizeof *fail);
            fail[j - start] = report_of(&on[j]);
            if (g_rules[on[j].run].when > newest) newest = g_rules[on[j].run].when;
        }
        free(posters);
        if (fails < RULE_FAIL_REPORTS || people < RULE_FAIL_PEOPLE) {
            free(fail);
            continue;
        }
        /* the other drivers' runs that pass, their builds near a failing one's */
        days = malloc((size_t)(end - start) * sizeof *days);
        if (!days) abort();
        for (j = start; j < end; ++j) days[j - start] = build_day(&g_rules[on[j].run]);
        sorted(days, end - start, sizeof *days, by_double);
        for (j = 0; j < n; ++j) {
            const StProbeRun* run = &g_rules[e[j].run];
            int m;
            if (!scarletnx(&run->p) || run->p.driver_id <= 0 || run->p.driver_id == d || e[j].result != ST_PASS)
                continue;
            if (!near_day(build_day(run), days, end - start)) continue;
            pass = grown(pass, npass, &pcap, sizeof *pass);
            pass[npass++] = run->report;
            for (m = 0; m < drivers && pass_driver[m] != run->p.driver_id; ++m) continue;
            if (m == drivers) {
                pass_driver = grown(pass_driver, drivers, &dcap, sizeof *pass_driver);
                pass_driver[drivers++] = run->p.driver_id;
            }
        }
        free(pass_driver);
        free(days);
        if (npass < RULE_PASS_REPORTS || drivers < RULE_PASS_DRIVERS) {
            free(fail);
            free(pass);
            continue;
        }
        r->candidates = grown(r->candidates, r->ncandidates, cap, sizeof *r->candidates);
        c = &r->candidates[r->ncandidates++];
        memset(c, 0, sizeof *c);
        c->name = name;
        c->version = g_rules[e[0].run].p.version;
        c->words = g_rules[e[0].run].p.words;
        c->driver_id = d;
        c->os = g_rules[on[start].run].p.os;
        c->low = g_rules[on[start].run].p.driver_version;
        c->high = end < non ? g_rules[on[end].run].p.driver_version : 0;
        for (j = end; j < non && g_rules[on[j].run].p.driver_version == c->high; ++j)
            c->mixed |= c->high && on[j].result == ST_FAIL;
        probe_version_text(d, c->low, low, sizeof low);
        probe_version_text(d, g_rules[on[end - 1].run].p.driver_version, top, sizeof top);
        if (c->high) {
            probe_version_text(d, c->high, high, sizeof high);
            c->versions = rei_format(">= %s < %s", low, high);
        } else
            c->versions = rei_format(">= %s", low);
        c->broken = rei_strdup(top);
        sorted(fail, end - start, sizeof *fail, by_long);
        sorted(pass, npass, sizeof *pass, by_long);
        c->failing = fail;
        c->nfailing = end - start;
        c->people = people;
        c->passing = pass;
        c->npassing = npass;
        c->pass_drivers = drivers;
        c->newest = newest;
        c->extensions = common_extensions(on + start, end - start);
        for (j = 0; j < n; ++j) { /* Eden's runs of the case on this driver */
            const Probe* p = &g_rules[e[j].run].p;
            if (!other(p) || p->driver_id != d) continue;
            c->eden_fail += e[j].result == ST_FAIL;
            c->eden_pass += e[j].result == ST_PASS;
        }
    }
    free(on);
}

static void candidates(Rules* r, int* cap, int name, const Entry* e, int n) {
    const Probe** seen = NULL; /* a run of each driver and system ScarletNX fails the case on */
    int i, k, nseen = 0, scap = 0;
    for (i = 0; i < n; ++i) {
        const Probe* p = &g_rules[e[i].run].p;
        if (!scarletnx(p) || p->driver_id <= 0 || e[i].result != ST_FAIL) continue;
        for (k = 0; k < nseen && !(seen[k]->driver_id == p->driver_id && !strcmp(seen[k]->os, p->os)); ++k) continue;
        if (k < nseen) continue;
        seen = grown(seen, nseen, &scap, sizeof *seen);
        seen[nseen++] = p;
        candidates_on(r, cap, name, e, n, p->driver_id, p->os);
    }
    free(seen);
}

/* ---- regressions ---- */

/* a commit as builds name a published one: hex digits, no -dirty */
static int published(const char* commit) {
    size_t n = commit ? strlen(commit) : 0, i;
    if (n < 7) return 0;
    for (i = 0; i < n; ++i)
        if (!((commit[i] >= '0' && commit[i] <= '9') || (commit[i] >= 'a' && commit[i] <= 'f'))) return 0;
    return 1;
}
typedef struct {
    const char* commit;
    double day;
    long first; /* its first report, to order builds of one day */
    int fails, passes;
} Build;
/* by commit, then report */
static int by_commit(const void* a, const void* b) {
    const Entry *x = a, *y = b;
    int c = strcmp(g_rules[x->run].p.commit, g_rules[y->run].p.commit);
    return c ? c : report_of(x) < report_of(y) ? -1 : report_of(x) > report_of(y);
}
static int by_build(const void* a, const void* b) {
    const Build *x = a, *y = b;
    if (x->day != y->day) return x->day < y->day ? -1 : 1;
    return x->first < y->first ? -1 : x->first > y->first;
}

static void regressions(Rules* r, int* cap, int name, const Entry* e, int n) {
    const Probe** seen = NULL; /* a run of each driver, version of it and system, once */
    Entry* group = NULL;
    int i, k, nseen = 0, scap = 0, gcap = 0;
    for (i = 0; i < n; ++i) {
        const Probe* p = &g_rules[e[i].run].p;
        Build* b = NULL;
        int nb = 0, bcap = 0, j, last, ng;
        if (!scarletnx(p) || p->driver_id <= 0) continue;
        for (k = 0; k < nseen && !(seen[k]->driver_id == p->driver_id && seen[k]->driver_version == p->driver_version &&
                                   !strcmp(seen[k]->os, p->os));
             ++k)
            continue;
        if (k < nseen) continue;
        seen = grown(seen, nseen, &scap, sizeof *seen);
        seen[nseen++] = p;
        for (j = 0, ng = 0; j < n; ++j) { /* the group's runs of published builds that passed or failed */
            const Probe* q = &g_rules[e[j].run].p;
            if (!scarletnx(q) || q->driver_id != p->driver_id || q->driver_version != p->driver_version ||
                strcmp(q->os, p->os) || !published(q->commit) || q->built <= 0 ||
                (e[j].result != ST_PASS && e[j].result != ST_FAIL))
                continue;
            group = grown(group, ng, &gcap, sizeof *group);
            group[ng++] = e[j];
        }
        sorted(group, ng, sizeof *group, by_commit);
        for (j = 0; j < ng; ++j) { /* a build for each commit, the group's runs being by commit */
            const StProbeRun* run = &g_rules[group[j].run];
            if (!nb || strcmp(b[nb - 1].commit, run->p.commit)) {
                b = grown(b, nb, &bcap, sizeof *b);
                b[nb].commit = run->p.commit;
                b[nb].day = run->p.built;
                b[nb].first = run->report;
                b[nb].fails = b[nb].passes = 0;
                ++nb;
            }
            b[nb - 1].fails += group[j].result == ST_FAIL;
            b[nb - 1].passes += group[j].result == ST_PASS;
        }
        sorted(b, nb, sizeof *b, by_build);
        for (last = nb - 1; last >= 0 && b[last].fails && !b[last].passes; --last) continue;
        if (last >= 0 && last < nb - 1 && b[last].passes && !b[last].fails) {
            RuleRegression* g;
            r->regressions = grown(r->regressions, r->nregressions, cap, sizeof *r->regressions);
            g = &r->regressions[r->nregressions++];
            memset(g, 0, sizeof *g);
            g->name = name;
            g->version = p->version;
            g->words = p->words;
            g->driver_id = p->driver_id;
            g->driver_version = p->driver_version;
            g->os = p->os;
            g->passed_commit = b[last].commit;
            g->passed_built = b[last].day;
            g->passed_report = b[last].first;
            g->failed_commit = b[last + 1].commit;
            g->failed_built = b[last + 1].day;
            g->failed_report = b[last + 1].first;
            g->builds = nb - 1 - last;
        }
        free(b);
    }
    free(group);
    free(seen);
}

/* ---- the whole ---- */

static int by_report(const void* a, const void* b) {
    long x = g_rules[*(const int*)a].report, y = g_rules[*(const int*)b].report;
    return x < y ? -1 : x > y;
}

int rules_make(const StProbeRun* runs, int nruns, const StProbeResult* results, int nresults, Rules* out) {
    int *order = malloc((size_t)(nruns ? nruns : 1) * sizeof *order), i, j, gcap = 0, ccap = 0, rcap = 0;
    Entry* e = malloc((size_t)(nresults ? nresults : 1) * sizeof *e);
    memset(out, 0, sizeof *out);
    if (!order || !e) {
        free(order);
        free(e);
        return -1;
    }
    g_rules = runs;
    for (i = 0; i < nruns; ++i) order[i] = i;
    sorted(order, nruns, sizeof *order, by_report);
    for (i = 0; i < nresults; i = j) {
        int n = 0, a, b;
        for (j = i; j < nresults && results[j].name == results[i].name; ++j) { /* the case's results, each run's */
            int lo = 0, hi = nruns - 1;
            while (lo <= hi) {
                int mid = (lo + hi) / 2;
                long at = runs[order[mid]].report;
                if (at == results[j].report) {
                    e[n].run = order[mid];
                    e[n++].result = results[j].result;
                    break;
                }
                if (at < results[j].report)
                    lo = mid + 1;
                else
                    hi = mid - 1;
            }
        }
        sorted(e, n, sizeof *e, by_mode);
        for (a = 0; a < n; a = b) { /* each version and words of edenprobe apart */
            for (b = a; b < n && same_mode(&e[a], &e[b]); ++b) continue;
            if (!general(out, &gcap, results[i].name, e + a, b - a))
                candidates(out, &ccap, results[i].name, e + a, b - a);
            regressions(out, &rcap, results[i].name, e + a, b - a);
        }
    }
    free(order);
    free(e);
    g_rules = NULL;
    return 0;
}

void rules_free(Rules* r) {
    int i;
    for (i = 0; i < r->ncandidates; ++i) {
        free(r->candidates[i].versions);
        free(r->candidates[i].broken);
        free(r->candidates[i].failing);
        free(r->candidates[i].passing);
        free(r->candidates[i].extensions);
    }
    for (i = 0; i < r->ngeneral; ++i) {
        free(r->general[i].drivers);
        free(r->general[i].reports);
    }
    free(r->candidates);
    free(r->general);
    free(r->regressions);
    memset(r, 0, sizeof *r);
}

/* text for a field of the ban table: no |, which ends a field, and no line end */
static void table_text(Buf* b, const char* s) {
    for (; s && *s; ++s) buf_addn(b, *s == '|' ? "/" : (*s == '\n' || *s == '\r') ? " " : s, 1);
}

char* rules_draft(const RuleCandidate* c, const char* suite, const char* name) {
    Buf b = {0};
    char day[16];
    time_t t = (time_t)c->newest;
    struct tm tm;
    int i;
    gmtime_r(&t, &tm);
    strftime(day, sizeof day, "%Y-%m-%d", &tm);
    buf_printf(&b, "<key> | off | %s | %s | %s | * | fails edenprobe's ",
               probe_driver_name(c->driver_id) ? probe_driver_name(c->driver_id) : "?", c->versions,
               strcmp(c->os, "unknown") ? c->os : "any");
    table_text(&b, suite);
    buf_add(&b, " case \"");
    table_text(&b, name);
    buf_add(&b, "\" | Tracer");
    for (i = 0; i < c->nfailing; ++i) buf_printf(&b, "%s #%ld", i ? "," : "", c->failing[i]);
    buf_add(&b, "; edenprobe ");
    table_text(&b, c->version);
    buf_add(&b, " \"");
    table_text(&b, name);
    buf_add(&b, "\" | edenprobe:");
    table_text(&b, suite);
    buf_add(&b, ": ");
    table_text(&b, name);
    buf_printf(&b, " | %s broken on %s | -", day, c->broken);
    return buf_take(&b);
}
