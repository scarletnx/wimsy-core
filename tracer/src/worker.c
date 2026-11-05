/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#if defined(                                                                                                           \
    __FreeBSD__) /* procctl's and Capsicum's types (u_int) and closefrom are BSD's: _POSIX_C_SOURCE hides them */
#undef _POSIX_C_SOURCE
#elif defined(__linux__) /* closefrom (glibc 2.34) and syscall, glibc's own */
#define _DEFAULT_SOURCE
#endif
#include "worker.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#if defined(__linux__)
#include <linux/landlock.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#elif defined(__FreeBSD__)
#include <sys/capsicum.h>
#include <sys/procctl.h>
#endif

#define QUESTION_MAX  (1 << 20) /* the bytes of a question at most */
#define NAME_MAX_KEPT 200       /* the bytes of a file's name kept from an answer */
#define SKIPPED_MAX   4000      /* the bytes of the lines of the files not taken */

char** wk_env(char* const* environ_now) {
    size_t n = 0, kept = 0, i;
    char** env;
    while (environ_now && environ_now[n]) ++n;
    env = malloc((n + 1) * sizeof *env);
    if (!env) {
        fputs(REI_NAME ": out of memory\n", stderr);
        abort();
    }
    for (i = 0; i < n; ++i)
        if (strncmp(environ_now[i], "TRACER_", 7)) env[kept++] = environ_now[i];
    env[kept] = NULL;
    return env;
}

/* ---- the worker's side ---- */

void wk_limit(void) {
    struct rlimit r;
    long fd, top;
    r.rlim_cur = r.rlim_max = WK_CPU;
    setrlimit(RLIMIT_CPU, &r);
    r.rlim_cur = r.rlim_max = (rlim_t)TR_LOG_MAX * 2; /* the largest file it writes: a log or a video, 100 MiB */
    setrlimit(RLIMIT_FSIZE, &r);
    r.rlim_cur = r.rlim_max = 0; /* a crash leaves no core, which would hold what strangers sent */
    setrlimit(RLIMIT_CORE, &r);
#if defined(__SANITIZE_ADDRESS__)
#define WK_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define WK_ASAN 1
#endif
#endif
#ifndef WK_ASAN
    r.rlim_cur = r.rlim_max = WK_MEMORY;
    setrlimit(RLIMIT_AS, &r);
#endif
    /* the bot's open files (its gateway, its database) are not the worker's: all but the standard three closed, at
       once where the system can, as one close a descriptor the limit allows takes 115 ms at devola's 463221 */
#if defined(__FreeBSD__) || (defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34)))
    (void)fd;
    (void)top;
    closefrom(3);
#else
    top = getrlimit(RLIMIT_NOFILE, &r) || r.rlim_cur == RLIM_INFINITY || r.rlim_cur > (1 << 20) ? (1 << 20)
                                                                                                : (long)r.rlim_cur;
    for (fd = 3; fd < top; ++fd) close((int)fd);
#endif
}

int wk_untraceable(char** why) {
    *why = NULL;
#if defined(__linux__) /* not dumpable: /proc/<pid>/environ and mem become root's, and ptrace refuses its user */
    if (!prctl(PR_SET_DUMPABLE, 0, 0, 0, 0)) return 0;
    *why = rei_format("prctl(PR_SET_DUMPABLE, 0): %s", strerror(errno));
#elif defined(__FreeBSD__) /* ptrace, the debugging sysctls (kern.proc.env among them) and core dumps refused */
    {
        int off = PROC_TRACE_CTL_DISABLE;
        if (!procctl(P_PID, getpid(), PROC_TRACE_CTL, &off)) return 0;
        *why = rei_format("procctl(PROC_TRACE_CTL, PROC_TRACE_CTL_DISABLE): %s", strerror(errno));
    }
#else
    *why = rei_strdup("this system has no call, known here, that keeps a process from debuggers of its own user");
#endif
    return -1;
}

#if defined(__linux__)
/* the file system rights of Landlock's ABI abi, all of which the worker gives up but in its folder */
static unsigned long long fs_rights(int abi) {
    unsigned long long all = (LANDLOCK_ACCESS_FS_MAKE_SYM << 1) - 1; /* ABI 1: execute to make_sym */
#ifdef LANDLOCK_ACCESS_FS_REFER
    if (abi >= 2) all |= LANDLOCK_ACCESS_FS_REFER;
#endif
#ifdef LANDLOCK_ACCESS_FS_TRUNCATE
    if (abi >= 3) all |= LANDLOCK_ACCESS_FS_TRUNCATE;
#endif
#ifdef LANDLOCK_ACCESS_FS_IOCTL_DEV
    if (abi >= 5) all |= LANDLOCK_ACCESS_FS_IOCTL_DEV;
#endif
    return all;
}
#endif

