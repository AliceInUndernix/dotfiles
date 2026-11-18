/* steno: a steno engine for a Gemini PR keyboard (the Moonlander's
 * steno layer), doing what Plover does with Dotterel's dictionaries,
 * and typing through X. See README.
 *
 *   steno [-v] [-c CONFIG] [-p PORT]    run: read the keyboard, type
 *   steno -T | -S | -TS                 tape, suggestions (view.c)
 *   steno -l X | -a O T | -A            the tools (tools.c)
 *   steno -t [-c CONFIG]                strokes from stdin, print what
 *                                       would be typed (for tests)
 *   steno -h                            help
 *
 * A port of Plover's code: GNU GPL version 2 or later (README). */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "steno.h"

#define CMD_MAX 32
#define LINE_MAX_LEN 2048

int verbose;

static const char *prog = "steno";
static char port_opt[1024] = "auto";
static char dict_dir[1024];
static char data_dir[1024];
static char cmd_name[CMD_MAX][64];
static char cmd_line[CMD_MAX][LINE_MAX_LEN];
static int ncmds;
static int reset_pending;
static volatile int stop_now;
static char cfg_path[1024];
static char terminal[LINE_MAX_LEN] = "st -e";  /* opens the viewers */

int atoi_small(const char *s);

void die(int code)
{
    tui_stop();
    exit(code);
}


/* --- small helpers ------------------------------------------------ */

void say(const char *fmt, const char *a)
{
    char m[2400];

    if (tui_on) {
        /* the status screen has the messages */
        sprintf(m, "%.200s", fmt);
        if (a)
            sprintf(m, fmt, a);
        tui_log(m);
        return;
    }
    fprintf(stderr, "%s: ", prog);
    fprintf(stderr, fmt, a ? a : "");
    fputc('\n', stderr);
}

void scopy(char *dst, const char *src, int size)
{
    int i;

    for (i = 0; i < size - 1 && src[i]; i++)
        dst[i] = src[i];
    for (; i < size; i++)
        dst[i] = 0;
}

void scat(char *dst, const char *src, int size)
{
    int n, i;

    n = strlen(dst);
    for (i = 0; n + i < size - 1 && src[i]; i++)
        dst[n + i] = src[i];
    dst[n + i] = 0;
}

int up_ascii(int c)
{
    return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
}

int low_ascii(int c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

/* Case for the letters that have it in Latin-1, Latin Extended-A,
 * Greek and Cyrillic (Plover uses Python's, which knows them all).
 * Each pair takes the same bytes in UTF-8, so text keeps its length. */
static long case_of(long c, int up)
{
    if (c < 0x80) {
        if (up)
            return c >= 'a' && c <= 'z' ? c - 32 : c;
        return c >= 'A' && c <= 'Z' ? c + 32 : c;
    }
    if (c >= 0xc0 && c <= 0xfe && c != 0xd7 && c != 0xf7 && c != 0xdf) {
        if (up)
            return c >= 0xe0 ? c - 32 : c;
        return c <= 0xde ? c + 32 : c;
    }
    if (c == 0xff || c == 0x178)
        return up ? 0x178 : 0xff;
    if ((c >= 0x100 && c <= 0x137) || (c >= 0x14a && c <= 0x177))
        return up ? (c & ~1L) : (c | 1);
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17e))
        return up ? (c & 1 ? c : c - 1) : (c & 1 ? c + 1 : c);
    if (c >= 0x391 && c <= 0x3a9 && c != 0x3a2)
        return up ? c : c + 32;
    if (c >= 0x3b1 && c <= 0x3c9)
        return up ? (c == 0x3c2 ? 0x3a3 : c - 32) : c;
    switch (c) {
    case 0x386: return up ? c : 0x3ac;
    case 0x388: return up ? c : 0x3ad;
    case 0x389: return up ? c : 0x3ae;
    case 0x38a: return up ? c : 0x3af;
    case 0x38c: return up ? c : 0x3cc;
    case 0x38e: return up ? c : 0x3cd;
    case 0x38f: return up ? c : 0x3ce;
    case 0x3ac: return up ? 0x386 : c;
    case 0x3ad: return up ? 0x388 : c;
    case 0x3ae: return up ? 0x389 : c;
    case 0x3af: return up ? 0x38a : c;
    case 0x3cc: return up ? 0x38c : c;
    case 0x3cd: return up ? 0x38e : c;
    case 0x3ce: return up ? 0x38f : c;
    }
    if (c >= 0x400 && c <= 0x42f)
        return up ? c : (c >= 0x410 ? c + 32 : c + 80);
    if (c >= 0x430 && c <= 0x45f)
        return up ? (c <= 0x44f ? c - 32 : c - 80) : c;
    return c;
}

