/* The formatter: translations -> text, backspaces, key combos and
 * commands, the way Plover's formatting.py does it.
 *
 * Plover's dictionary language: words, {^} attach, {-|} capital next,
 * {>} lower next, {<} upper next, {*-|} {*>} {*<} the word before,
 * {,} {.} punctuation, {&a} glue, {~|...} carry capitals, {#Combo},
 * {PLOVER:COMMAND}, {MODE:...}, {$} word end, {:name:arg}, and the
 * plugins Dotterel uses: {:stitch:x}, {:stitch_last_word:n:d},
 * {:retro_us}.
 *
 * Every translation becomes actions. The text they make is kept here
 * (the "buffer"), and what was really typed (the "screen"); after a
 * stroke the difference is typed: backspaces, then the new end. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "steno.h"

enum {
    case_none, case_cap_first_word, case_lower, case_lower_first_char,
    case_title, case_upper, case_upper_first_word
};

struct action {
    int prev_attach;
    int glue;
    int orthography;
    int upper_carry;
    int word_is_finished;
    int next_attach;
    int next_case;
    int mode_case;
    int has_text;
    int has_word;
    char space[SPACE_MAX];
    char trailing[SPACE_MAX];
    char text[TEXT_MAX];
    char prev_replace[TEXT_MAX];
    char word[WORD_MAX];
    char combo[COMBO_MAX];
    char command[COMBO_MAX];
};

int start_attached;
int start_capitalized;
int output_on = 1;

static char *buf, *scr;        /* the text made, the text typed */
static long blen, slen, bcap, scap;
static long base;              /* bytes cut off the front of both */
static long dirty;             /* buf is as typed below this */

static struct action last;     /* the action before the one being made */
static struct trans *cur;      /* the translation being made */

/* --- text helpers --------------------------------------------------- */

static void need_buf(long n)
{
    char *p;
    long c;

    if (n <= bcap)
        return;
    c = bcap ? bcap : 4096;
    while (c < n)
        c *= 2;
    p = malloc(c);
    if (!p) {
        say("out of memory", 0);
        die(1);
    }
    if (buf) {
        memcpy(p, buf, blen);
        free(buf);
    }
    buf = p;
    bcap = c;
}

static void need_scr(long n)
{
    char *p;
    long c;

    if (n <= scap)
        return;
    c = scap ? scap : 4096;
    while (c < n)
        c *= 2;
    p = malloc(c);
    if (!p) {
        say("out of memory", 0);
        die(1);
    }
    if (scr) {
        memcpy(p, scr, slen);
        free(scr);
    }
    scr = p;
    scap = c;
}

static void case_n(char *s, int n, int up)
{
    int i, k, len;

    len = strlen(s);
    if (n > len)
        n = len;
    for (i = 0; i < n; i += k) {
        k = case_at(s + i, n - i, up);
        if (!k)
            break;
    }
}

static void upper_n(char *s, int n)
{
    case_n(s, n, 1);
}

static void lower_n(char *s, int n)
{
    case_n(s, n, 0);
}

/* One word of Plover's WORD_RX at s, with its trailing spaces:
 * \d+(?:[.,]\d+)+ | [\'\w]+[-\w\']* | [^\w\s]+, then \s*.
 * 0 when s starts with a space. */
static int word_token(const char *s, int n)
{
    int i, j, k, reps;

    if (!n || is_space_at(s, n))
        return 0;
    i = 0;
    /* digits with . or , between them */
    while (i < n && s[i] >= '0' && s[i] <= '9')
        i++;
    if (i) {
        reps = 0;
        j = i;
        while (j + 1 < n && (s[j] == '.' || s[j] == ',') &&
            s[j + 1] >= '0' && s[j + 1] <= '9')
        {
            j++;
            while (j < n && s[j] >= '0' && s[j] <= '9')
                j++;
            reps++;
        }
        if (reps) {
            i = j;
            goto spaces;
        }
    }
    i = 0;
    k = is_wordc_at(s, n);
    if (k || s[0] == '\'') {
        i = k ? k : 1;
        for (;;) {
            if (i >= n)
                break;
            k = is_wordc_at(s + i, n - i);
            if (k) {
                i += k;
                continue;
            }
            if (s[i] == '\'' || s[i] == '-') {
                i++;
                continue;
            }
            break;
        }
        goto spaces;
    }
    while (i < n && !is_wordc_at(s + i, n - i) && !is_space_at(s + i,
        n - i))
    {
        k = u8get(s + i, n - i, 0);
        i += k ? k : 1;
    }
spaces:
    while (i < n) {
        k = is_space_at(s + i, n - i);
        if (!k)
            break;
        i += k;
    }
    return i;
}