const char* wk_shut_in(int dirfd, char** why) {
    *why = NULL;
#if defined(__FreeBSD__)
    { /* the folder: its files looked up, made, read, written and removed, nothing above it; then nothing else opened */
        cap_rights_t rights;
        cap_rights_init(&rights, CAP_LOOKUP, CAP_CREATE, CAP_READ, CAP_WRITE, CAP_SEEK, CAP_FSTAT, CAP_UNLINKAT);
        if (cap_rights_limit(dirfd, &rights)) {
            *why = rei_format("cap_rights_limit on its folder: %s", strerror(errno));
            return NULL;
        }
        if (cap_enter()) {
            *why = rei_format("cap_enter: %s", strerror(errno));
            return NULL;
        }
        return "capsicum";
    }
#elif defined(__linux__)
    {
        static char said[32];
        struct landlock_ruleset_attr attr;
        struct landlock_path_beneath_attr folder;
        int abi = (int)syscall(SYS_landlock_create_ruleset, NULL, 0, LANDLOCK_CREATE_RULESET_VERSION), rules;
        size_t size = sizeof attr;
        if (abi < 1) {
            *why = rei_format("Landlock is not in this kernel (%s), and a worker reads nothing without it",
                              strerror(errno));
            return NULL;
        }
        memset(&attr, 0, sizeof attr);
        attr.handled_access_fs = fs_rights(abi);
#ifdef LANDLOCK_ACCESS_NET_CONNECT_TCP
        if (abi >= 4) attr.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP | LANDLOCK_ACCESS_NET_CONNECT_TCP;
#endif
#ifdef LANDLOCK_SCOPE_SIGNAL
        if (abi >= 6) attr.scoped = LANDLOCK_SCOPE_SIGNAL | LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET;
#endif
        rules = (int)syscall(SYS_landlock_create_ruleset, &attr, size, 0);
        if (rules < 0) {
            *why = rei_format("landlock_create_ruleset (ABI %d): %s", abi, strerror(errno));
            return NULL;
        }
        memset(&folder, 0, sizeof folder);
        folder.parent_fd = dirfd;
        folder.allowed_access = LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_WRITE_FILE |
                                LANDLOCK_ACCESS_FS_MAKE_REG | LANDLOCK_ACCESS_FS_REMOVE_FILE;
        if (syscall(SYS_landlock_add_rule, rules, LANDLOCK_RULE_PATH_BENEATH, &folder, 0) ||
            prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) || syscall(SYS_landlock_restrict_self, rules, 0)) {
            *why = rei_format("Landlock (ABI %d) on its folder: %s", abi, strerror(errno));
            close(rules);
            return NULL;
        }
        close(rules);
        snprintf(said, sizeof said, "landlock %d", abi);
        return said;
    }
#else
    (void)dirfd;
    *why = rei_strdup("this system has no way, known here, to shut a worker in its folder: it reads nothing");
    return NULL;
#endif
}

