/* The terminal tools around the engine: the feeds for the paper tape
 * and suggestions (view.c shows them), adding a stroke (steno -a, -A)
 * and looking one up (steno -l).
 *
 * The engine writes to two FIFOs in $XDG_STATE_HOME/steno, each only
 * while a viewer has it open, so nothing is kept anywhere:
 *
 *   tape      one line a stroke: STROKE <tab> TRANSLATION
 *   suggest   one line when the last words change and have outlines:
 *             WORDS <tab> OUTLINES [<tab> WORDS <tab> OUTLINES]...
 *
 * Tabs, newlines and backslashes in the text are written as \t, \n
 * and \\. Without a suggestions viewer, no suggestions are looked for.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "steno.h"

#define TAPE_LINE 4000         /* under PIPE_BUF: one write, whole */
#define SUGGEST_WORDS 4
#define SUGGEST_MAX 6

char user_dict[1024];          /* where -a writes ("user" setting) */

static int tape_fd = -1, sugg_fd = -1;

/* $XDG_STATE_HOME/steno/NAME, or ~/.local/state/steno/NAME; the
 * directories are made (0700) when missing */
const char *feed_path(const char *name)
{
    static char p[1100];
    const char *x;
    char *s;

    x = getenv("XDG_STATE_HOME");
    if (x && x[0] == '/')
        scopy(p, x, 1000);
    else
        scopy(p, config_path("~/.local/state"), 1000);
    scat(p, "/steno", sizeof(p));
    for (s = p + 1; *s; s++) {
        if (*s == '/') {
            *s = 0;
            mkdir(p, 0700);
            *s = '/';
        }
    }
    mkdir(p, 0700);
    scat(p, "/", sizeof(p));
    scat(p, name, sizeof(p));
    return p;
}

/* 1: a viewer has it open */
static int feed_open(int *fd, const char *name)
{
    if (*fd >= 0)
        return 1;
    *fd = open(feed_path(name), O_WRONLY | O_NONBLOCK);
    if (*fd < 0)
        return 0;
    fcntl(*fd, F_SETFD, FD_CLOEXEC);
    return 1;
}

static void feed_write(int *fd, const char *s)
{
    int r;

    r = write(*fd, s, strlen(s));
    if (r < 0 && errno != EAGAIN) {
        close(*fd);            /* the viewer went away */
        *fd = -1;
    }
}

static void put_escaped(char *out, int size, const char *s)
{
    int n;

    n = strlen(out);
    for (; *s && n < size - 3; s++) {
        if (*s == '\t' || *s == '\n' || *s == '\\') {
            out[n] = '\\';
            n++;
            out[n] = *s == '\t' ? 't' : *s == '\n' ? 'n' : '\\';
        } else if ((unsigned char)*s < 32 || *s == 127) {
            out[n] = '?';
        } else {
            out[n] = *s;
        }
        n++;
    }
    out[n] = 0;
}

static void outline_text(const long *st, int n, char *out, int size)
{
    char s[32];
    int i;

    out[0] = 0;
    for (i = 0; i < n; i++) {
        stroke_text(st[i], s);
        if (i)
            scat(out, "/", size);
        scat(out, s, size);
    }
}

static void trim(char *s)
{
    int a, n;

    n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
        s[n - 1] == '\n'))
    {
        n--;
    }
    s[n] = 0;
    a = 0;
    while (s[a] == ' ' || s[a] == '\t' || s[a] == '\n')
        a++;
    if (a)
        memmove(s, s + a, n - a + 1);
}

/* The last few words, longest first, and how else they are written.
 * Sent only when they change; a new viewer gets the current ones. */
static void suggestions(int fresh)
{
    int res;
    static char last_key[TEXT_MAX];
    char key[TEXT_MAX], w[TEXT_MAX], seen[TEXT_MAX];
    char o[TEXT_MAX], out[TAPE_LINE];
    int i;

    res = last_words(SUGGEST_WORDS, key, sizeof(key));
    if (!res)
        return;
    if (!fresh && !strcmp(key, last_key))
        return;
    scopy(last_key, key, sizeof(last_key));
    out[0] = 0;
    seen[0] = 0;
    for (i = SUGGEST_WORDS; i >= 1; i--) {
        res = last_words(i, w, sizeof(w));
        if (!res)
            continue;
        trim(w);
        if (!w[0] || !strcmp(w, seen))
            continue;
        scopy(seen, w, sizeof(seen));
        res = dict_outlines(w, o, sizeof(o), SUGGEST_MAX);
        if (!res)
            continue;
        if (out[0])
            scat(out, "\t", sizeof(out));
        put_escaped(out, sizeof(out) - TEXT_MAX, w);
        scat(out, "\t", sizeof(out));
        scat(out, o, sizeof(out) - 2);
    }
    if (!out[0])
        return;
    scat(out, "\n", sizeof(out));
    feed_write(&sugg_fd, out);
}