/* Where each word of s starts; returns how many (at most max). */
static int word_starts(const char *s, int n, int *at, int max)
{
    int p, k, c;

    p = 0;
    c = 0;
    while (p < n) {
        k = word_token(s + p, n - p);
        if (!k) {
            k = is_space_at(s + p, n - p);
            p += k ? k : 1;
            continue;
        }
        if (c < max) {
            at[c] = p;
            c++;
        } else {
            memmove(at, at + 1, (max - 1) * sizeof(int));
            at[max - 1] = p;
        }
        p += k;
    }
    return c;
}

static void rightmost_word(const char *s, char *out, int size)
{
    int at[1], n, len;

    len = strlen(s);
    n = word_starts(s, len, at, 1);
    out[0] = 0;
    if (!n || is_space_at(s + len - u8back(s, len, 1), u8back(s, len, 1)))
        return;
    scopy(out, s + at[0], size);
}

static int has_word_boundary(const char *s)
{
    int at[2], len;

    len = strlen(s);
    if (!len)
        return 0;
    if (is_space_at(s, len) || is_space_at(s + len - u8back(s, len, 1),
        u8back(s, len, 1)))
    {
        return 1;
    }
    return word_starts(s, len, at, 2) > 1;
}

static void apply_case(char *t, int c)
{
    int n;

    switch (c) {
    case case_cap_first_word:
        case_at(t, strlen(t), 1);
        break;
    case case_lower_first_char:
        case_at(t, strlen(t), 0);
        break;
    case case_upper_first_word:
        n = word_token(t, strlen(t));
        upper_n(t, n);
        break;
    }
}

/* string.capwords(s, " "): every word Capital, the rest lower */
static void capwords(char *t)
{
    int i, start;

    start = 1;
    for (i = 0; t[i]; ) {
        if (t[i] == ' ') {
            start = 1;
            i++;
            continue;
        }
        i += case_at(t + i, strlen(t + i), start);
        start = 0;
    }
}

static void apply_mode(struct action *a, struct action *prev)
{
    char o[TEXT_MAX];
    int i, n, k, appended;

    appended = a->prev_attach;
    if (a->mode_case == case_lower)
        lower_n(a->text, TEXT_MAX);
    else if (a->mode_case == case_upper)
        upper_n(a->text, TEXT_MAX);
    else if (a->mode_case == case_title && !appended)
        capwords(a->text);
    if (strcmp(a->space, " ")) {
        n = 0;
        k = strlen(a->space);
        for (i = 0; a->text[i] && n < TEXT_MAX - k - 1; i++) {
            if (a->text[i] == ' ') {
                memcpy(o + n, a->space, k);
                n += k;
            } else {
                o[n] = a->text[i];
                n++;
            }
        }
        o[n] = 0;
        scopy(a->text, o, TEXT_MAX);
    }
    if (prev->next_case == case_lower_first_char && a->text[0] &&
        a->mode_case == case_title)
    {
        case_at(a->text, strlen(a->text), 0);
    }
}

/* --- actions ---------------------------------------------------------- */

static void act_init(struct action *a)
{
    memset(a, 0, sizeof(*a));
    a->orthography = 1;
    a->word_is_finished = 1;
    scopy(a->space, " ", SPACE_MAX);
}

static void new_state(struct action *a)
{
    act_init(a);
    a->prev_attach = last.next_attach;
    scopy(a->space, last.space, SPACE_MAX);
    a->mode_case = last.mode_case;
    scopy(a->trailing, last.trailing, SPACE_MAX);
}

static void copy_state(struct action *a)
{
    act_init(a);
    a->prev_attach = last.next_attach;
    a->mode_case = last.mode_case;
    a->glue = last.glue;
    a->orthography = last.orthography;
    scopy(a->space, last.space, SPACE_MAX);
    a->upper_carry = last.upper_carry;
    a->has_word = last.has_word;
    scopy(a->word, last.word, WORD_MAX);
    scopy(a->trailing, last.trailing, SPACE_MAX);
    a->word_is_finished = last.word_is_finished;
    a->next_attach = last.next_attach;
    a->next_case = last.next_case;
}

static void set_text(struct action *a, const char *t)
{
    scopy(a->text, t, TEXT_MAX);
    a->has_text = 1;
}

/* Put an action's text into the buffer, keeping what it overwrote. */
static void apply(struct trans *t, struct action *a)
{
    long d, n, at;
    int k;
    char *p;

    if (!a->has_text)
        return;
    k = u8len(a->prev_replace, strlen(a->prev_replace));
    at = blen - (long)k * 4 - 4;
    if (at < 0)
        at = 0;
    d = u8back(buf + at, (int)(blen - at), k);
    at = blen - d;
    if (at + base < t->lo) {
        n = t->lo - base - at;
        p = malloc(t->nsaved + n + 1);
        if (!p) {
            say("out of memory", 0);
            die(1);
        }
        memcpy(p, buf + at, n);
        if (t->saved)
            memcpy(p + n, t->saved, t->nsaved);
        free(t->saved);
        t->saved = p;
        t->nsaved += (int)n;
        t->lo = at + base;
    }
    blen = at;
    if (at < dirty)
        dirty = at;
    n = (a->prev_attach ? 0 : strlen(a->space)) + strlen(a->text);
    need_buf(blen + n + 1);
    if (!a->prev_attach) {
        memcpy(buf + blen, a->space, strlen(a->space));
        blen += strlen(a->space);
    }
    memcpy(buf + blen, a->text, strlen(a->text));
    blen += strlen(a->text);
}