/* Change the case of the character at s; returns its length. */
int case_at(char *s, int n, int up)
{
    long c, d;
    int k;
    unsigned char *u;

    k = u8get(s, n, &c);
    if (!k)
        return 0;
    d = case_of(c, up);
    if (d == c)
        return k;
    u = (unsigned char *)s;
    if (k == 1) {
        u[0] = (unsigned char)d;
    } else if (k == 2 && d < 0x800 && d >= 0x80) {
        u[0] = (unsigned char)(0xc0 | (d >> 6));
        u[1] = (unsigned char)(0x80 | (d & 0x3f));
    }
    return k;
}

/* One UTF-8 character: its length (0 at the end), *c its number. */
int u8get(const char *s, int n, long *c)
{
    const unsigned char *u;
    int len, i;
    long v;

    if (n <= 0)
        return 0;
    u = (const unsigned char *)s;
    if (u[0] < 0x80) {
        len = 1;
        v = u[0];
    } else if ((u[0] & 0xe0) == 0xc0) {
        len = 2;
        v = u[0] & 0x1f;
    } else if ((u[0] & 0xf0) == 0xe0) {
        len = 3;
        v = u[0] & 0x0f;
    } else if ((u[0] & 0xf8) == 0xf0) {
        len = 4;
        v = u[0] & 0x07;
    } else {
        len = 1;
        v = u[0];
    }
    if (len > n)
        len = n;
    for (i = 1; i < len; i++) {
        if ((u[i] & 0xc0) != 0x80) {
            len = i;
            break;
        }
        v = (v << 6) | (u[i] & 0x3f);
    }
    if (c)
        *c = v;
    return len;
}

int u8len(const char *s, int n)
{
    int i, c;

    c = 0;
    for (i = 0; i < n; i++)
        if (((unsigned char)s[i] & 0xc0) != 0x80)
            c++;
    return c;
}

/* How many bytes the last `chars` characters of s[0..n) take. */
int u8back(const char *s, int n, int chars)
{
    int i;

    i = n;
    while (i > 0 && chars > 0) {
        i--;
        if (((unsigned char)s[i] & 0xc0) != 0x80)
            chars--;
    }
    return n - i;
}

int is_space_at(const char *s, int n)
{
    long c;
    int k;

    if (n <= 0)
        return 0;
    if (s[0] == ' ' || (s[0] >= '\t' && s[0] <= '\r') ||
        (s[0] >= 0x1c && s[0] <= 0x1f))
    {
        return 1;
    }
    if (!((unsigned char)s[0] & 0x80))
        return 0;
    k = u8get(s, n, &c);
    if (c == 0x85 || c == 0xa0 || c == 0x1680 || (c >= 0x2000 &&
        c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f ||
        c == 0x205f || c == 0x3000)
    {
        return k;
    }
    return 0;
}

/* A character of \w: letters, digits, _; most non-ASCII letters. */
int is_wordc_at(const char *s, int n)
{
    long c;
    int k;
    unsigned char b;

    if (n <= 0)
        return 0;
    b = (unsigned char)s[0];
    if (b < 0x80)
        return ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') ||
            (b >= '0' && b <= '9') || b == '_') ? 1 : 0;
    k = u8get(s, n, &c);
    if (is_space_at(s, n))
        return 0;
    /* punctuation and symbols */
    if ((c >= 0x80 && c <= 0xbf && c != 0xaa && c != 0xb2 && c != 0xb3 &&
        c != 0xb5 && c != 0xb9 && c != 0xba && c != 0xbc && c != 0xbd &&
        c != 0xbe) || c == 0xd7 || c == 0xf7 ||
        (c >= 0x2000 && c <= 0x2bff) || (c >= 0x2e00 && c <= 0x2e7f) ||
        (c >= 0x3000 && c <= 0x303f) || (c >= 0xfe30 && c <= 0xfe4f) ||
        (c >= 0xff00 && c <= 0xff0f) || (c >= 0xff1a && c <= 0xff20) ||
        (c >= 0x1f000 && c <= 0x1faff))
    {
        return 0;
    }
    return k;
}

char *read_file(const char *path, long *size)
{
    int fd;
    long n, cap, k;
    char *p, *q;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    cap = 65536;
    p = malloc(cap + 1);
    n = 0;
    while (p) {
        if (n == cap) {
            q = malloc(cap * 2 + 1);
            if (q)
                memcpy(q, p, n);
            free(p);
            p = q;
            cap *= 2;
            if (!p)
                break;
        }
        k = read(fd, p + n, cap - n);
        if (k < 0 && errno == EINTR)
            continue;
        if (k <= 0)
            break;
        n += k;
    }
    close(fd);
    if (!p)
        return 0;
    p[n] = 0;
    *size = n;
    return p;
}