/* after each stroke; top is the newest translation before it */
void tape_stroke(long st, struct trans *top)
{
    char line[TAPE_LINE + 64], s[STROKE_MAX * 32];
    struct trans *t;
    int n, was, open_now;

    open_now = feed_open(&tape_fd, "tape");
    if (open_now) {
        stroke_text(st, line);
        scat(line, "\t", sizeof(line));
        n = history_count();
        t = n ? history_at(n - 1) : 0;
        if (t && t != top && t->nst && t->st[t->nst - 1] == st) {
            if (t->has_english) {
                put_escaped(line, TAPE_LINE, t->english);
            } else {
                outline_text(t->st, t->nst, s, sizeof(s));
                put_escaped(line, TAPE_LINE, s);
            }
        } else {
            scat(line, "*", sizeof(line));    /* undone */
        }
        scat(line, "\n", sizeof(line));
        feed_write(&tape_fd, line);
    }
    was = sugg_fd >= 0;
    if (!output_on)
        return;
    open_now = feed_open(&sugg_fd, "suggest");
    if (open_now)
        suggestions(!was);
}

/* --- steno -l -------------------------------------------------------- */

static int lookup_outline(const char *s, char *norm, int size)
{
    long st[STROKE_MAX];
    int n;

    n = outline_parse(s, st, STROKE_MAX);
    if (n <= 0)
        return 0;
    outline_text(st, n, norm, size);
    return n;
}

int tool_lookup(const char *s)
{
    int res;
    char out[TEXT_MAX * 8], norm[STROKE_MAX * 32];
    long st[STROKE_MAX];
    int n, found, got;

    found = 0;
    n = outline_parse(s, st, STROKE_MAX);
    got = n > 0 ? dict_explain(st, n, out, sizeof(out)) : 0;
    if (got) {
        outline_text(st, n, norm, sizeof(norm));
        printf("%s gives\n%s", norm, out);
        found = 1;
    }
    res = dict_outlines(s, out, sizeof(out), 50);
    if (res) {
        printf("%s is written %s\n", s, out);
        found = 1;
    }
    if (!found)
        printf("%s: not in the dictionaries\n", s);
    return found ? 0 : 1;
}

/* --- steno -a, -A ---------------------------------------------------- */

static void json_quote(const char *s, char *out, int size)
{
    static const char hex[] = "0123456789abcdef";
    int n;

    n = 0;
    out[n] = '"';
    n++;
    for (; *s && n < size - 8; s++) {
        if (*s == '"' || *s == '\\') {
            out[n] = '\\';
            n++;
            out[n] = *s;
        } else if (*s == '\n') {
            out[n] = '\\';
            n++;
            out[n] = 'n';
        } else if (*s == '\t') {
            out[n] = '\\';
            n++;
            out[n] = 't';
        } else if ((unsigned char)*s < 32) {
            out[n] = '\\';
            out[n + 1] = 'u';
            out[n + 2] = '0';
            out[n + 3] = '0';
            out[n + 4] = hex[(*s >> 4) & 15];
            out[n + 5] = hex[*s & 15];
            n += 5;
        } else {
            out[n] = *s;
        }
        n++;
    }
    out[n] = '"';
    out[n + 1] = 0;
}

/* Put "outline": "text" in the file, the way Plover writes it: one
 * entry a line. An outline already there gets the new text. */