static void revert(struct trans *t)
{
    long at;

    at = t->lo - base;
    if (at < 0)
        at = 0;
    if (at > blen)
        at = blen;
    blen = at;
    if (at < dirty)
        dirty = at;
    need_buf(blen + t->nsaved + 1);
    if (t->saved)
        memcpy(buf + blen, t->saved, t->nsaved);
    blen += t->nsaved;
}

static void record_start(struct trans *t)
{
    free(t->saved);
    t->saved = 0;
    t->nsaved = 0;
    t->lo = blen + base;
    t->end = t->lo;
}

static void translated(struct action *a)
{
    struct action *p;

    p = realloc(cur->act, (cur->nact + 1) * sizeof(*p));
    if (!p) {
        say("out of memory", 0);
        die(1);
    }
    cur->act = p;
    cur->act[cur->nact] = *a;
    cur->nact++;
    last = *a;
    apply(cur, a);
}

void format_free(struct trans *t)
{
    free(t->act);
    t->act = 0;
    t->nact = 0;
    free(t->saved);
    t->saved = 0;
    t->nsaved = 0;
}

int trans_has_undo(struct trans *t)
{
    int i;

    if (!t->nact || t->nreplaced)
        return 1;
    for (i = 0; i < t->nact; i++)
        if ((t->act[i].has_text && t->act[i].text[0]) ||
            t->act[i].prev_replace[0])
        {
            return 1;
        }
    return 0;
}

int trans_word_finished(struct trans *t)
{
    if (!t->nact)
        return 1;
    return t->act[t->nact - 1].word_is_finished;
}

/* --- what is already there (RetroFormatter) -------------------------- */

#define LOOK_BACK 4096

static long look_start(void)
{
    long s;
    int k;

    s = blen > LOOK_BACK ? blen - LOOK_BACK : 0;
    /* start after a space, so no word is cut in two */
    if (s > 0) {
        while (s < blen && !is_space_at(buf + s, blen - s))
            s++;
        while (s < blen) {
            k = is_space_at(buf + s, blen - s);
            if (!k)
                break;
            s += k;
        }
    }
    return s;
}

int last_words(int count, char *out, int size)
{
    int at[64], n;
    long s;

    out[0] = 0;
    if (count < 1)
        count = 1;
    if (count > 64)
        count = 64;
    s = look_start();
    n = word_starts(buf + s, (int)(blen - s), at, count);
    if (!n)
        return 0;
    if (blen - s - at[0] >= size)
        return 0;
    memcpy(out, buf + s + at[0], blen - s - at[0]);
    out[blen - s - at[0]] = 0;
    return (int)(blen - s - at[0]);
}

/* fragments: \s*[^\s]+\s* ; the first one without its leading spaces */
int last_fragments(int count, char *out, int size)
{
    long p, s;
    int found, k;

    out[0] = 0;
    s = look_start();
    p = blen;
    found = 0;
    while (p > s && found < count) {
        while (p > s && is_space_at(buf + p - u8back(buf, (int)p, 1),
            u8back(buf, (int)p, 1)))
        {
            p -= u8back(buf, (int)p, 1);
        }
        if (p == s)
            break;
        while (p > s && !is_space_at(buf + p - u8back(buf, (int)p, 1),
            u8back(buf, (int)p, 1)))
        {
            p -= u8back(buf, (int)p, 1);
        }
        found++;
    }
    if (!found || blen - p >= size)
        return 0;
    k = (int)(blen - p);
    memcpy(out, buf + p, k);
    out[k] = 0;
    return k;
}

int last_chars(int count, char *out, int size)
{
    int n;
    long from;

    if (count <= 0) {
        out[0] = 0;
        return 0;
    }
    from = blen - (long)count * 4 - 4;
    if (from < 0)
        from = 0;
    n = u8back(buf + from, (int)(blen - from), count);
    if (n >= size)
        n = size - 1;
    memcpy(out, buf + blen - n, n);
    out[n] = 0;
    return n;
}

/* --- the UK/US table for {:retro_us} --------------------------------- */

static char *ukus;               /* "uk\0us\0uk\0us\0..." */
static long nukus;
static int ukus_read;

char *json_pairs(const char *path, long *count);

static const char *ukus_find(const char *w, int by_us)
{
    long i;
    const char *p, *found;

    found = 0;
    p = ukus;
    for (i = 0; i < nukus; i++) {
        const char *k, *v;
        k = p;
        v = k + strlen(k) + 1;
        p = v + strlen(v) + 1;
        if (by_us) {
            if (!strcmp(v, w))
                return k;           /* the first UK one */
        } else if (!strcmp(k, w)) {
            found = v;              /* the last one wins */
        }
    }
    return found;
}