int wk_serve(FILE* in, FILE* out, TrGet get, void* ctx) {
    Buf text = {0};
    char piece[8192];
    size_t got;
    cJSON *q, *a, *list, *f;
    const cJSON* each;
    const char* dir;
    Tracer t;
    Taken k;
    TrMessage m;
    char *printed, *why[TR_FILES], *failed;
    const char* shut;
    int i, dirfd;
    while ((got = fread(piece, 1, sizeof piece, in)) && text.len <= QUESTION_MAX) buf_addn(&text, piece, got);
    q = text.len <= QUESTION_MAX ? cJSON_ParseWithLength(text.text ? text.text : "", text.len) : NULL;
    buf_free(&text);
    dir = jstr(q, "dir");
    if (!dir || dir[0] != '/' || !cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(q, "files"))) {
        rei_log("the worker's question is no question: %s", q ? "it names no folder or no files" : "no JSON");
        cJSON_Delete(q);
        return 1;
    }
    memset(&t, 0, sizeof t);
    memset(&k, 0, sizeof k);
    memset(&m, 0, sizeof m);
    t.get = get;
    t.ctx = ctx;
    k.dir = rei_strdup(dir);
    k.base = (int)jnum(q, "base");
    k.logs_had = (int)jnum(q, "logs_had");
    k.images_had = (int)jnum(q, "images_had");
    k.videos_had = (int)jnum(q, "videos_had");
    cJSON_ArrayForEach(each, cJSON_GetObjectItemCaseSensitive(q, "files")) {
        TrAttachment* at = &m.files[m.nfiles];
        if (m.nfiles == TR_FILES) break;
        if (!jstr(each, "name") || !jstr(each, "url")) continue;
        at->name = rei_strdup(jstr(each, "name"));
        at->url = rei_strdup(jstr(each, "url"));
        at->type = rei_strdup(jstr(each, "type") ? jstr(each, "type") : "");
        at->size = jnum(each, "size");
        ++m.nfiles;
    }
    m.content = rei_strdup("");
    cJSON_Delete(q);
    if ((dirfd = open(k.dir, O_RDONLY | O_DIRECTORY)) < 0) {
        rei_log("the worker's folder %s could not be opened: %s", k.dir, strerror(errno));
        tr_taken_free(&k);
        tr_message_free(&m);
        return 1;
    }
    /* the downloads first, the network and the CA files still open to it; then it is shut in with what came */
    tr_download(&t, &k, &m, dirfd, why);
    if (!(shut = wk_shut_in(dirfd, &failed))) {
        rei_log("the worker could not shut itself in its folder: %s", failed);
        for (i = 0; i < m.nfiles; ++i) {
            char name[16];
            snprintf(name, sizeof name, "in.%d", i);
            if (!why[i]) unlinkat(dirfd, name, 0);
            free(why[i]);
        }
        free(failed);
        close(dirfd);
        tr_taken_free(&k);
        tr_message_free(&m);
        return 1;
    }
    tr_read(&k, &m, dirfd, why);
    close(dirfd);
    a = cJSON_CreateObject();
    jset(a, "shut_in", shut);
    cJSON_AddNumberToObject(a, "logs", k.logs);
    cJSON_AddNumberToObject(a, "images", k.images);
    cJSON_AddNumberToObject(a, "videos", k.videos);
    jset(a, "skipped", k.skipped.text ? k.skipped.text : "");
    if (k.first) cJSON_AddItemToObject(a, "first", cJSON_Duplicate(k.first, 1));
    list = cJSON_AddArrayToObject(a, "files");
    for (i = 0; i < k.n; ++i) {
        f = cJSON_CreateObject();
        cJSON_AddNumberToObject(f, "n", k.files[i].n);
        jset(f, "kind", k.files[i].kind);
        jset(f, "name", k.files[i].name);
        jset(f, "path", k.files[i].path);
        jset(f, "summary", k.files[i].summary);
        jset(f, "taken", k.taken[i]);
        cJSON_AddItemToArray(list, f);
        free(k.taken[i]); /* the bot's to move or remove now */
        k.taken[i] = NULL;
    }
    free(k.dir);
    k.dir = NULL;
    printed = cJSON_PrintUnformatted(a);
    cJSON_Delete(a);
    if (printed) fputs(printed, out);
    fflush(out);
    free(printed);
    tr_taken_free(&k);
    tr_message_free(&m);
    return 0;
}

/* ---- the bot's side ---- */

