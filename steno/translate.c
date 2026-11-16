/* The translator: strokes -> translations, Plover's greedy way.
 *
 * Each stroke is looked up together with the strokes before it, the
 * longest outline first; a longer outline replaces the translations it
 * takes in (KAT "cat", then KAT/A*LG "catalogue"). The history keeps
 * enough translations for undo_levels strokes; * takes them back. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "steno.h"

#define UNDO_STROKE (1L << 10)    /* * */
#define LIVE_MAX 8192

int undo_levels = 100;

static struct trans **hist;
static int nhist, chist;
static struct trans *tail;        /* the last one no longer undoable */
static struct trans **to_undo;
static int nto_undo, cto_undo;
static int to_do;

/* every translation there is, to free the unused ones after a stroke */
static struct trans *live[LIVE_MAX];
static int nlive;

/* the suffix keys, tried in this order: -Z -D -S -G */
static const long suffix_keys[] = { 1L << 22, 1L << 21, 1L << 20,
    1L << 18 };

static void translate_stroke(long st);

static void *need(void *p, int *cap, int n, int size)
{
    void *q;
    int c;

    if (n <= *cap)
        return p;
    c = *cap ? *cap * 2 : 64;
    while (c < n)
        c *= 2;
    q = malloc((long)c * size);
    if (!q) {
        say("out of memory", 0);
        die(1);
    }
    if (p) {
        memcpy(q, p, (long)*cap * size);
        free(p);
    }
    *cap = c;
    return q;
}

struct trans *trans_new(const long *st, int nst, const char *english)
{
    struct trans *t;

    if (nlive == LIVE_MAX) {
        say("too many translations at once", 0);
        die(1);
    }
    t = malloc(sizeof(*t));
    if (!t) {
        say("out of memory", 0);
        die(1);
    }
    memset(t, 0, sizeof(*t));
    if (nst > STROKE_MAX)
        nst = STROKE_MAX;
    memcpy(t->st, st, nst * sizeof(long));
    t->nst = nst;
    if (english) {
        scopy(t->english, english, TEXT_MAX);
        t->has_english = 1;
    }
    live[nlive] = t;
    nlive++;
    return t;
}

void trans_free(struct trans *t)
{
    format_free(t);
    free(t->replaced);
    free(t);
}

static void set_replaced(struct trans *t, struct trans **r, int n)
{
    t->replaced = malloc((n ? n : 1) * sizeof(*r));
    if (!t->replaced) {
        say("out of memory", 0);
        die(1);
    }
    memcpy(t->replaced, r, n * sizeof(*r));
    t->nreplaced = n;
}

/* --- the history ------------------------------------------------ */

static void hist_push(struct trans *t)
{
    hist = need(hist, &chist, nhist + 1, sizeof(*hist));
    hist[nhist] = t;
    nhist++;
}

static void undo_list(struct trans **ts, int n)
{
    int i;
    struct trans *t;

    for (i = n - 1; i >= 0; i--) {
        t = ts[i];
        if (!nhist || hist[nhist - 1] != t)
            say("internal error: undo out of order", 0);
        nhist--;
        if (to_do) {
            to_do--;
        } else {
            to_undo = need(to_undo, &cto_undo, nto_undo + 1,
                sizeof(*to_undo));
            memmove(to_undo + 1, to_undo, nto_undo * sizeof(*to_undo));
            to_undo[0] = t;
            nto_undo++;
        }
    }
}

static void do_list(struct trans **ts, int n)
{
    int i;

    for (i = 0; i < n; i++)
        hist_push(ts[i]);
    to_do += n;
}

static void translate_translation(struct trans *t)
{
    struct trans **r;
    int n;

    /* copy: undo_list may move entries while it pops */
    n = t->nreplaced;
    r = t->replaced;
    undo_list(r, n);
    do_list(&t, 1);
}

static void untranslate_translation(struct trans *t)
{
    undo_list(&t, 1);
    do_list(t->replaced, t->nreplaced);
}

/* Keep translations for n strokes; the one before becomes the tail. */
static void restrict_size(int n)
{
    int strokes, count, i, cut;

    strokes = 0;
    count = 0;
    for (i = nhist - 1; i >= 0; i--) {
        strokes += hist[i]->nst;
        count++;
        if (strokes >= n)
            break;
    }
    cut = nhist - count;
    if (cut <= 0)
        return;
    tail = hist[cut - 1];
    memmove(hist, hist + cut, (nhist - cut) * sizeof(*hist));
    nhist -= cut;
}

static void mark(struct trans *t)
{
    int i;

    if (t->mark)
        return;
    t->mark = 1;
    for (i = 0; i < t->nreplaced; i++)
        mark(t->replaced[i]);
}