const char *config_path(const char *name)
{
    static char p[2][1024];
    static int w;
    const char *h;

    w = !w;
    h = getenv("HOME");
    if (name[0] == '~' && (name[1] == '/' || !name[1]) && h)
        sprintf(p[w], "%.500s%.500s", h, name + 1);
    else
        scopy(p[w], name, sizeof(p[w]));
    return p[w];
}

/* NAME in the dictionaries directory (a path as it is), static */
const char *dict_dir_file(const char *name)
{
    static char p[2048];

    if (name[0] == '/' || name[0] == '~' || !dict_dir[0])
        scopy(p, config_path(name), sizeof(p));
    else
        sprintf(p, "%.1000s/%.1000s", dict_dir, name);
    return p;
}

const char *dotterel_data(const char *name)
{
    static char p[1100];

    if (data_dir[0])
        sprintf(p, "%.1000s/%s", data_dir, name);
    else
        sprintf(p, "%.1000s/../data/%s", dict_dir[0] ? dict_dir : ".",
            name);
    return p;
}

/* A JSON object of strings (uk_to_us.json) as "k\0v\0k\0v\0". */
char *json_pairs(const char *path, long *count);

/* UK<tab>US lines (uk_to_us.txt) as "k\0v\0k\0v\0" */
char *pairs_read(const char *path, long *count)
{
    char *f, *o;
    long size, i, n;
    int field;

    f = read_file(path, &size);
    if (!f)
        return 0;
    o = malloc(size + 2);
    if (!o) {
        free(f);
        return 0;
    }
    n = 0;
    field = 0;
    *count = 0;
    for (i = 0; i < size; i++) {
        if (f[i] == '\r')
            continue;
        if (f[i] == '\t' && field == 0) {
            o[n] = 0;
            n++;
            field = 1;
        } else if (f[i] == '\n') {
            if (field == 1) {
                o[n] = 0;
                n++;
                (*count)++;
            } else {
                /* a line without a tab: dropped */
                while (n > 0 && o[n - 1] != 0)
                    n--;
            }
            field = 0;
        } else {
            o[n] = f[i];
            n++;
        }
    }
    if (field == 1) {
        o[n] = 0;
        n++;
        (*count)++;
    }
    free(f);
    return o;
}

/* uk_to_us.json -> uk_to_us.txt, for steno -C */
static int convert_pairs(const char *in, const char *outdir, char *made,
    int size)
{
    char *p, *s_ptr, tmp[1400];
    long n, i;
    FILE *f;
    int r;

    p = json_pairs(in, &n);
    if (!p) {
        say("can't read %s", in);
        return -1;
    }
    scopy(made, outdir, size - 32);
    scat(made, "/uk_to_us.txt", size);
    sprintf(tmp, "%.1300s.new", made);
    f = fopen(tmp, "w");
    if (!f) {
        free(p);
        say("can't write %s", tmp);
        return -1;
    }
    s_ptr = p;
    for (i = 0; i < n; i++) {
        fputs(s_ptr, f);
        fputc('\t', f);
        s_ptr += strlen(s_ptr) + 1;
        fputs(s_ptr, f);
        fputc('\n', f);
        s_ptr += strlen(s_ptr) + 1;
    }
    free(p);
    r = fclose(f);
    if (r == 0)
        r = rename(tmp, made);
    if (r != 0) {
        unlink(tmp);
        say("can't write %s", made);
        return -1;
    }
    return 0;
}

/* steno -C DIR FILE...: Plover's files in steno's format, into DIR */
static int convert_files(char **files, int n)
{
    char made[1400];
    const char *b;
    int i, r, bad;

    bad = 0;
    /* the directory, and those above it, as mkdir -p */
    scopy(made, files[0], sizeof(made));
    for (i = 1; made[i]; i++) {
        if (made[i] == '/') {
            made[i] = 0;
            mkdir(made, 0755);
            made[i] = '/';
        }
    }
    mkdir(made, 0755);
    for (i = 1; i < n; i++) {
        b = strrchr(files[i], '/');
        b = b ? b + 1 : files[i];
        if (!strcmp(b, "uk_to_us.json"))
            r = convert_pairs(files[i], files[0], made, sizeof(made));
        else
            r = dict_convert(files[i], files[0], made, sizeof(made));
        if (r < 0)
            bad = 1;
        else if (r == 0)
            printf("%s -> %s\n", files[i], made);
        else
            printf("%s: built into steno, nothing made\n", files[i]);
    }
    return bad;
}