/* the files a worker may have left in dir, by their names (dir/0 to dir/12), removed */
static void cleaned(const char* dir) {
    int i;
    for (i = 0; i < TR_LOGS + TR_IMAGES + TR_VIDEOS; ++i) {
        char* path = rei_format("%s/%d", dir, i);
        unlink(path);
        free(path);
    }
    for (i = 0; i < TR_FILES; ++i) { /* the downloads a worker that stopped did not read */
        char* path = rei_format("%s/in.%d", dir, i);
        unlink(path);
        free(path);
    }
}
static char* question(const TrMessage* m, const Taken* k) {
    cJSON *q = cJSON_CreateObject(), *list;
    char* text;
    int i;
    jset(q, "dir", k->dir);
    cJSON_AddNumberToObject(q, "base", k->base);
    cJSON_AddNumberToObject(q, "logs_had", k->logs_had);
    cJSON_AddNumberToObject(q, "images_had", k->images_had);
    cJSON_AddNumberToObject(q, "videos_had", k->videos_had);
    list = cJSON_AddArrayToObject(q, "files");
    for (i = 0; i < m->nfiles; ++i) {
        cJSON* f = cJSON_CreateObject();
        jset(f, "name", m->files[i].name);
        jset(f, "url", m->files[i].url);
        jset(f, "type", m->files[i].type);
        cJSON_AddNumberToObject(f, "size", m->files[i].size);
        cJSON_AddItemToArray(list, f);
    }
    text = cJSON_PrintUnformatted(q);
    cJSON_Delete(q);
    return text;
}
static int extension_of(const char* kind, const char* ext) {
    if (!strcmp(kind, "log")) return !strcmp(ext, "log");
    if (!strcmp(kind, "video")) return !strcmp(ext, "mp4") || !strcmp(ext, "webm");
    return !strcmp(ext, "png") || !strcmp(ext, "jpg") || !strcmp(ext, "gif") || !strcmp(ext, "webp");
}
/* the answer read into k, each of its claims checked, as the worker reads what strangers send: 0, or -1 with why */
static int answered(const char* text, size_t n, Taken* k, char** why) {
    cJSON* a = cJSON_ParseWithLength(text, n);
    const cJSON *each, *first = cJSON_GetObjectItemCaseSensitive(a, "first");
    const char* skipped = jstr(a, "skipped");
    int logs = 0, images = 0, videos = 0;
    if (!cJSON_IsObject(a)) {
        *why = rei_strdup("its answer is no JSON object");
        cJSON_Delete(a);
        return -1;
    }
    if (!jstr(a, "shut_in") || !*jstr(a, "shut_in")) { /* it read what strangers sent with the site in its reach */
        *why = rei_strdup("its answer does not say it shut itself in its folder before it read the files");
        cJSON_Delete(a);
        return -1;
    }
    cJSON_ArrayForEach(each, cJSON_GetObjectItemCaseSensitive(a, "files")) {
        const char *kind = jstr(each, "kind"), *name = jstr(each, "name"), *path = jstr(each, "path"),
                   *taken = jstr(each, "taken"), *summary = jstr(each, "summary"), *dot;
        char *want, *named;
        struct stat st;
        int number = k->base + k->n + 1, ok;
        cJSON* parsed = NULL;
        if (k->n == TR_LOGS + TR_IMAGES + TR_VIDEOS) {
            *why = rei_format("it answered more than %d files", TR_LOGS + TR_IMAGES + TR_VIDEOS);
            break;
        }
        want = rei_format("%s/%d", k->dir, k->n);
        named = rei_format("%d.", number);
        dot = path ? strchr(path, '.') : NULL;
        ok = kind && (!strcmp(kind, "log") || !strcmp(kind, "image") || !strcmp(kind, "video")) && name && path &&
             taken && dot && !strncmp(path, named, strlen(named)) && extension_of(kind, dot + 1) &&
             !strcmp(taken, want) && !lstat(want, &st) && S_ISREG(st.st_mode) &&
             (strcmp(kind, "log") || (summary && cJSON_IsObject(parsed = cJSON_Parse(summary))));
        cJSON_Delete(parsed);
        free(named);
        if (!ok) {
            *why = rei_format("its file %d is not one it may answer (%s %s, at %s)", k->n, kind ? kind : "no kind",
                              path ? path : "no name", taken ? taken : "no place");
            free(want);
            break;
        }
        logs += !strcmp(kind, "log");
        images += !strcmp(kind, "image");
        videos += !strcmp(kind, "video");
        if (k->logs_had + logs > TR_LOGS || k->images_had + images > TR_IMAGES || k->videos_had + videos > TR_VIDEOS) {
            *why = rei_format("it answered %d logs, %d screenshots and %d videos for a report that had %d, %d and %d",
                              logs, images, videos, k->logs_had, k->images_had, k->videos_had);
            free(want);
            break;
        }
        k->files[k->n].n = number;
        strcpy(k->files[k->n].kind, kind);
        k->files[k->n].name = rei_format("%.*s", NAME_MAX_KEPT, name);
        k->files[k->n].path = rei_strdup(path);
        k->files[k->n].bytes = (double)st.st_size;
        k->files[k->n].summary = !strcmp(kind, "log") ? rei_strdup(summary) : NULL;
        k->taken[k->n++] = want;
    }
    if (!*why) {
        k->logs = logs;
        k->images = images;
        k->videos = videos;
        if (cJSON_IsObject(first)) k->first = cJSON_Duplicate(first, 1);
        if (skipped && *skipped) buf_printf(&k->skipped, "%.*s", SKIPPED_MAX, skipped);
        if (logs && !k->first) *why = rei_strdup("it answered logs and no first log's summary");
    }
    cJSON_Delete(a);
    return *why ? -1 : 0;
}

