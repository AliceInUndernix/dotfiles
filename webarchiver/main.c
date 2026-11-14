/* webarchiver - archive whole websites with wget: several at the same time,
 * every file stored once, safe to stop and resume, with version history.
 * See README for usage. */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "webarchiver.h"
#include "config.h"

char *out_dir;
char *logs_dir;
char *state_dir;
char *hist_dir;
char *obj_dir;
struct strlist big_exts;
struct strlist req_exts;
struct strlist wait_steps;
char *fixed_wait;
char *user_agent;
char *hsts_opt;
int use_ipv4;
int jobs;
int refresh;
char *self_path;

static const char usage_text[] =
    "usage: webarchiver LINKS_FILE [ARCHIVE_DIR]  download/resume/update\n"
    "       webarchiver ARCHIVE_DIR               update the whole archive\n"
    "       webarchiver --versions [DOMAIN] [ARCHIVE_DIR]\n"
    "       webarchiver --restore DOMAIN VERSION [ARCHIVE_DIR]\n"
    "       webarchiver --serve [ARCHIVE_DIR] [PORT]\n";

int stop_requested(void)
{
    char *p;
    int res;

    p = path2(state_dir, "STOP");
    res = is_file(p);
    free(p);
    return res;
}

/* ------------------------------------------------------------ setup */

static void settings(void)
{
    const char *v;

    sl_init(&big_exts);
    sl_init(&req_exts);
    sl_init(&wait_steps);
    v = getenv("BIG_EXT");
    sl_split(&big_exts, v && v[0] ? v : DEF_BIG_EXT, ',');
    sl_split(&req_exts, REQ_EXTS, ',');
    v = getenv("WAIT_STEPS");
    sl_split(&wait_steps, v && v[0] ? v : DEF_WAIT_STEPS, ' ');
    if (!wait_steps.n)
        sl_add(&wait_steps, "1");
    v = getenv("WAIT");
    fixed_wait = v && v[0] ? xstrdup(v) : 0;
    v = getenv("UA");
    user_agent = xstrdup(v && v[0] ? v : DEF_USER_AGENT);
    v = getenv("IPV4");
    use_ipv4 = v && !strcmp(v, "1");
    v = getenv("JOBS");
    jobs = v && v[0] ? (int)parse_ll(v) : DEF_JOBS;
    if (jobs < 1)
        jobs = 1;
    v = getenv("REFRESH");
    refresh = v && v[0] ? (int)parse_ll(v) : DEF_REFRESH;
    if (refresh < 1)
        refresh = 1;
    /* wget must write its log in English (we read it); the character
     * set stays as the user has it */
    v = getenv("LC_ALL");
    if (v && v[0]) {
        setenv("LC_CTYPE", v, 1);
        unsetenv("LC_ALL");
    }
    setenv("LC_MESSAGES", "C", 1);
    setenv("LANGUAGE", "C", 1);
}

static char *absolute(const char *dir)
{
    char cwd[4096];
    char *joined, *res;

    if (dir[0] == '/')
        return norm_path(dir);
    if (!getcwd(cwd, sizeof(cwd)))
        die("cannot read the current directory", 0);
    joined = path2(cwd, dir);
    res = norm_path(joined);
    free(joined);
    return res;
}

static void use_archive(const char *dir)
{
    char *p;

    mkdirs(dir);
    out_dir = absolute(dir);
    logs_dir = path2(out_dir, ".archive-logs");
    state_dir = path2(out_dir, ".archive-state");
    hist_dir = path2(out_dir, ".archive-history");
    obj_dir = path2(hist_dir, "objects");
    mkdirs(logs_dir);
    mkdirs(state_dir);
    p = path2(state_dir, "wget-hsts");
    hsts_opt = str2("--hsts-file=", p);
    free(p);
}

/* ------------------------------------------------------------ links */

struct dom {
    char *name;
    struct strlist links;
    char mode[16];
    long long offset;
    int pid;            /* downloading */
    long long start;
    long long end;
    int fpid;           /* finishing (linking) */
    long long fend;
    int frc;
    /* progress read from the log */
    long long lpos;
    long long files;
    long long bytes;
    long long errors;
    char last[300];
    long long w0_t, w0_f, w1_t, w1_f;
};

