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
 *             after a fingerspelled letter: = <tab> WORD <tab> N, the
 *             word so far and its letters, shown on the last of them
 *   suggest   one line when the last words change and have outlines:
 *             WORDS <tab> OUTLINES [<tab> WORDS <tab> OUTLINES]...;
 *             while fingerspelling, = [<tab> WORD <tab> OUTLINES]:
 *             the word so far, in place of the one before
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

/* The outline you just used for the text w: the strokes of the last
 * k translations (k up to 6) that the dictionary reads as w. Gives
 * the number of strokes and writes them as text, or gives 0. */
static int used_outline(const char *w, char *out, int size)
{
    long st[64];
    int n, k, i, j, tot;
    const char *v;
    struct trans *t;

    n = history_count();
    for (k = 1; k <= 6 && k <= n; k++) {
        tot = 0;
        for (i = n - k; i < n; i++)
            tot += history_at(i)->nst;
        if (tot > 64)
            break;
        tot = 0;
        for (i = n - k; i < n; i++) {
            t = history_at(i);
            for (j = 0; j < t->nst; j++) {
                st[tot] = t->st[j];
                tot++;
            }
        }
        v = dict_peek(st, tot);
        if (v && !strcmp(v, w)) {
            outline_text(st, tot, out, size);
            return tot;
        }
    }
    return 0;
}

/* strokes in an outline written as "A/B/C" */
static int outline_strokes(const char *o, int len)
{
    int i, n;

    n = 1;
    for (i = 0; i < len; i++)
        if (o[i] == '/')
            n++;
    return n;
}

/* o is "A  B  C" (shortest first). The outline used last gets a + in
 * front when no listed outline has fewer strokes (the same number is
 * as good), a ! when one has; it is added at the end if it is not in
 * the list. */
static void mark_used(const char *w, char *o, int size)
{
    char used[TEXT_MAX], tok[TEXT_MAX], res[TEXT_MAX];
    const char *p, *q;
    int n, best, len, done;

    n = used_outline(w, used, sizeof(used));
    if (!n || !o[0])
        return;
    best = 0;
    res[0] = 0;
    done = 0;
    for (p = o; *p; p = q) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        for (q = p; *q && *q != ' '; q++)
            ;
        len = (int)(q - p);
        if (len > (int)sizeof(tok) - 2)
            len = (int)sizeof(tok) - 2;
        memcpy(tok, p, len);
        tok[len] = 0;
        if (!best)
            best = outline_strokes(tok, len);
        if (res[0])
            scat(res, "  ", sizeof(res));
        if (!done && !strcmp(tok, used)) {
            scat(res, n <= best ? "+" : "!", sizeof(res));
            done = 1;
        }
        scat(res, tok, sizeof(res));
    }
    if (!done) {
        scat(res, "  ", sizeof(res));
        scat(res, n <= best ? "+" : "!", sizeof(res));
        scat(res, used, sizeof(res));
    }
    scopy(o, res, size);
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
        mark_used(w, o, sizeof(o));
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

/* {^ed}, {^ing}: letters only, put on the word before */
static int is_suffix(struct trans *t)
{
    const char *e;
    int i;

    if (!t || !t->has_english)
        return 0;
    e = t->english;
    if (e[0] != '{' || e[1] != '^')
        return 0;
    for (i = 2; (e[i] >= 'a' && e[i] <= 'z') || e[i] == '\''; i++)
        ;
    return i > 2 && e[i] == '}' && !e[i + 1];
}