/* Free what neither the history nor the tail can reach any more. */
static void sweep(void)
{
    int i, j;
    struct trans *t;

    for (i = 0; i < nlive; i++)
        live[i]->mark = 0;
    if (tail)
        mark(tail);
    for (i = 0; i < nhist; i++)
        mark(hist[i]);
    j = 0;
    for (i = 0; i < nlive; i++) {
        t = live[i];
        if (t->mark) {
            live[j] = t;
            j++;
        } else {
            trans_free(t);
        }
    }
    nlive = j;
}

/* --- lookups ------------------------------------------------------ */

static const char *lookup_strokes(const long *st, int n)
{
    return dict_lookup(st, n);
}

struct suffix {
    long st;
    const char *mapping;
};

static int involved_suffixes(long st, struct suffix *out)
{
    int i, n;
    const char *m;

    n = 0;
    for (i = 0; i < 4; i++) {
        if (!(st & suffix_keys[i]))
            continue;
        m = lookup_strokes(&suffix_keys[i], 1);
        if (!m)
            continue;
        out[n].st = suffix_keys[i];
        out[n].mapping = m;
        n++;
    }
    return n;
}

static char joined[TEXT_MAX * 2];

static const char *lookup_with_suffix(long *st, int n, struct suffix *sx,
    int nsx)
{
    int i;
    long last;
    const char *m;

    if (!nsx)
        return lookup_strokes(st, n);
    last = st[n - 1];
    for (i = 0; i < nsx; i++) {
        st[n - 1] = last & ~sx[i].st;
        m = lookup_strokes(st, n);
        st[n - 1] = last;
        if (m) {
            sprintf(joined, "%.*s %.*s", TEXT_MAX - 1, m, TEXT_MAX - 1,
                sx[i].mapping);
            return joined;
        }
    }
    return 0;
}

/* last: the translation before the strokes, or 0 when there is none */
static const char *lookup_with_prefix(int max_len, struct trans *last,
    const long *st, int n, struct suffix *sx, int nsx)
{
    long buf[STROKE_MAX + 1];
    const char *m;

    if (n > STROKE_MAX)
        return 0;
    if (n < max_len && (!last || trans_word_finished(last))) {
        buf[0] = 0;
        memcpy(buf + 1, st, n * sizeof(long));
        m = lookup_with_suffix(buf, n + 1, sx, nsx);
        if (m)
            return m;
    }
    if (n <= max_len) {
        memcpy(buf, st, n * sizeof(long));
        return lookup_with_suffix(buf, n, sx, nsx);
    }
    return 0;
}

static struct trans *find_longest_match(int min_len, int max_len,
    long st, int with_suffixes)
{
    struct suffix sx[4];
    int nsx, strokes, count, first, i, j, n;
    long all[STROKE_MAX + 1];
    const char *m;
    struct trans *t;

    nsx = 0;
    if (with_suffixes) {
        nsx = involved_suffixes(st, sx);
        if (!nsx)
            return 0;
    }
    strokes = 1;
    count = 0;
    for (i = nhist - 1; i >= 0; i--) {
        strokes += hist[i]->nst;
        if (strokes > max_len)
            break;
        count++;
    }
    first = nhist - count;
    for (i = first; i <= nhist; i++) {
        n = 0;
        for (j = i; j < nhist; j++) {
            if (n + hist[j]->nst > STROKE_MAX)
                break;
            memcpy(all + n, hist[j]->st, hist[j]->nst * sizeof(long));
            n += hist[j]->nst;
        }
        if (j < nhist)
            continue;
        all[n] = st;
        n++;
        if (n < min_len)
            continue;
        m = lookup_with_prefix(max_len, i > first ? hist[i - 1] : 0,
            all, n, sx, nsx);
        if (m) {
            t = trans_new(all, n, m);
            set_replaced(t, hist + i, nhist - i);
            return t;
        }
    }
    return 0;
}

/* Translator.lookup: plain, then with a suffix. */
static const char *lookup_any(long *st, int n)
{
    const char *m;
    struct suffix sx[4];
    int nsx;

    m = lookup_strokes(st, n);
    if (m)
        return m;
    nsx = involved_suffixes(st[n - 1], sx);
    if (!nsx)
        return 0;
    return lookup_with_suffix(st, n, sx, nsx);
}

/* --- macros ------------------------------------------------------- */

static void flush(struct trans *extra);

