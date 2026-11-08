/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "probe.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "files.h"

/* ---- edenprobe's families ---- */

/* which family each suite is of, by the name its lines carry: edenprobe's ui.c, FAMILIES (edenprobe 86e1d11) */
static const struct {
    const char* suite;
    ProbeFamily family;
} FAMILIES[] = {
    {"selfcheck", PROBE_JUDGE},
    {"support", PROBE_JUDGE},
    {"reference", PROBE_JUDGE},
    {"cpu", PROBE_CPU},
    {"pad", PROBE_PAD},
    {"touch", PROBE_PAD},
    {"clear", PROBE_GRAPHICS},
    {"format", PROBE_GRAPHICS},
    {"raster", PROBE_GRAPHICS},
    {"intmath", PROBE_GRAPHICS},
    {"blend", PROBE_GRAPHICS},
    {"depth", PROBE_GRAPHICS},
    {"fixed", PROBE_GRAPHICS},
    {"texture", PROBE_GRAPHICS},
    {"coherence", PROBE_GRAPHICS},
    {"fmath", PROBE_GRAPHICS},
    {"order", PROBE_GRAPHICS},
    {"index", PROBE_GRAPHICS},
    {"instance", PROBE_GRAPHICS},
    {"bindless", PROBE_GRAPHICS},
    {"astc", PROBE_GRAPHICS},
    {"astcimage", PROBE_GRAPHICS},
    {"eac", PROBE_GRAPHICS},
    {"etc2", PROBE_GRAPHICS},
    {"bcn", PROBE_GRAPHICS},
    {"zeta", PROBE_GRAPHICS},
    {"swizzle", PROBE_GRAPHICS},
    {"texel", PROBE_GRAPHICS},
    {"stream", PROBE_GRAPHICS},
    {"fill", PROBE_GRAPHICS},
    {"ring", PROBE_GRAPHICS},
    {"occlusion", PROBE_GRAPHICS},
    {"drawtex", PROBE_GRAPHICS},
    {"geometry", PROBE_GRAPHICS},
    {"query", PROBE_GRAPHICS},
    {"dma", PROBE_GRAPHICS},
    {"blit2d", PROBE_GRAPHICS},
    {"tex8k", PROBE_GRAPHICS},
    {"fs", PROBE_FS},
    {"sched", PROBE_KERNEL},
    {"binder", PROBE_DISPLAY},
    {"vic", PROBE_VIDEO},
    {"nvdec", PROBE_VIDEO},
    {"audio", PROBE_AUDIO},
    {"hwopus", PROBE_SERVICE},
    {"am", PROBE_SERVICE},
    {"fsp", PROBE_SERVICE},
    {"account", PROBE_SERVICE},
    {"ldn", PROBE_SERVICE},
    {"set", PROBE_SERVICE},
    {"csrng", PROBE_SERVICE},
    {"apm", PROBE_SERVICE},
    {"nfp", PROBE_SERVICE},
    {"aoc", PROBE_SERVICE},
    {"time", PROBE_SERVICE},
    {"pl", PROBE_SERVICE},
    {"profile", PROBE_SERVICE},
    {"am-state", PROBE_SERVICE},
    {"am-self", PROBE_SERVICE},
    {"am-functions", PROBE_SERVICE},
    {"auddev", PROBE_SERVICE},
    {"fsdir", PROBE_SERVICE},
    {"am-audio", PROBE_SERVICE},
    {"am-channel", PROBE_SERVICE},
    {"am-storage", PROBE_SERVICE},
    {"mii", PROBE_SERVICE},
    {"am-mode", PROBE_SERVICE},
    {"bsd-local", PROBE_SERVICE},
    {"ipc", PROBE_SERVICE},
    {"hid", PROBE_SERVICE},
    {"time-snap", PROBE_SERVICE},
    {"vi", PROBE_SERVICE},
    {"renderer", PROBE_SERVICE},
    {"ngc", PROBE_SERVICE},
    {"am-lock", PROBE_SERVICE},
    {"set-sys", PROBE_SERVICE},
    {"time-zone", PROBE_SERVICE},
    {"time-perm", PROBE_SERVICE},
    {"audout", PROBE_SERVICE},
    {"fs-file", PROBE_SERVICE},
    {"acc-app", PROBE_SERVICE},
    {"nifm", PROBE_NET},
    {"dns", PROBE_NET},
    {"bsd", PROBE_NET},
    {"ssl", PROBE_NET},
    {"acc", PROBE_NET},
    {"friend", PROBE_NET},
    {"game", PROBE_GAME},
};

ProbeFamily probe_family(const char* suite) {
    size_t i;
    for (i = 0; suite && i < sizeof FAMILIES / sizeof *FAMILIES; ++i)
        if (!strcmp(FAMILIES[i].suite, suite)) return FAMILIES[i].family;
    return PROBE_OTHER;
}

const char* probe_family_name(ProbeFamily f) {
    static const char* const names[] = {"judge", "cpu",   "pad",     "graphics", "fs",   "kernel", "display",
                                        "video", "audio", "service", "net",      "game", "other"};
    return f >= PROBE_JUDGE && f <= PROBE_OTHER ? names[f] : "other";
}

int probe_keeps_detail(const char* suite) {
    ProbeFamily f = probe_family(suite);
    return f != PROBE_SERVICE && f != PROBE_NET && f != PROBE_PAD && f != PROBE_OTHER;
}

/* ---- the drivers: ScarletNX's ban table's names and numbering (src/nx/gpu/vk/extension_bans.c, #1390) ---- */

