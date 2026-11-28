/* The terminal tools around the engine: the feeds for the paper tape
 * and suggestions (view.c shows them), adding a stroke (steno -a, -A)
 * and looking one up (steno -l).
 *
 * The engine writes to two FIFOs in $XDG_STATE_HOME/steno, each only
 * while a viewer has it open, so nothing is kept anywhere:
 *
 *   tape      one line a stroke: STROKE <tab> TRANSLATION <tab> N,
 *             N - 1 the strokes it took in or replaced, whose lines
 *             the viewer clears; a * is STROKE <tab> * <tab> 0 <tab>
 *             K, K the translations it took back, one line each; and
 *             after a stroke that made one word with the ones before
 *             (a suffix, a fingerspelled letter, #SU): = <tab> WORD
 *             <tab> N, the word as it is now, shown on the last of its
 *             N strokes, the others cleared
 *   suggest   one line when the last words change: WORDS <tab>
 *             OUTLINES [<tab> WORDS <tab> OUTLINES]..., the outlines
 *             shortest first, the one used marked + (none shorter) or
 *             ! (one is); a word no dictionary has gets the ways to
 *             write it that work (stem + suffix, prefix + word, letter
 *             by letter); after ~ <tab> when they take the place of
 *             the last line's (the stroke joined the word before, or
 *             *)
 *
 * Tabs, newlines and backslashes in the text are written as \t, \n
 * and \\. Without a suggestions viewer, no suggestions are looked for.
 */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "steno.h"

#define TAPE_LINE 4000         /* under PIPE_BUF: one write, whole */
#define SUGGEST_WORDS 4
#define SUGGEST_MAX 6

char user_dict[1024];          /* where -a writes ("user" setting) */

static int tape_fd = -1, sugg_fd = -1;

/* $XDG_STATE_HOME/steno/NAME, or ~/.system/steno/NAME; the
 * directories are made (0700) when missing */
