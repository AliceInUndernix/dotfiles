/* small helpers: buffers, string lists, tables, files, time */
#define _XOPEN_SOURCE 700
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "webarchiver.h"

/* ------------------------------------------------------------ memory */

void die(const char *msg, const char *arg)
{
    fprintf(stderr, "webarchiver: %s%s\n", msg, arg ? arg : "");
    exit(1);
}

void *xmalloc(int n)
{
    void *p;

    p = malloc(n > 0 ? n : 1);
    if (!p)
        die("out of memory", 0);
    return p;
}

char *xstrndup(const char *s, int n)
{
    char *p;

    p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

char *xstrdup(const char *s)
{
    return xstrndup(s, strlen(s));
}

char *str2(const char *a, const char *b)
{
    return str3(a, b, "");
}

char *str3(const char *a, const char *b, const char *c)
{
    int la, lb, lc;
    char *p;

    la = strlen(a);
    lb = strlen(b);
    lc = strlen(c);
    p = xmalloc(la + lb + lc + 1);
    memcpy(p, a, la);
    memcpy(p + la, b, lb);
    memcpy(p + la + lb, c, lc + 1);
    return p;
}

char *path2(const char *dir, const char *name)
{
    return str3(dir, "/", name);
}

/* ------------------------------------------------------------ buffer */

void buf_init(struct buf *b)
{
    b->cap = 256;
    b->len = 0;
    b->s = xmalloc(b->cap);
    b->s[0] = 0;
}

void buf_addn(struct buf *b, const char *s, int n)
{
    char *p;

    if (b->len + n + 1 > b->cap) {
        while (b->len + n + 1 > b->cap)
            b->cap *= 2;
        p = xmalloc(b->cap);
        memcpy(p, b->s, b->len);
        free(b->s);
        b->s = p;
    }
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = 0;
}

void buf_add(struct buf *b, const char *s)
{
    buf_addn(b, s, strlen(s));
}

void buf_addc(struct buf *b, int c)
{
    char ch;

    ch = c;
    buf_addn(b, &ch, 1);
}

void buf_addll(struct buf *b, long long v)
{
    char tmp[32];

    sprintf(tmp, "%lld", v);
    buf_add(b, tmp);
}

/* text for an HTML page: escape the characters that mean markup */
void buf_addhtml(struct buf *b, const char *s)
{
    for (; *s; s++) {
        if (*s == '&')
            buf_add(b, "&amp;");
        else if (*s == '<')
            buf_add(b, "&lt;");
        else if (*s == '>')
            buf_add(b, "&gt;");
        else if (*s == '"')
            buf_add(b, "&quot;");
        else if (*s == '\'')
            buf_add(b, "&#39;");
        else
            buf_addc(b, *s);
    }
}

void buf_free(struct buf *b)
{
    free(b->s);
    b->s = 0;
    b->len = 0;
    b->cap = 0;
}

/* ------------------------------------------------------------ string list */

void sl_init(struct strlist *l)
{
    l->n = 0;
    l->cap = 16;
    l->v = xmalloc(l->cap * sizeof(char *));
}

void sl_add(struct strlist *l, const char *s)
{
    char **v;

    if (l->n == l->cap) {
        l->cap *= 2;
        v = xmalloc(l->cap * sizeof(char *));
        memcpy(v, l->v, l->n * sizeof(char *));
        free(l->v);
        l->v = v;
    }
    l->v[l->n] = xstrdup(s);
    l->n++;
}

/* merge sort: stable, byte order like the C locale */
static void merge_sort(char **v, char **tmp, int n)
{
    int half, i, j, k;

    if (n < 2)
        return;
    half = n / 2;
    merge_sort(v, tmp, half);
    merge_sort(v + half, tmp, n - half);
    i = 0;
    j = half;
    k = 0;
    while (i < half && j < n) {
        if (strcmp(v[j], v[i]) < 0) {
            tmp[k] = v[j];
            j++;
        } else {
            tmp[k] = v[i];
            i++;
        }
        k++;
    }
    while (i < half) {
        tmp[k] = v[i];
        i++;
        k++;
    }
    while (j < n) {
        tmp[k] = v[j];
        j++;
        k++;
    }
    memcpy(v, tmp, n * sizeof(char *));
}

void sl_sort(struct strlist *l)
{
    char **tmp;

    tmp = xmalloc((l->n + 1) * sizeof(char *));
    merge_sort(l->v, tmp, l->n);
    free(tmp);
}

int sl_has(struct strlist *l, const char *s)
{
    int i;

    for (i = 0; i < l->n; i++) {
        if (!strcmp(l->v[i], s))
            return 1;
    }
    return 0;
}

void sl_free(struct strlist *l)
{
    int i;

    for (i = 0; i < l->n; i++)
        free(l->v[i]);
    free(l->v);
    l->v = 0;
    l->n = 0;
}

/* split s at sep, skipping empty parts and surrounding spaces */
void sl_split(struct strlist *l, const char *s, int sep)
{
    const char *start, *end;

    while (*s) {
        while (*s == ' ' || *s == sep)
            s++;
        start = s;
        while (*s && *s != sep)
            s++;
        end = s;
        while (end > start && end[-1] == ' ')
            end--;
        if (end > start) {
            char *part;

            part = xstrndup(start, end - start);
            sl_add(l, part);
            free(part);
        }
    }
}

/* ------------------------------------------------------------ table */

static unsigned long tab_hash(const char *s)
{
    unsigned long h;

    h = 5381;
    for (; *s; s++)
        h = (h * 33 + (unsigned char)*s) & 0xffffffffUL;
    return h;
}

void tab_init(struct table *t)
{
    int i;

    t->nslot = 1024;
    t->count = 0;
    t->slot = xmalloc(t->nslot * sizeof(struct tentry *));
    for (i = 0; i < t->nslot; i++)
        t->slot[i] = 0;
}

static void tab_grow(struct table *t)
{
    struct tentry **old, *e, *next;
    int oldn, i, k;

    old = t->slot;
    oldn = t->nslot;
    t->nslot = oldn * 4;
    t->slot = xmalloc(t->nslot * sizeof(struct tentry *));
    for (i = 0; i < t->nslot; i++)
        t->slot[i] = 0;
    for (i = 0; i < oldn; i++) {
        for (e = old[i]; e; e = next) {
            next = e->next;
            k = tab_hash(e->key) % t->nslot;
            e->next = t->slot[k];
            t->slot[k] = e;
        }
    }
    free(old);
}

const char *tab_get(struct table *t, const char *key)
{
    struct tentry *e;

    for (e = t->slot[tab_hash(key) % t->nslot]; e; e = e->next) {
        if (!strcmp(e->key, key))
            return e->val;
    }
    return 0;
}

void tab_set(struct table *t, const char *key, const char *val)
{
    struct tentry *e;
    int k;

    k = tab_hash(key) % t->nslot;
    for (e = t->slot[k]; e; e = e->next) {
        if (!strcmp(e->key, key)) {
            free(e->val);
            e->val = xstrdup(val);
            return;
        }
    }
    e = xmalloc(sizeof(*e));
    e->key = xstrdup(key);
    e->val = xstrdup(val);
    e->next = t->slot[k];
    t->slot[k] = e;
    t->count++;
    if (t->count > t->nslot * 2)
        tab_grow(t);
}

void tab_del(struct table *t, const char *key)
{
    struct tentry **pp, *e;

    pp = &t->slot[tab_hash(key) % t->nslot];
    for (e = *pp; e; e = *pp) {
        if (!strcmp(e->key, key)) {
            *pp = e->next;
            free(e->key);
            free(e->val);
            free(e);
            t->count--;
            return;
        }
        pp = &e->next;
    }
}

void tab_free(struct table *t)
{
    struct tentry *e, *next;
    int i;

    for (i = 0; i < t->nslot; i++) {
        for (e = t->slot[i]; e; e = next) {
            next = e->next;
            free(e->key);
            free(e->val);
            free(e);
        }
    }
    free(t->slot);
    t->slot = 0;
    t->count = 0;
}

void tab_keys(struct table *t, struct strlist *keys)
{
    struct tentry *e;
    int i;

    for (i = 0; i < t->nslot; i++) {
        for (e = t->slot[i]; e; e = e->next)
            sl_add(keys, e->key);
    }
    sl_sort(keys);
}

/* flat text: one "key<TAB>value" per line */
int tab_load(struct table *t, const char *path)
{
    char *data, *line, *next, *tab;
    int len;

    data = read_file(path, &len);
    if (!data)
        return 0;
    for (line = data; *line; line = next) {
        next = strchr(line, '\n');
        if (next) {
            *next = 0;
            next++;
        } else {
            next = line + strlen(line);
        }
        tab = strchr(line, '\t');
        if (tab) {
            *tab = 0;
            tab_set(t, line, tab + 1);
        }
    }
    free(data);
    return 1;
}

int tab_save(struct table *t, const char *path)
{
    struct strlist keys;
    struct buf b;
    int i, res;

    sl_init(&keys);
    tab_keys(t, &keys);
    buf_init(&b);
    for (i = 0; i < keys.n; i++) {
        if (strchr(keys.v[i], '\n') || strchr(keys.v[i], '\t'))
            continue;
        buf_add(&b, keys.v[i]);
        buf_addc(&b, '\t');
        buf_add(&b, tab_get(t, keys.v[i]));
        buf_addc(&b, '\n');
    }
    res = write_file(path, b.s, b.len);
    buf_free(&b);
    sl_free(&keys);
    return res;
}

/* ------------------------------------------------------------ strings */

int starts_with(const char *s, const char *prefix)
{
    return !strncmp(s, prefix, strlen(prefix));
}

int ends_with(const char *s, const char *suffix)
{
    int ls, lx;

    ls = strlen(s);
    lx = strlen(suffix);
    return ls >= lx && !strcmp(s + ls - lx, suffix);
}

int lower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 'a';
    return c;
}