char *json_pairs(const char *path, long *count)
{
    char *f, *o;
    long size, i, n;
    int in, k, which;
    long u;

    f = read_file(path, &size);
    if (!f)
        return 0;
    o = malloc(size + 2);
    if (!o) {
        free(f);
        return 0;
    }
    n = 0;
    in = 0;
    which = 0;
    *count = 0;
    for (i = 0; i < size; i++) {
        if (!in) {
            if (f[i] == '"')
                in = 1;
            continue;
        }
        if (f[i] == '"') {
            o[n] = 0;
            n++;
            in = 0;
            which = !which;
            if (!which)
                (*count)++;
            continue;
        }
        if (f[i] == '\\' && i + 1 < size) {
            i++;
            if (f[i] == 'u' && i + 4 < size) {
                u = 0;
                for (k = 1; k <= 4; k++) {
                    int c = f[i + k];
                    u = u * 16 + (c >= 'a' ? c - 'a' + 10 : c >= 'A' ?
                        c - 'A' + 10 : c - '0');
                }
                i += 4;
                if (u < 0x80) {
                    o[n] = (char)u;
                    n++;
                } else if (u < 0x800) {
                    o[n] = (char)(0xc0 | (u >> 6));
                    o[n + 1] = (char)(0x80 | (u & 0x3f));
                    n += 2;
                } else {
                    o[n] = (char)(0xe0 | (u >> 12));
                    o[n + 1] = (char)(0x80 | ((u >> 6) & 0x3f));
                    o[n + 2] = (char)(0x80 | (u & 0x3f));
                    n += 3;
                }
                continue;
            }
        }
        o[n] = f[i];
        n++;
    }
    free(f);
    return o;
}

/* --- the settings file ------------------------------------------- */

static int yes(const char *v)
{
    return !strcmp(v, "yes") || !strcmp(v, "on") || !strcmp(v, "1");
}

static int make_empty_dict(const char *path)
{
    int fd, r;

    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0)
        return errno == EEXIST ? 0 : -1;
    r = close(fd);             /* an empty .dict: no entries */
    return r;
}

/* "NAME" or "NAME off", from a dict or user line or from the status
 * screen: read it, and keep how it was named for config_save. A user
 * dictionary is made empty when it isn't there yet. */
int add_dict_line(const char *val, int user)
{
    char name[1024], p[2048];
    int n, off, r, i;

    scopy(name, val, sizeof(name));
    n = strlen(name);
    off = 0;
    if (n > 4 && !strcmp(name + n - 4, " off")) {
        off = 1;
        n -= 4;
        while (n > 0 && (name[n - 1] == ' ' || name[n - 1] == '\t'))
            n--;
        name[n] = 0;
    }
    if (!name[0])
        return -1;
    if (!user && dict_is_builtin(name))
        scopy(p, name, sizeof(p));
    else
        scopy(p, dict_dir_file(name), sizeof(p));
    for (i = 0; i < dict_count(); i++) {
        if (!strcmp(dict_path(i), p)) {
            say("already in the list: %s", p);
            return -1;
        }
    }
    if (user) {
        r = make_empty_dict(p);
        if (r < 0) {
            say("can't make %s", p);
            return -1;
        }
        scopy(user_dict, p, sizeof(user_dict));
    }
    r = dict_add(p);
    if (r < 0)
        return -1;
    dict_set_origin(dict_count() - 1, name, user);
    if (off)
        dict_set_enabled(dict_count() - 1, 0);
    return 0;
}

/* Write the dictionary list back into the settings file, as it is
 * now: the dict and user lines are replaced by the list, where the
 * first of them was; everything else stays. A link (the repo's file)
 * stays a link: the file it points to is the one written. */