/* after each stroke; top is the newest translation before it */
void tape_stroke(long st, struct trans *top, int was_strokes)
{
    char line[TAPE_LINE + 64], o[TEXT_MAX];
    struct trans *t;
    int n, was, open_now, k, made, spelling, res;

    /* fingerspelling: the letters so far make a word, shown as it
     * grows on the tape and in the suggestions */
    n = history_count();
    t = n ? history_at(n - 1) : 0;
    made = t && t != top && t->nst && t->st[t->nst - 1] == st;
    spelling = 0;
    if (made && is_spelled(t)) {
        spell_n++;
        spelling = 1;
    } else if (!made && spell_n > 0) {
        spell_n--;             /* a letter undone */
        spelling = 1;
    } else if (made) {
        spell_n = 0;
    }
    spell_word[0] = 0;
    if (spell_n) {
        last_words(1, spell_word, sizeof(spell_word));
        trim(spell_word);
    }
    open_now = feed_open(&tape_fd, "tape");
    if (open_now) {
        stroke_text(st, line);
        scat(line, "\t", sizeof(line));
        n = strlen(line);
        k = stroke_result(st, top, line + n, TAPE_LINE - n);
        n = strlen(line);
        if (k)
            sprintf(line + n, "\t%d\n", k);
        else    /* * : how many strokes it took back (as the engine
                 * does: no-text ones such as KPA go with the word) */
            sprintf(line + n, "\t0\t%d\n", was_strokes - history_strokes());
        feed_write(&tape_fd, line);
    }
    if (tape_fd >= 0 && spelling && spell_n && spell_word[0] &&
        (spell_n > 1 || !made))
    {
        scopy(line, "=\t", sizeof(line));
        put_escaped(line, TAPE_LINE, spell_word);
        n = strlen(line);
        sprintf(line + n, "\t%d\n", spell_n);
        feed_write(&tape_fd, line);
    }
    /* a suffix (-D: {^ed}) put on the word before: the tape shows the
     * word it made (prescribed), not just the ending */
    if (tape_fd >= 0 && made && !spelling && is_suffix(t) &&
        history_count() > 1)
    {
        last_words(1, spell_word, sizeof(spell_word));
        trim(spell_word);
        if (spell_word[0]) {
            scopy(line, "=\t", sizeof(line));
            put_escaped(line, TAPE_LINE, spell_word);
            n = strlen(line);
            sprintf(line + n, "\t%d\n",
                t->nst + history_at(history_count() - 2)->nst);
            feed_write(&tape_fd, line);
        }
    }
    was = sugg_fd >= 0;
    if (!output_on)
        return;
    open_now = feed_open(&sugg_fd, "suggest");
    if (!open_now)
        return;
    if (!spelling) {
        suggestions(!was);
        return;
    }
    /* = and the word being spelled, when it has outlines: it takes
     * the place of the one before */
    scopy(line, "=", sizeof(line));
    res = spell_word[0] ? dict_outlines(spell_word, o, sizeof(o),
        SUGGEST_MAX) : 0;
    if (res) {
        scat(line, "\t", sizeof(line));
        put_escaped(line, TAPE_LINE - TEXT_MAX, spell_word);
        scat(line, "\t", sizeof(line));
        scat(line, o, TAPE_LINE);
    }
    scat(line, "\n", sizeof(line));
    feed_write(&sugg_fd, line);
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

/* --- typographic quotes, as Dotterel saves what you add ----------------
 *
 * Prose never gets a straight ' or ": in a translation with a letter
 * or a digit, ' becomes the apostrophe U+2019 and " opens (U+201C)
 * after nothing, a space or an opening bracket and closes (U+201D)
 * after anything else, also inside text braces ({^'s}, {~|'^}em). Left
 * alone: no letter or digit (a bare symbol, for code), {commands} and
 * fingerspelling ({#apostrophe}, {&'}), and =metas. As Dotterel's
 * words.curly. */

#define Q_APOS "\342\200\231"
#define Q_OPEN "\342\200\234"
#define Q_CLOSE "\342\200\235"

/* a letter or digit: ASCII ones, and any non-ASCII but the general
 * punctuation block (E2 80 xx: quotes, dashes, the ellipsis) */
static int is_word_char(const char *s)
{
    unsigned char c;

    c = (unsigned char)s[0];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9'))
    {
        return 1;
    }
    if (c < 0xc0)
        return 0;
    return !(c == 0xe2 && (unsigned char)s[1] == 0x80);
}

/* the inside of {...} that Plover writes out as text: its text part
 * (between an optional ~| and ^ and a final ^), or 0 */
static int text_brace(const char *in, int n, int *a, int *b)
{
    int i, e;

    i = 0;
    if (n >= 2 && in[0] == '~' && in[1] == '|')
        i = 2;
    if (i < n && in[i] == '^')
        i++;
    e = n;
    if (e > i && in[e - 1] == '^')
        e--;
    for (*a = i; i < e; i++)
        if (strchr("#:&*{}=<>|", in[i]))
            return 0;
    *b = e;
    return 1;
}

/* is it after an opening: nothing, a space, ( [ { < / - or an opening
 * quote or a dash */
static int opens(const char *before)
{
    if (!before[0])
        return 1;
    if (!before[1])
        return strchr(" \t\n([{</-", before[0]) != 0;
    return !strcmp(before, "\342\200\230") || !strcmp(before, Q_OPEN) ||
        !strcmp(before, "\342\200\224") || !strcmp(before, "\342\200\223");
}

/* text[a..b) with its quotes made typographic, onto out */
static void curly_part(const char *text, int a, int b, char *before,
    char *out, int size)
{
    int i, k, n;
    char c[8];

    for (i = a; i < b; i += k) {
        k = 1;
        if (text[i] == '\'') {
            scopy(c, Q_APOS, sizeof(c));
        } else if (text[i] == '"') {
            scopy(c, opens(before) ? Q_OPEN : Q_CLOSE, sizeof(c));
        } else {
            while ((unsigned char)text[i + k] >= 0x80 &&
                (unsigned char)text[i + k] < 0xc0 && i + k < b && k < 4)
            {
                k++;           /* the rest of a UTF-8 character */
            }
            memcpy(c, text + i, k);
            c[k] = 0;
        }
        scopy(before, c, 8);
        n = strlen(out);
        if (n + (int)strlen(c) < size)
            strcpy(out + n, c);
    }
}

static void put_raw(char *out, int size, const char *s, int k)
{
    int n;

    n = strlen(out);
    if (n + k < size) {
        memcpy(out + n, s, k);
        out[n + k] = 0;
    }
}

void typographic(const char *text, char *out, int size)
{
    const char *p, *e;
    char before[8];
    int words, a, b, n, i;

    scopy(out, text, size);
    if (text[0] == '=' || (!strchr(text, '\'') && !strchr(text, '"')))
        return;
    /* a letter or digit in the text, or in a text brace? */
    words = 0;
    for (p = text; *p && !words; p++) {
        if (*p == '{') {
            e = strchr(p, '}');
            if (!e)
                break;
            if (text_brace(p + 1, (int)(e - p - 1), &a, &b))
                for (i = a; i < b && !words; i++)
                    words = is_word_char(p + 1 + i);
            p = e;
        } else {
            words = is_word_char(p);
        }
    }
    if (!words)
        return;
    out[0] = 0;
    before[0] = 0;
    for (p = text; *p; p = e) {
        e = *p == '{' ? strchr(p, '}') : 0;
        if (e) {
            /* a brace: as it is, but the text of a text brace */
            e++;
            n = (int)(e - p);
            if (text_brace(p + 1, n - 2, &a, &b) && b > a) {
                put_raw(out, size, p, a + 1);
                curly_part(p, a + 1, b + 1, before, out, size);
                put_raw(out, size, p + b + 1, n - b - 1);
            } else {
                put_raw(out, size, p, n);
            }
        } else {
            e = strchr(p + 1, '{');
            if (!e)
                e = p + strlen(p);
            curly_part(p, 0, (int)(e - p), before, out, size);
        }
    }
}

int tool_add(const char *outline, const char *text)
{
    int res;
    char norm[STROKE_MAX * 32], typo[TEXT_MAX];
    const char *t;

    t = target();
    if (!t) {
        say("no .dict dictionary to write to", 0);
        return 1;
    }
    if (!lookup_outline(outline, norm, sizeof(norm))) {
        say("not an outline: %s", outline);
        return 1;
    }
    typographic(text, typo, sizeof(typo));
    show_conflicts(norm, typo);
    res = write_entry(t, norm, typo);
    if (res < 0)
        return 1;
    printf("%s: %s -> %s\n", t, norm, typo);
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
    char typo[TEXT_MAX];
    const char *t;

    t = target();
    if (!t) {
        say("no .dict dictionary to write to", 0);
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
        res = ask_line("translation: ", typo, sizeof(typo));
        if (res <= 0)
            continue;
        /* straight quotes saved typographic, as in Dotterel */
        typographic(typo, text, sizeof(text));
        if (strcmp(typo, text))
            printf("  saved as %s\n", text);
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