static int ukus_lookup(const char *w, int by_us, char *out, int size)
{
    const char *o;
    char low[TEXT_MAX];
    int i, upper, n;

    o = ukus_find(w, by_us);
    if (o) {
        scopy(out, o, size);
        return 1;
    }
    scopy(low, w, sizeof(low));
    lower_n(low, sizeof(low));
    o = ukus_find(low, by_us);
    if (!o)
        return 0;
    scopy(out, o, size);
    n = strlen(w);
    upper = 0;
    for (i = 0; w[i]; i++) {
        if (w[i] >= 'a' && w[i] <= 'z')
            break;
        if (w[i] >= 'A' && w[i] <= 'Z')
            upper = 1;
    }
    if (w[i] == 0 && upper && n > 1) {
        upper_n(out, size);
        return 1;
    }
    if (w[0] >= 'A' && w[0] <= 'Z')
        case_at(out, strlen(out), 1);
    return 1;
}

static void uk_us_switch(const char *w, char *out, int size)
{
    int res;

    if (!ukus_read) {
        ukus_read = 1;
        ukus = json_pairs(dotterel_data("uk_to_us.json"), &nukus);
        if (!ukus)
            say("no UK/US list (%s)", dotterel_data("uk_to_us.json"));
    }
    if (!w[0] || !ukus) {
        scopy(out, w, size);
        return;
    }
    res = ukus_lookup(w, 1, out, size);
    if (res && strcmp(out, w))
        return;
    res = ukus_lookup(w, 0, out, size);
    if (res)
        return;
    scopy(out, w, size);
}

/* --- metas -------------------------------------------------------------- */

static const char *case_name(const char *s, int *c)
{
    static const char *const names[] = { "cap_first_word", "lower",
        "lower_first_char", "title", "upper", "upper_first_word" };
    static const int vals[] = { case_cap_first_word, case_lower,
        case_lower_first_char, case_title, case_upper,
        case_upper_first_word };
    char low[32];
    int i;

    scopy(low, s, sizeof(low));
    lower_n(low, sizeof(low));
    for (i = 0; i < 6; i++)
        if (!strcmp(low, names[i])) {
            *c = vals[i];
            return names[i];
        }
    return 0;
}

static int all_space(const char *s)
{
    int n, k;

    n = strlen(s);
    if (!n)
        return 0;
    while (n > 0) {
        k = is_space_at(s, n);
        if (!k)
            return 0;
        s += k;
        n -= k;
    }
    return 1;
}

static void meta_attach(struct action *a, const char *meta)
{
    char m[TEXT_MAX], lw[WORD_MAX], nw[TEXT_MAX], t[TEXT_MAX * 2];
    int begin, end, n, common, k;

    new_state(a);
    scopy(m, meta, sizeof(m));
    n = strlen(m);
    begin = n > 0 && m[0] == '^';
    end = n > 0 && m[n - 1] == '^';
    if (!begin && !end) {
        begin = end = 1;
    } else {
        if (begin) {
            memmove(m, m + 1, n);
            n--;
        }
        if (end && n > 0) {
            m[n - 1] = 0;
            n--;
        }
    }
    if (begin)
        a->prev_attach = 1;
    if (end) {
        a->next_attach = 1;
        a->word_is_finished = 0;
    }
    scopy(lw, last.has_word ? last.word : "", sizeof(lw));
    if (!m[0]) {
        a->orthography = 0;
    } else if (lw[0] && !all_space(m) && last.orthography &&
        begin && (!end || has_word_boundary(m)))
    {
        add_suffix(lw, m, nw, sizeof(nw));
        common = 0;
        while (lw[common] && lw[common] == nw[common])
            common++;
        while (common > 0 && (((unsigned char)lw[common] & 0xc0) == 0x80 ||
            ((unsigned char)nw[common] & 0xc0) == 0x80))
        {
            common--;
        }
        k = u8len(lw + common, strlen(lw + common));
        last_chars(k, a->prev_replace, TEXT_MAX);
        lw[common] = 0;
        scopy(m, nw + common, sizeof(m));
    }
    set_text(a, m);
    if (a->prev_attach) {
        sprintf(t, "%s%s", lw, m);
        rightmost_word(t, a->word, WORD_MAX);
        a->has_word = 1;
    }
}

static void meta_carry_capitalize(struct action *a, const char *meta)
{
    char m[TEXT_MAX];
    int begin, end, n;

    new_state(a);
    if (last.next_case == case_cap_first_word)
        a->next_case = case_cap_first_word;
    scopy(m, meta, sizeof(m));
    begin = m[0] == '^';
    if (begin) {
        memmove(m, m + 1, strlen(m));
        a->prev_attach = 1;
    }
    memmove(m, m + 2, strlen(m + 2) + 1);    /* ~| */
    n = strlen(m);
    end = n > 0 && m[n - 1] == '^';
    if (end) {
        m[n - 1] = 0;
        a->next_attach = 1;
        a->word_is_finished = 0;
    }
    if (m[0] || begin || end)
        set_text(a, m);
}