static const char* const DRIVER_NAMES[] = {
    NULL,          "amd-proprietary", "amdvlk", "radv",        "nvidia",       "intel-windows", "anv",
    "powervr",     "qualcomm",        "mali",   "swiftshader", "ggp",          "broadcom",      "lavapipe",
    "moltenvk",    "coreavi",         "juice",  "verisilicon", "turnip",       "v3dv",          "panvk",
    "samsung",     "venus",           "dozen",  "nvk",         "powervr-mesa", "honeykrisp",    "vulkansc",
    "kosmickrisp", "gfxstream",       "apesoft"};
static const char* const DRIVER_LABELS[] = {NULL,
                                            "AMD's proprietary driver",
                                            "AMDVLK",
                                            "RADV",
                                            "NVIDIA's driver",
                                            "Intel's Windows driver",
                                            "ANV",
                                            "Imagination's PowerVR driver",
                                            "Qualcomm's driver",
                                            "Arm's Mali driver",
                                            "SwiftShader",
                                            "GGP's driver",
                                            "Broadcom's driver",
                                            "lavapipe",
                                            "MoltenVK",
                                            "CoreAVI's driver",
                                            "Juice's driver",
                                            "VeriSilicon's driver",
                                            "Turnip",
                                            "v3dv",
                                            "PanVK",
                                            "Samsung's Xclipse driver",
                                            "Venus",
                                            "Dozen",
                                            "NVK",
                                            "Imagination's Mesa driver",
                                            "Honeykrisp",
                                            "Vulkan SC emulation",
                                            "KosmicKrisp",
                                            "gfxstream",
                                            "APE Soft's driver"};
#define DRIVERS ((int)(sizeof DRIVER_NAMES / sizeof *DRIVER_NAMES))
enum { DRV_NVIDIA = 4, DRV_INTEL_WINDOWS = 5, DRV_MOLTENVK = 14 };

const char* probe_driver_name(int id) { return id > 0 && id < DRIVERS ? DRIVER_NAMES[id] : NULL; }
const char* probe_driver_label(int id) { return id > 0 && id < DRIVERS ? DRIVER_LABELS[id] : NULL; }
int probe_driver_id(const char* name) {
    int id;
    for (id = 1; name && id < DRIVERS; ++id)
        if (!strcmp(DRIVER_NAMES[id], name)) return id;
    return 0;
}

static int has_word(const char* text, const char* word) {
    size_t n = strlen(word);
    const char* s;
    for (s = text; *s; ++s)
        if (!strncasecmp(s, word, n)) return 1;
    return 0;
}
int probe_driver_of_name(const char* driver_name) {
    /* the words in the drivers' own names, each more particular one before the word it holds: NVK before NVIDIA */
    static const struct {
        const char* word;
        int id;
    } words[] = {{"radv", 3},
                 {"nvk", 24},
                 {"nvidia", 4},
                 {"turnip", 18},
                 {"qualcomm", 8},
                 {"adreno", 8},
                 {"mali", 9},
                 {"amd proprietary", 1},
                 {"amd open-source", 2},
                 {"amdvlk", 2},
                 {"intel open-source", 6},
                 {"anv", 6},
                 {"intel", 5},
                 {"llvmpipe", 13},
                 {"lavapipe", 13},
                 {"moltenvk", 14},
                 {"xclipse", 21},
                 {"samsung", 21},
                 {"panvk", 20},
                 {"v3dv", 19},
                 {"venus", 22},
                 {"dozen", 23},
                 {"swiftshader", 10},
                 {"honeykrisp", 26},
                 {"kosmickrisp", 28},
                 {"powervr", 7}};
    size_t i;
    for (i = 0; driver_name && i < sizeof words / sizeof *words; ++i)
        if (has_word(driver_name, words[i].word)) return words[i].id;
    return 0;
}

void probe_version_text(int id, unsigned long v, char* out, size_t n) {
    if (id == DRV_NVIDIA)
        snprintf(out, n, "%lu.%lu.%02lu", v >> 22, v >> 14 & 0xffu, v >> 6 & 0xffu);
    else if (id == DRV_INTEL_WINDOWS)
        snprintf(out, n, "%lu.%lu", v >> 14, v & 0x3fffu);
    else if (id == DRV_MOLTENVK)
        snprintf(out, n, "%lu.%lu.%lu", v / 10000u, v / 100u % 100u, v % 100u);
    else /* VK_MAKE_VERSION's fields: a major of 10 bits, as Qualcomm's 512 needs */
        snprintf(out, n, "%lu.%lu.%lu", v >> 22, v >> 12 & 0x3ffu, v & 0xfffu);
}

int probe_version_parse(int id, const char* text, unsigned long* v) {
    unsigned long part[4] = {0};
    int parts = 0;
    const char* s = text;
    while (*s) {
        unsigned long value = 0;
        int digits = 0;
        while (*s >= '0' && *s <= '9' && digits < 10) value = value * 10 + (unsigned long)(*s++ - '0'), ++digits;
        if (!digits || value > 0xffffffffUL || parts == 4 || (*s && *s != '.') || (*s == '.' && !s[1])) return 0;
        part[parts++] = value;
        s += *s == '.';
    }
    if (id == DRV_NVIDIA) { /* A.B[.C[.D]] */
        if (parts < 2 || part[0] > 1023 || part[1] > 255 || part[2] > 255 || part[3] > 63) return 0;
        *v = part[0] << 22 | part[1] << 14 | part[2] << 6 | part[3];
    } else if (id == DRV_INTEL_WINDOWS) { /* the last two of its four numbers */
        if ((parts != 2 && parts != 4) || part[parts - 2] > 0x3ffffUL || part[parts - 1] > 0x3fffUL) return 0;
        *v = part[parts - 2] << 14 | part[parts - 1];
    } else if (id == DRV_MOLTENVK) {
        if (parts < 2 || parts > 3 || part[0] > 429495 || part[1] > 99 || part[2] > 99) return 0;
        *v = part[0] * 10000 + part[1] * 100 + part[2];
    } else {
        if (parts < 2 || parts > 3 || part[0] > 1023 || part[1] > 1023 || part[2] > 4095) return 0;
        *v = part[0] << 22 | part[1] << 12 | part[2];
    }
    return 1;
}