static void macro_undo(long st)
{
    int i;
    struct trans *t;

    for (i = nhist - 1; i >= 0 && i < nhist; i--) {
        t = hist[i];
        untranslate_translation(t);
        if (trans_has_undo(t))
            return;
    }
    /* nothing left to take back: the word before goes (Ctrl+BackSpace),
     * and that is not kept in the history */
    flush(trans_new(&st, 1, "{#Control_L(BackSpace)}{^}"));
}

static void macro_repeat_last_stroke(void)
{
    if (!nhist)
        return;
    translate_stroke(hist[nhist - 1]->st[hist[nhist - 1]->nst - 1]);
}

static void macro_toggle_asterisk(void)
{
    struct trans *t;
    long st;

    if (!nhist)
        return;
    t = hist[nhist - 1];
    untranslate_translation(t);
    st = t->st[t->nst - 1] ^ (1L << 10);
    translate_stroke(st);
}

static void macro_delete_space(long st)
{
    struct trans *r[2], *t;
    char e[TEXT_MAX * 2], one[TEXT_MAX];
    int i, n;

    if (nhist < 2)
        return;
    r[0] = hist[nhist - 2];
    r[1] = hist[nhist - 1];
    if (r[1]->retro)
        return;
    e[0] = 0;
    n = 0;
    for (i = 0; i < 2; i++) {
        one[0] = 0;
        if (r[i]->has_english) {
            scopy(one, r[i]->english, sizeof(one));
        } else if (r[i]->nst == 1) {
            char s[32];
            int k, digits;
            stroke_text(r[i]->st[0], s);
            digits = s[0] != 0;
            for (k = 0; s[k]; k++)
                if (s[k] < '0' || s[k] > '9')
                    digits = 0;
            if (!digits)
                continue;
            sprintf(one, "{&%s}", s);
        } else {
            continue;
        }
        if (n)
            scat(e, "{^~|^}", sizeof(e));
        scat(e, one, sizeof(e));
        n++;
    }
    if (n > 1) {
        t = trans_new(&st, 1, e);
        set_replaced(t, r, 2);
        t->retro = 1;
        translate_translation(t);
    }
}

static void macro_insert_space(long st)
{
    struct trans *r, *t, *p;
    char e[TEXT_MAX * 2], s[32];
    const char *m;
    long last;
    int i, k;

    if (!nhist)
        return;
    r = hist[nhist - 1];
    if (r->retro || !r->nreplaced)
        return;
    e[0] = 0;
    for (i = 0; i < r->nreplaced; i++) {
        p = r->replaced[i];
        if (i)
            scat(e, " ", sizeof(e));
        if (p->has_english) {
            scat(e, p->english, sizeof(e));
        } else {
            for (k = 0; k < p->nst; k++) {
                if (k)
                    scat(e, "/", sizeof(e));
                stroke_text(p->st[k], s);
                scat(e, s, sizeof(e));
            }
        }
    }
    last = r->st[r->nst - 1];
    m = lookup_any(&last, 1);
    scat(e, " ", sizeof(e));
    if (m) {
        scat(e, m, sizeof(e));
    } else {
        stroke_text(last, s);
        scat(e, s, sizeof(e));
    }
    t = trans_new(&st, 1, e);
    set_replaced(t, &r, 1);
    t->retro = 1;
    translate_translation(t);
}

static int count_arg(const char *a)
{
    int n;

    n = 0;
    if (*a < '0' || *a > '9')
        return 1;
    while (*a >= '0' && *a <= '9') {
        n = n * 10 + (*a - '0');
        a++;
    }
    return n;
}

static void macro_repeat_translation(const char *arg)
{
    int n, i, first, count;
    struct trans **copy;
    struct trans *t, *c;

    n = count_arg(arg);
    if (!nhist)
        return;
    first = nhist - n;
    if (first < 0 || n <= 0)
        first = 0;
    /* the ones to repeat, before the copies join the history */
    count = nhist - first;
    copy = malloc(count * sizeof(*copy));
    if (!copy)
        return;
    memcpy(copy, hist + first, count * sizeof(*copy));
    for (i = 0; i < count; i++) {
        t = copy[i];
        c = trans_new(t->st, t->nst, t->has_english ? t->english : 0);
        translate_translation(c);
    }
    free(copy);
}

/* repeat_last_word, _fragment, _character: new text from what is
 * there */
static void macro_repeat_text(long st, int what, const char *arg)
{
    int res;
    char text[TEXT_MAX];
    int n;
    struct trans *t;

    if (!nhist)
        return;
    n = count_arg(arg);
    if (what == 'w') {
        res = last_words(n, text, sizeof(text));
        if (!res)
            return;
    } else if (what == 'f') {
        res = last_fragments(n, text, sizeof(text));
        if (!res)
            return;
    } else {
        last_chars(n, text, sizeof(text));
    }
    t = trans_new(&st, 1, text);
    translate_translation(t);
}

