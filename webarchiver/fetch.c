/* everything that talks to the network goes through wget */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "webarchiver.h"
#include "config.h"

/* ------------------------------------------------------------ processes */

static int start(char **argv, const char *log, int out_fd, int *pid)
{
    int fd;

    *pid = fork();
    if (*pid < 0)
        return 0;
    if (*pid > 0)
        return 1;
    fd = open("/dev/null", O_RDONLY);
    if (fd >= 0) {
        dup2(fd, 0);
        close(fd);
    }
    if (log)
        fd = open(log, O_WRONLY | O_CREAT | O_APPEND, 0644);
    else
        fd = open("/dev/null", O_WRONLY);
    if (fd >= 0) {
        dup2(fd, 2);
        dup2(out_fd >= 0 ? out_fd : fd, 1);
        close(fd);
    }
    if (out_fd >= 0)
        close(out_fd);
    execvp(argv[0], argv);
    _exit(127);
    return 0;
}

int spawn(char **argv, const char *log, int *pid)
{
    return start(argv, log, -1, pid);
}

static int exit_code(int ws)
{
    if (WIFEXITED(ws))
        return WEXITSTATUS(ws);
    return 128 + WTERMSIG(ws);
}

int run_wait(char **argv, const char *log)
{
    int pid, ws;

    if (!spawn(argv, log, &pid))
        return 127;
    while (waitpid(pid, &ws, 0) < 0 && errno == EINTR)
        ;
    return exit_code(ws);
}

/* ------------------------------------------------------------ wget */

struct args {
    char *v[64];
    int n;
};

static void arg(struct args *a, const char *s)
{
    if (a->n < 63) {
        a->v[a->n] = xstrdup(s);
        a->n++;
    }
    a->v[a->n] = 0;
}

static void args_free(struct args *a)
{
    int i;

    for (i = 0; i < a->n; i++)
        free(a->v[i]);
    a->n = 0;
}

static void wget_base(struct args *a)
{
    char *ua;

    a->n = 0;
    arg(a, "wget");
    ua = str2("--user-agent=", user_agent);
    arg(a, ua);
    free(ua);
    if (use_ipv4)
        arg(a, "--inet4-only");
    /* wget would otherwise list every https site it visits in
     * ~/.wget-hsts; keep that list inside the archive instead */
    arg(a, hsts_opt);
}

/* https if the site answers on it, otherwise plain http */
char *pick_scheme(const char *url)
{
    struct args a;
    char *plain;
    int rc;

    if (!starts_with(url, "https://"))
        return xstrdup(url);
    wget_base(&a);
    arg(&a, "-q");
    arg(&a, "--spider");
    arg(&a, "--tries=1");
    arg(&a, "--connect-timeout=10");
    arg(&a, "--dns-timeout=10");
    arg(&a, "--read-timeout=20");
    arg(&a, url);
    rc = run_wait(a.v, 0);
    args_free(&a);
    /* 4 = network failure, 5 = SSL failure: https is not usable */
    if (rc != 4 && rc != 5)
        return xstrdup(url);
    plain = str2("http://", url + 8);
    wget_base(&a);
    arg(&a, "-q");
    arg(&a, "--spider");
    arg(&a, "--tries=1");
    arg(&a, "--connect-timeout=10");
    arg(&a, "--dns-timeout=10");
    arg(&a, "--read-timeout=20");
    arg(&a, plain);
    rc = run_wait(a.v, 0);
    args_free(&a);
    if (rc != 4 && rc != 5)
        return plain;
    free(plain);
    return xstrdup(url);
}

/* ------------------------------------------------------------ worker */

static char *log_path(const char *d)
{
    char *p, *res;

    p = path2(logs_dir, d);
    res = str2(p, ".log");
    free(p);
    return res;
}

static void log_line(const char *log, const char *a, const char *b)
{
    FILE *f;
    char when[32];

    f = fopen(log, "a");
    if (!f)
        return;
    utc_id(now(), when);
    when[10] = ' ';
    when[13] = ':';
    when[16] = ':';
    fprintf(f, "### %s UTC %s%s\n", when, a, b ? b : "");
    fclose(f);
}

