/* data.c: what is in the diary, read from its plain text files.
 *
 *   days/2026-11-21                   your day
 *   events                            your plans (see below)
 *   people/mira/note                  "key: value" lines, "--", text
 *   people/mira/notes/appearance      more notes about her
 *   people/mira/days/2026-11-21       what you know of her that day
 *   people/mira/events                her plans
 *   people/mira/files/...             attachments (cleaned)
 *   people/mira/attached              "name<TAB>clean|text|as-is"
 *   places/bakery/...                 the same for places
 *
 * events, one plan per line (# starts a comment):
 *   2026-11-25 dentist at 10:00          one day
 *   2026-11-25..2026-11-28 riding trip   several days
 *   weekly mon,thu chess at 18:00        every week
 *   monthly 15 rent                      every month
 *   yearly 03-14 birthday                every year
 *
 * @mira in any text mentions a person, #bakery a place. Everything is
 * read again after each edit: it is small, and it lives in RAM. */
#define _XOPEN_SOURCE 700
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "seal.h"
#include "diary.h"
#include "data.h"

struct subj *subjs;
int nsubj;
struct dayent *days;
int ndays;
struct event *events;
int nevents;
char *pool;

static int cap_subj, cap_days, cap_events;
static long pool_used, pool_cap;
static char filebuf[FILE_MAX + 1];

/* ---------------------------------------------------------- memory */

static void *grow(void *old, long used, long size)
{
    void *p;

    p = malloc(size);
    if (!p) {
        warn("%s", "out of memory", 0);
        exit(1);
    }
    if (old) {
        memcpy(p, old, used);
        free(old);
    }
    return p;
}

static long pool_add(const char *s)
{
    long n, off;

    n = strlen(s) + 1;
    if (pool_used + n > pool_cap) {
        pool_cap = pool_cap * 2 + n + 4096;
        pool = grow(pool, pool_used, pool_cap);
    }
    off = pool_used;
    memcpy(pool + off, s, n);
    pool_used += n;
    return off;
}

/* ----------------------------------------------------------- text */

int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

int is_slug_char(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

int is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
        c == '\v';
}

/* the whole file (at most FILE_MAX bytes) into filebuf; its length,
 * or -1 */
long read_file(const char *path, char *buf, long size)
{
    long n;
    int fd;

    fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0)
        return -1;
    n = read_full(fd, buf, size);
    close(fd);
    if (n < 0)
        return -1;
    buf[n] = 0;
    return n;
}

/* copy at most size-1 bytes of s up to the end of its line, without
 * spaces at either end */
static void copy_line(char *dst, const char *s, long size)
{
    long n;

    while (*s == ' ' || *s == '\t')
        s++;
    n = 0;
    while (s[n] && s[n] != '\n' && n < size - 1)
        n++;
    memcpy(dst, s, n);
    while (n > 0 && is_space((unsigned char)dst[n - 1]))
        n--;
    dst[n] = 0;
}

/* the mentions in s, as " @mira #bakery " (lower case, each once) */
static void find_mentions(const char *s, char *out, long size)
{
    char tok[NAME_MAX_D + 4];
    const char *start;
    long n, used;
    int c;

    out[0] = ' ';
    out[1] = 0;
    used = 1;
    start = s;
    for (; *s; s++) {
        c = (unsigned char)*s;
        if (c != '@' && c != '#')
            continue;
        if (!is_slug_char((unsigned char)s[1]))
            continue;
        /* not inside a word (an e-mail address, a#b) */
        if (s > start && is_slug_char((unsigned char)s[-1]))
            continue;
        tok[0] = ' ';
        tok[1] = (char)c;
        n = 2;
        while (is_slug_char((unsigned char)s[n - 1]) && n < NAME_MAX_D) {
            tok[n] = (char)lower((unsigned char)s[n - 1]);
            n++;
        }
        while (n > 2 && (tok[n - 1] == '.' || tok[n - 1] == '-'))
            n--;
        tok[n] = ' ';
        tok[n + 1] = 0;
        if (strstr(out, tok))
            continue;
        if (used + n >= size - 1)
            break;
        memcpy(out + used, tok + 1, n + 1);
        used += n;
    }
}