const char* probe_os(const char* system) {
    /* Android before Linux, as its line names its kernel: "Android 15 (Linux 6.1...)" */
    static const struct {
        const char *word, *os;
    } systems[] = {{"android", "android"}, {"windows", "windows"}, {"macos", "macos"}, {"mac os", "macos"},
                   {"darwin", "macos"},    {"ipados", "ios"},      {"ios ", "ios"},    {"freebsd", "freebsd"},
                   {"openbsd", "openbsd"}, {"netbsd", "netbsd"},   {"linux", "linux"}};
    size_t i;
    for (i = 0; system && i < sizeof systems / sizeof *systems; ++i)
        if (has_word(system, systems[i].word)) return systems[i].os;
    return "unknown";
}

/* ---- the lines ---- */

/* the forms a report line comes in */
enum { FORM_SCARLETNX = 1, FORM_EDEN, FORM_REPORT };
static const char* find_in(const char* s, size_t n, const char* lit);
/* the report line's JSON object in a line of the n bytes at s: after "[game] " (ScarletNX), after Eden's
   "Debug.Emulated <Level> file:line:function: ", or the line itself (report.jsonl), its form in *form. Its offset, or
   -1 for none */
static long json_at(const char* s, size_t n, int* form) {
    size_t i = tr_stamp(s, n);
    const char *emulated, *colon;
    if (n - i >= 8 && !memcmp(s + i, "[game] {", 8)) {
        *form = FORM_SCARLETNX;
        return (long)(i + 7);
    }
    if (i == 0 && n && s[0] == '{') {
        *form = FORM_REPORT;
        return 0;
    }
    emulated = find_in(s, n, "Debug.Emulated <");
    colon = emulated ? find_in(emulated, n - (size_t)(emulated - s), ": {") : NULL;
    if (!colon) return -1;
    *form = FORM_EDEN;
    return (long)(colon + 2 - s);
}
/* the line from at: its length without its \r\n, *next the next one's offset */
static size_t line_of(const char* text, size_t n, size_t at, size_t* next) {
    const char* nl = memchr(text + at, '\n', n - at);
    size_t len = nl ? (size_t)(nl - (text + at)) : n - at;
    *next = at + len + (nl != NULL);
    if (len && text[at + len - 1] == '\r') --len;
    return len;
}
/* the object at s, n bytes, when it is one of edenprobe's lines ({"type":...}); owned, NULL else */
static cJSON* report_line(const char* s, size_t n) {
    cJSON* o;
    if (n < 10 || memcmp(s, "{\"type\":\"", 9)) return NULL;
    o = cJSON_ParseWithLength(s, n);
    if (!cJSON_IsObject(o) || !jstr(o, "type")) {
        cJSON_Delete(o);
        return NULL;
    }
    return o;
}

/* the note that begins a run: "edenprobe 0.9.0 (v0.9.0) starting", its version and describe owned when wanted */
static int starting(const cJSON* o, char** version, char** describe) {
    const char *t = jstr(o, "text"), *open, *close;
    size_t n;
    if (!t || strcmp(jstr(o, "type"), "note") || strncmp(t, "edenprobe ", 10)) return 0;
    n = strlen(t);
    if (n < 21 || strcmp(t + n - 10, ") starting")) return 0;
    open = strstr(t + 10, " (");
    close = t + n - 10;
    if (!open || open > close || open == t + 10) return 0;
    if (version) *version = rei_format("%.*s", (int)(open - (t + 10)), t + 10);
    if (describe) *describe = rei_format("%.*s", (int)(close - (open + 2)), open + 2);
    return 1;
}

char* probe_cut(const char* text, size_t n, size_t* size, int* cases, int* notes) {
    Buf b = {0};
    size_t at = 0, next;
    *cases = *notes = 0;
    while (at < n) {
        size_t len = line_of(text, n, at, &next), whole = next - at;
        int form = 0;
        long j = json_at(text + at, len, &form);
        cJSON* o = j >= 0 ? report_line(text + at + j, len - (size_t)j) : NULL;
        int changed = 0;
        if (o && !strcmp(jstr(o, "type"), "case") && !probe_keeps_detail(jstr(o, "suite")) && jstr(o, "detail") &&
            *jstr(o, "detail")) {
            cJSON_ReplaceItemInObjectCaseSensitive(o, "detail", cJSON_CreateString(""));
            ++*cases;
            changed = 1;
        } else if (o && !strcmp(jstr(o, "type"), "note") && jstr(o, "text") && !strncmp(jstr(o, "text"), "net:", 4)) {
            cJSON_ReplaceItemInObjectCaseSensitive(
                o, "text",
                cJSON_CreateString("net: left out by Tracer, as the net family's notes name the player's sessions, "
                                   "accounts and servers"));
            ++*notes;
            changed = 1;
        }
        if (changed) {
            char* line = cJSON_PrintUnformatted(o);
            buf_addn(&b, text + at, (size_t)j);
            buf_add(&b, line ? line : "{}");
            buf_addn(&b, text + at + len, whole - len); /* its \r\n as it was */
            free(line);
        } else
            buf_addn(&b, text + at, whole);
        cJSON_Delete(o);
        at = next;
    }
    *size = b.len;
    return buf_take(&b);
}