/* pace step of a domain (0 = fastest); a slow-down expires */
static int wait_level(const char *d)
{
    char *p, *data;
    long long lvl;
    int len;

    p = state_file(d, ".slow");
    lvl = 0;
    if (file_mtime(p) >= 0 &&
        now() - file_mtime(p) / 1000000000LL < SLOW_DAYS * 86400LL)
    {
        data = read_file(p, &len);
        lvl = data && data[0] >= '0' && data[0] <= '9' ? parse_ll(data) : 1;
        free(data);
    }
    free(p);
    if (lvl >= wait_steps.n)
        lvl = wait_steps.n - 1;
    return (int)lvl;
}

/* answers that mean "slow down": 429, 503, or files needing retries.
 * Reads only what wget added since the last call: *pos is where it
 * stopped, *count the total so far. */
static int pushback_count(const char *log, long long *pos, int *count)
{
    char tmp[65536];
    char *line, *next, *last;
    int fd, n, used;

    fd = open(log, O_RDONLY);
    if (fd < 0)
        return *count;
    lseek(fd, (off_t)*pos, SEEK_SET);
    while ((n = read(fd, tmp, sizeof(tmp) - 1)) > 0) {
        tmp[n] = 0;
        used = 0;
        for (line = tmp; (next = strchr(line, '\n')) != 0;
            line = next + 1)
        {
            *next = 0;
            used = next + 1 - tmp;
            if (strstr(line, "ERROR 429") || strstr(line, "ERROR 503")) {
                *count += 1;
                continue;
            }
            last = strrchr(line, '[');
            if (strstr(line, "URL:") && last && last[1] >= '2' &&
                last[1] <= '9' && parse_ll(last + 1) >= 2)
            {
                *count += 1;
            }
        }
        if (!used && n == (int)sizeof(tmp) - 1)
            used = n;   /* a line longer than the buffer: skip it */
        *pos += used;
        if (used < n)
            break;      /* half a line: read it again next time */
    }
    close(fd);
    return *count;
}

static volatile int worker_wget;
static char *worker_rc_file;

static void worker_term(int sig)
{
    int fd, n;

    (void)sig;
    if (worker_wget > 0)
        kill(worker_wget, SIGTERM);
    fd = open(worker_rc_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        n = write(fd, "143\n", 4);
        (void)n;    /* nothing more can be done in a signal handler */
        close(fd);
    }
    _exit(143);
}

static void write_text(const char *path, const char *text)
{
    write_file(path, text, strlen(text));
}

static int mirror(const char *d, const char *mode, const char *site,
    const char *wait, const char *log)
{
    struct args a;
    char *p;
    int pid, ws, rc, slowed, pushbacks;
    long long off;

    wget_base(&a);
    arg(&a, "--recursive");
    arg(&a, "--level=inf");
    arg(&a, "--page-requisites");
    arg(&a, "--adjust-extension");
    arg(&a, "--no-parent");
    arg(&a, "--no-verbose");
    if (!strcmp(mode, "update")) {
        /* only changed files (HEAD check, so unchanged pages are still
         * followed); keep the old copy as name.1 */
        arg(&a, "--timestamping");
        arg(&a, "--no-if-modified-since");
        arg(&a, "--backups=1");
    } else {
        arg(&a, "--no-clobber");    /* resume: skip finished files */
    }
    {
        struct buf b;
        int i;

        /* big files are checked for duplicates first (finish.c) */
        buf_init(&b);
        buf_add(&b, "--reject=");
        for (i = 0; i < big_exts.n; i++) {
            if (i)
                buf_addc(&b, ',');
            buf_add(&b, big_exts.v[i]);
        }
        arg(&a, b.s);
        buf_free(&b);
    }
    p = str2("--wait=", wait);
    arg(&a, p);
    free(p);
    arg(&a, "--random-wait");
    arg(&a, "--tries=5");
    arg(&a, "--connect-timeout=15");
    arg(&a, "--dns-timeout=15");
    arg(&a, "--read-timeout=60");
    arg(&a, "--retry-on-http-error=429,503");
    arg(&a, "--waitretry=10");
    arg(&a, "--no-host-directories");
    p = domain_dir(d);
    {
        char *opt;

        opt = str2("--directory-prefix=", p);
        arg(&a, opt);
        free(opt);
    }
    free(p);
    p = str2("--append-output=", log);
    arg(&a, p);
    free(p);
    arg(&a, site);
    off = file_size(log);
    if (off < 0)
        off = 0;
    if (!spawn(a.v, 0, &pid)) {
        args_free(&a);
        return 127;
    }
    args_free(&a);
    worker_wget = pid;
    slowed = 0;
    pushbacks = 0;
    while (waitpid(pid, &ws, WNOHANG) == 0) {
        if (!fixed_wait && wait_level(d) + 1 < wait_steps.n &&
            pushback_count(log, &off, &pushbacks) >= 2)
        {
            kill(pid, SIGTERM);
            waitpid(pid, &ws, 0);
            slowed = 1;
            break;
        }
        sleep_ms(2000);
    }
    worker_wget = 0;
    rc = exit_code(ws);
    return slowed ? -1 : rc;
}