/* A mapping that is a macro ("=undo", "{*}", ...), run it; 0 if not. */
static int run_macro(const char *m, long st)
{
    char name[64];
    const char *arg;
    int i;

    if (!m) {
        if (st != UNDO_STROKE)
            return 0;
        macro_undo(st);
        return 1;
    }
    if (!strcmp(m, "{*}")) {
        macro_toggle_asterisk();
        return 1;
    }
    if (!strcmp(m, "{*!}")) {
        macro_delete_space(st);
        return 1;
    }
    if (!strcmp(m, "{*?}")) {
        macro_insert_space(st);
        return 1;
    }
    if (!strcmp(m, "{*+}")) {
        macro_repeat_last_stroke();
        return 1;
    }
    if (m[0] != '=' || !is_wordc_at(m + 1, strlen(m + 1)))
        return 0;
    i = 1;
    while (m[i] && m[i] != ':' && i < (int)sizeof(name)) {
        if (!is_wordc_at(m + i, strlen(m + i)))
            return 0;
        name[i - 1] = m[i];
        i++;
    }
    if (i >= (int)sizeof(name))
        return 0;
    name[i - 1] = 0;
    arg = m[i] == ':' ? m + i + 1 : "";
    if (!strcmp(name, "undo"))
        macro_undo(st);
    else if (!strcmp(name, "repeat_last_stroke"))
        macro_repeat_last_stroke();
    else if (!strcmp(name, "retro_toggle_asterisk") ||
        !strcmp(name, "retrospective_toggle_asterisk"))
    {
        macro_toggle_asterisk();
    } else if (!strcmp(name, "retro_delete_space") ||
        !strcmp(name, "retrospective_delete_space"))
    {
        macro_delete_space(st);
    } else if (!strcmp(name, "retro_insert_space") ||
        !strcmp(name, "retrospective_insert_space"))
    {
        macro_insert_space(st);
    } else if (!strcmp(name, "repeat_last_translation"))
        macro_repeat_translation(arg);
    else if (!strcmp(name, "repeat_last_word"))
        macro_repeat_text(st, 'w', arg);
    else if (!strcmp(name, "repeat_last_fragment"))
        macro_repeat_text(st, 'f', arg);
    else if (!strcmp(name, "repeat_last_character"))
        macro_repeat_text(st, 'c', arg);
    else
        say("unknown macro: %s", name);
    return 1;
}

/* --- strokes ------------------------------------------------------ */

static void translate_stroke(long st)
{
    int res;
    int max_len;
    const char *m;
    struct trans *t;
    char raw[TEXT_MAX];

    max_len = dict_longest();
    m = lookup_with_prefix(max_len, nhist ? hist[nhist - 1] : 0, &st, 1,
        0, 0);
    /* the mapping may be in a static buffer: keep a copy */
    if (m) {
        scopy(raw, m, sizeof(raw));
        m = raw;
    }
    res = run_macro(m, st);
    if (res)
        return;
    t = find_longest_match(2, max_len, st, 0);
    if (!t && m)
        t = trans_new(&st, 1, m);
    if (!t)
        t = find_longest_match(1, max_len, st, 1);
    if (!t)
        t = trans_new(&st, 1, 0);
    translate_translation(t);
}

static void flush(struct trans *extra)
{
    struct trans *prev;
    struct trans **doo;
    struct trans **list;
    int ndo, n;

    prev = 0;
    if (nhist - to_do > 0)
        prev = hist[nhist - to_do - 1];
    else if (tail)
        prev = tail;
    ndo = to_do;
    list = malloc((ndo + 1) * sizeof(*list));
    if (!list) {
        say("out of memory", 0);
        die(1);
    }
    memcpy(list, hist + nhist - ndo, ndo * sizeof(*list));
    n = ndo;
    if (extra) {
        list[n] = extra;
        n++;
    }
    doo = list;
    if (nto_undo || n)
        format_flush(to_undo, nto_undo, doo, n, prev);
    free(list);
    nto_undo = 0;
    to_do = 0;
    n = dict_longest();
    restrict_size(n > undo_levels ? n : undo_levels);
}

void translate(long st)
{
    translate_stroke(st);
    flush(0);
    sweep();
}

void translate_reset(void)
{
    nhist = 0;
    tail = 0;
    nto_undo = 0;
    to_do = 0;
    sweep();
    format_reset();
}

/* for the formatter: the history as Plover's RetroFormatter sees it */
int history_count(void)
{
    return nhist;
}

struct trans *history_at(int i)
{
    return hist[i];
}