static void meta_glue(struct action *a, const char *text)
{
    new_state(a);
    a->glue = 1;
    set_text(a, text);
    if (last.glue)
        a->prev_attach = 1;
}

static void meta_mode(struct action *a, const char *cmd)
{
    char mode[32];
    const char *arg;
    int i;

    copy_state(a);
    for (i = 0; cmd[i] && cmd[i] != ':' && i < 31; i++)
        mode[i] = (char)low_ascii((unsigned char)cmd[i]);
    mode[i] = 0;
    arg = cmd[i] == ':' ? cmd + i + 1 : 0;
    if (!strcmp(mode, "set_space")) {
        scopy(a->space, arg ? arg : "", SPACE_MAX);
        return;
    }
    if (arg) {
        say("not a mode: %s", cmd);
        return;
    }
    if (!strcmp(mode, "caps")) {
        a->mode_case = case_upper;
    } else if (!strcmp(mode, "title")) {
        a->mode_case = case_title;
    } else if (!strcmp(mode, "lower")) {
        a->mode_case = case_lower;
    } else if (!strcmp(mode, "snake")) {
        scopy(a->space, "_", SPACE_MAX);
    } else if (!strcmp(mode, "camel")) {
        a->mode_case = case_title;
        scopy(a->space, "", SPACE_MAX);
        a->next_case = case_lower_first_char;
    } else if (!strcmp(mode, "reset")) {
        scopy(a->space, " ", SPACE_MAX);
        a->mode_case = case_none;
    } else if (!strcmp(mode, "reset_space")) {
        scopy(a->space, " ", SPACE_MAX);
    } else if (!strcmp(mode, "reset_case")) {
        a->mode_case = case_none;
    } else {
        say("not a mode: %s", cmd);
    }
}

static void meta_retro_case(struct action *a, int c)
{
    int res;
    char w[TEXT_MAX];

    copy_state(a);
    a->prev_attach = 1;
    res = last_words(1, w, sizeof(w));
    if (res) {
        scopy(a->prev_replace, w, TEXT_MAX);
        apply_case(w, c);
        set_text(a, w);
    } else {
        set_text(a, "");
    }
}

static void meta_retro_us(struct action *a)
{
    int res;
    char w[TEXT_MAX], o[TEXT_MAX];

    copy_state(a);
    a->prev_attach = 1;
    res = last_words(1, w, sizeof(w));
    if (res) {
        scopy(a->prev_replace, w, TEXT_MAX);
        uk_us_switch(w, o, sizeof(o));
        set_text(a, o);
    } else {
        set_text(a, "");
    }
}

/* every character with delim between them, word by word */
static void stitch_word(const char *w, const char *delim, char *out,
    int size)
{
    int n, p, k, sp, c;

    out[0] = 0;
    n = strlen(w);
    p = 0;
    while (p < n) {
        k = word_token(w + p, n - p);
        if (!k) {
            k = is_space_at(w + p, n - p);
            p += k ? k : 1;
            continue;
        }
        sp = k;
        while (sp > 0 && is_space_at(w + p + sp - u8back(w + p, sp, 1),
            u8back(w + p, sp, 1)))
        {
            sp -= u8back(w + p, sp, 1);
        }
        c = 0;
        while (c < sp) {
            int len;
            char ch[8];
            len = u8get(w + p + c, sp - c, 0);
            if (!len)
                len = 1;
            if (c)
                scat(out, delim, size);
            memcpy(ch, w + p + c, len);
            ch[len] = 0;
            scat(out, ch, size);
            c += len;
        }
        {
            char rest[TEXT_MAX];
            memcpy(rest, w + p + sp, k - sp);
            rest[k - sp] = 0;
            scat(out, rest, size);
        }
        p += k;
    }
}

static void split2(const char *s, char *a, int na, char *b, int nb,
    int *has_b)
{
    const char *c;

    c = strchr(s, ':');
    *has_b = c != 0;
    if (!c) {
        scopy(a, s, na);
        b[0] = 0;
        return;
    }
    if (c - s < na) {
        memcpy(a, s, c - s);
        a[c - s] = 0;
    } else {
        scopy(a, s, na);
    }
    /* only up to the next ':', as Python's split(':')[1] */
    scopy(b, c + 1, nb);
    c = strchr(b, ':');
    if (c)
        b[c - b] = 0;
}

static void meta_stitch(struct action *a, const char *cmd)
{
    char word[TEXT_MAX], delim[TEXT_MAX], t[TEXT_MAX * 2];
    int has;

    split2(cmd, word, sizeof(word), delim, sizeof(delim), &has);
    if (!has)
        scopy(delim, "-", sizeof(delim));
    new_state(a);
    a->prev_attach = last.glue || last.next_attach;
    a->glue = 1;
    if (last.glue)
        sprintf(t, "%s%s", delim, word);
    else
        scopy(t, word, sizeof(t));
    set_text(a, t);
}