static int write_entry(const char *path, const char *outline,
    const char *text)
{
    char *f, *o, *p, *line, *end, *close_at, k[STROKE_MAX * 32 + 8];
    char v[TEXT_MAX * 6 + 8], tmp[1100];
    long size, need;
    int fd, r, w;

    f = read_file(path, &size);
    if (!f) {
        f = malloc(8);
        if (!f)
            return -1;
        strcpy(f, "{\n}\n");
        size = 4;
    }
    json_quote(outline, k, sizeof(k));
    json_quote(text, v, sizeof(v));
    need = size + strlen(k) + strlen(v) + 16;
    o = malloc(need);
    if (!o) {
        free(f);
        return -1;
    }
    /* the outline's line, if it is there */
    line = 0;
    for (p = f; *p; p = end) {
        end = strchr(p, '\n');
        end = end ? end + 1 : p + strlen(p);
        while (*p == ' ' || *p == '\t')
            p++;
        if (!strncmp(p, k, strlen(k)) && p[strlen(k)] == ':') {
            line = p;
            break;
        }
    }
    if (line) {
        p = end;
        while (p > line && (p[-1] == '\n' || p[-1] == '\r'))
            p--;
        w = p > line && p[-1] == ',';
        sprintf(o, "%.*s%s: %s%s%s", (int)(line - f), f, k, v,
            w ? "," : "", p);
    } else {
        close_at = strrchr(f, '}');
        if (!close_at) {
            say("not a dictionary: %s", path);
            free(f);
            free(o);
            return -1;
        }
        p = close_at;
        while (p > f && (p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' ||
            p[-1] == '\r'))
        {
            p--;
        }
        if (p > f && p[-1] == '{')
            sprintf(o, "%.*s\n%s: %s\n%s", (int)(p - f), f, k, v, close_at);
        else
            sprintf(o, "%.*s,\n%s: %s%s", (int)(p - f), f, k, v, p);
    }
    free(f);
    /* a new file, then rename: never half a dictionary */
    sprintf(tmp, "%.1000s.new", path);
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
        say("can't write %s", tmp);
        unlink(tmp);
        return -1;
    }
    r = rename(tmp, path);
    if (r < 0) {
        say("can't replace %s", path);
        unlink(tmp);
        return -1;
    }
    return 0;
}

static const char *target(void)
{
    const char *p;
    int i;

    if (user_dict[0])
        return user_dict;
    for (i = 0; i < dict_count(); i++) {
        p = dict_path(i);
        if (strlen(p) > 5 && !strcmp(p + strlen(p) - 5, ".json"))
            return p;
    }
    return 0;
}

static void show_conflicts(const char *norm, const char *text)
{
    char out[TEXT_MAX * 8];
    long st[STROKE_MAX];
    int n, got;

    n = norm ? outline_parse(norm, st, STROKE_MAX) : 0;
    got = n > 0 ? dict_explain(st, n, out, sizeof(out)) : 0;
    if (got)
        printf("%s gives now\n%s", norm, out);
    got = text ? dict_outlines(text, out, sizeof(out), 20) : 0;
    if (got)
        printf("%s is written now %s\n", text, out);
}

int tool_add(const char *outline, const char *text)
{
    int res;
    char norm[STROKE_MAX * 32];
    const char *t;

    t = target();
    if (!t) {
        say("no .json dictionary to write to", 0);
        return 1;
    }
    if (!lookup_outline(outline, norm, sizeof(norm))) {
        say("not an outline: %s", outline);
        return 1;
    }
    show_conflicts(norm, text);
    res = write_entry(t, norm, text);
    if (res < 0)
        return 1;
    printf("%s: %s -> %s\n", t, norm, text);
    return 0;
}

static int ask_line(const char *prompt, char *s, int size)
{
    int n;

    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(s, size, stdin))
        return -1;
    n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        n--;
    s[n] = 0;
    return n;
}

int tool_add_loop(void)
{
    int res;
    char o[1024], norm[STROKE_MAX * 32], text[TEXT_MAX], a[16];
    const char *t;

    t = target();
    if (!t) {
        say("no .json dictionary to write to", 0);
        return 1;
    }
    printf("adding to %s (empty outline: done)\n", t);
    for (;;) {
        res = ask_line("outline: ", o, sizeof(o));
        if (res <= 0)
            break;
        trim(o);
        if (!lookup_outline(o, norm, sizeof(norm))) {
            printf("  not an outline\n");
            continue;
        }
        show_conflicts(norm, 0);
        res = ask_line("translation: ", text, sizeof(text));
        if (res <= 0)
            continue;
        show_conflicts(0, text);
        res = ask_line("add it? [y/N] ", a, sizeof(a));
        if (res < 0)
            break;
        if (a[0] != 'y' && a[0] != 'Y')
            continue;
        res = write_entry(t, norm, text);
        if (res == 0)
            printf("  added %s -> %s\n", norm, text);
    }
    return 0;
}