static struct dom *doms;
static int ndoms;

/* "example.org" from "https://User@Example.org:8080/x" -> folder name */
static char *link_domain(const char *link)
{
    const char *p, *e, *at;
    char *host, *res;

    p = strstr(link, "://");
    p = p ? p + 3 : link;
    e = p + strcspn(p, "/?#");
    at = p;
    while (at < e && *at != '@')
        at++;
    if (at < e)
        p = at + 1;
    host = xstrndup(p, e - p);
    res = host_dir(host);
    free(host);
    return res;
}

static void remember_link(const char *link)
{
    struct strlist known;
    char *p, *data;
    int len;
    FILE *f;

    p = path2(out_dir, ".archive-sites.txt");
    data = read_file(p, &len);
    sl_init(&known);
    if (data)
        sl_split(&known, data, '\n');
    if (!sl_has(&known, link)) {
        f = fopen(p, "a");
        if (f) {
            fprintf(f, "%s\n", link);
            fclose(f);
        }
    }
    sl_free(&known);
    free(data);
    free(p);
}

static void read_links(const char *file)
{
    struct strlist lines;
    char *data, *line, *hash, *link, *d;
    int len, i, j;

    data = read_file(file, &len);
    if (!data)
        die("cannot read ", file);
    sl_init(&lines);
    sl_split(&lines, data, '\n');
    free(data);
    doms = xmalloc((lines.n + 1) * sizeof(struct dom));
    ndoms = 0;
    for (i = 0; i < lines.n; i++) {
        line = lines.v[i];
        hash = strchr(line, '#');
        if (hash)
            *hash = 0;
        while (*line == ' ' || *line == '\t' || *line == '\r')
            line++;
        len = strlen(line);
        while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t' ||
            line[len - 1] == '\r'))
        {
            len--;
        }
        line[len] = 0;
        if (!line[0])
            continue;
        link = str_ieq_n(line, "http://", 7) || str_ieq_n(line, "https://",
            8) ? xstrdup(line) : str2("https://", line);
        d = link_domain(link);
        if (!d[0]) {
            fprintf(stderr, "Skipping bad line: %s\n", link);
            free(d);
            free(link);
            continue;
        }
        for (j = 0; j < ndoms && strcmp(doms[j].name, d); j++)
            ;
        if (j == ndoms) {
            memset(&doms[j], 0, sizeof(struct dom));
            doms[j].name = d;
            sl_init(&doms[j].links);
            doms[j].last[0] = '-';
            ndoms++;
        } else {
            free(d);
        }
        if (!sl_has(&doms[j].links, link))
            sl_add(&doms[j].links, link);
        remember_link(link);
        free(link);
    }
    sl_free(&lines);
    if (!ndoms)
        die("no links found in ", file);
}

/* ------------------------------------------------------------ prepare */