/* download one domain: every link of it, one after another */
void worker(const char *d, const char *mode, struct strlist *links)
{
    struct dstate st;
    struct table pre;
    struct urlmap m;
    char *log, *site, *p, *q, *wait;
    char num[32];
    int i, rc, worst, lvl;

    worker_rc_file = state_file(d, ".rc");
    on_signal_do(SIGINT, SIG_IGN);  /* the main process stops us cleanly */
    on_signal_do(SIGTERM, worker_term);
    log = log_path(d);
    worst = 0;
    for (i = 0; i < links->n; i++) {
        site = pick_scheme(links->v[i]);
        if (strcmp(site, links->v[i])) {
            p = str3(links->v[i], " does not answer over https - using ",
                site);
            log_line(log, p, 0);
            free(p);
        }
        p = state_file(d, ".base");
        write_text(p, site);
        free(p);
        for (;;) {
            lvl = wait_level(d);
            wait = fixed_wait ? fixed_wait : wait_steps.v[lvl];
            p = state_file(d, ".wait");
            write_text(p, wait);
            free(p);
            p = str3(mode, " ", site);
            q = str3(" (wait ", wait, "s)");
            log_line(log, p, q);
            free(q);
            free(p);
            rc = mirror(d, mode, site, wait, log);
            if (rc != -1)
                break;
            sprintf(num, "%d", lvl + 1);
            p = state_file(d, ".slow");
            write_text(p, num);
            free(p);
            q = str2(wait_steps.v[lvl + 1], "s");
            log_line(log, "site is limiting requests - slowing down to ",
                q);
            free(q);
            /* clean up the file wget was writing when it was stopped */
            state_load(d, &st);
            tab_init(&pre);
            pre_load(d, &pre);
            map_load(d, &m);
            repair(d, &st, &pre, &m);
            map_save(d, &m);
            map_free(&m);
            tab_free(&pre);
        }
        if (rc == 8)
            rc = 0;     /* some pages returned errors (404 ...): normal */
        if (rc > worst)
            worst = rc;
        free(site);
    }
    sprintf(num, "%d\n", worst);
    write_text(worker_rc_file, num);
    free(log);
    exit(0);
}

/* ------------------------------------------------------------ big files */

/* headers of the last answer in "wget -S" output */
struct answer {
    char *final;
    char *length;
    char *etag;
    char *range_total;
    int code;
};

static void read_answer(const char *text, const char *url,
    struct answer *r)
{
    const char *line, *v;
    char *next_url, *val;
    int n;