int probe_marks(const char* text, size_t n) {
    size_t at = 0, next;
    int marks = 0;
    while (at < n) {
        size_t len = line_of(text, n, at, &next);
        int form = 0;
        long j = json_at(text + at, len, &form);
        if (j >= 0 && len - (size_t)j > 30) {
            const char* s = text + at + j;
            size_t k = len - (size_t)j;
            if (k > 35 && !memcmp(s, "{\"type\":\"note\",\"text\":\"edenprobe ", 33)) {
                cJSON* o = report_line(s, k);
                if (o && starting(o, NULL, NULL)) marks = 1;
                cJSON_Delete(o);
            } else if (marks && !memcmp(s, "{\"type\":\"summary\"", 17))
                marks = 2;
        }
        at = next;
    }
    return marks;
}

/* ---- a run ---- */

void probe_free(Probe* p) {
    int i;
    free(p->emulator);
    free(p->emulator_version);
    free(p->commit);
    free(p->version);
    free(p->describe);
    free(p->words);
    free(p->system);
    free(p->os);
    free(p->gpu);
    free(p->device);
    free(p->driver_name);
    free(p->driver_info);
    free(p->driver_text);
    free(p->vulkan);
    free(p->extensions);
    for (i = 0; i < p->ncases; ++i) {
        free(p->cases[i].suite);
        free(p->cases[i].name);
        free(p->cases[i].tier);
        free(p->cases[i].result);
        free(p->cases[i].reason);
        free(p->cases[i].detail);
    }
    free(p->cases);
    memset(p, 0, sizeof *p);
}

static void replace(char** field, const char* s, size_t n) {
    free(*field);
    while (n && isspace((unsigned char)s[n - 1])) --n;
    *field = rei_format("%.*s", (int)n, s);
}
static int starts(const char* s, size_t n, const char* lit) {
    size_t k = strlen(lit);
    return n >= k && !memcmp(s, lit, k);
}
static const char* find_in(const char* s, size_t n, const char* lit) {
    size_t k = strlen(lit), i;
    for (i = 0; i + k <= n; ++i)
        if (!memcmp(s + i, lit, k)) return s + i;
    return NULL;
}
/* a YYYY-MM-DD day as seconds since the epoch (UTC), 0 for text that is none */
static double day_of(const char* s, size_t n) {
    int y, m, d, k;
    if (n < 10) return 0;
    for (k = 0; k < 10; ++k)
        if ((k == 4 || k == 7) ? s[k] != '-' : !isdigit((unsigned char)s[k])) return 0;
    y = atoi(s);
    m = atoi(s + 5);
    d = atoi(s + 8);
    if (m < 1 || m > 12 || d < 1 || d > 31 || y < 1970) return 0;
    { /* days from the civil date (Howard Hinnant's), so no time zone or timegm is needed */
        long yy = y - (m <= 2), era = (yy >= 0 ? yy : yy - 399) / 400, yoe = yy - era * 400;
        long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return (double)(era * 146097 + doe - 719468) * 86400.0;
    }
}

/* a ScarletNX header line of the n bytes at s (its time stamp left out), into p */
static void scarletnx_line(Probe* p, const char* s, size_t n) {
    const char* v;
    if (starts(s, n, "system: ")) {
        replace(&p->system, s + 8, n - 8);
        return;
    }
    if (starts(s, n, "build: ScarletNX ")) { /* "build: ScarletNX <branch> <commit>[, committed <day>] (...)" */
        const char *b = s + 17, *sp = memchr(b, ' ', n - 17), *end, *at;
        if (!sp) return;
        end = find_in(sp + 1, n - (size_t)(sp + 1 - s), " (");
        if (!end) return;
        at = find_in(sp + 1, (size_t)(end - (sp + 1)), ", committed ");
        free(p->emulator_version);
        p->emulator_version = rei_format("%.*s %.*s", (int)(sp - b), b, (int)((at ? at : end) - (sp + 1)), sp + 1);
        replace(&p->commit, sp + 1, (size_t)((at ? at : end) - (sp + 1)));
        p->built = at ? day_of(at + 12, (size_t)(end - (at + 12))) : 0;
        return;
    }
    if (starts(s, n, "vulkan: the driver that runs: ")) { /* "<name>, <info> (driver ID <n>)[, device <v>:<d>]" */
        const char *b = s + 30, *comma = find_in(b, n - 30, ", "), *id = find_in(b, n - 30, " (driver ID "), *dev;
        if (!comma || !id || comma > id) return;
        replace(&p->driver_name, b, (size_t)(comma - b));
        replace(&p->driver_info, comma + 2, (size_t)(id - (comma + 2)));
        p->driver_id = atoi(id + 12);
        p->inferred = 0;
        dev = find_in(id, n - (size_t)(id - s), "), device ");
        if (dev) replace(&p->device, dev + 10, n - (size_t)(dev + 10 - s));
        return;
    }
    if (starts(s, n, "vulkan: optional extensions on: ")) { /* "<a>, <b>; banned here: ..." */
        const char *b = s + 32, *end = find_in(b, n - 32, "; ");
        replace(&p->extensions, b, end ? (size_t)(end - b) : n - 32);
        return;
    }
    if (starts(s, n, "vulkan: ") && (v = find_in(s, n, ", Vulkan ")) != NULL) { /* "<gpu>, Vulkan <v>, driver <hex>" */
        const char* d = find_in(v + 9, n - (size_t)(v + 9 - s), ", driver ");
        const char *hex, *h;
        if (!d) return;
        replace(&p->gpu, s + 8, (size_t)(v - (s + 8)));
        replace(&p->vulkan, v + 9, (size_t)(d - (v + 9)));
        hex = d + 9;
        for (h = hex; h < s + n && isxdigit((unsigned char)*h); ++h) continue;
        if (h > hex && h - hex <= 8) p->driver_version = strtoul(hex, NULL, 16);
    }
}
/* an Eden log line of the n bytes at s (its time stamp left out), "<Class> <Level> file:line:function: <message>",
   into p */