int str_ieq_n(const char *a, const char *b, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        if (lower((unsigned char)a[i]) != lower((unsigned char)b[i]))
            return 0;
        if (!a[i])
            return 1;
    }
    return 1;
}

/* does the path end in ".ext" for one of exts (any letter case)? */
int has_ext(const char *path, struct strlist *exts)
{
    const char *dot;
    int i, n;

    dot = strrchr(path, '.');
    if (!dot || strchr(dot, '/'))
        return 0;
    dot++;
    n = strlen(dot);
    for (i = 0; i < exts->n; i++) {
        if ((int)strlen(exts->v[i]) == n && str_ieq_n(dot, exts->v[i], n))
            return 1;
    }
    return 0;
}

long long parse_ll(const char *s)
{
    long long v;

    v = 0;
    while (*s == ' ')
        s++;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v;
}

void human(long long n, char *out)
{
    static const char *unit[] = { "B", "KB", "MB", "GB", "TB" };
    double v;
    int i;

    v = (double)n;
    i = 0;
    while (v >= 1024 && i < 4) {
        v /= 1024;
        i++;
    }
    if (i == 0)
        sprintf(out, "%lld %s", n, unit[0]);
    else
        sprintf(out, "%.2f %s", v, unit[i]);
}

void hms(long long secs, char *out)
{
    if (secs < 0)
        secs = 0;
    sprintf(out, "%02lld:%02lld:%02lld", secs / 3600, secs % 3600 / 60,
        secs % 60);
}