static void meta_stitch_last_word(struct action *a, const char *cmd)
{
    char num[32], delim[TEXT_MAX], w[TEXT_MAX], t[TEXT_MAX];
    int has, n, i, got;

    split2(cmd, num, sizeof(num), delim, sizeof(delim), &has);
    if (!has)
        scopy(delim, "-", sizeof(delim));
    n = 0;
    for (i = 0; num[i] >= '0' && num[i] <= '9'; i++)
        n = n * 10 + num[i] - '0';
    if (!num[0])
        n = 1;
    copy_state(a);
    got = n > 0 ? last_words(n, w, sizeof(w)) : 0;
    if (got) {
        stitch_word(w, delim, t, sizeof(t));
        set_text(a, t);
        scopy(a->prev_replace, w, TEXT_MAX);
        a->prev_attach = 1;
        a->has_word = 0;
        a->word[0] = 0;
    }
}

static int ci_prefix(const char *s, const char *p)
{
    while (*p) {
        if (low_ascii((unsigned char)*s) != *p)
            return 0;
        s++;
        p++;
    }
    return 1;
}

/* A named meta, as in {:name:arg} or what a {...} stands for. */
static int named_meta(struct action *a, const char *name, const char *arg)
{
    int c;

    if (!strcmp(name, "attach")) {
        meta_attach(a, arg);
    } else if (!strcmp(name, "carry_capitalize")) {
        meta_carry_capitalize(a, arg);
    } else if (!strcmp(name, "case")) {
        copy_state(a);
        if (case_name(arg, &c))
            a->next_case = c;
        else
            say("not a case: %s", arg);
    } else if (!strcmp(name, "retro_case")) {
        if (case_name(arg, &c)) {
            meta_retro_case(a, c);
        } else {
            copy_state(a);
            say("not a case: %s", arg);
        }
    } else if (!strcmp(name, "comma") || !strcmp(name, "stop")) {
        new_state(a);
        set_text(a, arg);
        a->prev_attach = 1;
        if (name[0] == 's')
            a->next_case = case_cap_first_word;
    } else if (!strcmp(name, "command")) {
        copy_state(a);
        scopy(a->command, arg, COMBO_MAX);
    } else if (!strcmp(name, "key_combo")) {
        copy_state(a);
        scopy(a->combo, arg, COMBO_MAX);
    } else if (!strcmp(name, "glue")) {
        meta_glue(a, arg);
    } else if (!strcmp(name, "mode")) {
        meta_mode(a, arg);
    } else if (!strcmp(name, "word_end")) {
        copy_state(a);
        a->word_is_finished = 1;
    } else if (!strcmp(name, "stitch")) {
        meta_stitch(a, arg);
    } else if (!strcmp(name, "stitch_last_word")) {
        meta_stitch_last_word(a, arg);
    } else if (!strcmp(name, "retro_us")) {
        meta_retro_us(a);
    } else {
        return 0;
    }
    return 1;
}

static void meta_to_action(struct action *a, const char *m)
{
    int res;
    char name[64];
    const char *p;
    int n, i;

    n = strlen(m);
    /* {:name:arg} */
    if (m[0] == ':' && m[1] && m[1] != ':') {
        p = m + 1;
        for (i = 0; p[i] && p[i] != ':' && i < 63; i++)
            name[i] = p[i];
        name[i] = 0;
        p += i;
        if (*p == ':')
            p++;
        res = named_meta(a, name, p);
        if (res)
            return;
        say("unknown meta: %s", name);
        new_state(a);
        return;
    }
    if (ci_prefix(m, "plover:")) {
        named_meta(a, "command", m + 7);
        return;
    }
    if (m[0] == '#') {
        named_meta(a, "key_combo", m + 1);
        return;
    }
    if (n == 1 && (m[0] == ',' || m[0] == ':' || m[0] == ';')) {
        named_meta(a, "comma", m);
        return;
    }
    if (n == 1 && (m[0] == '.' || m[0] == '!' || m[0] == '?')) {
        named_meta(a, "stop", m);
        return;
    }
    if (!strcmp(m, "-|") || !strcmp(m, ">") || !strcmp(m, "<")) {
        copy_state(a);
        a->next_case = m[0] == '-' ? case_cap_first_word : m[0] == '>' ?
            case_lower_first_char : case_upper_first_word;
        return;
    }
    if (!strcmp(m, "*-|") || !strcmp(m, "*>") || !strcmp(m, "*<")) {
        meta_retro_case(a, m[1] == '-' ? case_cap_first_word :
            m[1] == '>' ? case_lower_first_char : case_upper_first_word);
        return;
    }
    if (!strcmp(m, "$")) {
        named_meta(a, "word_end", "");
        return;
    }
    if (m[0] == '=') {
        say("{=...} (if next matches) is not supported: %s", m);
        new_state(a);
        return;
    }
    if (ci_prefix(m, "mode:")) {
        meta_mode(a, m + 5);
        return;
    }
    if (n >= 3 && m[0] == '*' && m[1] == '(' && m[n - 1] == ')') {
        say("{*(...)} (retro currency) is not supported: %s", m);
        new_state(a);
        return;
    }
    if (m[0] == '&') {
        meta_glue(a, m + 1);
        return;
    }
    if ((m[0] == '~' && m[1] == '|') ||
        (m[0] == '^' && m[1] == '~' && m[2] == '|'))
    {
        meta_carry_capitalize(a, m);
        return;
    }
    if (n && (m[0] == '^' || m[n - 1] == '^')) {
        meta_attach(a, m);
        return;
    }
    new_state(a);
}