static int count_words(const char *s)
{
    int n, in;

    n = 0;
    in = 0;
    for (; *s; s++) {
        if (is_space((unsigned char)*s)) {
            in = 0;
        } else if (!in) {
            in = 1;
            n++;
        }
    }
    return n;
}

/* ---------------------------------------------------------- dates */

int days_in_month(int y, int m)
{
    static const int dm[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31,
        30, 31 };

    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0))
        return 29;
    return dm[m - 1];
}

/* day number (days since 1970-01-01) of a date yyyymmdd, and back */
long day_number(int date)
{
    long y, m, d, era, yoe, doy, doe;

    y = date / 10000;
    m = date / 100 % 100;
    d = date % 100;
    if (m <= 2)
        y--;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int from_number(long z)
{
    long era, doe, yoe, y, doy, mp, d, m;

    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    if (m <= 2)
        y++;
    return (int)(y * 10000 + m * 100 + d);
}

int date_add(int date, long n)
{
    return from_number(day_number(date) + n);
}

/* 0 monday .. 6 sunday */
int date_wday(int date)
{
    long n;

    n = (day_number(date) + 3) % 7;
    if (n < 0)
        n += 7;
    return (int)n;
}

int date_today(void)
{
    struct tm *tm;
    time_t t;

    t = time(0);
    tm = localtime(&t);
    if (!tm)
        return 20000101;
    return (tm->tm_year + 1900) * 10000 + (tm->tm_mon + 1) * 100 +
        tm->tm_mday;
}

static int digits(const char *s, int n, int *v)
{
    int i;

    *v = 0;
    for (i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9')
            return -1;
        *v = *v * 10 + (s[i] - '0');
    }
    return 0;
}

/* "2026-11-21" at s: the date, or 0 */
int parse_date(const char *s)
{
    int y, m, d;

    if (digits(s, 4, &y) < 0 || s[4] != '-' || digits(s + 5, 2, &m) < 0 ||
        s[7] != '-' || digits(s + 8, 2, &d) < 0)
    {
        return 0;
    }
    if (m < 1 || m > 12 || d < 1 || d > days_in_month(y, m))
        return 0;
    return y * 10000 + m * 100 + d;
}

void format_date(char *s, int date)
{
    sprintf(s, "%04d-%02d-%02d", date / 10000, date / 100 % 100,
        date % 100);
}

const char *wday_name(int w)
{
    static const char *n[] = { "mon", "tue", "wed", "thu", "fri", "sat",
        "sun" };

    return n[w % 7];
}

const char *month_name(int m)
{
    static const char *n[] = { "january", "february", "march", "april",
        "may", "june", "july", "august", "september", "october",
        "november", "december" };

    return n[(m + 11) % 12];
}

/* --------------------------------------------------------- events */

static int event_line(const char *s, struct event *e)
{
    int w, i, v;

    memset(e, 0, sizeof *e);
    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '#' || *s == 0 || *s == '\n')
        return -1;
    if (!strncmp(s, "weekly ", 7)) {
        s += 7;
        for (;;) {
            for (w = 0; w < 7; w++) {
                if (!strncmp(s, wday_name(w), 3))
                    break;
            }
            if (w == 7)
                return -1;
            e->arg |= 1 << w;
            s += 3;
            if (*s != ',')
                break;
            s++;
        }
        e->kind = ev_weekly;
    } else if (!strncmp(s, "monthly ", 8)) {
        s += 8;
        v = 0;
        for (i = 0; s[i] >= '0' && s[i] <= '9' && i < 2; i++)
            v = v * 10 + (s[i] - '0');
        if (i == 0 || v < 1 || v > 31)
            return -1;
        s += i;
        e->kind = ev_monthly;
        e->arg = v;
    } else if (!strncmp(s, "yearly ", 7)) {
        s += 7;
        if (digits(s, 2, &v) < 0 || s[2] != '-' || digits(s + 3, 2, &i) < 0
            || v < 1 || v > 12 || i < 1 || i > 31)
        {
            return -1;
        }
        e->kind = ev_yearly;
        e->arg = v * 100 + i;
        s += 5;
    } else {
        e->from = parse_date(s);
        if (!e->from)
            return -1;
        s += 10;
        e->to = e->from;
        e->kind = ev_once;
        if (s[0] == '.' && s[1] == '.') {
            e->to = parse_date(s + 2);
            if (!e->to || e->to < e->from)
                return -1;
            s += 12;
            e->kind = ev_range;
        }
    }
    if (*s != ' ' && *s != '\t' && *s != '\n' && *s != 0)
        return -1;
    copy_line(e->text, s, sizeof e->text);
    find_mentions(e->text, e->ment, sizeof e->ment);
    return 0;
}