void say(const char *a, const char *b, const char *c)
{
    fputs(a, stdout);
    if (b)
        fputs(b, stdout);
    if (c)
        fputs(c, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

/* set what a signal does: a handler, SIG_IGN or SIG_DFL */
void on_signal_do(int sig, void (*fn)(int))
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = fn;
    sigaction(sig, &sa, 0);
}

/* ------------------------------------------------------------ time */

long long now(void)
{
    long long t;

    t = time(0);
    return t;
}

/* "2026-11-14_09-31-05" in UTC; local time would reveal the time zone */
void utc_id(long long t, char *out)
{
    long long days, secs, y, era, doe, yoe, doy, mp, d, m;

    days = t / 86400;
    secs = t % 86400;
    /* civil date from day number (H. Hinnant's algorithm) */
    days += 719468;
    era = days / 146097;
    doe = days - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2)
        y++;
    sprintf(out, "%04lld-%02lld-%02lld_%02lld-%02lld-%02lld", y, m, d,
        secs / 3600, secs % 3600 / 60, secs % 60);
}

/* "2026-11-14_09-31-05" -> "2026-11-14 09:31" */
void nice_time(const char *id, char *out)
{
    if (strlen(id) < 16) {
        strcpy(out, id);
        return;
    }
    memcpy(out, id, 10);
    out[10] = ' ';
    memcpy(out + 11, id + 11, 5);
    out[13] = ':';
    out[16] = 0;
}

void sleep_ms(int ms)
{
    struct timeval tv;

    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    select(0, 0, 0, 0, &tv);
}

/* ------------------------------------------------------------ files */

int is_file(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0)
        return 0;
    return S_ISREG(st.st_mode);
}

int is_dir(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0)
        return 0;
    return S_ISDIR(st.st_mode);
}

int is_link(const char *path)
{
    struct stat st;

    if (lstat(path, &st) != 0)
        return 0;
    return S_ISLNK(st.st_mode);
}

long long file_size(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0)
        return -1;
    return (long long)st.st_size;
}