    r->final = xstrdup(url);
    r->length = 0;
    r->etag = 0;
    r->range_total = 0;
    r->code = 0;
    for (line = text; line && *line; line = strchr(line, '\n')) {
        if (*line == '\n')
            line++;
        while (*line == ' ')
            line++;
        n = strcspn(line, "\r\n");
        val = xstrndup(line, n);
        v = strchr(val, ':');
        if (starts_with(val, "HTTP/")) {
            r->code = (int)parse_ll(strchr(val, ' ') ? strchr(val, ' ') :
                "");
            free(r->length);
            free(r->etag);
            free(r->range_total);
            r->length = 0;
            r->etag = 0;
            r->range_total = 0;
        } else if (v && str_ieq_n(val, "Location:", 9)) {
            v++;
            while (*v == ' ')
                v++;
            next_url = url_join(r->final, v);
            free(r->final);
            r->final = next_url;
        } else if (v && str_ieq_n(val, "Content-Length:", 15)) {
            free(r->length);
            r->length = xstrdup(v + 1 + (v[1] == ' '));
        } else if (v && str_ieq_n(val, "ETag:", 5)) {
            free(r->etag);
            r->etag = xstrdup(v + 1 + (v[1] == ' '));
        } else if (v && str_ieq_n(val, "Content-Range:", 14) &&
            strrchr(val, '/'))
        {
            free(r->range_total);
            r->range_total = xstrdup(strrchr(val, '/') + 1);
        }
        free(val);
    }
}

static void answer_free(struct answer *r)
{
    free(r->final);
    free(r->length);
    free(r->etag);
    free(r->range_total);
}

static char *temp_name(const char *what)
{
    char num[32];
    char *name, *p;

    sprintf(num, ".%ld", (long)getpid());
    name = str3("tmp-", what, num);
    p = path2(state_dir, name);
    free(name);
    return p;
}

/* first bytes of a file (Range request, but read only that much even
 * if the server ignores the range) */
static int read_prefix(const char *url, char *sha, char **total)
{
    struct args a;
    struct answer r;
    struct sha256 c;
    unsigned char digest[32];
    char tmp[65536];
    char *hdr, *range, *text;
    int fds[2], pid, ws, n, got, len;

    if (pipe(fds) != 0)
        return 0;
    hdr = temp_name("headers");
    wget_base(&a);
    arg(&a, "-S");
    arg(&a, "-q");
    arg(&a, "--tries=1");
    arg(&a, "--timeout=60");
    arg(&a, "-O");
    arg(&a, "-");
    sprintf(tmp, "--header=Range: bytes=0-%d", PREFIX_BYTES - 1);
    arg(&a, tmp);
    arg(&a, url);
    range = 0;
    if (!start(a.v, hdr, fds[1], &pid)) {
        args_free(&a);
        free(hdr);
        return 0;
    }
    args_free(&a);
    close(fds[1]);
    sha_init(&c);
    got = 0;
    while (got < PREFIX_BYTES &&
        (n = read(fds[0], tmp, PREFIX_BYTES - got < (int)sizeof(tmp) ?
        PREFIX_BYTES - got : (int)sizeof(tmp))) > 0)
    {
        sha_update(&c, (unsigned char *)tmp, n);
        got += n;
    }
    close(fds[0]);
    kill(pid, SIGTERM);
    waitpid(pid, &ws, 0);
    sha_final(&c, digest);
    sha_hex(digest, sha);
    text = read_file(hdr, &len);
    unlink(hdr);
    free(hdr);
    if (text) {
        read_answer(text, url, &r);
        if (r.range_total)
            range = xstrdup(r.range_total);
        else if (r.length)
            range = xstrdup(r.length);
        answer_free(&r);
        free(text);
    }
    *total = range;
    return got > 0;
}

/* what the server says about a file, without downloading it:
 * final URL after redirects, size, and keys that identify it */