/* --- a translation -> actions ---------------------------------------- */

static void finalize(struct action *a)
{
    char t[TEXT_MAX * 2];
    int c;

    if (!a->has_text)
        return;
    if (!a->has_word) {
        sprintf(t, "%s%s", a->glue && last.glue && last.has_word ?
            last.word : "", a->text);
        rightmost_word(t, a->word, WORD_MAX);
        a->has_word = 1;
    }
    c = last.next_case;
    if (c == case_none && a->prev_attach && last.upper_carry)
        c = case_upper_first_word;
    apply_case(a->text, c);
    if (c == case_upper_first_word)
        a->upper_carry = !has_word_boundary(a->text);
    apply_mode(a, &last);
    scopy(a->trailing, a->next_attach ? "" : a->space, SPACE_MAX);
}

static void unescape(char *s)
{
    int i, j;

    for (i = j = 0; s[i]; i++) {
        if (s[i] == '\\' && (s[i + 1] == '{' || s[i + 1] == '}'))
            i++;
        s[j] = s[i];
        j++;
    }
    s[j] = 0;
}

static void atom_to_action(const char *atom, int n)
{
    struct action a;
    char s[TEXT_MAX];

    if (n >= TEXT_MAX)
        n = TEXT_MAX - 1;
    memcpy(s, atom, n);
    s[n] = 0;
    if (n >= 2 && s[0] == '{' && s[n - 1] == '}') {
        memmove(s, s + 1, n - 2);
        s[n - 2] = 0;
        unescape(s);
        meta_to_action(&a, s);
    } else {
        new_state(&a);
        unescape(s);
        set_text(&a, s);
    }
    finalize(&a);
    translated(&a);
}

/* One atom of Plover's ATOM_RE at s[i]: plain text (with \{ \}) or
 * {...}. Returns its length; *skip says how much to step over when
 * there is none there. */
static int atom_at(const char *s, int i, int *skip)
{
    int j, esc;

    *skip = 0;
    j = i;
    while (s[j]) {
        if (s[j] == '\\' && (s[j + 1] == '{' || s[j + 1] == '}'))
            j += 2;
        else if (s[j] != '{' && s[j] != '}')
            j++;
        else
            break;
    }
    if (j > i)
        return j - i;
    if (s[i] != '{') {
        *skip = 1;
        return 0;
    }
    j = i + 1;
    esc = -1;
    while (s[j]) {
        if (s[j] == '\\' && (s[j + 1] == '{' || s[j + 1] == '}')) {
            if (s[j + 1] == '}')
                esc = j + 1;
            j += 2;
        } else if (s[j] != '{' && s[j] != '}') {
            j++;
        } else {
            break;
        }
    }
    if (s[j] == '}')
        return j + 1 - i;
    if (esc >= 0)
        return esc + 1 - i;
    *skip = 1;
    return 0;
}

static void translation_to_actions(const char *e)
{
    int i, n, skip, a, b, s, digits;
    char g[TEXT_MAX + 4];

    digits = e[0] != 0;
    for (i = 0; e[i]; i++)
        if (e[i] < '0' || e[i] > '9')
            digits = 0;
    if (digits) {
        sprintf(g, "{&%.*s}", TEXT_MAX - 4, e);
        atom_to_action(g, strlen(g));
        return;
    }
    n = cur->nact;
    i = 0;
    while (e[i]) {
        a = atom_at(e, i, &skip);
        if (!a) {
            i += skip;
            continue;
        }
        /* strip(" ") */
        s = i;
        b = i + a;
        while (s < b && e[s] == ' ')
            s++;
        while (b > s && e[b - 1] == ' ')
            b--;
        if (b > s)
            atom_to_action(e + s, b - s);
        i += a;
    }
    if (cur->nact == n) {
        struct action c;
        copy_state(&c);
        translated(&c);
    }
}