int event_on(const struct event *e, int date)
{
    int md;

    switch (e->kind) {
    case ev_once:
    case ev_range:
        return date >= e->from && date <= e->to;
    case ev_weekly:
        return (e->arg >> date_wday(date)) & 1;
    case ev_monthly:
        md = days_in_month(date / 10000, date / 100 % 100);
        return date % 100 == (e->arg > md ? md : e->arg);
    case ev_yearly:
        md = date % 10000;
        if (e->arg == 229 && days_in_month(date / 10000, 2) == 28)
            return md == 228;
        return md == e->arg;
    default:
        return 0;
    }
}

/* does the event belong to subject s, or mention it? */
int event_for(const struct event *e, int s)
{
    char tok[NAME_MAX_D + 4];

    if (e->owner == s)
        return 1;
    if (s == 0)
        return 0;
    sprintf(tok, " %c%.120s ", subjs[s].kind == sk_place ? '#' : '@',
        subjs[s].slug);
    return strstr(e->ment, tok) != 0;
}

static void load_events(int owner, const char *path)
{
    struct event e;
    const char *s;
    long n;
    int line, r;

    n = read_file(path, filebuf, FILE_MAX);
    if (n < 0)
        return;
    s = filebuf;
    line = 1;
    while (*s) {
        r = event_line(s, &e);
        if (r == 0) {
            e.owner = owner;
            e.line = line;
            if (nevents == cap_events) {
                cap_events = cap_events * 2 + 64;
                events = grow(events, nevents * (long)sizeof *events,
                    cap_events * (long)sizeof *events);
            }
            events[nevents] = e;
            nevents++;
        }
        while (*s && *s != '\n')
            s++;
        if (*s)
            s++;
        line++;
    }
}

/* ----------------------------------------------------------- days */

static void add_day(int owner, int date, const char *path)
{
    struct dayent *d;
    char ment[MENT_MAX];
    const char *s;
    long n;

    n = read_file(path, filebuf, FILE_MAX);
    if (n < 0)
        return;
    if (ndays == cap_days) {
        cap_days = cap_days * 2 + 256;
        days = grow(days, ndays * (long)sizeof *days,
            cap_days * (long)sizeof *days);
    }
    d = days + ndays;
    d->owner = owner;
    d->date = date;
    d->words = count_words(filebuf);
    s = filebuf;
    while (*s && is_space((unsigned char)*s))
        s++;
    copy_line(d->first, s, sizeof d->first);
    find_mentions(filebuf, ment, sizeof ment);
    d->ment = pool_add(ment);
    ndays++;
}

static void load_days(int owner, const char *dir)
{
    char path[PATH_SIZE];
    struct dirent *e;
    DIR *d;
    long n;
    int date;

    d = opendir(dir);
    if (!d)
        return;
    while ((e = readdir(d)) != 0) {
        n = strlen(e->d_name);
        if (n != 10)
            continue;
        date = parse_date(e->d_name);
        if (!date || strlen(dir) + 16 >= PATH_SIZE)
            continue;
        sprintf(path, "%.3000s/%s", dir, e->d_name);
        add_day(owner, date, path);
    }
    closedir(d);
}

/* newest first, then by owner: insertion sort is enough for a few
 * thousand days, which are mostly in order already */