/* repair an interrupted run, decide new/resume/update, take a snapshot */
static void prepare(struct dom *dm)
{
    struct dstate st;
    struct table pre, referenced, none;
    struct strlist files;
    struct urlmap m;
    char *dir, *log, *p;
    int i;

    dir = domain_dir(dm->name);
    mkdirs(dir);
    state_load(dm->name, &st);
    tab_init(&pre);
    pre_load(dm->name, &pre);
    map_load(dm->name, &m);
    if (!strcmp(st.phase, "crawl")) {   /* died while downloading */
        repair(dm->name, &st, &pre, &m);
        referenced_shas(dm->name, &referenced);
        tab_init(&none);
        process_backups(dm->name, &pre, &referenced, &none);
        tab_free(&none);
        tab_free(&referenced);
    }
    update_urlmap(dm->name, &m, 0, 0);
    strcpy(dm->mode, st.complete ? "update" :
        (dir_empty(dir) ? "new" : "resume"));
    p = path2(logs_dir, dm->name);
    log = str2(p, ".log");
    free(p);
    if (!strcmp(st.phase, "idle")) {
        /* the last run finished and its files are in the url map:
         * its wget log is not needed any more, so each run starts a
         * new one instead of keeping every request ever made */
        map_save(dm->name, &m);
        write_file(log, "", 0);
    }
    dm->offset = file_size(log) > 0 ? file_size(log) : 0;
    if (strcmp(st.phase, "finalize")) {
        if (!strcmp(dm->mode, "update")) {
            /* make wget fetch every page again: pages are small and a
             * fresh copy is the only reliable way to see every change;
             * other files are fetched only if they changed */
            sl_init(&files);
            list_files(dir, &files);
            for (i = 0; i < files.n; i++) {
                if (ends_with(files.v[i], ".html") ||
                    ends_with(files.v[i], ".htm") ||
                    ends_with(files.v[i], ".xhtml") ||
                    ends_with(files.v[i], ".shtml") ||
                    ends_with(files.v[i], ".css"))
                {
                    set_mtime(files.v[i], 0);
                }
            }
            sl_free(&files);
        }
        strcpy(st.phase, "crawl");
        tab_free(&pre);
        tab_init(&pre);
        snapshot(dm->name, &pre);
        pre_save(dm->name, &pre);
        st.log_offset = dm->offset;
    }
    strcpy(st.mode, dm->mode);
    state_save(dm->name, &st);
    map_save(dm->name, &m);
    dm->lpos = dm->offset;
    map_free(&m);
    tab_free(&pre);
    free(log);
    free(dir);
}

/* ------------------------------------------------------------ progress */

static int color_on;

#define C_RESET (color_on ? "\033[0m" : "")
#define C_DIMMED (color_on ? "\033[2m" : "")
#define C_STRONG (color_on ? "\033[1m" : "")
#define C_CYAN (color_on ? "\033[36m" : "")
#define C_YELLOW (color_on ? "\033[33m" : "")
#define C_GREEN (color_on ? "\033[32m" : "")
#define C_PURPLE (color_on ? "\033[35m" : "")
#define C_RED (color_on ? "\033[31m" : "")

/* new lines of a domain's wget log: files, bytes, errors, last URL */
static void read_progress(struct dom *dm)
{
    char tmp[65536];
    char *p, *log, *line, *next, *u, *e;
    int fd, n, used;

    p = path2(logs_dir, dm->name);
    log = str2(p, ".log");
    free(p);
    fd = open(log, O_RDONLY);
    free(log);
    if (fd < 0)
        return;
    lseek(fd, (off_t)dm->lpos, SEEK_SET);
    while ((n = read(fd, tmp, sizeof(tmp) - 1)) > 0) {
        tmp[n] = 0;
        used = 0;
        for (line = tmp; (next = strchr(line, '\n')) != 0;
            line = next + 1)
        {
            *next = 0;
            used = next + 1 - tmp;
            if (strstr(line, " URL:") && strstr(line, " -> ")) {
                dm->files++;
                p = strchr(strstr(line, " URL:"), '[');
                if (p)
                    dm->bytes += parse_ll(p + 1);
                u = strstr(line, "URL:") + 4;
                while (*u == ' ')
                    u++;
                e = u + strcspn(u, " ");
                n = e - u < (int)sizeof(dm->last) - 1 ? e - u :
                    (int)sizeof(dm->last) - 1;
                memcpy(dm->last, u, n);
                dm->last[n] = 0;
            }
            if (strstr(line, "failed:") ||
                strstr(line, "unable to resolve") ||
                (strstr(line, "ERROR ") && strstr(line, "ERROR ")[6] >= '0'
                && strstr(line, "ERROR ")[6] <= '9'))
            {
                dm->errors++;
            }
        }
        if (!used)
            break;      /* an unfinished line: read it next time */
        dm->lpos += used;
        lseek(fd, (off_t)dm->lpos, SEEK_SET);
    }
    close(fd);
}