int config_save(void)
{
    char *f, *o, *line, *next, *k, target[2048], tmp[2100], *slash;
    long size, cap;
    int i, fd, r, w, done, user, is_dict;
    struct stat sb;
    const char *name;

    f = read_file(cfg_path, &size);
    if (!f) {
        say("can't read %s", cfg_path);
        return -1;
    }
    cap = size + (long)dict_count() * 1100 + 16;
    o = malloc(cap);
    if (!o) {
        free(f);
        return -1;
    }
    o[0] = 0;
    done = 0;
    for (line = f; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next) {
            *next = 0;
            next++;
        }
        k = line;
        while (*k == ' ' || *k == '\t')
            k++;
        is_dict = (!strncmp(k, "dict", 4) || !strncmp(k, "user", 4)) &&
            (k[4] == ' ' || k[4] == '\t');
        if (is_dict && !done) {
            for (i = 0; i < dict_count(); i++) {
                name = dict_name(i, &user);
                strcat(o, user ? "user " : "dict ");
                strcat(o, name);
                if (!dict_enabled(i))
                    strcat(o, " off");
                strcat(o, "\n");
            }
            done = 1;
        }
        if (is_dict)
            continue;
        strcat(o, line);
        strcat(o, "\n");
    }
    free(f);
    scopy(target, cfg_path, sizeof(target));
    r = lstat(cfg_path, &sb);
    if (r == 0 && S_ISLNK(sb.st_mode)) {
        r = readlink(cfg_path, tmp, sizeof(tmp) - 1);
        if (r > 0) {
            tmp[r] = 0;
            if (tmp[0] == '/') {
                scopy(target, tmp, sizeof(target));
            } else {
                scopy(target, cfg_path, sizeof(target));
                slash = strrchr(target, '/');
                if (slash)
                    slash[1] = 0;
                else
                    target[0] = 0;
                scat(target, tmp, sizeof(target));
            }
        }
    }
    sprintf(tmp, "%.2000s.new", target);
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        say("can't write %s", tmp);
        free(o);
        return -1;
    }
    size = strlen(o);
    r = write(fd, o, size);
    free(o);
    w = close(fd);
    if (r != size || w < 0) {
        unlink(tmp);
        say("can't write %s", tmp);
        return -1;
    }
    r = rename(tmp, target);
    if (r < 0) {
        unlink(tmp);
        say("can't replace %s", target);
        return -1;
    }
    return 0;
}

static int read_config(const char *path, int must)
{
    int res;
    char *f, *line, *next, *key, *val, *e;
    long size;
    int ln, bad, n;

    f = read_file(path, &size);
    if (!f) {
        if (must)
            say("can't read %s", path);
        return must ? -1 : 1;
    }
    bad = 0;
    ln = 0;
    n = 0;
    for (line = f; line && *line; line = next) {
        ln++;
        next = strchr(line, '\n');
        if (next) {
            *next = 0;
            next++;
        }
        while (*line == ' ' || *line == '\t')
            line++;
        if (!*line || *line == '#')
            continue;
        e = line + strlen(line);
        while (e > line && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
            e--;
        *e = 0;
        key = line;
        val = line;
        while (*val && *val != ' ' && *val != '\t')
            val++;
        if (*val) {
            *val = 0;
            val++;
            while (*val == ' ' || *val == '\t')
                val++;
        }
        if (!strcmp(key, "port")) {
            if (!strcmp(port_opt, "auto"))
                scopy(port_opt, config_path(val), sizeof(port_opt));
        } else if (!strcmp(key, "dictionaries")) {
            scopy(dict_dir, config_path(val), sizeof(dict_dir));
        } else if (!strcmp(key, "data")) {
            scopy(data_dir, config_path(val), sizeof(data_dir));
        } else if (!strcmp(key, "dict") || !strcmp(key, "user")) {
            res = add_dict_line(val, key[0] == 'u');
            if (res < 0)
                bad = 1;
            n++;
        } else if (!strcmp(key, "undo")) {
            undo_levels = atoi_small(val);
            if (undo_levels < 1)
                undo_levels = 100;
        } else if (!strcmp(key, "start_capitalized")) {
            start_capitalized = yes(val);
        } else if (!strcmp(key, "start_attached")) {
            start_attached = yes(val);
        } else if (!strcmp(key, "terminal")) {
            scopy(terminal, val, sizeof(terminal));
        } else if (!strcmp(key, "command")) {
            char *sp;
            sp = val;
            while (*sp && *sp != ' ' && *sp != '\t')
                sp++;
            if (!*sp || ncmds == CMD_MAX) {
                fprintf(stderr, "%s: %s:%d: command NAME SHELL-COMMAND\n",
                    prog, path, ln);
                bad = 1;
                continue;
            }
            *sp = 0;
            sp++;
            while (*sp == ' ' || *sp == '\t')
                sp++;
            scopy(cmd_name[ncmds], val, sizeof(cmd_name[0]));
            for (e = cmd_name[ncmds]; *e; e++)
                *e = (char)low_ascii((unsigned char)*e);
            scopy(cmd_line[ncmds], sp, sizeof(cmd_line[0]));
            ncmds++;
        } else {
            fprintf(stderr, "%s: %s:%d: unknown setting %s\n", prog, path,
                ln, key);
            bad = 1;
        }
    }
    free(f);
    if (!n) {
        say("no dictionaries in %s (dict NAME lines)", path);
        bad = 1;
    }
    return bad ? -1 : 0;
}

/* --- commands ({PLOVER:NAME}) -------------------------------------- */

static void run_shell(const char *line)
{
    pid_t pid;

    pid = fork();
    if (pid == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", line, (char *)0);
        _exit(127);
    }
}

/* t, s and b on the status screen: steno -T, -S or -TS in a new
 * terminal window (the "terminal" setting, st -e by default) */
void open_viewer(const char *flag)
{
    char exe[1024], line[LINE_MAX_LEN + 2200];
    int n;

    n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0 || strchr(cfg_path, '\'') || memchr(exe, '\'', n)) {
        say("can't open the viewer", 0);
        return;
    }
    exe[n] = 0;
    sprintf(line, "exec %s '%s' -c '%s' %s", terminal, exe, cfg_path,
        flag);
    run_shell(line);
}