static void sort_days(void)
{
    struct dayent t;
    int i, j;

    for (i = 1; i < ndays; i++) {
        t = days[i];
        j = i - 1;
        while (j >= 0 && (days[j].date < t.date ||
            (days[j].date == t.date && days[j].owner > t.owner)))
        {
            days[j + 1] = days[j];
            j--;
        }
        days[j + 1] = t;
    }
}

int find_day(int owner, int date)
{
    int i;

    for (i = 0; i < ndays; i++) {
        if (days[i].owner == owner && days[i].date == date)
            return i;
    }
    return -1;
}

/* does day i mention subject s? */
int day_mentions(int i, int s)
{
    char tok[NAME_MAX_D + 4];

    if (s <= 0)
        return 0;
    sprintf(tok, " %c%.120s ", subjs[s].kind == sk_place ? '#' : '@',
        subjs[s].slug);
    return strstr(pool + days[i].ment, tok) != 0;
}

/* ------------------------------------------------------- subjects */

/* the diary's own files used to end in .txt; take it off (once,
 * after opening). Attachments (files/) keep their names. */
static void untxt_dir(const char *dir, int all)
{
    static const char *fixed[] = { "note.txt", "events.txt",
        "attached.txt", 0 };
    char from[PATH_SIZE], to[PATH_SIZE];
    struct dirent *e;
    struct stat st;
    DIR *d;
    long n;
    int i, r;

    d = opendir(dir);
    while (d && (e = readdir(d)) != 0) {
        n = strlen(e->d_name);
        if (n < 5 || strcmp(e->d_name + n - 4, ".txt"))
            continue;
        for (i = 0; !all && fixed[i] && strcmp(fixed[i], e->d_name); i++)
            ;
        if (!all && !fixed[i])
            continue;
        sprintf(from, "%.3000s/%.255s", dir, e->d_name);
        sprintf(to, "%.3000s/%.*s", dir, (int)(n - 4), e->d_name);
        r = lstat(to, &st);
        if (r < 0)
            rename(from, to);
    }
    if (d)
        closedir(d);
}

void data_untxt(void)
{
    static const char *kinds[] = { "people", "places" };
    char dir[PATH_SIZE], sub[PATH_SIZE];
    struct dirent *e;
    DIR *d;
    int k;

    untxt_dir(tree_dir, 0);
    sprintf(dir, "%.3000s/days", tree_dir);
    untxt_dir(dir, 1);
    for (k = 0; k < 2; k++) {
        sprintf(dir, "%.3000s/%s", tree_dir, kinds[k]);
        d = opendir(dir);
        while (d && (e = readdir(d)) != 0) {
            if (e->d_name[0] == '.')
                continue;
            sprintf(sub, "%.3000s/%.255s", dir, e->d_name);
            untxt_dir(sub, 0);
            sprintf(sub, "%.3000s/%.255s/days", dir, e->d_name);
            untxt_dir(sub, 1);
            sprintf(sub, "%.3000s/%.255s/notes", dir, e->d_name);
            untxt_dir(sub, 1);
        }
        if (d)
            closedir(d);
    }
}

void subj_dir(char *out, int s)
{
    if (s == 0)
        strcpy(out, tree_dir);
    else
        sprintf(out, "%.3000s/%s/%s", tree_dir,
            subjs[s].kind == sk_place ? "places" : "people", subjs[s].slug);
}

static void read_note(struct subj *p, const char *dir)
{
    char path[PATH_SIZE], key[64];
    const char *s, *colon;
    long n, k;

    sprintf(path, "%.3000s/note", dir);
    p->nfields = 0;
    p->summary[0] = 0;
    n = read_file(path, filebuf, FILE_MAX);
    if (n < 0)
        return;
    s = filebuf;
    /* "key: value" lines up to "--" (or the first line that isn't
     * one); then the first line of text is the summary */
    while (*s) {
        if (s[0] == '-' && s[1] == '-') {
            while (*s && *s != '\n')
                s++;
            break;
        }
        colon = s;
        k = 0;
        while (is_slug_char((unsigned char)*colon) && k < 63) {
            key[k] = *colon;
            colon++;
            k++;
        }
        key[k] = 0;
        if (k == 0 || *colon != ':')
            break;
        if (!strcmp(key, "name")) {
            copy_line(p->name, colon + 1, sizeof p->name);
        } else if (p->nfields < FIELDS_MAX) {
            sprintf(p->fields[p->nfields], "%.20s", key);
            copy_line(p->values[p->nfields], colon + 1,
                sizeof p->values[0]);
            p->nfields++;
        }
        while (*s && *s != '\n')
            s++;
        if (*s)
            s++;
    }
    while (*s && is_space((unsigned char)*s))
        s++;
    copy_line(p->summary, s, sizeof p->summary);
}