static void eden_line(Probe* p, const char* s, size_t n) {
    const char* msg = find_in(s, n, "> ");
    msg = msg ? find_in(msg, n - (size_t)(msg - s), ": ") : NULL;
    if (!msg) return;
    msg += 2;
    n -= (size_t)(msg - s);
    if (starts(msg, n, "Driver: ")) { /* driverName then its version, as Eden numbers it */
        const char* last = msg + n;
        while (last > msg + 8 && last[-1] != ' ') --last;
        if (last == msg + 8) return;
        replace(&p->driver_name, msg + 8, (size_t)(last - 1 - (msg + 8)));
        replace(&p->driver_info, last, (size_t)(msg + n - last));
        p->driver_id = probe_driver_of_name(p->driver_name);
        p->inferred = 1;
    } else if (starts(msg, n, "Device: "))
        replace(&p->gpu, msg + 8, n - 8);
    else if (starts(msg, n, "Vulkan: "))
        replace(&p->vulkan, msg + 8, n - 8);
    else if (starts(msg, n, "Eden Version: ")) {
        replace(&p->emulator_version, msg + 14, n - 14);
        if (find_in(msg + 14, n - 14, "rEden")) { /* rEden logs as Eden does, its build named in this line */
            free(p->emulator);
            p->emulator = rei_strdup("rEden");
        }
    } else if (starts(msg, n, "Host OS: ")) {
        char* was = p->system;
        p->system = rei_format("%.*s", (int)(n - 9), msg + 9);
        free(was);
    }
}
/* a machine line of the n bytes at s, of a log of the form, into p */
static void machine_line(Probe* p, int form, const char* s, size_t n) {
    size_t i = tr_stamp(s, n);
    if (form == FORM_EDEN)
        eden_line(p, s + i, n - i);
    else
        scarletnx_line(p, s + i, n - i);
}

/* the words of a mode note: "mode: quick, from the mode file; pictures of failures only; families cpu graphics; no
   timing passes; ..." as "cpu graphics notime" */
static char* words_of(const char* note) {
    Buf b = {0};
    const char *s = note, *fam = strstr(note, "; families ");
    int fatal = 0, hang = 0, notime = 0, retry = 0, stress = 0;
    long frames = 0;
    if (fam) {
        const char* end = strchr(fam + 11, ';');
        size_t n = end ? (size_t)(end - (fam + 11)) : strlen(fam + 11);
        if (n != 4 || memcmp(fam + 11, "none", 4)) buf_addn(&b, fam + 11, n);
    }
    while ((s = strstr(s, "; ")) != NULL) {
        s += 2;
        fatal |= !strncmp(s, "the cases that may crash", 24);
        hang |= !strncmp(s, "the cases that hang", 19);
        notime |= !strncmp(s, "no timing passes", 16);
        retry |= !strncmp(s, "the crash list's cases again", 28);
        stress |= !strncmp(s, "stress cases", 12);
        if (!strncmp(s, "frames ", 7)) frames = strtol(s + 7, NULL, 10);
    }
    if (fatal) buf_printf(&b, "%sfatal", b.len ? " " : "");
    if (frames > 0) buf_printf(&b, "%sframes=%ld", b.len ? " " : "", frames);
    if (hang) buf_printf(&b, "%shang", b.len ? " " : "");
    if (notime) buf_printf(&b, "%snotime", b.len ? " " : "");
    if (retry) buf_printf(&b, "%sretry", b.len ? " " : "");
    if (stress) buf_printf(&b, "%sstress", b.len ? " " : "");
    return buf_take(&b);
}

static char* cut_text(const char* s, size_t max) {
    size_t n = strlen(s);
    if (n > max) {
        n = max;
        while (n && ((unsigned char)s[n] & 0xc0) == 0x80) --n;
    }
    return rei_format("%.*s", (int)n, s);
}
/* a word of edenprobe's (a suite, a tier, a result, a reason): letters, digits, '-' and spaces, 1 to max bytes */
static int word(const char* s, size_t max) {
    size_t n = s ? strlen(s) : 0, i;
    if (!n || n > max) return 0;
    for (i = 0; i < n; ++i)
        if (!isalnum((unsigned char)s[i]) && s[i] != '-' && s[i] != ' ' && s[i] != '_') return 0;
    return 1;
}
/* case o into p, in place of one of the same suite and name: 0, or -1 for a case line that is not one */
static int add_case(Probe* p, const cJSON* o, int* cap) {
    const char *suite = jstr(o, "suite"), *name = jstr(o, "name"), *tier = jstr(o, "tier"), *result = jstr(o, "result"),
               *reason = jstr(o, "reason"), *detail = jstr(o, "detail");
    const cJSON* us = cJSON_GetObjectItemCaseSensitive(o, "us");
    ProbeCase* c;
    int i;
    if (!word(suite, 32) || !name || !*name || strlen(name) > 512 || (tier && !word(tier, 16)) || !result ||
        (strcmp(result, "pass") && strcmp(result, "fail") && strcmp(result, "skip")) ||
        (reason && *reason && !word(reason, 32)))
        return -1;
    for (i = 0; i < p->ncases; ++i) /* a line again (two copies of one run): the later one */
        if (!strcmp(p->cases[i].suite, suite) && !strcmp(p->cases[i].name, name)) break;
    if (i == p->ncases) {
        if (p->ncases == PROBE_CASES) {
            ++p->over;
            return 0;
        }
        if (p->ncases == *cap) {
            *cap = *cap ? *cap * 2 : 256;
            p->cases = realloc(p->cases, (size_t)*cap * sizeof *p->cases);
            if (!p->cases) {
                fputs(REI_NAME ": out of memory\n", stderr);
                abort();
            }
        }
        memset(&p->cases[p->ncases++], 0, sizeof *p->cases);
    } else {
        free(p->cases[i].suite);
        free(p->cases[i].name);
        free(p->cases[i].tier);
        free(p->cases[i].result);
        free(p->cases[i].reason);
        free(p->cases[i].detail);
    }
    c = &p->cases[i];
    c->suite = rei_strdup(suite);
    c->name = rei_strdup(name);
    c->tier = rei_strdup(tier ? tier : "none");
    c->result = rei_strdup(result);
    c->reason = rei_strdup(reason ? reason : "");
    c->detail = cut_text(detail ? detail : "", PROBE_DETAIL);
    c->us = cJSON_IsNumber(us) && us->valuedouble >= 0 ? us->valuedouble : -1;
    return 0;
}