static void rate(long long files, long long secs, char *out)
{
    double r;

    if (secs <= 0) {
        strcpy(out, "-");
        return;
    }
    r = files * 60.0 / secs;
    if (r >= 10)
        sprintf(out, "%ld files/min", (long)(r + 0.5));
    else
        sprintf(out, "%.1f files/min", r);
}

static char *small_file(const char *d, const char *ext)
{
    char *p, *data;
    int len;

    p = state_file(d, ext);
    data = read_file(p, &len);
    free(p);
    if (!data)
        return xstrdup("");
    while (len > 0 && (data[len - 1] == '\n' || data[len - 1] == '\r'))
        len--;
    data[len] = 0;
    return data;
}

static int stopping;
static int drawn;
static long long last_plain;

/* one line, cut to the terminal width; colour codes take no space */
static void put_line(const char *s, int width)
{
    int n;

    n = 0;
    while (*s) {
        if (s[0] == '\033') {
            while (*s && *s != 'm') {
                putchar(*s);
                s++;
            }
            if (*s) {
                putchar(*s);
                s++;
            }
            continue;
        }
        if (n < width)
            putchar(*s);
        if (((unsigned char)*s & 0xc0) != 0x80)
            n++;
        s++;
    }
    fputs(C_RESET, stdout);
    putchar('\n');
}