/* o on the status screen: as PLOVER:TOGGLE */
void output_toggle(void)
{
    run_command("toggle");
    if (reset_pending) {
        reset_pending = 0;
        translate_reset();
    }
}

void run_command(const char *cmd)
{
    char name[64];
    int i;

    for (i = 0; cmd[i] && cmd[i] != ':' && i < 63; i++)
        name[i] = (char)low_ascii((unsigned char)cmd[i]);
    name[i] = 0;
    if (out_mode) {
        printf("C %s\n", cmd);
        if (!strcmp(name, "dotterel_raw"))
            dict_raw_toggle();
        return;
    }
    if (!strcmp(name, "toggle") || !strcmp(name, "resume") ||
        !strcmp(name, "suspend"))
    {
        i = name[0] == 't' ? !output_on : name[0] == 'r';
        if (i != output_on) {
            output_on = i;
            reset_pending = 1;
            say(output_on ? "on" : "off (stroke PLOVER:TOGGLE again)", 0);
        }
        return;
    }
    if (!strcmp(name, "quit")) {
        stop_now = 1;
        return;
    }
    if (!output_on)
        return;
    if (!strcmp(name, "dotterel_raw")) {
        i = dict_raw_toggle();
        if (i < 0)
            say("raw mode needs dotterel-raw in the list", 0);
        else if (verbose)
            say(i ? "raw mode on" : "raw mode off", 0);
        return;
    }
    for (i = 0; i < ncmds; i++)
        if (!strcmp(cmd_name[i], name)) {
            run_shell(cmd_line[i]);
            return;
        }
    if (verbose)
        say("no command set for %s (a command line in the settings)",
            name);
}

/* --- the keyboard: Gemini PR ---------------------------------------- *
 * Six bytes a stroke; the first has its top bit set. Each byte's other
 * seven bits are keys, in this order. */

static const char *const gemini_keys[42] = {
    "Fn", "#1", "#2", "#3", "#4", "#5", "#6",
    "S1-", "S2-", "T-", "K-", "P-", "W-", "H-",
    "R-", "A-", "O-", "*1", "*2", "res1", "res2",
    "pwr", "*3", "*4", "-E", "-U", "-F", "-R",
    "-P", "-B", "-L", "-G", "-T", "-S", "-D",
    "#7", "#8", "#9", "#A", "#B", "#C", "-Z"
};

/* Dotterel's map: which Gemini key is which steno key (-1: none) */
static int gemini_to_key(const char *g)
{
    static const char *const names[NUM_KEYS] = {
        "#", "S-", "T-", "K-", "P-", "W-", "H-", "R-", "A-", "O-", "*",
        "-E", "-U", "-F", "-R", "-P", "-B", "-L", "-G", "-T", "-S", "-D",
        "-Z"
    };
    int i;

    if (!strcmp(g, "#1"))
        return 0;
    if (!strcmp(g, "S1-") || !strcmp(g, "S2-"))
        return 1;
    if (g[0] == '*')
        return 10;
    if (g[0] == '#')
        return -1;
    for (i = 0; i < NUM_KEYS; i++)
        if (!strcmp(g, names[i]))
            return i;
    return -1;
}

static int gemini_map[42];

static long gemini_stroke(const unsigned char *p)
{
    long st;
    int i, j, k;

    st = 0;
    for (i = 0; i < 6; i++)
        for (j = 1; j < 8; j++)
            if (p[i] & (0x80 >> j)) {
                k = gemini_map[i * 7 + j - 1];
                if (k >= 0)
                    st |= 1L << k;
            }
    return st;
}

static int serial_fd = -1;
static char serial_name[1024];
static unsigned char packet[6];
static int npacket;