static int count_dir(const char *dir)
{
    struct dirent *e;
    DIR *d;
    int n;

    d = opendir(dir);
    if (!d)
        return 0;
    n = 0;
    while ((e = readdir(d)) != 0) {
        if (e->d_name[0] != '.')
            n++;
    }
    closedir(d);
    return n;
}

static void load_subjects(int kind, const char *top)
{
    char dir[PATH_SIZE], sub[PATH_SIZE];
    struct dirent *e;
    struct subj *p;
    struct stat st;
    DIR *d;
    int r, idx;

    sprintf(dir, "%.3000s/%s", tree_dir, top);
    d = opendir(dir);
    if (!d)
        return;
    while ((e = readdir(d)) != 0) {
        if (e->d_name[0] == '.' || strlen(e->d_name) >= NAME_MAX_D)
            continue;
        sprintf(sub, "%.3000s/%s", dir, e->d_name);
        r = stat(sub, &st);
        if (r < 0 || !S_ISDIR(st.st_mode))
            continue;
        if (nsubj == cap_subj) {
            cap_subj = cap_subj * 2 + 32;
            subjs = grow(subjs, nsubj * (long)sizeof *subjs,
                cap_subj * (long)sizeof *subjs);
        }
        idx = nsubj;
        p = subjs + idx;
        memset(p, 0, sizeof *p);
        p->kind = kind;
        strcpy(p->slug, e->d_name);
        read_note(p, sub);
        strcat(sub, "/files");
        p->nfiles = count_dir(sub);
        nsubj++;
    }
    closedir(d);
}

static void sort_subjects(void)
{
    struct subj t;
    int i, j;

    for (i = 2; i < nsubj; i++) {
        t = subjs[i];
        j = i - 1;
        while (j >= 1 && (subjs[j].kind > t.kind || (subjs[j].kind ==
            t.kind && strcmp(subjs[j].slug, t.slug) > 0)))
        {
            subjs[j + 1] = subjs[j];
            j--;
        }
        subjs[j + 1] = t;
    }
}

int find_subj(int kind, const char *slug)
{
    int i;

    for (i = 1; i < nsubj; i++) {
        if (subjs[i].kind == kind && !strcmp(subjs[i].slug, slug))
            return i;
    }
    return -1;
}

void data_load(void)
{
    char dir[PATH_SIZE];
    int i, s;

    nsubj = 0;
    ndays = 0;
    nevents = 0;
    pool_used = 0;
    if (cap_subj == 0) {
        cap_subj = 32;
        subjs = grow(0, 0, cap_subj * (long)sizeof *subjs);
    }
    memset(subjs, 0, sizeof *subjs);
    subjs[0].kind = sk_me;
    strcpy(subjs[0].slug, "me");
    strcpy(subjs[0].name, "you");
    nsubj = 1;
    load_subjects(sk_person, "people");
    load_subjects(sk_place, "places");
    sort_subjects();
    for (i = 0; i < nsubj; i++) {
        subj_dir(dir, i);
        if (strlen(dir) + 20 >= PATH_SIZE)
            continue;
        strcat(dir, "/days");
        load_days(i, dir);
        subj_dir(dir, i);
        strcat(dir, "/events");
        load_events(i, dir);
    }
    sort_days();
    /* when each was last mentioned, and how often */
    for (i = 0; i < ndays; i++) {
        for (s = 1; s < nsubj; s++) {
            if (days[i].owner == s || day_mentions(i, s)) {
                subjs[s].seen++;
                if (days[i].date > subjs[s].last)
                    subjs[s].last = days[i].date;
            }
        }
    }
}