int probe_parse(const char* text, size_t n, Probe* p, char** why) {
    size_t at = 0, next, start = 0, end = n, run_start = 0, run_end = n;
    int runs = 0, chosen_whole = 0, lines = 0, resumed = 0, cap = 0, open_run = 0, open_whole = 0, open_form = 0,
        chosen_form = 0;
    *why = NULL;
    memset(p, 0, sizeof *p);
    /* the runs: each from its starting note to the next one; the last whole one, else the last one */
    while (at < n) {
        size_t len = line_of(text, n, at, &next);
        int form = 0;
        long j = json_at(text + at, len, &form);
        cJSON* o = j >= 0 ? report_line(text + at + j, len - (size_t)j) : NULL;
        if (o) {
            const char* type = jstr(o, "type");
            ++lines;
            if (starting(o, NULL, NULL)) {
                if (open_run && (open_whole || !chosen_whole)) {
                    run_start = start;
                    run_end = at;
                    chosen_whole = open_whole;
                    chosen_form = open_form;
                }
                open_run = 1;
                open_whole = 0;
                open_form = form;
                start = at;
                ++runs;
            } else if (!strcmp(type, "summary"))
                open_whole = open_run;
            else if (!open_run && !strcmp(type, "note") && jstr(o, "text") &&
                     !strncmp(jstr(o, "text"), "journal: launch ", 16))
                resumed = 1;
        }
        cJSON_Delete(o);
        at = next;
    }
    if (open_run && (open_whole || !chosen_whole)) {
        run_start = start;
        run_end = n;
        chosen_whole = open_whole;
        chosen_form = open_form;
    }
    if (!runs) {
        if (lines && resumed) {
            *why = rei_strdup("its edenprobe lines are a launch that went on with a run after a crash (its first note "
                              "says \"journal: launch\"), without the run's start: send the run's report.jsonl, or the "
                              "log of the run's first launch");
            return -1;
        }
        return 0;
    }
    start = run_start;
    end = run_end;
    p->passed = p->failed = p->skipped = p->crashed = -1;
    p->whole = chosen_whole;
    /* the emulator by the form its run's lines come in: a report.jsonl alone names no machine, the console's
       reference */
    p->emulator = rei_strdup(chosen_form == FORM_SCARLETNX ? "ScarletNX"
                             : chosen_form == FORM_EDEN    ? "Eden"
                                                           : "console");
    /* the machine: the lines before the run's start, the last of each; a report.jsonl's has none */
    for (at = 0; chosen_form != FORM_REPORT && at < start; at = next) {
        size_t len = line_of(text, n, at, &next);
        if (len) machine_line(p, chosen_form, text + at, len);
    }
    p->os = rei_strdup(probe_os(p->system));
    if (p->driver_id && !p->driver_version && p->driver_info && strcmp(p->emulator, "ScarletNX"))
        probe_version_parse(p->driver_id, p->driver_info, &p->driver_version); /* Eden's, as its vendor numbers it */
    if (p->driver_id && p->driver_version) { /* ScarletNX logs driverVersion as it is, in hex */
        char v[32];
        probe_version_text(p->driver_id, p->driver_version, v, sizeof v);
        p->driver_text = rei_strdup(v);
    }
    /* the run's lines */
    for (at = start; at < end; at = next) {
        size_t len = line_of(text, n, at, &next);
        int form = 0;
        long j = json_at(text + at, len, &form);
        cJSON* o = j >= 0 ? report_line(text + at + j, len - (size_t)j) : NULL;
        const char* type = o ? jstr(o, "type") : NULL;
        if (!o) continue;
        if (starting(o, &p->version, &p->describe))
            ;
        else if (!strcmp(type, "case")) {
            if (add_case(p, o, &cap)) ++p->unread;
        } else if (!strcmp(type, "summary")) {
            p->passed = (long)jnum(o, "passed");
            p->failed = (long)jnum(o, "failed");
            p->skipped = (long)jnum(o, "skipped");
            p->crashed = (long)jnum(o, "crashed");
        } else if (!strcmp(type, "note")) {
            const char* t = jstr(o, "text");
            if (t && !strncmp(t, "mode: ", 6) && !p->words) p->words = words_of(t);
        } else if (strcmp(type, "perf") && strcmp(type, "baseline") && strcmp(type, "baseline_summary"))
            ++p->unread;
        cJSON_Delete(o);
    }
    if (!p->words) p->words = rei_strdup("");
    return 1;
}