int wk_take(char* const argv[], int seconds, const TrMessage* m, Taken* k, char** why) {
    extern char** environ;
    int in[2], out[2], status = 0, result = -1;
    char *ask = question(m, k), **env = wk_env(environ);
    size_t sent = 0, len = strlen(ask);
    posix_spawn_file_actions_t acts;
    pid_t pid;
    Buf answer = {0};
    struct timespec now;
    double deadline;
    *why = NULL;
    if (pipe(in)) {
        *why = rei_format("no pipe for the worker: %s", strerror(errno));
        free(ask);
        free(env);
        return -1;
    }
    if (pipe(out)) {
        *why = rei_format("no pipe for the worker: %s", strerror(errno));
        close(in[0]);
        close(in[1]);
        free(ask);
        free(env);
        return -1;
    }
    posix_spawn_file_actions_init(&acts);
    posix_spawn_file_actions_adddup2(&acts, in[0], 0);
    posix_spawn_file_actions_adddup2(&acts, out[1], 1);
    posix_spawn_file_actions_addclose(&acts, in[1]);
    posix_spawn_file_actions_addclose(&acts, out[0]);
    errno = posix_spawn(&pid, argv[0], &acts, NULL, argv, env);
    posix_spawn_file_actions_destroy(&acts);
    free(env);
    close(in[0]);
    close(out[1]);
    if (errno) {
        *why = rei_format("the worker %s did not start: %s", argv[0], strerror(errno));
        close(in[1]);
        close(out[0]);
        free(ask);
        return -1;
    }
    { /* a worker gone before it read the question is said by its end, not a SIGPIPE that would end the bot */
        struct sigaction ignore, was;
        memset(&ignore, 0, sizeof ignore);
        ignore.sa_handler = SIG_IGN;
        sigemptyset(&ignore.sa_mask);
        sigaction(SIGPIPE, &ignore, &was);
        while (sent < len) { /* the worker reads the question whole before it answers */
            ssize_t w = write(in[1], ask + sent, len - sent);
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) break;
            sent += (size_t)w;
        }
        sigaction(SIGPIPE, &was, NULL);
    }
    close(in[1]);
    free(ask);
    clock_gettime(CLOCK_MONOTONIC, &now);
    deadline = (double)now.tv_sec + (double)now.tv_nsec / 1e9 + seconds;
    for (;;) {
        struct pollfd p;
        char piece[65536];
        ssize_t r;
        double left;
        int ready;
        clock_gettime(CLOCK_MONOTONIC, &now);
        left = deadline - ((double)now.tv_sec + (double)now.tv_nsec / 1e9);
        p.fd = out[0];
        p.events = POLLIN;
        ready = left > 0 ? poll(&p, 1, (int)(left * 1000) + 1) : 0;
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) {
            *why = rei_format("the worker's answer could not be waited for: %s", strerror(errno));
            break;
        }
        if (!ready) {
            *why = rei_format("the worker took over %d s, and was stopped", seconds);
            break;
        }
        r = read(out[0], piece, sizeof piece);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        if (answer.len + (size_t)r > WK_ANSWER) {
            *why = rei_format("the worker's answer is over %d MiB, and it was stopped", WK_ANSWER >> 20);
            break;
        }
        buf_addn(&answer, piece, (size_t)r);
    }
    close(out[0]);
    if (*why) kill(pid, SIGKILL);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) continue;
    if (!*why) {
        if (WIFSIGNALED(status))
            *why = rei_format("the worker stopped on signal %d (%s)", WTERMSIG(status), strsignal(WTERMSIG(status)));
        else if (!WIFEXITED(status) || WEXITSTATUS(status))
            *why = rei_format("the worker ended with status %d", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        else
            result = answered(answer.text ? answer.text : "", answer.len, k, why);
    }
    buf_free(&answer);
    if (result) cleaned(k->dir);
    return result;
}