const char *feed_path(const char *name)
{
    static char p[1100];
    static int dir_len;        /* the directory is made once */
    const char *x;
    char *s;

    if (dir_len) {
        p[dir_len] = 0;
        scat(p, name, sizeof(p));
        return p;
    }
    x = getenv("XDG_STATE_HOME");
    if (x && x[0] == '/')
        scopy(p, x, 1000);
    else
        scopy(p, config_path("~/.system"), 1000);
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
    dir_len = strlen(p);
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

/* --- the words just written ---------------------------------------- *
 *
 * The suggestions and the tape both look at the last words as the
 * translations that wrote them: a word is a run of the newest
 * translations whose text, as it is now, starts a word, and that no
 * translation before the run reaches into. So the strokes of a word
 * are the ones that really wrote it: KAT/-S for cats, KHROR/#SU for
 * color (never #SU alone), KPA left out of Cat, and o'clock is one
 * word, not o + clock. */

#define CAND_BACK 32           /* translations looked back over */
#define CAND_MAX 8

struct cand {
    long start;                /* where its text starts (as trans->lo) */
    char text[TEXT_MAX];       /* the words as they are now, trimmed */
    char used[TEXT_MAX];       /* the strokes that wrote them: KAT/-S */
    int nst;                   /* strokes in used */
    int nlines;                /* strokes on the tape for it (KPA too) */
    int ntrans;                /* translations in it */
};

static int all_space(const char *s)
{
    for (; *s; s++)
        if (*s != ' ' && *s != '\t' && *s != '\n' && *s != '\r')
            return 0;
    return 1;
}

/* . ? -- : only symbols, no letter, digit or space */
static int symbol_only(const char *s)
{
    int n;

    const unsigned char *p;

    p = (const unsigned char *)s;
    for (n = 0; p[n]; n++) {
        if (p[n] == 0xe2 && p[n + 1] == 0x80 && p[n + 2]) {
            n += 2;            /* quotes, dashes, the ellipsis */
            continue;
        }
        if ((p[n] >= 'a' && p[n] <= 'z') || (p[n] >= 'A' && p[n] <= 'Z') ||
            (p[n] >= '0' && p[n] <= '9') || p[n] == ' ' || p[n] == '\t' ||
            p[n] >= 0x80)
        {
            return 0;
        }
    }
    return n > 0;
}

/* One word for guessing outlines: letters, digits, ' and - inside,
 * and any letter beyond ASCII (but quotes, dashes, the ellipsis) */
static int one_word(const char *s)
{
    const unsigned char *p;
    int n;

    p = (const unsigned char *)s;
    if (!*p)
        return 0;
    for (n = 0; p[n]; n++) {
        if ((p[n] >= 'a' && p[n] <= 'z') || (p[n] >= 'A' && p[n] <= 'Z') ||
            (p[n] >= '0' && p[n] <= '9'))
        {
            continue;
        }
        if ((p[n] == '\'' || p[n] == '-') && n > 0 && p[n + 1])
            continue;
        if (p[n] == 0xe2 && p[n + 1] == 0x80 && p[n + 2] == 0x99 && n > 0) {
            n += 2;            /* the apostrophe, as in don't */
            continue;
        }
        if (p[n] == 0xe2)
            return 0;          /* other punctuation: quotes, dashes */
        if (p[n] < 0x80)
            return 0;
    }
    return 1;
}

/* ends by joining the next one on: {for^}, {^-^}, an opening bracket;
 * the word it starts is not there yet */
static int joins_next(struct trans *t)
{
    int n;

    if (!t->has_english)
        return 0;
    n = strlen(t->english);
    return n >= 2 && t->english[n - 1] == '}' && t->english[n - 2] == '^';
}

/* wrote nothing at all (KPA, {^}): not part of a word's strokes */
static int wrote_nothing(struct trans *t)
{
    return !t->retro && t->lo == t->end;
}

/* The last words, the shortest first, at most max; 0 when the newest
 * word isn't finished ({for^}) or there is none. Translations at the
 * end that wrote only spaces or nothing (Enter, KPA) are not in it.
 * Only up to the start of the line, and SUGGEST_WORDS words. */
static int candidates(struct cand *c, int max, int *last_out)
{
    static char text[TEXT_MAX];
    struct trans *t, *p;
    char s[32];
    long start, prev;
    int n, last, low, i, j, k, word, nc, len, spaces;

    nc = 0;
    n = history_count();
    low = n - CAND_BACK > 0 ? n - CAND_BACK : 0;
    for (last = n - 1; last >= low; last--) {
        t = history_at(last);
        if (!t->nact)
            continue;
        len = text_since(t->lo, text, sizeof(text), &word);
        if (len < 0)
            return 0;
        if (!all_space(text))
            break;
    }
    if (last_out)
        *last_out = last;
    if (last < low || joins_next(history_at(last)))
        return 0;
    start = -1;
    prev = -1;
    for (i = last; i >= low && nc < max; i--) {
        t = history_at(i);
        if (!t->nact)
            continue;
        if (start < 0 || t->lo < start)
            start = t->lo;
        if (t->lo != start || start == prev)
            continue;          /* one after it reaches before it */
        /* none before it may have written past its start: then it
         * changed them (#SU on colour, a capital on a spelled word) */
        for (j = i - 1; j >= low; j--) {
            p = history_at(j);
            if (p->nact && p->end > start && p->end > p->lo)
                break;
        }
        if (j >= low)
            continue;
        len = text_since(start, text, sizeof(text), &word);
        if (len < 0)
            break;
        if (strchr(text, '\n') && !all_space(text)) {
            k = 0;             /* only this line */
            while (text[k] == '\n' || text[k] == ' ')
                k++;
            if (strchr(text + k, '\n'))
                break;
        }
        trim(text);
        if (!text[0])
            continue;
        for (spaces = 0, k = 0; text[k]; k++)
            if (text[k] == ' ')
                spaces++;
        if (spaces >= SUGGEST_WORDS)
            break;
        if (!word && !symbol_only(text))
            continue;          /* the end of a word: -S of cats */
        prev = start;
        for (j = i; j <= last && history_at(j)->has_english; j++)
            ;
        if (j <= last)
            break;             /* raw strokes (no entry): no words */
        scopy(c[nc].text, text, TEXT_MAX);
        c[nc].start = start;
        c[nc].used[0] = 0;
        c[nc].nst = 0;
        c[nc].nlines = 0;
        c[nc].ntrans = 0;
        for (j = i; j <= last; j++) {
            t = history_at(j);
            c[nc].nlines += t->nst;
            if (wrote_nothing(t) && j != last)
                continue;
            c[nc].ntrans++;
            for (k = 0; k < t->nst; k++) {
                stroke_text(t->st[k], s);
                if (c[nc].used[0])
                    scat(c[nc].used, "/", TEXT_MAX);
                scat(c[nc].used, s, TEXT_MAX);
                c[nc].nst++;
            }
        }
        nc++;
    }
    return nc;
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

#define OUTS_MAX 64

/* "A  B  C" into its outlines (marks + and ! kept) */
static int split_outlines(const char *o, char out[][TEXT_MAX / 4], int max)
{
    const char *p, *q;
    int n, len;

    n = 0;
    for (p = o; *p && n < max; p = q) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        for (q = p; *q && *q != ' '; q++)
            ;
        len = (int)(q - p);
        if (len >= TEXT_MAX / 4)
            continue;
        memcpy(out[n], p, len);
        out[n][len] = 0;
        n++;
    }
    return n;
}

static const char *unmarked(const char *o)
{
    return o[0] == '+' || o[0] == '!' ? o + 1 : o;
}

/* o is "A  B  C". The outline used gets a + in front when no outline
 * listed has fewer strokes (the same number is as good), a ! when one
 * has; it goes in when it isn't there, and all of them are sorted,
 * shortest first (strokes, then keys). */
static void mark_used(const struct cand *c, char *o, int size)
{
    static char outs[OUTS_MAX][TEXT_MAX / 4];
    char tmp[TEXT_MAX / 4];
    int n, i, j, best, k;

    n = split_outlines(o, outs, OUTS_MAX - 1);
    for (i = 0; i < n && strcmp(outs[i], c->used); i++)
        ;
    if (i == n && c->used[0] && (int)strlen(c->used) < TEXT_MAX / 4) {
        scopy(outs[n], c->used, TEXT_MAX / 4);
        n++;
    }
    for (i = 1; i < n; i++) {
        for (j = i; j > 0 && outline_text_cmp(unmarked(outs[j - 1]),
            unmarked(outs[j])) > 0; j--)
        {
            scopy(tmp, outs[j], sizeof(tmp));
            scopy(outs[j], outs[j - 1], sizeof(tmp));
            scopy(outs[j - 1], tmp, sizeof(tmp));
        }
    }
    best = n ? outline_strokes(outs[0], strlen(outs[0])) : 0;
    o[0] = 0;
    for (i = 0; i < n; i++) {
        if (i)
            scat(o, "  ", size);
        if (c->used[0] && !strcmp(outs[i], c->used)) {
            k = strlen(o);
            if (k < size - 1) {
                o[k] = c->nst <= best ? '+' : '!';
                o[k + 1] = 0;
            }
        }
        scat(o, outs[i], size);
    }
}

/* --- guesses: ways to write a word that has no outline -------------- *
 *
 * A word no dictionary has can still be written: a stem and a suffix
 * (figure + -S, or -S folded into the stroke), a prefix and a word,
 * or letter by letter. The guesses are tried for real, in a copy of
 * steno (fork): only the ones that type the word are kept, and the
 * history you can undo is not touched. */

#define AFFIX_MAX 512

struct affix {
    char text[32];             /* the letters: ed, ing, for */
    char outline[64];          /* its shortest outline */
    int nst;
};

static struct affix suffixes[AFFIX_MAX], prefixes[AFFIX_MAX];
static int nsuffixes, nprefixes;
static long affix_gen = -1;

/* {^ing} or {for^}: 1 suffix, 2 prefix, 0 neither; the letters into
 * out */
static int affix_of(const char *v, char *out, int size)
{
    int n, k, suffix;

    n = strlen(v);
    if (n < 4 || v[0] != '{' || v[n - 1] != '}')
        return 0;
    suffix = v[1] == '^' && v[n - 2] != '^';
    if (!suffix && !(v[1] != '^' && v[n - 2] == '^'))
        return 0;
    v += suffix ? 2 : 1;
    n -= 3;
    if (n < 1 || n >= size)
        return 0;
    for (k = 0; k < n; k++)
        if (!(v[k] >= 'a' && v[k] <= 'z'))
            return 0;
    memcpy(out, v, n);
    out[n] = 0;
    return suffix ? 1 : 2;
}

static int keep_affix(const char *v, const long *st, int n, void *arg)
{
    char text[32], o[64], s[32];
    struct affix *a, *list;
    int kind, i, *count;

    (void)arg;
    kind = affix_of(v, text, sizeof(text));
    if (!kind || n > 2)
        return 0;
    list = kind == 1 ? suffixes : prefixes;
    count = kind == 1 ? &nsuffixes : &nprefixes;
    o[0] = 0;
    for (i = 0; i < n; i++) {
        stroke_text(st[i], s);
        if (i)
            scat(o, "/", sizeof(o));
        scat(o, s, sizeof(o));
    }
    for (i = 0; i < *count; i++) {
        a = &list[i];
        if (!strcmp(a->text, text)) {
            if (outline_text_cmp(o, a->outline) < 0) {
                scopy(a->outline, o, sizeof(a->outline));
                a->nst = n;
            }
            return 0;
        }
    }
    if (*count == AFFIX_MAX)
        return 0;
    a = &list[*count];
    scopy(a->text, text, sizeof(a->text));
    scopy(a->outline, o, sizeof(a->outline));
    a->nst = n;
    (*count)++;
    return 0;
}

static void load_affixes(void)
{
    if (affix_gen == dict_generation())
        return;
    nsuffixes = 0;
    nprefixes = 0;
    dict_each(keep_affix, 0);
    affix_gen = dict_generation();
}

#define GUESS_MAX 48

static char guess[GUESS_MAX][TEXT_MAX / 4];
static int nguess;

static void add_guess(const char *a, const char *b)
{
    int i;

    if (nguess == GUESS_MAX || strlen(a) + strlen(b) + 2 >= TEXT_MAX / 4)
        return;
    scopy(guess[nguess], a, TEXT_MAX / 4);
    if (b[0]) {
        scat(guess[nguess], "/", TEXT_MAX / 4);
        scat(guess[nguess], b, TEXT_MAX / 4);
    }
    for (i = 0; i < nguess; i++)
        if (!strcmp(guess[i], guess[nguess]))
            return;
    nguess++;
}

/* the shortest two outlines of a word, each with b after it */
static void guess_with(const char *word, const char *b, int before)
{
    static char outs[4][TEXT_MAX / 4];
    char o[TEXT_MAX];
    int n, i;

    if (!word[0] || !dict_outlines(word, o, sizeof(o), 2))
        return;
    n = split_outlines(o, outs, 2);
    for (i = 0; i < n; i++) {
        if (before)
            add_guess(b, outs[i]);
        else
            add_guess(outs[i], b);
    }
}

/* a stem's outline with a one-key suffix in its last stroke: KAT, -S:
 * KATS (folding, as Plover does when KATS is no entry) */
static void guess_folded(const char *stem, const char *suffix)
{
    static char outs[4][TEXT_MAX / 4];
    char o[TEXT_MAX];
    long st[STROKE_MAX], sx;
    int n, i, k;

    if (outline_parse(suffix, &sx, 1) != 1 || !sx || (sx & (sx - 1)))
        return;
    if (!dict_outlines(stem, o, sizeof(o), 2))
        return;
    n = split_outlines(o, outs, 2);
    for (i = 0; i < n; i++) {
        k = outline_parse(outs[i], st, STROKE_MAX);
        if (k <= 0 || (st[k - 1] & sx))
            continue;
        st[k - 1] |= sx;
        outline_text(st, k, o, sizeof(o));
        add_guess(o, "");
    }
}

/* stems that word could come from with this ending, as Plover's
 * spelling rules make them: figure+s, make+ing, happy+ly, run+ing,
 * carry+s */
static int strict_affixes;     /* steno -W: no one-letter endings but
                                  s, stems of three letters or more */

static void guess_suffix(const char *w, const struct affix *a)
{
    char stem[TEXT_MAX], v[TEXT_MAX];
    int n, k, cut, m;

    n = strlen(w);
    k = strlen(a->text);
    if (strict_affixes && ((k == 1 && a->text[0] != 's') || n - k < 3))
        return;
    for (cut = k; cut >= k - 1 && cut >= 1; cut--) {
        if (n - cut < 2 || strcmp(w + n - cut, a->text + (k - cut)))
            continue;
        memcpy(stem, w, n - cut);
        stem[n - cut] = 0;
        m = n - cut;
        /* as it is, with an e, without a doubled last letter, y for i,
         * y for ie */
        guess_with(stem, a->outline, 0);
        guess_folded(stem, a->outline);
        scopy(v, stem, sizeof(v));
        scat(v, "e", sizeof(v));
        guess_with(v, a->outline, 0);
        guess_folded(v, a->outline);
        if (m >= 3 && stem[m - 1] == stem[m - 2]) {
            scopy(v, stem, sizeof(v));
            v[m - 1] = 0;
            guess_with(v, a->outline, 0);
            guess_folded(v, a->outline);
        }
        if (stem[m - 1] == 'i') {
            scopy(v, stem, sizeof(v));
            v[m - 1] = 'y';
            guess_with(v, a->outline, 0);
            guess_folded(v, a->outline);
        }
        if (m >= 3 && stem[m - 1] == 'e' && stem[m - 2] == 'i') {
            scopy(v, stem, sizeof(v));
            v[m - 2] = 'y';
            v[m - 1] = 0;
            guess_with(v, a->outline, 0);
            guess_folded(v, a->outline);
        }
    }
}

/* letter by letter: {>}{&a} for a small letter, {&A} for a capital */
static void guess_spelled(const char *w)
{
    char o[TEXT_MAX], all[TEXT_MAX], t[16];
    static char outs[2][TEXT_MAX / 4];
    int i;

    all[0] = 0;
    for (i = 0; w[i]; i++) {
        if ((unsigned char)w[i] >= 0x80)
            return;
        sprintf(t, w[i] >= 'A' && w[i] <= 'Z' ? "{&%c}" : "{>}{&%c}",
            w[i]);
        if (!dict_outlines(t, o, sizeof(o), 1)) {
            sprintf(t, "{&%c}", w[i]);
            if (!dict_outlines(t, o, sizeof(o), 1))
                return;
        }
        split_outlines(o, outs, 1);
        if (all[0])
            scat(all, "/", sizeof(all));
        scat(all, outs[0], sizeof(all));
    }
    if (strlen(all) < TEXT_MAX / 4)
        add_guess(all, "");
}

static void outline_typed(const long *st, int n, char *out, int size);

static int same_fold(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (*a != *b && !((*a | 32) == (*b | 32) && (*a | 32) >= 'a' &&
            (*a | 32) <= 'z'))
        {
            return 0;
        }
    return *a == *b;
}

/* each guess for real, in a child: a bit for each that types w */
static void try_guesses(const char *w, unsigned char *ok)
{
    char out[TEXT_MAX];
    long st[STROKE_MAX];
    int fd[2], i, n, r, got;
    pid_t pid;

    memset(ok, 0, GUESS_MAX);
    if (!nguess || pipe(fd) < 0)
        return;
    pid = fork();
    if (pid < 0) {
        close(fd[0]);
        close(fd[1]);
        return;
    }
    if (pid == 0) {
        /* nothing of the engine's may be reached from here: X, the
         * keyboard, the feeds, the terminal */
        for (i = 3; i < 1024; i++)
            if (i != fd[1])
                close(i);
        i = open("/dev/null", O_RDWR);
        if (i >= 0) {
            dup2(i, 0);
            dup2(i, 1);
            dup2(i, 2);
        }
        output_on = 0;
        translate_reset();
        for (i = 0; i < nguess; i++) {
            n = outline_parse(guess[i], st, STROKE_MAX);
            ok[i] = 0;
            if (n > 0) {
                outline_typed(st, n, out, sizeof(out));
                ok[i] = same_fold(out, w);
            }
        }
        r = write(fd[1], ok, nguess);
        (void)r;
        _exit(0);
    }
    close(fd[1]);
    got = 0;
    while (got < nguess) {
        r = read(fd[0], ok + got, nguess - got);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            break;
        got += r;
    }
    close(fd[0]);
    while (waitpid(pid, 0, 0) < 0 && errno == EINTR)
        ;
    if (got < nguess)
        memset(ok, 0, GUESS_MAX);
}

/* Ways to write w that work, shortest first, as "A  B", at most max;
 * the one used (it wrote w) is among them. */
#define GUESS_CACHE 16

struct guessed {
    long gen;                  /* dict_generation() + 1; 0: empty */
    char text[64];
    char out[TEXT_MAX];        /* the ones that work, sorted */
};
static struct guessed guessed[GUESS_CACHE];
static int next_guessed;

static int guesses(const struct cand *c, char *out, int size, int max)
{
    static char lower[TEXT_MAX], sw[TEXT_MAX];
    static char swo[1][TEXT_MAX / 4];
    unsigned char ok[GUESS_MAX];
    const char *w;
    struct guessed *g;
    int i, n, k;

    w = c->text;
    /* a word asked again (typed again, or the suggestions sent
     * again): what was found for it */
    for (i = 0; i < GUESS_CACHE; i++) {
        g = &guessed[i];
        if (g->gen == dict_generation() + 1 && !strcmp(g->text, w)) {
            scopy(out, g->out, size);
            goto found;
        }
    }
    load_affixes();
    nguess = 0;
    scopy(lower, w, sizeof(lower));
    for (i = 0; lower[i]; i++)
        if (lower[i] >= 'A' && lower[i] <= 'Z')
            lower[i] += 32;
    for (i = 0; i < nsuffixes; i++)
        guess_suffix(lower, &suffixes[i]);
    for (i = 0; i < nprefixes; i++) {
        k = strlen(prefixes[i].text);
        if (!strncmp(lower, prefixes[i].text, k) && (int)strlen(lower) - k
            >= 2)
        {
            guess_with(lower + k, prefixes[i].outline, 1);
        }
    }
    /* an American spelling: the British word, then the UK/US switch
     * (KHROR/#SU: color) */
    if (uk_spelling(w, lower, sizeof(lower)) && dict_outlines("{:retro_us}",
        sw, sizeof(sw), 1))
    {
        split_outlines(sw, swo, 1);
        guess_with(lower, swo[0], 0);
    }
    guess_spelled(w);
    try_guesses(w, ok);
    out[0] = 0;
    n = 0;
    /* shortest first: the ones that work, sorted */
    for (i = 0; i < nguess; i++) {
        if (!ok[i])
            continue;
        if (n)
            scat(out, "  ", size);
        scat(out, guess[i], size);
        n++;
    }
    if (strlen(w) < sizeof(g->text) && strlen(out) < sizeof(g->out)) {
        g = &guessed[next_guessed];
        next_guessed = (next_guessed + 1) % GUESS_CACHE;
        g->gen = dict_generation() + 1;
        scopy(g->text, w, sizeof(g->text));
        scopy(g->out, out, sizeof(g->out));
    }
found:
    mark_used(c, out, size);
    /* at most max, the used one kept */
    {
        static char outs[OUTS_MAX][TEXT_MAX / 4];
        int m, kept;

        m = split_outlines(out, outs, OUTS_MAX);
        out[0] = 0;
        kept = 0;
        for (i = 0; i < m; i++) {
            if (kept >= max - 1 && outs[i][0] != '+' && outs[i][0] != '!')
                continue;
            if (kept >= max && (outs[i][0] == '+' || outs[i][0] == '!'))
                continue;
            if (kept)
                scat(out, "  ", size);
            scat(out, outs[i], size);
            kept++;
        }
        return kept;
    }
}

/* --- the suggestions ------------------------------------------------ */

/* The last few words (the longest first, the newest word last) and how
 * else they are written. Sent only when they change; a new viewer gets
 * the current ones. replace: the words sent last go (taken in by this
 * stroke, or undone). */
static long sent_start = -1;   /* where the newest word sent starts */

static void suggestions(int fresh, int replace, struct cand *c, int nc)
{
    static char last_key[TEXT_MAX];
    char key[TEXT_MAX], o[TEXT_MAX], out[TAPE_LINE];
    int i, res;

    key[0] = 0;
    for (i = 0; i < nc; i++) {
        scat(key, c[i].text, sizeof(key));
        scat(key, "\t", sizeof(key));
        scat(key, c[i].used, sizeof(key));
        scat(key, "\t", sizeof(key));
    }
    if (!fresh && !strcmp(key, last_key))
        return;
    scopy(last_key, key, sizeof(last_key));
    scopy(out, replace && !fresh ? "~" : "", sizeof(out));
    for (i = nc - 1; i >= 0; i--) {
        res = dict_outlines(c[i].text, o, sizeof(o), SUGGEST_MAX);
        if (res)
            mark_used(&c[i], o, sizeof(o));
        else if (one_word(c[i].text))
            res = guesses(&c[i], o, sizeof(o), SUGGEST_MAX);
        if (!res)
            continue;
        if (out[0] && strcmp(out, "~"))
            scat(out, "\t", sizeof(out));
        else if (out[0])
            scat(out, "\t", sizeof(out));
        put_escaped(out, sizeof(out) - TEXT_MAX, c[i].text);
        scat(out, "\t", sizeof(out));
        scat(out, o, sizeof(out) - 2);
    }
    sent_start = nc ? c[0].start : -1;
    if (!out[0])
        return;
    /* ~ alone: the words sent last are gone (undone), nothing new */
    scat(out, "\n", sizeof(out));
    feed_write(&sugg_fd, out);
}

/* --- the tape ------------------------------------------------------- */

/* what a translation that writes no text does, in words */
static void describe(const char *e, char *out, int size)
{
    static const char *const known[][2] = {
        { "{:case:cap_first_word}", "(capital next)" },
        { "{-|}", "(capital next)" },
        { "{>}", "(small letter next)" },
        { "{<}", "(capitals next)" },
        { "{*-|}", "(capital last)" },
        { "{*>}", "(small letter last)" },
        { "{*<}", "(capitals last)" },
        { "{^}", "(join)" },
        { "{}", "(nothing)" },
    };
    char t[TEXT_MAX];
    int i, n, found;

    out[0] = 0;
    scopy(t, e, sizeof(t));
    /* each meta it is made of, said; any other as it is */
    found = 0;
    for (i = 0; i < (int)(sizeof(known) / sizeof(known[0])); i++) {
        if (strstr(t, known[i][0])) {
            if (out[0] && strcmp(known[i][1], "(nothing)"))
                scat(out, " ", size);
            if (strcmp(known[i][1], "(nothing)") || !found)
                scat(out, known[i][1], size);
            found = 1;
        }
    }
    if (found && strcmp(out, "(nothing)"))
        return;
    n = strlen(t);
    if (n > 3 && t[0] == '{' && t[1] == '#' && t[n - 1] == '}') {
        t[n - 1] = 0;
        scopy(out, t + 2, size);   /* the keys: Control_L(BackSpace) */
        return;
    }
    if (found)
        return;
    put_escaped(out, size, e);
}

/* What the last stroke did: the text it wrote, as it is in your
 * document; what it does when it writes nothing (capital next, the
 * keys it pressed); its strokes when they mean nothing; nothing when
 * it changed nothing, or * when it undid. Gives 1 + the strokes of the
 * translations it took in or replaced (the tape clears their lines:
 * KAT/A*LG, SP-LS on cat hat, #* on KAT), 0 for *. */
int stroke_result(char *out, int size)
{
    static char t[TEXT_MAX];
    char s[STROKE_MAX * 32];
    struct trans *tr;
    int n, word, k, i;

    out[0] = 0;
    if (stroke_undo) {
        scopy(out, "*", size);
        return 0;
    }
    n = history_count();
    if (!stroke_added || !n)
        return 1;              /* nothing happened: just the stroke */
    tr = history_at(n - 1);
    if (tr->has_english && tr->nact && text_since(tr->lo, t, sizeof(t),
        &word) >= 0 && t[0])
    {
        if (all_space(t)) {
            /* Enter, Tab: shown as signs */
            for (i = 0, k = 0; t[i]; i++) {
                if (t[i] == '\n')
                    scat(out, "\342\206\265", size);   /* return */
                else if (t[i] == '\t')
                    scat(out, "\342\207\245", size);   /* tab */
                else
                    k++;
            }
            if (!out[0] && k)
                scopy(out, "(space)", size);
        } else {
            trim(t);
            put_escaped(out, size, t);
        }
    } else if (tr->has_english) {
        describe(tr->english, out, size);
    } else {
        outline_text(tr->st, tr->nst, s, sizeof(s));
        put_escaped(out, size, s);
    }
    return 1 + stroke_removed_strokes;
}

/* after each stroke */
void tape_stroke(long st)
{
    static struct cand c[CAND_MAX];
    char line[TAPE_LINE + 64];
    struct trans *t;
    int n, was, open_now, k, made, nc, last;

    n = history_count();
    t = n ? history_at(n - 1) : 0;
    made = t && stroke_added && !stroke_undo;
    open_now = feed_open(&tape_fd, "tape");
    was = sugg_fd >= 0;
    nc = 0;
    last = -1;
    if (output_on)
        feed_open(&sugg_fd, "suggest");
    if (open_now || sugg_fd >= 0)
        nc = candidates(c, CAND_MAX, &last);
    if (open_now) {
        stroke_text(st, line);
        scat(line, "\t", sizeof(line));
        n = strlen(line);
        k = stroke_result(line + n, TAPE_LINE - n);
        n = strlen(line);
        if (k)
            sprintf(line + n, "\t%d\n", k);
        else    /* * : how many translations it took back (as the
                 * engine does: no-text ones such as KPA go with the
                 * word), each one line */
            sprintf(line + n, "\t0\t%d\n", stroke_removed);
        feed_write(&tape_fd, line);
    }
    /* a stroke that made one word with the ones before (a suffix, a
     * letter of a fingerspelled word, #SU on colour, SP-LS): the tape
     * shows the word, on the strokes of all that wrote it */
    if (tape_fd >= 0 && nc && c[0].ntrans > 1 && c[0].nlines > 1 &&
        last == history_count() - 1 && (made || stroke_undo))
    {
        scopy(line, "=\t", sizeof(line));
        put_escaped(line, TAPE_LINE, c[0].text);
        n = strlen(line);
        sprintf(line + n, "\t%d\n", c[0].nlines);
        feed_write(&tape_fd, line);
    }
    if (!output_on || sugg_fd < 0)
        return;
    /* the words sent last go when this stroke undid them, or made
     * the newest word again from where it started or before (cat, then
     * cats; colour, then color), not when it starts one of its own */
    suggestions(!was, stroke_undo || (made && nc && sent_start >= 0 &&
        c[0].start <= sent_start), c, nc);
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

/* What steno types for an outline that is not in the dictionaries as
 * a whole (KAT/-S: cats, SKRAOEUBD: scribed): its strokes through the
 * translator, from nothing, with output off (no keys, no commands).
 * Empty when it types nothing. */
static void outline_typed(const long *st, int n, char *out, int size)
{
    char t[TEXT_MAX];
    int i;

    output_on = 0;
    for (i = 0; i < n; i++) {
        translate(st[i]);
        dict_stroked();
    }
    last_chars(TEXT_MAX / 4 - 1, t, sizeof(t));
    trim(t);
    out[0] = 0;
    put_escaped(out, size, t);
    translate_reset();
    output_on = 1;
}

int tool_lookup(const char *s)
{
    int res;
    char out[TEXT_MAX * 8], norm[STROKE_MAX * 32], parts[TEXT_MAX * 2];
    char *o, *e;
    long st[STROKE_MAX];
    int n, found, got, end, w, old;

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
    } else if (n > 0) {
        /* not an entry, but steno still types something for it, as
         * KAT/-S does (cat with -S on it) */
        outline_typed(st, n, out, sizeof(out));
        outline_text(st, n, norm, sizeof(norm));
        if (out[0] && strcmp(out, norm)) {
            printf("%s is no entry, but types\n  %s\n", norm, out);
            outline_parts(st, n, parts, sizeof(parts));
            if (parts[0])
                printf("  stroke by stroke: %s\n", parts);
            found = 1;
        }
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
    old = dict_off_outlines(s, out, sizeof(out), 50);
    if (old) {
        /* the old ones you turned off: how you used to write it */
        printf("%s is written in dictionaries that are off\n", s);
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
            outline_off_says(o, parts, sizeof(parts));
            printf("  %-24s  %s\n", o, parts);
        }
    }
    if (!found && one_word(s) && strlen(s) < TEXT_MAX) {
        /* no entry: the ways to write it that work, as the
         * suggestions give them */
        static struct cand c;

        memset(&c, 0, sizeof(c));
        scopy(c.text, s, sizeof(c.text));
        if (guesses(&c, out, sizeof(out), 8)) {
            printf("%s has no entry, but is written\n", s);
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
                    printf("  %-24s  %s\n", o, parts);
                else
                    printf("  %s\n", o);
            }
            found = 1;
        }
    }
    if (!found)
        printf("%s: not in the dictionaries%s\n", s, old ? " that are on" :
            "");
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

/* put_str: s at o + n; the new end (o stays a C string). Saving
 * with strcat re-read the whole output for every line: a big
 * dictionary took minutes. */
static long put_str(char *o, long n, const char *s)
{
    long k;

    k = (long)strlen(s);
    memcpy(o + n, s, (unsigned long)k + 1);
    return n + k;
}

/* Put outline -> text in a .dict file: on the line of that text when
 * there is one (else a new line at the end), and off any other line,
 * since an outline writes one thing. A line left with no outline
 * goes. */
/* where a write must go: a dictionary that is a link (into a git
 * directory, say) is written through the link, so it stays a link */
static void real_path(const char *path, char *out, long size)
{
    struct stat sb;
    char tmp[1100], *slash;
    long r;
    int i;

    scopy(out, path, size);
    for (i = 0; i < 8; i++) {
        r = lstat(out, &sb);
        if (r < 0 || !S_ISLNK(sb.st_mode))
            return;
        r = readlink(out, tmp, sizeof(tmp) - 1);
        if (r <= 0)
            return;
        tmp[r] = 0;
        if (tmp[0] != '/') {
            slash = strrchr(out, '/');
            if (slash) {
                slash[1] = 0;
                scat(out, tmp, size);
                continue;
            }
        }
        scopy(out, tmp, size);
    }
}

static int write_entry(const char *path, const char *outline,
    const char *text)
{
    char *f, *o, *line, *next, *tab, esc[TEXT_MAX * 2 + 8];
    char rest[TEXT_MAX * 4], tmp[1100], real[1100];
    long size, need, n;
    int done, r, w, fd;

    errno = 0;
    f = read_file(path, &size);
    if (!f && errno != ENOENT) {
        say("can't read %s; nothing was changed", path);
        return -1;
    }
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
    n = 0;
    done = 0;
    for (line = f; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next) {
            *next = 0;
            next++;
        }
        tab = strrchr(line, '\t');
        if (!tab) {
            n = put_str(o, n, line);   /* not an entry: as it was */
            n = put_str(o, n, "\n");
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
        n = put_str(o, n, line);
        n = put_str(o, n, "\t");
        n = put_str(o, n, rest);
        n = put_str(o, n, "\n");
    }
    if (!done) {
        n = put_str(o, n, esc);
        n = put_str(o, n, "\t");
        n = put_str(o, n, outline);
        n = put_str(o, n, "\n");
    }
    free(f);
    /* a new file, then rename: never half a dictionary */
    real_path(path, real, sizeof(real));
    sprintf(tmp, "%.1000s.new", real);
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
    r = rename(tmp, real);
    if (r < 0) {
        say("can't replace %s", real);
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
    else if (n > 0)
        printf("%s is new: no dictionary has it\n", norm);
    got = text ? dict_outlines(text, out, sizeof(out), 20) : 0;
    if (got)
        printf("%s is written now %s\n", text, out);
    else if (text)
        printf("%s has no outline yet\n", text);
}

/* the file name alone, for messages that must fit a box */
static const char *base_name(const char *p)
{
    const char *s;

    s = strrchr(p, '/');
    return s ? s + 1 : p;
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

/* outline -> text into the user dictionary, from the status screen:
 * what happened goes into msg, not to stdout. 0 when added. */
int add_stroke(const char *outline, const char *text, char *msg, int size)
{
    int res;
    char norm[STROKE_MAX * 32], typo[TEXT_MAX];
    const char *t;

    t = target();
    if (!t) {
        scopy(msg, "no .dict dictionary to add to", size);
        return -1;
    }
    if (!lookup_outline(outline, norm, sizeof(norm))) {
        scopy(msg, "not an outline: ", size);
        scat(msg, outline, size);
        return -1;
    }
    if (!text[0]) {
        scopy(msg, "no translation: nothing added", size);
        return -1;
    }
    typographic(text, typo, sizeof(typo));
    res = write_entry(t, norm, typo);
    if (res < 0) {
        scopy(msg, "could not write ", size);
        scat(msg, t, size);
        return -1;
    }
    dict_reload();
    scopy(msg, "added ", size);
    scat(msg, norm, size);
    scat(msg, " -> ", size);
    scat(msg, typo, size);
    return 0;
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
    /* an empty entry would hide the real one; a long one was cut */
    if (!text[0]) {
        say("no text for %s: nothing was added", norm);
        return 1;
    }
    if (strlen(text) > TEXT_MAX / 2) {
        say("the text for %s is too long: nothing was added", norm);
        return 1;
    }
    typographic(text, typo, sizeof(typo));
    show_conflicts(norm, typo);
    res = write_entry(t, norm, typo);
    if (res < 0)
        return 1;
    printf("added %s -> %s (%s)\n", norm, typo, base_name(t));
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

/* --- for steno -W (gen.c) -------------------------------------------- */

/* the ways to write a word from its affixes and the dictionaries
 * (KAT/-S), as the suggestions find them */
int word_guesses(const char *w, char *out, int size, int max)
{
    static struct cand c;

    int r;

    memset(&c, 0, sizeof(c));
    scopy(c.text, w, sizeof(c.text));
    out[0] = 0;
    if (!one_word(w) || strlen(w) >= TEXT_MAX)
        return 0;
    strict_affixes = 1;
    r = guesses(&c, out, size, max);
    strict_affixes = 0;
    return r;
}

/* a line of dict_explain's from a dictionary that is turned off */
static int off_line(const char *p, const char *nl)
{
    return nl - p >= 6 && !strncmp(nl - 6, ", off)", 6);
}

/* what an outline types now: "TEXT  (dictionary)" a line, or what its
 * strokes type when it is no entry ("cats  (no entry)"), or "".
 * Dictionaries that are turned off type nothing, so they don't count. */
void outline_says(const char *outline, char *out, int size)
{
    char t[TEXT_MAX * 8], *p, *nl;
    long st[STROKE_MAX];
    int n;

    out[0] = 0;
    n = outline_parse(outline, st, STROKE_MAX);
    if (n <= 0)
        return;
    n = dict_explain(st, n, t, sizeof(t));
    for (p = t; n && *p; p = nl + 1) {
        /* "  cat  (dotterel-base.dict)\n...": the first one on */
        nl = strchr(p, '\n');
        if (!nl)
            nl = p + strlen(p);
        if (!off_line(p, nl)) {
            *nl = 0;
            while (*p == ' ')
                p++;
            scopy(out, p, size);
            return;
        }
        if (!*nl)
            break;
    }
    n = outline_parse(outline, st, STROKE_MAX);
    outline_typed(st, n, t, sizeof(t));
    if (t[0] && strcmp(t, outline)) {
        scopy(out, t, size);
        scat(out, "  (no entry: its strokes)", size);
    }
}

/* the first dictionary that is turned off and has the outline, as
 * "cat  (dotterel-old-base.dict, off)", or "" */
void outline_off_says(const char *outline, char *out, int size)
{
    char t[TEXT_MAX * 8], *p, *nl;
    long st[STROKE_MAX];
    int n;

    out[0] = 0;
    n = outline_parse(outline, st, STROKE_MAX);
    if (n <= 0)
        return;
    n = dict_explain(st, n, t, sizeof(t));
    for (p = t; n && *p; p = nl + 1) {
        nl = strchr(p, '\n');
        if (!nl)
            nl = p + strlen(p);
        if (off_line(p, nl)) {
            *nl = 0;
            while (*p == ' ')
                p++;
            scopy(out, p, size);
            return;
        }
        if (!*nl)
            break;
    }
}

/* --- the engine's side of steno -W ---------------------------------- *
 *
 *   outline    while steno -W waits for an outline, it has this FIFO
 *              open: each stroke goes there, as STROKE <tab> what it
 *              would write alone, and is not typed (so stroking an
 *              outline needs no raw mode)
 *   ask        steno -W writes "last" to it when it starts; the
 *              engine answers on
 *   lastword   with the last word written, for steno -W to begin with
 */

static int outline_fd = -1;

/* 1: the stroke went to steno -W, don't type it */
int capture_stroke(long st)
{
    char line[TAPE_LINE], s[32];
    const char *v;
    int r;

    if (outline_fd < 0 && !feed_open(&outline_fd, "outline"))
        return 0;
    stroke_text(st, s);
    scopy(line, s, sizeof(line));
    scat(line, "\t", sizeof(line));
    v = dict_lookup(&st, 1);
    if (v)
        put_escaped(line, TAPE_LINE - 2, v);
    scat(line, "\n", sizeof(line));
    r = write(outline_fd, line, strlen(line));
    if (r < 0) {
        /* steno -W is gone: this stroke is typed after all, and the
         * FIFO is looked for again at the next one */
        close(outline_fd);
        outline_fd = -1;
        if (!feed_open(&outline_fd, "outline"))
            return 0;
        r = write(outline_fd, line, strlen(line));
        if (r < 0) {
            close(outline_fd);
            outline_fd = -1;
            return 0;
        }
    }
    return 1;
}

static int ask_rd = -1;

/* the FIFO steno -W asks on, read and write so it never ends; -1 */
int ask_fd(void)
{
    const char *p;

    if (ask_rd >= 0)
        return ask_rd;
    p = feed_path("ask");
    if (mkfifo(p, 0600) < 0 && errno != EEXIST)
        return -1;
    ask_rd = open(p, O_RDWR | O_NONBLOCK);
    if (ask_rd >= 0)
        fcntl(ask_rd, F_SETFD, FD_CLOEXEC);
    return ask_rd;
}

/* steno -W asked: the last word, on lastword */
void ask_answer(void)
{
    char b[256], w[TEXT_MAX];
    int r, fd;

    r = read(ask_rd, b, sizeof(b));
    if (r <= 0)
        return;
    fd = -1;
    if (!feed_open(&fd, "lastword"))
        return;
    last_words(1, w, sizeof(w) - 2);
    trim(w);
    scat(w, "\n", sizeof(w));
    r = write(fd, w, strlen(w));
    (void)r;
    close(fd);
}

/* outline -> text into a .dict file, saved typographic as Dotterel
 * does; 0 when written */
int word_save(const char *path, const char *outline, const char *text)
{
    char norm[STROKE_MAX * 32], typo[TEXT_MAX];
    int r;

    if (!lookup_outline(outline, norm, sizeof(norm)) || !text[0])
        return -1;
    typographic(text, typo, sizeof(typo));
    r = write_entry(path, norm, typo);
    if (r == 0)
        dict_reload();
    return r;
}

/* take outline off whatever it writes in a .dict file (a line left
 * with no outline goes); 0 when done */
int word_drop(const char *path, const char *outline)
{
    char *f, *o, *line, *next, *tab, rest[TEXT_MAX * 4], tmp[1100], real[1100];
    long size, n;
    int r, w, fd;

    f = read_file(path, &size);
    if (!f)
        return -1;
    o = malloc(size + 2);
    if (!o) {
        free(f);
        return -1;
    }
    o[0] = 0;
    n = 0;
    for (line = f; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next) {
            *next = 0;
            next++;
        }
        tab = strrchr(line, '\t');
        if (!tab) {
            n = put_str(o, n, line);
            n = put_str(o, n, "\n");
            continue;
        }
        *tab = 0;
        drop_outline(tab + 1, outline, rest, sizeof(rest));
        if (!rest[0])
            continue;
        n = put_str(o, n, line);
        n = put_str(o, n, "\t");
        n = put_str(o, n, rest);
        n = put_str(o, n, "\n");
    }
    free(f);
    real_path(path, real, sizeof(real));
    sprintf(tmp, "%.1000s.new", real);
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        free(o);
        return -1;
    }
    size = strlen(o);
    r = write(fd, o, size);
    free(o);
    w = close(fd);
    if (r != size || w < 0 || rename(tmp, real) < 0) {
        unlink(tmp);
        return -1;
    }
    dict_reload();
    return 0;
}
