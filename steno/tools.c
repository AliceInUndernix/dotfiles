/* The terminal tools around the engine: the feeds for the paper tape
 * and suggestions (view.c shows them), adding a stroke (steno -a, -A)
 * and looking one up (steno -l).
 *
 * The engine writes to two FIFOs in $XDG_STATE_HOME/steno, each only
 * while a viewer has it open, so nothing is kept anywhere:
 *
 *   tape      one line a stroke: STROKE <tab> TRANSLATION <tab> N,
 *             N the strokes of that translation (0: the stroke undid
 *             one), so the viewer can clear the N - 1 before it; and
 *             when a fingerspelled word ends, before the stroke after
 *             it: = <tab> WORD <tab> N, N its letters, so the viewer
 *             shows the word on the last of them
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

/* the word being fingerspelled: its letters so far, and as typed */
static int spell_n;
static char spell_word[TEXT_MAX];

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

/* The last few words, longest first, and how else they are written;
 * spelled, a word just fingerspelled (it is not among the last words
 * when the stroke after it made a word of its own). Sent only when
 * they change; a new viewer gets the current ones. */
static void suggestions(int fresh, const char *spelled)
{
    int res;
    static char last_key[TEXT_MAX];
    char key[TEXT_MAX], w[TEXT_MAX], seen[TEXT_MAX];
    char o[TEXT_MAX], out[TAPE_LINE];
    int i;

    res = last_words(SUGGEST_WORDS, key, sizeof(key));
    if (!res)
        return;
    if (!fresh && !spelled[0] && !strcmp(key, last_key))
        return;
    scopy(last_key, key, sizeof(last_key));
    out[0] = 0;
    seen[0] = 0;
    if (spelled[0]) {
        res = dict_outlines(spelled, o, sizeof(o), SUGGEST_MAX);
        if (res) {
            put_escaped(out, sizeof(out) - TEXT_MAX, spelled);
            scat(out, "\t", sizeof(out));
            scat(out, o, sizeof(out) - 2);
        }
    }
    for (i = SUGGEST_WORDS; i >= 1; i--) {
        res = last_words(i, w, sizeof(w));
        if (!res)
            continue;
        trim(w);
        if (!w[0] || !strcmp(w, seen) || !strcmp(w, spelled))
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

/* What a stroke did: the text of the translation it made, its
 * strokes when they mean nothing, or * when it undid one. top is the
 * newest translation before it. Gives the translation's strokes, 0
 * for *. */
int stroke_result(long st, struct trans *top, char *out, int size)
{
    char s[STROKE_MAX * 32];
    const char *g, *e;
    struct trans *t;
    int n;

    out[0] = 0;
    n = history_count();
    t = n ? history_at(n - 1) : 0;
    if (t && t != top && t->nst && t->st[t->nst - 1] == st) {
        g = t->has_english ? strstr(t->english, "{&") : 0;
        e = g ? strchr(g, '}') : 0;
        if (e && e - g - 2 < (int)sizeof(s)) {
            /* a fingerspelled letter: just the letter */
            memcpy(s, g + 2, e - g - 2);
            s[e - g - 2] = 0;
            put_escaped(out, size, s);
        } else if (t->has_english) {
            put_escaped(out, size, t->english);
        } else {
            outline_text(t->st, t->nst, s, sizeof(s));
            put_escaped(out, size, s);
        }
        return t->nst;
    }
    scopy(out, "*", size);        /* undone */
    return 0;
}

/* A fingerspelled letter: {&s}, {>}{&s}, ... (Plover's glue) */
static int is_spelled(struct trans *t)
{
    return t && t->has_english && strstr(t->english, "{&") != 0;
}

/* after each stroke; top is the newest translation before it */
void tape_stroke(long st, struct trans *top)
{
    char line[TAPE_LINE + 64], spelled[TEXT_MAX];
    struct trans *t;
    int n, was, open_now, k, made, ended;

    /* fingerspelling: count the letters; the stroke after the last
     * ends the word, for the tape and the suggestions */
    n = history_count();
    t = n ? history_at(n - 1) : 0;
    made = t && t != top && t->nst && t->st[t->nst - 1] == st;
    spelled[0] = 0;
    ended = 0;
    if (made && is_spelled(t)) {
        spell_n++;
        last_words(1, spell_word, sizeof(spell_word));
        trim(spell_word);
    } else if (!made && spell_n > 0) {
        spell_n--;             /* a letter undone */
        if (spell_n) {
            last_words(1, spell_word, sizeof(spell_word));
            trim(spell_word);
        }
    } else if (made) {
        if (spell_n > 1 && spell_word[0]) {
            scopy(spelled, spell_word, sizeof(spelled));
            ended = spell_n;
        }
        spell_n = 0;
    }
    open_now = feed_open(&tape_fd, "tape");
    if (open_now && ended) {
        scopy(line, "=\t", sizeof(line));
        put_escaped(line, TAPE_LINE, spelled);
        n = strlen(line);
        sprintf(line + n, "\t%d\n", ended);
        feed_write(&tape_fd, line);
        open_now = tape_fd >= 0;
    }
    if (open_now) {
        stroke_text(st, line);
        scat(line, "\t", sizeof(line));
        n = strlen(line);
        k = stroke_result(st, top, line + n, TAPE_LINE - n);
        n = strlen(line);
        sprintf(line + n, "\t%d\n", k);
        feed_write(&tape_fd, line);
    }
    was = sugg_fd >= 0;
    if (!output_on)
        return;
    if (spell_n)
        return;                /* no suggestions for half a word */
    open_now = feed_open(&sugg_fd, "suggest");
    if (open_now)
        suggestions(!was, spelled);
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

/* What each stroke of an outline writes alone, as "high + draw +
 * lick": why the outline is written so. Empty for one stroke. */
static void outline_parts(const long *st, int n, char *out, int size)
{
    char s[32];
    const char *v;
    int i;

    out[0] = 0;
    if (n < 2)
        return;
    for (i = 0; i < n; i++) {
        if (i)
            scat(out, " + ", size);
        v = dict_lookup(st + i, 1);
        if (v) {
            put_escaped(out, size, v);
        } else {
            stroke_text(st[i], s);
            scat(out, s, size);
            scat(out, "?", size);
        }
    }
}

int tool_lookup(const char *s)
{
    int res;
    char out[TEXT_MAX * 8], norm[STROKE_MAX * 32], parts[TEXT_MAX * 2];
    char *o, *e;
    long st[STROKE_MAX];
    int n, found, got, end, w;

    found = 0;
    n = outline_parse(s, st, STROKE_MAX);
    got = n > 0 ? dict_explain(st, n, out, sizeof(out)) : 0;
    if (got) {
        outline_text(st, n, norm, sizeof(norm));
        printf("%s gives\n%s", norm, out);
        outline_parts(st, n, parts, sizeof(parts));
        if (parts[0])
            printf("  stroke by stroke: %s\n", parts);
        found = 1;
    }
    res = dict_outlines(s, out, sizeof(out), 50);
    if (res) {
        /* one outline a line, shortest first, each with its parts */
        printf("%s is written\n", s);
        w = 0;
        for (o = out; *o; o = e) {
            while (*o == ' ')
                o++;
            e = o;
            while (*e && *e != ' ')
                e++;
            if (e - o > w)
                w = (int)(e - o);
        }
        for (o = out; *o; o = end ? e : e + 1) {
            while (*o == ' ')
                o++;
            if (!*o)
                break;
            e = o;
            while (*e && *e != ' ')
                e++;
            end = !*e;
            *e = 0;
            n = outline_parse(o, st, STROKE_MAX);
            parts[0] = 0;
            if (n > 0)
                outline_parts(st, n, parts, sizeof(parts));
            if (parts[0])
                printf("  %-*s  %s\n", w, o, parts);
            else
                printf("  %s\n", o);
        }
        found = 1;
    }
    if (!found)
        printf("%s: not in the dictionaries\n", s);
    return found ? 0 : 1;
}

/* --- steno -a, -A ---------------------------------------------------- */

static void escape_text(const char *s, char *o, int size)
{
    int n;

    n = 0;
    for (; *s && n < size - 3; s++) {
        if (*s == '\\' || *s == '\t' || *s == '\n' || *s == '\r') {
            o[n] = '\\';
            n++;
            o[n] = *s == '\t' ? 't' : *s == '\n' ? 'n' :
                *s == '\r' ? 'r' : '\\';
        } else {
            o[n] = *s;
        }
        n++;
    }
    o[n] = 0;
}

/* line: "TEXT<tab>OUTLINES"; out gets the outlines without outline;
 * 1 when it had it */
static int drop_outline(const char *outlines, const char *outline,
    char *out, int size)
{
    const char *o, *e;
    int had, n;

    out[0] = 0;
    had = 0;
    n = strlen(outline);
    for (o = outlines; *o; o = e) {
        while (*o == ' ')
            o++;
        if (!*o)
            break;
        e = o;
        while (*e && *e != ' ')
            e++;
        if (e - o == n && !strncmp(o, outline, n)) {
            had = 1;
            continue;
        }
        if (out[0])
            scat(out, " ", size);
        if ((int)strlen(out) + (e - o) < size - 1)
            strncat(out, o, e - o);
    }
    return had;
}

/* Put outline -> text in a .dict file: on the line of that text when
 * there is one (else a new line at the end), and off any other line,
 * since an outline writes one thing. A line left with no outline
 * goes. */
static int write_entry(const char *path, const char *outline,
    const char *text)
{
    char *f, *o, *line, *next, *tab, esc[TEXT_MAX * 2 + 8];
    char rest[TEXT_MAX * 4], tmp[1100];
    long size, need;
    int done, r, w, fd;

    f = read_file(path, &size);
    if (!f) {
        f = malloc(1);
        if (!f)
            return -1;
        f[0] = 0;
        size = 0;
    }
    escape_text(text, esc, sizeof(esc));
    need = size + strlen(esc) + strlen(outline) + 16;
    o = malloc(need);
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
        tab = strrchr(line, '\t');
        if (!tab) {
            strcat(o, line);   /* not an entry: as it was */
            strcat(o, "\n");
            continue;
        }
        *tab = 0;
        drop_outline(tab + 1, outline, rest, sizeof(rest) - 64);
        if (!strcmp(line, esc) && !done) {
            if (rest[0])
                scat(rest, " ", sizeof(rest));
            scat(rest, outline, sizeof(rest));
            done = 1;
        }
        if (!rest[0])
            continue;
        strcat(o, line);
        strcat(o, "\t");
        strcat(o, rest);
        strcat(o, "\n");
    }
    if (!done) {
        strcat(o, esc);
        strcat(o, "\t");
        strcat(o, outline);
        strcat(o, "\n");
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
        if (strlen(p) > 5 && !strcmp(p + strlen(p) - 5, ".dict"))
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