/* ---- as JSON ---- */

static void put_number(cJSON* o, const char* key, double v) { cJSON_AddNumberToObject(o, key, v); }

cJSON* probe_json(const Probe* p) {
    cJSON *o = cJSON_CreateObject(), *list;
    int i;
    jset(o, "emulator", p->emulator);
    jset(o, "emulator_version", p->emulator_version);
    jset(o, "commit", p->commit);
    put_number(o, "built", p->built);
    jset(o, "version", p->version);
    jset(o, "describe", p->describe);
    jset(o, "words", p->words);
    jset(o, "system", p->system);
    jset(o, "os", p->os);
    jset(o, "gpu", p->gpu);
    jset(o, "device", p->device);
    jset(o, "driver_name", p->driver_name);
    jset(o, "driver_info", p->driver_info);
    put_number(o, "driver_id", p->driver_id);
    cJSON_AddBoolToObject(o, "inferred", p->inferred);
    put_number(o, "driver_version", (double)p->driver_version);
    jset(o, "driver_text", p->driver_text);
    jset(o, "vulkan", p->vulkan);
    jset(o, "extensions", p->extensions);
    put_number(o, "passed", (double)p->passed);
    put_number(o, "failed", (double)p->failed);
    put_number(o, "skipped", (double)p->skipped);
    put_number(o, "crashed", (double)p->crashed);
    cJSON_AddBoolToObject(o, "whole", p->whole);
    put_number(o, "cut_cases", p->cut_cases);
    put_number(o, "cut_notes", p->cut_notes);
    put_number(o, "unread", p->unread);
    put_number(o, "over", p->over);
    list = cJSON_AddArrayToObject(o, "cases");
    for (i = 0; i < p->ncases; ++i) {
        const ProbeCase* c = &p->cases[i];
        cJSON* e = cJSON_CreateObject();
        jset(e, "suite", c->suite);
        jset(e, "name", c->name);
        jset(e, "tier", c->tier);
        jset(e, "result", c->result);
        if (*c->reason) jset(e, "reason", c->reason);
        if (*c->detail) jset(e, "detail", c->detail);
        if (c->us >= 0) put_number(e, "us", c->us);
        cJSON_AddItemToArray(list, e);
    }
    return o;
}

/* key's string of o, cut to max bytes, owned; NULL when o has none */
static char* field(const cJSON* o, const char* key, size_t max) {
    const char* s = jstr(o, key);
    return s ? cut_text(s, max) : NULL;
}
static long count_of(const cJSON* o, const char* key) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, key);
    double d = cJSON_IsNumber(v) ? v->valuedouble : -1;
    return d < 0 || d > 1e7 ? -1 : (long)d;
}

int probe_of_json(const cJSON* o, Probe* p, char** why) {
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(o, "cases"), *e;
    const char* emulator = jstr(o, "emulator");
    int cap = 0;
    double id = jnum(o, "driver_id"), version = jnum(o, "driver_version");
    *why = NULL;
    memset(p, 0, sizeof *p);
    if (!cJSON_IsObject(o) || !emulator ||
        (strcmp(emulator, "ScarletNX") && strcmp(emulator, "Eden") && strcmp(emulator, "rEden") &&
         strcmp(emulator, "console"))) {
        *why = rei_format("its probe names no emulator it may (%.40s)", emulator ? emulator : "none");
        return -1;
    }
    if (!jstr(o, "version") || !*jstr(o, "version") || strlen(jstr(o, "version")) > 40 || !jstr(o, "words") ||
        !cJSON_IsArray(list) || id < 0 || id >= DRIVERS || version < 0 || version > 4294967295.0) {
        *why = rei_strdup("its probe has no edenprobe version, mode or cases, or a driver that is none");
        return -1;
    }
    p->emulator = rei_strdup(emulator);
    p->emulator_version = field(o, "emulator_version", 200);
    p->commit = field(o, "commit", 64);
    p->built = jnum(o, "built") > 0 ? jnum(o, "built") : 0;
    p->version = field(o, "version", 40);
    p->describe = field(o, "describe", 80);
    p->words = field(o, "words", 200);
    p->system = field(o, "system", 300);
    p->os = rei_strdup(probe_os(p->system)); /* drawn again here, not taken from the answer */
    p->gpu = field(o, "gpu", 200);
    p->device = field(o, "device", 20);
    p->driver_name = field(o, "driver_name", 120);
    p->driver_info = field(o, "driver_info", 200);
    p->driver_id = (int)id;
    p->inferred = jbool(o, "inferred");
    p->driver_version = (unsigned long)version;
    if (p->driver_id && p->driver_version) { /* drawn again too */
        char text[32];
        probe_version_text(p->driver_id, p->driver_version, text, sizeof text);
        p->driver_text = rei_strdup(text);
    }
    p->vulkan = field(o, "vulkan", 40);
    p->extensions = field(o, "extensions", 4000);
    p->passed = count_of(o, "passed");
    p->failed = count_of(o, "failed");
    p->skipped = count_of(o, "skipped");
    p->crashed = count_of(o, "crashed");
    p->whole = jbool(o, "whole");
    p->cut_cases = (int)(count_of(o, "cut_cases") > 0 ? count_of(o, "cut_cases") : 0);
    p->cut_notes = (int)(count_of(o, "cut_notes") > 0 ? count_of(o, "cut_notes") : 0);
    p->unread = (int)(count_of(o, "unread") > 0 ? count_of(o, "unread") : 0);
    p->over = (int)(count_of(o, "over") > 0 ? count_of(o, "over") : 0);
    cJSON_ArrayForEach(e, list) {
        if (add_case(p, e, &cap)) {
            *why = rei_format("its probe's case %d is not one edenprobe writes", p->ncases + 1);
            probe_free(p);
            return -1;
        }
        if (!probe_keeps_detail(p->cases[p->ncases - 1].suite) && *p->cases[p->ncases - 1].detail) {
            *why = rei_format("its probe's case %d keeps a detail of a family that loses it (%s)", p->ncases,
                              p->cases[p->ncases - 1].suite);
            probe_free(p);
            return -1;
        }
    }
    return 0;
}