/* modification time in nanoseconds, -1 if missing */
long long file_mtime(const char *path)
{
    struct stat st;

    if (lstat(path, &st) != 0)
        return -1;
    return (long long)st.st_mtim.tv_sec * 1000000000LL +
        st.st_mtim.tv_nsec;
}

void set_mtime(const char *path, long long ns)
{
    struct timespec ts[2];

    ts[0].tv_sec = 0;
    ts[0].tv_nsec = UTIME_OMIT;
    ts[1].tv_sec = ns / 1000000000LL;
    ts[1].tv_nsec = ns % 1000000000LL;
    utimensat(AT_FDCWD, path, ts, 0);
}

int mkdirs(const char *path)
{
    char *p, *s;
    int res;

    p = xstrdup(path);
    for (s = p + 1; *s; s++) {
        if (*s == '/') {
            *s = 0;
            mkdir(p, 0755);
            *s = '/';
        }
    }
    mkdir(p, 0755);
    res = is_dir(p);
    free(p);
    return res;
}

/* whole file in memory, 0-terminated; 0 if it cannot be read */
char *read_file(const char *path, int *len)
{
    struct buf b;
    char tmp[65536];
    int fd, n;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    buf_init(&b);
    while ((n = read(fd, tmp, sizeof(tmp))) > 0)
        buf_addn(&b, tmp, n);
    close(fd);
    if (len)
        *len = b.len;
    return b.s;
}

static int write_all(int fd, const char *data, int len)
{
    int n;

    while (len > 0) {
        n = write(fd, data, len);
        if (n <= 0)
            return 0;
        data += n;
        len -= n;
    }
    return 1;
}

/* write through a temporary file, so a crash never leaves half a file */
int write_file(const char *path, const char *data, int len)
{
    char tail[32];
    char *tmp;
    int fd, ok;

    sprintf(tail, ".%ld.tmp", (long)getpid());
    tmp = str2(path, tail);
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        free(tmp);
        return 0;
    }
    ok = write_all(fd, data, len);
    if (ok)
        ok = fsync(fd) == 0;
    close(fd);
    if (ok)
        ok = rename(tmp, path) == 0;
    if (!ok)
        unlink(tmp);
    free(tmp);
    return ok;
}

int copy_file(const char *src, const char *dst)
{
    char tmp[65536];
    int in, out, n, ok;

    in = open(src, O_RDONLY);
    if (in < 0)
        return 0;
    out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) {
        close(in);
        return 0;
    }
    ok = 1;
    while (ok && (n = read(in, tmp, sizeof(tmp))) > 0)
        ok = write_all(out, tmp, n);
    close(in);
    close(out);
    return ok;
}

static void walk(const char *dir, struct strlist *out)
{
    struct strlist names;
    char *p;
    int i;

    sl_init(&names);
    list_dir(dir, &names);
    for (i = 0; i < names.n; i++) {
        p = path2(dir, names.v[i]);
        if (!is_link(p) && is_dir(p))
            walk(p, out);
        else
            sl_add(out, p);
        free(p);
    }
    sl_free(&names);
}

/* every file below dir (symlinks included as files), sorted per dir */
void list_files(const char *dir, struct strlist *out)
{
    walk(dir, out);
}

/* names in one directory, sorted, without "." and ".." */
void list_dir(const char *dir, struct strlist *out)
{
    DIR *dp;
    struct dirent *de;

    dp = opendir(dir);
    if (!dp)
        return;
    while ((de = readdir(dp)) != 0) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        sl_add(out, de->d_name);
    }
    closedir(dp);
    sl_sort(out);
}

int dir_empty(const char *dir)
{
    struct strlist names;
    int empty;

    sl_init(&names);
    list_dir(dir, &names);
    empty = names.n == 0;
    sl_free(&names);
    return empty;
}

/* remove empty directories below dir (not dir itself) */
void remove_empty_dirs(const char *dir)
{
    struct strlist names;
    char *p;
    int i;

    sl_init(&names);
    list_dir(dir, &names);
    for (i = 0; i < names.n; i++) {
        p = path2(dir, names.v[i]);
        if (!is_link(p) && is_dir(p)) {
            remove_empty_dirs(p);
            if (dir_empty(p))
                rmdir(p);
        }
        free(p);
    }
    sl_free(&names);
}

long long dir_size(const char *dir)
{
    struct strlist files;
    long long total, n;
    int i;

    sl_init(&files);
    list_files(dir, &files);
    total = 0;
    for (i = 0; i < files.n; i++) {
        n = file_size(files.v[i]);
        if (n > 0)
            total += n;
    }
    sl_free(&files);
    return total;
}