static void draw(int final)
{
    struct winsize ws;
    struct strlist lines;
    char line[2048], size[32], fr[48], el_s[32], state[64], tmp[64];
    const char *color;
    char *stat, *rc, *msg;
    long long t, el, span;
    int cols, rows, i, k, pass, n_q, n_dl, n_big, n_ln, n_ok, n_pt, n_bad;
    int max;

    cols = 150;
    rows = 40;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        cols = ws.ws_col;
        rows = ws.ws_row;
    }
    t = now();
    sl_init(&lines);
    n_q = 0;
    n_dl = 0;
    n_big = 0;
    n_ln = 0;
    n_ok = 0;
    n_pt = 0;
    n_bad = 0;
    /* order: downloading, finishing, finished, queued */
    for (pass = 0; pass < 4; pass++) {
        for (i = 0; i < ndoms; i++) {
            struct dom *dm;

            dm = &doms[i];
            k = !dm->pid ? 3 : !dm->end ? 0 : !dm->fend ? 1 : 2;
            if (k != pass)
                continue;
            if (k == 3) {
                n_q++;
                sprintf(line, "%s%-28.28s %-16s %s%s", C_DIMMED, dm->name,
                    stopping ? "not started" : "queued",
                    !strcmp(dm->mode, "update") ?
                    "already archived - will update" :
                    !strcmp(dm->mode, "resume") ?
                    "unfinished - will resume" : "new", C_RESET);
                sl_add(&lines, line);
                continue;
            }
            read_progress(dm);
            msg = 0;
            if (k == 0) {
                n_dl++;
                el = t - dm->start;
                stat = small_file(dm->name, ".wait");
                sprintf(state, "%s@%ss", !strcmp(dm->mode, "update") ?
                    "updating" : !strcmp(dm->mode, "resume") ?
                    "resuming" : "downloading", stat[0] ? stat : "?");
                free(stat);
                color = C_CYAN;
                /* speed over the last 30-60 seconds */
                if (!dm->w0_t) {
                    dm->w0_t = dm->start;
                    dm->w0_f = 0;
                    dm->w1_t = t;
                    dm->w1_f = dm->files;
                } else if (t - dm->w1_t >= 30) {
                    dm->w0_t = dm->w1_t;
                    dm->w0_f = dm->w1_f;
                    dm->w1_t = t;
                    dm->w1_f = dm->files;
                }
                span = t - dm->w0_t;
                rate(dm->files - dm->w0_f, span, fr);
                msg = xstrdup(dm->last);
            } else {
                el = dm->end - dm->start;
                rate(dm->files, el, fr);
                msg = small_file(dm->name, ".status");
                rc = small_file(dm->name, ".rc");
                if (k == 1) {
                    el = t - dm->start;
                    color = C_YELLOW;
                    if (starts_with(msg, "BIG|")) {
                        n_big++;
                        strcpy(state, "big files");
                        memmove(msg, msg + 4, strlen(msg + 4) + 1);
                    } else {
                        n_ln++;
                        strcpy(state, "linking");
                    }
                } else if (dm->frc != 0) {
                    n_bad++;
                    strcpy(state, "failed");
                    color = C_RED;
                    el = dm->fend - dm->start;
                    free(msg);
                    msg = str3("FAILED - details in .archive-logs/",
                        dm->name, ".linking.log");
                } else if (!strcmp(rc, "0")) {
                    n_ok++;
                    strcpy(state, !strcmp(dm->mode, "update") ? "updated" :
                        "ready");
                    color = C_GREEN;
                    el = dm->fend - dm->start;
                } else {
                    n_pt++;
                    strcpy(state, "partial");
                    color = C_PURPLE;
                    el = dm->fend - dm->start;
                }
                free(rc);
            }
            human(dm->bytes, size);
            hms(el, el_s);
            sprintf(tmp, "%lld", dm->files);
            if (strlen(msg) > 1000)
                msg[1000] = 0;
            sprintf(line, "%s%-28.28s %-16s%s %10s %6s downloaded %16s  "
                "%s  errors:%-3lld %s%s%s", color, dm->name, state,
                C_RESET, size, tmp, fr, el_s, dm->errors, C_DIMMED, msg,
                C_RESET);
            free(msg);
            sl_add(&lines, line);
        }
    }
    sprintf(line, "%s===%s %s%d downloading%s | %s%d big files%s | "
        "%s%d linking%s | %s%d ready/updated%s", C_STRONG, C_RESET, C_CYAN,
        n_dl, C_RESET, C_YELLOW, n_big, C_RESET, C_YELLOW, n_ln, C_RESET,
        C_GREEN, n_ok, C_RESET);
    if (n_pt) {
        sprintf(tmp, " | %s%d partial%s", C_PURPLE, n_pt, C_RESET);
        strcat(line, tmp);
    }
    if (n_bad) {
        sprintf(tmp, " | %s%d failed%s", C_RED, n_bad, C_RESET);
        strcat(line, tmp);
    }
    sprintf(tmp, " | %d %s", n_q, stopping ? "not started" : "queued");
    strcat(line, tmp);
    sprintf(tmp, "   %s(max %d at once, Ctrl+C = stop safely)%s",
        C_DIMMED, jobs, C_RESET);
    strcat(line, tmp);
    if (color_on && !final) {
        max = rows - 2 < 3 ? 3 : rows - 2;
        if (drawn)
            printf("\033[%dA", drawn);
        fputs("\033[J", stdout);
        put_line(line, cols - 1);
        drawn = 1;
        for (i = 0; i < lines.n; i++) {
            if (i >= max - 1 && lines.n > max) {
                printf("    ... %d more\n", lines.n - i);
                drawn++;
                break;
            }
            put_line(lines.v[i], cols - 1);
            drawn++;
        }
    } else if (final || t - last_plain >= 30) {
        last_plain = t;
        if (color_on && drawn)
            printf("\033[%dA\033[J", drawn);
        drawn = 0;
        put_line(line, 100000);
        for (i = 0; i < lines.n; i++)
            put_line(lines.v[i], 100000);
    }
    fflush(stdout);
    sl_free(&lines);
}

/* ------------------------------------------------------------ main loop */

static volatile int stop_count;

static void on_stop(int sig)
{
    (void)sig;
    stop_count++;
}

static void stop_downloads(void)
{
    int i;

    for (i = 0; i < ndoms; i++) {
        if (doms[i].pid > 0 && !doms[i].end)
            kill(doms[i].pid, SIGTERM);
    }
}

static void start_worker(struct dom *dm)
{
    dm->pid = fork();
    if (dm->pid == 0)
        worker(dm->name, dm->mode, &dm->links);
    dm->start = now();
}