static void raw_to_actions(long st)
{
    char s[32], nd[32];
    struct action a;
    int i, j, dashed, digits;

    stroke_text(st, s);
    dashed = 0;
    for (i = j = 0; s[i]; i++) {
        if (s[i] == '-' && !dashed) {
            dashed = 1;
            continue;
        }
        nd[j] = s[i];
        j++;
    }
    nd[j] = 0;
    digits = nd[0] != 0;
    for (i = 0; nd[i]; i++)
        if (nd[i] < '0' || nd[i] > '9')
            digits = 0;
    if (digits) {
        translation_to_actions(nd);
        return;
    }
    act_init(&a);
    set_text(&a, s);
    scopy(a.word, s, WORD_MAX);
    a.has_word = 1;
    a.mode_case = last.mode_case;
    a.prev_attach = last.next_attach;
    scopy(a.space, last.space, SPACE_MAX);
    scopy(a.trailing, last.space, SPACE_MAX);
    translated(&a);
}

static void format_one(struct trans *t)
{
    format_free(t);
    cur = t;
    record_start(t);
    if (t->has_english)
        translation_to_actions(t->english);
    else
        raw_to_actions(t->st[0]);
    t->end = blen + base;
    cur = 0;
}

/* --- output ------------------------------------------------------------ */

static void emit(void)
{
    long common, n;
    int bs;

    n = blen < slen ? blen : slen;
    common = dirty < n ? dirty : n;
    while (common < n && buf[common] == scr[common])
        common++;
    while (common > 0 && ((common < slen &&
        ((unsigned char)scr[common] & 0xc0) == 0x80) ||
        (common < blen && ((unsigned char)buf[common] & 0xc0) == 0x80)))
    {
        common--;
    }
    bs = 0;
    if (slen > common)
        bs = u8len(scr + common, (int)(slen - common));
    if (bs && output_on)
        out_backspaces(bs);
    if (blen > common && output_on)
        out_string(buf + common, (int)(blen - common));
    need_scr(blen + 1);
    memcpy(scr + common, buf + common, blen - common);
    slen = blen;
    dirty = blen;
}

static void trim(void)
{
    long cut;

    if (blen < 65536L || blen != slen)
        return;
    cut = blen - 32768L;
    while (cut < blen && ((unsigned char)buf[cut] & 0xc0) == 0x80)
        cut++;
    memmove(buf, buf + cut, blen - cut);
    memmove(scr, scr + cut, slen - cut);
    blen -= cut;
    slen -= cut;
    base += cut;
    dirty = blen;
}

static void initial_last(struct trans *prev)
{
    if (prev && prev->nact) {
        last = prev->act[prev->nact - 1];
        return;
    }
    act_init(&last);
    last.next_attach = start_attached;
    last.word_is_finished = !last.next_attach;
    if (start_capitalized)
        last.next_case = case_cap_first_word;
}

void format_flush(struct trans **undo, int nundo, struct trans **doo,
    int ndo, struct trans *prev)
{
    struct action **old;
    int nold, i, j, k, first, nnew;
    struct action *a;

    need_buf(1);
    need_scr(1);
    nold = 0;
    for (i = 0; i < nundo; i++)
        nold += undo[i]->nact;
    old = malloc((nold + 1) * sizeof(*old));
    if (!old) {
        say("out of memory", 0);
        die(1);
    }
    k = 0;
    for (i = 0; i < nundo; i++)
        for (j = 0; j < undo[i]->nact; j++) {
            old[k] = &undo[i]->act[j];
            k++;
        }
    for (i = nundo - 1; i >= 0; i--)
        revert(undo[i]);
    /* pass 1: make the actions */
    initial_last(prev);
    for (i = 0; i < ndo; i++)
        format_one(doo[i]);
    /* actions that are the same as before are not done again */
    first = 0;
    nnew = 0;
    for (i = 0; i < ndo; i++)
        nnew += doo[i]->nact;
    k = 0;
    for (i = 0; i < ndo && k == first; i++)
        for (j = 0; j < doo[i]->nact && k == first; j++) {
            if (k < nold && !memcmp(old[k], &doo[i]->act[j],
                sizeof(struct action)))
            {
                first++;
            }
            k++;
        }
    /* pass 2: the same text again, typing it before each combo or
     * command that is new */
    for (i = ndo - 1; i >= 0; i--)
        revert(doo[i]);
    k = 0;
    for (i = 0; i < ndo; i++) {
        record_start(doo[i]);
        for (j = 0; j < doo[i]->nact; j++) {
            a = &doo[i]->act[j];
            if (a->has_text) {
                apply(doo[i], a);
            } else if (k >= first) {
                /* as Plover: type what is there before any other action */
                emit();
                if (a->combo[0]) {
                    if (output_on)
                        out_combo(a->combo);
                } else if (a->command[0]) {
                    run_command(a->command);
                }
            }
            k++;
        }
        doo[i]->end = blen + base;
    }
    emit();
    trim();
    free(old);
}

void format_reset(void)
{
    blen = 0;
    slen = 0;
    base = 0;
    dirty = 0;
}