/* ---- marks and words ---- */

typedef struct {
    unsigned long long h;
} Fnv;
static void fnv(Fnv* f, const char* s) {
    for (; s && *s; ++s) f->h = (f->h ^ (unsigned char)*s) * 1099511628211ULL;
    f->h = (f->h ^ 0x1f) * 1099511628211ULL; /* a separator, so "ab"+"c" is not "a"+"bc" */
}

void probe_digest(const Probe* p, char out[17]) {
    Fnv f = {14695981039346656037ULL};
    char num[32];
    int i;
    const char* fields[] = {p->emulator, p->emulator_version, p->version,    p->words, p->system,
                            p->gpu,      p->driver_name,      p->driver_info};
    size_t k;
    for (k = 0; k < sizeof fields / sizeof *fields; ++k) fnv(&f, fields[k]);
    snprintf(num, sizeof num, "%d %lu %ld %ld %ld %ld", p->driver_id, p->driver_version, p->passed, p->failed,
             p->skipped, p->crashed);
    fnv(&f, num);
    for (i = 0; i < p->ncases; ++i) {
        const ProbeCase* c = &p->cases[i];
        fnv(&f, c->suite);
        fnv(&f, c->name);
        fnv(&f, c->result);
        fnv(&f, c->reason);
        fnv(&f, c->detail);
        snprintf(num, sizeof num, "%.0f", c->us);
        fnv(&f, num);
    }
    snprintf(out, 17, "%016llx", f.h);
}

void probe_case_key(const char* suite, const char* name, char out[17]) {
    Fnv f = {14695981039346656037ULL};
    fnv(&f, suite);
    fnv(&f, name);
    snprintf(out, 17, "%016llx", f.h);
}

char* probe_counts(const Probe* p) {
    long passed = 0, failed = 0, skipped = 0, crashed = 0;
    Buf b = {0};
    int i, k, nsuites = 0, shown;
    struct {
        const char* suite;
        int fails;
    } suites[64];
    int more = 0;
    for (i = 0; i < p->ncases; ++i) {
        const ProbeCase* c = &p->cases[i];
        if (!strcmp(c->result, "pass"))
            ++passed;
        else if (!strcmp(c->result, "fail")) {
            ++failed;
            for (k = 0; k < nsuites && strcmp(suites[k].suite, c->suite); ++k) continue;
            if (k == nsuites && nsuites < 64) {
                suites[nsuites].suite = c->suite;
                suites[nsuites++].fails = 0;
            } else if (k == nsuites)
                continue;
            ++suites[k].fails;
        } else if (!strcmp(c->reason, "crashed"))
            ++crashed;
        else
            ++skipped;
    }
    if (p->passed >= 0) { /* the summary's own counts, when it has them */
        passed = p->passed;
        failed = p->failed;
        skipped = p->skipped;
        crashed = p->crashed;
    }
    for (i = 1; i < nsuites; ++i) /* the most failures first, then by name */
        for (k = i;
             k > 0 && (suites[k].fails > suites[k - 1].fails ||
                       (suites[k].fails == suites[k - 1].fails && strcmp(suites[k].suite, suites[k - 1].suite) < 0));
             --k) {
            const char* s = suites[k].suite;
            int f = suites[k].fails;
            suites[k] = suites[k - 1];
            suites[k - 1].suite = s;
            suites[k - 1].fails = f;
        }
    buf_printf(&b, "%ld passed, %ld failed", passed, failed);
    shown = nsuites < 8 ? nsuites : 8;
    for (i = 0; i < shown; ++i) buf_printf(&b, "%s%s %d", i ? ", " : " (", suites[i].suite, suites[i].fails);
    more = nsuites - shown;
    if (more) buf_printf(&b, " and %d more suite%s", more, more == 1 ? "" : "s");
    if (shown) buf_add(&b, ")");
    buf_printf(&b, ", %ld skipped, %ld crashed", skipped, crashed);
    return buf_take(&b);
}

static const char* os_label(const char* os) {
    static const char* const names[][2] = {{"android", "Android"}, {"windows", "Windows"}, {"macos", "macOS"},
                                           {"ios", "iOS"},         {"freebsd", "FreeBSD"}, {"openbsd", "OpenBSD"},
                                           {"netbsd", "NetBSD"},   {"linux", "Linux"}};
    size_t i;
    for (i = 0; os && i < sizeof names / sizeof *names; ++i)
        if (!strcmp(names[i][0], os)) return names[i][1];
    return "system unknown";
}
char* probe_machine(const Probe* p) {
    Buf b = {0};
    if (!strcmp(p->emulator, "console") && !p->driver_name)
        return rei_strdup("the console (a report.jsonl alone names no machine)");
    if (p->driver_name) {
        buf_add(&b, p->driver_name);
        if (p->driver_info) buf_printf(&b, "%s%s", strcmp(p->emulator, "ScarletNX") ? " " : ", ", p->driver_info);
    } else
        buf_add(&b, "a driver its log does not name");
    if (p->inferred) buf_add(&b, p->driver_id ? ", driver ID inferred" : ", driver ID unknown");
    buf_printf(&b, " (%s)", os_label(p->os));
    return buf_take(&b);
}