static void start_linking(struct dom *dm, int fetch)
{
    char *p, *log, *rc;
    int fd;

    status(dm->name, "starting", 0);
    rc = small_file(dm->name, ".rc");
    if (!rc[0]) {
        free(rc);
        rc = xstrdup("1");      /* the download died without a result */
    }
    dm->fpid = fork();
    if (dm->fpid == 0) {
        on_signal_do(SIGINT, SIG_IGN);
        on_signal_do(SIGTERM, SIG_DFL);
        p = path2(logs_dir, dm->name);
        log = str2(p, ".linking.log");
        fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            dup2(fd, 1);
            dup2(fd, 2);
            close(fd);
        }
        exit(finalize(dm->name, rc, fetch));
    }
    free(rc);
}

static char *lock_dir;

static void take_lock(void)
{
    char *pidfile, *data;
    char num[32];
    long long pid;
    int len;

    lock_dir = path2(out_dir, ".archive-lock");
    pidfile = path2(lock_dir, "pid");
    if (mkdir(lock_dir, 0755) != 0) {
        data = read_file(pidfile, &len);
        pid = data ? parse_ll(data) : 0;
        free(data);
        if (pid > 0 && kill((int)pid, 0) == 0)
            die("another run is already working on ", out_dir);
        unlink(pidfile);
        rmdir(lock_dir);
        mkdir(lock_dir, 0755);
    }
    sprintf(num, "%ld\n", (long)getpid());
    write_file(pidfile, num, strlen(num));
    free(pidfile);
}

static void release_lock(void)
{
    char *pidfile;

    pidfile = path2(lock_dir, "pid");
    unlink(pidfile);
    rmdir(lock_dir);
    free(pidfile);
}

static void clear_run_files(void)
{
    static const char *ext[] = { ".rc", ".status", 0 };
    char *p;
    int i, j;

    for (i = 0; i < ndoms; i++) {
        for (j = 0; ext[j]; j++) {
            p = state_file(doms[i].name, ext[j]);
            unlink(p);
            free(p);
        }
    }
}

static void print_problems(void)
{
    char *p, *log, *data, *line, *next;
    int i, len, shown;

    shown = 0;
    for (i = 0; i < ndoms; i++) {
        p = path2(logs_dir, doms[i].name);
        log = str2(p, ".linking.log");
        free(p);
        data = read_file(log, &len);
        free(log);
        for (line = data; line && *line; line = next) {
            next = strchr(line, '\n');
            if (next) {
                *next = 0;
                next++;
            } else {
                next = line + strlen(line);
            }
            if (!starts_with(line, "  !"))
                continue;
            if (!shown)
                printf("--- problems:\n");
            printf("  %s: %s\n", doms[i].name, line + 2);
            shown = 1;
        }
        free(data);
    }
}