static int open_port(const char *path)
{
    int fd, r;
    struct termios t;

    fd = open(path, O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
        return -1;
    r = tcgetattr(fd, &t);
    if (r == 0) {
        t.c_iflag = 0;
        t.c_oflag = 0;
        t.c_lflag = 0;
        t.c_cflag = CS8 | CREAD | CLOCAL;
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
        cfsetispeed(&t, B9600);
        cfsetospeed(&t, B9600);
        tcsetattr(fd, TCSANOW, &t);
        tcflush(fd, TCIFLUSH);
    }
    return fd;
}

static int last_open_error;
static int wait_once;          /* after losing it: a second first */

/* for the status screen: 1 and the port while the keyboard is there;
 * else 0, and what is wrong in why */
int keyboard_state(const char **port, const char **why)
{
    *port = serial_fd >= 0 ? serial_name :
        strcmp(port_opt, "auto") ? port_opt : "/dev/ttyACM0-9";
    *why = last_open_error == EACCES ?
        "no permission (group dialout?)" : "not plugged in";
    return serial_fd >= 0;
}

static void connect_keyboard(void)
{
    char p[32];
    int i;

    if (strcmp(port_opt, "auto")) {
        serial_fd = open_port(port_opt);
        if (serial_fd >= 0)
            scopy(serial_name, port_opt, sizeof(serial_name));
    } else {
        for (i = 0; i < 10 && serial_fd < 0; i++) {
            sprintf(p, "/dev/ttyACM%d", i);
            serial_fd = open_port(p);
            if (serial_fd >= 0)
                scopy(serial_name, p, sizeof(serial_name));
        }
    }
    if (serial_fd >= 0) {
        npacket = 0;
        last_open_error = 0;
        say("keyboard on %s", serial_name);
    } else if (errno != last_open_error) {
        last_open_error = errno;
        if (errno == EACCES)
            say("no permission for the serial port: as root, "
                "usermod -aG dialout user, then log in again", 0);
        else
            say("waiting for the keyboard (%s)", strcmp(port_opt, "auto") ?
                port_opt : "/dev/ttyACM0-9");
    }
}

static void drop_keyboard(void)
{
    close(serial_fd);
    serial_fd = -1;
    last_open_error = 0;
    wait_once = 1;
    say("keyboard gone; waiting for it", 0);
}

static void stroked(long st)
{
    static long long last_check;
    long long now;
    char s[32];

    if (!st)
        return;
    if (verbose) {
        stroke_text(st, s);
        say("stroke %s", s);
    }
    /* a dictionary changed (steno -a, or by hand): read it again, but
     * look at most once a second */
    now = time(0);
    if (now != last_check) {
        last_check = now;
        dict_reload();
    }
    translate(st);
    dict_stroked();
    tape_stroke(st);              /* only if a viewer is there */
    if (!out_mode)
        tui_stroke(st);
    if (reset_pending) {
        reset_pending = 0;
        translate_reset();
    }
}

static void read_keyboard(void)
{
    unsigned char b[256];
    int n, i;

    n = read(serial_fd, b, sizeof(b));
    if (n < 0 && (errno == EAGAIN || errno == EINTR))
        return;
    if (n <= 0) {
        drop_keyboard();
        return;
    }
    for (i = 0; i < n; i++) {
        if (b[i] & 0x80)
            npacket = 0;
        else if (npacket == 0)
            continue;          /* not the start of a stroke: skip */
        packet[npacket] = b[i];
        npacket++;
        if (npacket == 6) {
            npacket = 0;
            stroked(gemini_stroke(packet));
        }
    }
}

/* --- main ------------------------------------------------------------ */

static void on_resize(int sig)
{
    (void)sig;
    tui_dirty = 1;
}

static void on_signal(int sig)
{
    (void)sig;
    stop_now = 1;
}

static void usage(int code)
{
    fprintf(code ? stderr : stdout,
        "usage: steno [-qv] [-c CONFIG] [-p PORT]  read the keyboard, type\n"
        "       steno -T | -S | -TS                paper tape, suggestions,\n"
        "                                          or both\n"
        "       steno -l WORD|OUTLINE              look it up\n"
        "       steno -a OUTLINE TRANSLATION       add a stroke\n"
        "       steno -A                           add strokes, asking\n"
        "       steno -C DIR FILE...               Plover's files in\n"
        "                                          steno's format\n"
        "       steno -t                           strokes from stdin,\n"
        "                                          print what is typed\n"
        "  -c  settings file (~/.config/steno/config)\n"
        "  -p  serial port of the keyboard (auto: /dev/ttyACM0 to 9)\n"
        "  -t  test: one stroke or outline a line, as KAT or KAT/-S\n"
        "  -q  no status screen in a terminal\n"
        "  -v  say each stroke\n");
    exit(code);
}

static int test_loop(void)
{
    char line[LINE_MAX_LEN];
    long st[STROKE_MAX];
    int n, i, len;

    while (fgets(line, sizeof(line), stdin)) {
        len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' ||
            line[len - 1] == ' '))
        {
            len--;
        }
        line[len] = 0;
        if (!len || (line[0] == '#' && line[1] == '!'))
            continue;
        printf("> %s\n", line);
        n = outline_parse(line, st, STROKE_MAX);
        if (n < 0) {
            printf("? not steno\n");
            fflush(stdout);
            continue;
        }
        for (i = 0; i < n; i++)
            stroked(st[i]);
        fflush(stdout);
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *cfg, *arg1, *arg2;
    struct pollfd pf[3];
    int i, n, test, r, tool, show_tape, show_sugg, quiet, kb, keys;
    int sugg_first;
    struct sigaction sa;

    cfg = 0;
    test = 0;
    tool = 0;
    show_tape = 0;
    show_sugg = 0;
    sugg_first = 0;
    quiet = 0;
    arg1 = 0;
    arg2 = 0;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c") && i + 1 < argc) {
            i++;
            cfg = argv[i];
        } else if (!strcmp(argv[i], "-p") && i + 1 < argc) {
            i++;
            scopy(port_opt, argv[i], sizeof(port_opt));
        } else if (!strcmp(argv[i], "-t")) {
            test = 1;
        } else if (!strcmp(argv[i], "-T")) {
            show_tape = 1;
        } else if (!strcmp(argv[i], "-S")) {
            sugg_first = !show_tape;   /* -S -T: suggestions first */
            show_sugg = 1;
        } else if (!strcmp(argv[i], "-TS") || !strcmp(argv[i], "-ST")) {
            sugg_first = argv[i][1] == 'S';
            show_tape = 1;
            show_sugg = 1;
        } else if (!strcmp(argv[i], "-C") && i + 2 < argc) {
            return convert_files(argv + i + 1, argc - i - 1);
        } else if (!strcmp(argv[i], "-A")) {
            tool = 'A';
        } else if (!strcmp(argv[i], "-l") && i + 1 < argc) {
            tool = 'l';
            i++;
            arg1 = argv[i];
        } else if (!strcmp(argv[i], "-a") && i + 2 < argc) {
            tool = 'a';
            arg1 = argv[i + 1];
            arg2 = argv[i + 2];
            i += 2;
        } else if (!strcmp(argv[i], "-q")) {
            quiet = 1;
        } else if (!strcmp(argv[i], "-v")) {
            verbose = 1;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(0);
        } else {
            usage(1);
        }
    }
    if (show_tape || show_sugg)
        return view_main(show_tape, show_sugg, sugg_first, cfg);
    out_mode = test;
    scopy(cfg_path, cfg ? cfg : config_path("~/.config/steno/config"),
        sizeof(cfg_path));
    r = read_config(cfg_path, 1);
    if (r < 0)
        return 1;
    if (verbose)
        fprintf(stderr, "%s: %d dictionaries, %ld entries\n", prog,
            dict_count(), dict_entries());
    for (i = 0; i < 42; i++)
        gemini_map[i] = gemini_to_key(gemini_keys[i]);
    if (tool == 'l')
        return tool_lookup(arg1);
    if (tool == 'a')
        return tool_add(arg1, arg2);
    if (tool == 'A')
        return tool_add_loop();
    if (test)
        return test_loop();

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, 0);
    sigaction(SIGTERM, &sa, 0);
    sigaction(SIGHUP, &sa, 0);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGCHLD, &sa, 0);
    sigaction(SIGPIPE, &sa, 0);
    r = out_open();
    if (r < 0)
        return 1;
    if (!quiet) {
        sa.sa_handler = on_resize;
        sigaction(SIGWINCH, &sa, 0);
        tui_start();
    }
    while (!stop_now) {
        if (serial_fd < 0 && !wait_once)
            connect_keyboard();
        wait_once = 0;
        if (tui_dirty)
            tui_draw();
        n = 0;
        pf[n].fd = out_fd();
        pf[n].events = POLLIN;
        n++;
        kb = -1;
        if (serial_fd >= 0) {
            kb = n;
            pf[n].fd = serial_fd;
            pf[n].events = POLLIN;
            n++;
        }
        keys = -1;
        if (tui_on) {
            keys = n;
            pf[n].fd = 0;
            pf[n].events = POLLIN;
            n++;
        }
        /* without the keyboard, look for it again every second */
        r = poll(pf, n, serial_fd >= 0 ? -1 : 1000);
        if (r < 0 && errno != EINTR)
            break;
        if (r <= 0)
            continue;
        if (pf[0].revents)
            out_event();
        if (kb >= 0 && pf[kb].revents & POLLIN)
            read_keyboard();
        else if (kb >= 0 && pf[kb].revents & (POLLHUP | POLLERR | POLLNVAL))
            drop_keyboard();
        if (keys >= 0 && pf[keys].revents) {
            r = tui_key();
            if (r < 0)
                break;
        }
    }
    tui_stop();
    return 0;
}