int probe(const char *url, char **final, char **size, struct strlist *keys)
{
    struct args a;
    struct answer r;
    char *hdr, *text, *host, *k, *total;
    char sha[65];
    int rc, len;

    hdr = temp_name("headers");
    wget_base(&a);
    arg(&a, "-S");
    arg(&a, "--spider");
    arg(&a, "--tries=1");
    arg(&a, "--timeout=60");
    arg(&a, "--max-redirect=20");
    arg(&a, url);
    unlink(hdr);
    rc = run_wait(a.v, hdr);
    args_free(&a);
    text = read_file(hdr, &len);
    unlink(hdr);
    free(hdr);
    read_answer(text ? text : "", url, &r);
    free(text);
    if (rc != 0 && r.code != 403 && r.code != 405 && r.code != 501) {
        answer_free(&r);
        return 0;
    }
    *final = xstrdup(r.final);
    *size = 0;
    k = str2("url:", r.final);
    sl_add(keys, k);
    free(k);
    if (rc == 0 && r.etag && r.length) {
        host = url_host(r.final);
        k = str3("etag:", host, ":");
        free(host);
        host = str3(k, r.etag, ":");
        free(k);
        k = str2(host, r.length);
        free(host);
        sl_add(keys, k);
        free(k);
        *size = xstrdup(r.length);
    } else if (read_prefix(r.final, sha, &total)) {
        if (rc == 0 && r.length) {
            free(total);
            total = xstrdup(r.length);
        }
        if (total && strcmp(total, "*")) {
            k = str3("head:", total, ":");
            host = str2(k, sha);
            sl_add(keys, host);
            free(host);
            free(k);
            *size = total;
        } else {
            free(total);
        }
    }
    answer_free(&r);
    return 1;
}

static void progress(const char *dest, long long got, long long have,
    long long total, long long started)
{
    char a[32], b[32], c[32], line[256];
    const char *name;
    long long secs;

    name = strrchr(dest, '/') ? strrchr(dest, '/') + 1 : dest;
    secs = now() - started;
    human(got, a);
    human(secs > 0 ? (got - have) / secs : 0, c);
    if (total > 0) {
        human(total, b);
        sprintf(line, ": %s / %s (%lld%%)  %s/s", a, b, got * 100 / total,
            c);
    } else {
        sprintf(line, ": %s  %s/s", a, c);
    }
    {
        char *d, *msg;

        d = rel_out(dest);
        *strchr(d, '/') = 0;
        msg = str3("BIG|", strlen(name) > 120 ? "file" : name, line);
        status(d, msg, 0);
        free(msg);
        free(d);
    }
}

/* download into dest.part (continuing an earlier part), then hash it.
 * 1 = done, 0 = failed, -1 = stopped by the user */
int download(const char *url, const char *dest, const char *size,
    char *sha)
{
    struct args a;
    char *part, *dir;
    long long have, total, started;
    int pid, ws, rc, attempt;

    part = str2(dest, ".part");
    dir = xstrndup(dest, strrchr(dest, '/') - dest);
    mkdirs(dir);
    free(dir);
    total = size ? parse_ll(size) : 0;
    rc = 1;
    for (attempt = 0; attempt < 2; attempt++) {
        have = file_size(part);
        if (have < 0)
            have = 0;
        wget_base(&a);
        arg(&a, "-q");
        arg(&a, "--tries=5");
        arg(&a, "--connect-timeout=15");
        arg(&a, "--read-timeout=60");
        if (have > 0)
            arg(&a, "--continue");
        arg(&a, "-O");
        arg(&a, part);
        arg(&a, url);
        if (!spawn(a.v, 0, &pid)) {
            args_free(&a);
            free(part);
            return 0;
        }
        args_free(&a);
        started = now();
        while (waitpid(pid, &ws, WNOHANG) == 0) {
            if (stop_requested()) {
                kill(pid, SIGTERM);
                waitpid(pid, &ws, 0);
                free(part);
                return -1;
            }
            progress(dest, file_size(part), have, total, started);
            sleep_ms(1000);
        }
        rc = exit_code(ws);
        if (rc == 0 || have == 0)
            break;
        /* the server cannot continue: start this file again */
        unlink(part);
    }
    if (rc != 0) {
        free(part);
        return 0;
    }
    chmod(part, 0644);
    sha_file(part, sha);
    free(part);
    return 1;
}