static int run(void)
{
    char *p;
    int i, next, running, linking, ws, pid, forced;

    take_lock();
    printf("=== Checking %d domain(s)\n", ndoms);
    fflush(stdout);
    for (i = 0; i < ndoms; i++)
        prepare(&doms[i]);
    clear_run_files();
    p = path2(state_dir, "STOP");
    unlink(p);
    free(p);
    on_signal_do(SIGINT, on_stop);
    on_signal_do(SIGTERM, on_stop);
    on_signal_do(SIGHUP, on_stop);
    {
        struct winsize ws;

        /* colours and redrawing only on a terminal */
        color_on = ioctl(1, TIOCGWINSZ, &ws) == 0;
    }
    next = 0;
    forced = 0;
    for (;;) {
        if (stop_count == 1 && !stopping) {
            stopping = 1;
            p = path2(state_dir, "STOP");   /* linking: no new big files */
            write_file(p, "1\n", 2);
            free(p);
            fprintf(stderr, "\n>>> Stopping: ending downloads, then "
                "fixing links (Ctrl+C again to quit at once)\n");
            stop_downloads();
        }
        if (stop_count >= 2) {
            fprintf(stderr, "\nForced exit. Nothing is lost: the next run "
                "repairs and continues.\n");
            stop_downloads();
            for (i = 0; i < ndoms; i++) {
                if (doms[i].fpid > 0 && !doms[i].fend)
                    kill(doms[i].fpid, SIGTERM);
            }
            forced = 1;
            break;
        }
        running = 0;
        linking = 0;
        for (i = 0; i < ndoms; i++) {
            struct dom *dm;

            dm = &doms[i];
            if (dm->pid > 0 && !dm->end) {
                pid = waitpid(dm->pid, &ws, WNOHANG);
                if (pid == dm->pid) {
                    dm->end = now();
                    /* downloaded (or stopped): fix its links right away */
                    start_linking(dm, !stopping);
                    linking++;
                } else {
                    running++;
                }
            }
            if (dm->fpid > 0 && !dm->fend) {
                pid = waitpid(dm->fpid, &ws, WNOHANG);
                if (pid == dm->fpid) {
                    dm->fend = now();
                    dm->frc = WIFEXITED(ws) ? WEXITSTATUS(ws) : 1;
                } else {
                    linking++;
                }
            }
        }
        while (!stopping && running < jobs && next < ndoms) {
            start_worker(&doms[next]);
            next++;
            running++;
        }
        draw(0);
        if (!running && !linking && (stopping || next >= ndoms))
            break;
        sleep_ms(refresh * 1000);
    }
    if (!forced)
        draw(1);
    print_problems();
    p = path2(state_dir, "STOP");
    unlink(p);
    free(p);
    for (i = 0; i < ndoms; i++) {
        p = state_file(doms[i].name, ".rc");
        unlink(p);
        free(p);
    }
    release_lock();
    if (forced)
        return 130;
    write_pages();
    printf("Archive is in: %s\n", out_dir);
    printf("Main page: %s/archive.html   (details per site: "
        "%s/<site>.linking.log)\n", out_dir, logs_dir);
    if (stopping)
        printf("Stopped early - run the same command again to "
            "continue.\n");
    return 0;
}

/* ------------------------------------------------------------ commands */

static char *find_self(const char *argv0)
{
    char tmp[4096];
    int n;

    n = readlink("/proc/self/exe", tmp, sizeof(tmp) - 1);
    if (n > 0) {
        tmp[n] = 0;
        return xstrdup(tmp);
    }
    return absolute(argv0);
}

int main(int argc, char **argv)
{
    const char *dir;
    char *list;

    settings();
    self_path = find_self(argv[0]);
    if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        fputs(usage_text, argc < 2 ? stderr : stdout);
        return argc < 2;
    }
    if (!strcmp(argv[1], "--versions")) {
        dir = argc > 3 ? argv[3] : ".";
        if (argc == 3) {
            list = path2(argv[2], ".archive-history");
            if (is_dir(list))
                dir = argv[2];
            free(list);
        }
        use_archive(dir);
        cmd_versions(argc > 2 && dir != argv[2] ? argv[2] : 0);
        return 0;
    }
    if (!strcmp(argv[1], "--restore")) {
        if (argc < 4)
            die("usage: webarchiver --restore DOMAIN VERSION "
                "[ARCHIVE_DIR]", 0);
        use_archive(argc > 4 ? argv[4] : ".");
        return cmd_restore(argv[2], argv[3]);
    }
    if (!strcmp(argv[1], "--serve")) {
        use_archive(argc > 2 ? argv[2] : ".");
        cmd_serve(argc > 3 ? (int)parse_ll(argv[3]) : SERVE_PORT);
        return 0;
    }
    if (argc == 2 && is_dir(argv[1])) {
        /* a directory: update everything ever archived into it */
        use_archive(argv[1]);
        list = path2(out_dir, ".archive-sites.txt");
        if (file_size(list) <= 0)
            die("not an archive yet (no .archive-sites.txt) in ", out_dir);
        printf("=== Updating the whole archive in %s\n", out_dir);
        read_links(list);
        free(list);
        return run();
    }
    if (!is_file(argv[1])) {
        fputs(usage_text, stderr);
        return 1;
    }
    list = absolute(argv[1]);
    use_archive(argc > 2 ? argv[2] : ".");
    read_links(list);
    free(list);
    return run();
}
