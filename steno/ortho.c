/* Spelling rules for endings: cat + {^s} = cats, but box + {^s} =
 * boxes. These are Plover's English rules (and Dotterel's o + s), as
 * regular expressions, and Plover's word list to choose between them:
 * the result that is a real word, the most common one first.
 *
 * The rules are matched by a small regular expression engine that
 * knows what they use: ( ) (?: ) (?<! ) | * + ? [ ] . ^ $ and \1 in
 * the result. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "steno.h"

/* --- the rules -------------------------------------------------------- */

static const char *const rules[][2] = {
    /* == +ly == */
    { "^(.*[aeiou]c) \\^ ly$", "\\1ally" },
    { "^(.+[aeioubmnp])le \\^ ly$", "\\1ly" },
    /* == +ry == */
    { "^(.*t)e \\^ (ry|ary)$", "\\1ory" },
    { "^(.+)m \\^ tor(y|ily)$", "\\1mator\\2" },
    { "^(.+)se \\^ ar(y|ies)$", "\\1sor\\2" },
    /* == t +cy == */
    { "^(.*[naeiou])te? \\^ cy$", "\\1cy" },
    /* == +s == */
    { "^(.*(?:s|sh|x|z|zh)) \\^ s$", "\\1es" },
    { "^(.*(?:oa|ea|i|ee|oo|au|ou|l|n|(?<![gin]a)r|t)ch) \\^ s$",
        "\\1es" },
    { "^(.+[bcdfghjklmnpqrstvwxz])y \\^ s$", "\\1ies" },
    /* == y == */
    { "^(.+)ie \\^ ing$", "\\1ying" },
    { "^(.+[cdfghlmnpr])y \\^ ist$", "\\1ist" },
    { "^(.+[bcdfghjklmnpqrstvwxz])y \\^ ([a-hj-xz].*)$", "\\1i\\2" },
    /* == +en == */
    { "^(.+)te \\^ en$", "\\1tten" },
    { "^(.+[ae]) \\^ e(n|ns)$", "\\1\\2" },
    /* == +ial == */
    { "^(.+)y \\^ (ial|ially)$", "\\1\\2" },
    /* == +if == */
    { "^(.+i) \\^ if(y|ying|ied|ies|ication|ications)$", "\\1f\\2" },
    { "^(.*[^aeiou])y \\^ if(y|ying|ied|ies|ication|ications)$",
        "\\1if\\2" },
    /* == +ical == */
    { "^(.+)ic \\^ (ical|ically)$", "\\1\\2" },
    { "^(.+)ology \\^ ic(al|ally)$", "\\1ologic\\2" },
    { "^(.*)ry \\^ ica(l|lly|lity)$", "\\1rica\\2" },
    /* == +ist == */
    { "^(.*[l]) \\^ is(t|ts)$", "\\1is\\2" },
    /* == +ity == */
    { "^(.*)ry \\^ ity$", "\\1rity" },
    { "^(.*)l \\^ ity$", "\\1lity" },
    /* == +ive, +tive == */
    { "^(.+)rm \\^ tiv(e|ity|ities)$", "\\1rmativ\\2" },
    { "^(.+)e \\^ tiv(e|ity|ities)$", "\\1ativ\\2" },
    /* == +ize == */
    { "^(.+)y \\^ iz(e|es|ing|ed|er|ers|ation|ations|able|ability)$",
        "\\1iz\\2" },
    { "^(.+)y \\^ is(e|es|ing|ed|er|ers|ation|ations|able|ability)$",
        "\\1is\\2" },
    { "^(.+)al \\^ iz(e|ed|es|ing|er|ers|ation|ations|m|ms|able|"
        "ability|abilities)$", "\\1aliz\\2" },
    { "^(.+)al \\^ is(e|ed|es|ing|er|ers|ation|ations|m|ms|able|"
        "ability|abilities)$", "\\1alis\\2" },
    { "^(.+)ar \\^ iz(e|ed|es|ing|er|ers|ation|ations|m|ms)$",
        "\\1ariz\\2" },
    { "^(.+)ar \\^ is(e|ed|es|ing|er|ers|ation|ations|m|ms)$",
        "\\1aris\\2" },
    { "^(.*[lmnty]) \\^ iz(e|es|ing|ed|er|ers|ation|ations|m|ms|able|"
        "ability|abilities)$", "\\1iz\\2" },
    { "^(.*[lmnty]) \\^ is(e|es|ing|ed|er|ers|ation|ations|m|ms|able|"
        "ability|abilities)$", "\\1is\\2" },
    /* == +olog == */
    { "^(.+)al \\^ olog(y|ist|ists|ical|ically)$", "\\1olog\\2" },
    /* == +ish == */
    { "^(.+)(ar|er|or) \\^ ish$", "\\1\\2ish" },
    /* free + ed = freed */
    { "^(.+e)e \\^ (e.+)$", "\\1\\2" },
    /* narrate + ing = narrating (silent e) */
    { "^(.+[bcdfghjklmnpqrstuvwxz])e \\^ ([aeiouy].*)$", "\\1\\2" },
    /* defer + ed = deferred (consonant doubling) */
    { "^(.*(?:[bcdfghjklmnprstvwxyz]|qu)[aeiou])([bcdfgklmnprtvz]) "
        "\\^ ([aeiouy].*)$", "\\1\\2\\2\\3" },
    /* Dotterel: a word ending in o may take es (potatoes) */
    { "^(.*o) \\^ s$", "\\1es" }
};

#define NUM_RULES ((int)(sizeof(rules) / sizeof(rules[0])))

/* --- the regular expression engine ------------------------------------ */

enum { op_char, op_any, op_class, op_split, op_jmp, op_save, op_bol,
    op_eol, op_notbehind, op_match };

struct inst {
    int op;
    int c;                     /* op_char */
    unsigned char set[32];     /* op_class */
    int x, y;                  /* op_split, op_jmp; op_notbehind: x..y */
    int n;                     /* op_save */
};

#define PROG_MAX 512
#define CAPS 20

struct prog {
    struct inst in[PROG_MAX];
    int len;
    int ngroups;
    const char *p;             /* while compiling */
    int bad;
};

static int emit(struct prog *g, int op)
{
    if (g->len == PROG_MAX) {
        g->bad = 1;
        return g->len - 1;
    }
    memset(&g->in[g->len], 0, sizeof(g->in[0]));
    g->in[g->len].op = op;
    g->len++;
    return g->len - 1;
}

/* make room: move in[at..] one up (jump targets after it too) */
static void insert(struct prog *g, int at)
{
    int i;

    if (g->len == PROG_MAX) {
        g->bad = 1;
        return;
    }
    memmove(&g->in[at + 1], &g->in[at], (g->len - at) * sizeof(g->in[0]));
    g->len++;
    for (i = 0; i < g->len; i++) {
        if (i == at)
            continue;
        if (g->in[i].op == op_split || g->in[i].op == op_jmp ||
            g->in[i].op == op_notbehind)
        {
            /* a jump from before the new instruction to where it is
             * now goes to it; jumps inside what moved move with it */
            if (g->in[i].x > at || (i > at && g->in[i].x == at))
                g->in[i].x++;
            if (g->in[i].y > at || (i > at && g->in[i].y == at))
                g->in[i].y++;
        }
    }
    memset(&g->in[at], 0, sizeof(g->in[0]));
}

static void compile_alt(struct prog *g);

static void set_bit(unsigned char *s, int c)
{
    s[c >> 3] |= (unsigned char)(1 << (c & 7));
}

static void compile_atom(struct prog *g)
{
    int i, neg, a, b, k, start;

    switch (*g->p) {
    case '(':
        g->p++;
        if (g->p[0] == '?' && g->p[1] == ':') {
            g->p += 2;
            compile_alt(g);
        } else if (g->p[0] == '?' && g->p[1] == '<' && g->p[2] == '!') {
            g->p += 3;
            i = emit(g, op_notbehind);
            start = g->len;
            compile_alt(g);
            emit(g, op_match);
            g->in[i].x = start;
            g->in[i].y = g->len;
        } else {
            k = g->ngroups + 1;
            g->ngroups = k;
            i = emit(g, op_save);
            g->in[i].n = 2 * k;
            compile_alt(g);
            i = emit(g, op_save);
            g->in[i].n = 2 * k + 1;
        }
        if (*g->p != ')')
            g->bad = 1;
        else
            g->p++;
        return;
    case '[':
        g->p++;
        i = emit(g, op_class);
        neg = 0;
        if (*g->p == '^') {
            neg = 1;
            g->p++;
        }
        while (*g->p && *g->p != ']') {
            a = (unsigned char)*g->p;
            g->p++;
            b = a;
            if (g->p[0] == '-' && g->p[1] && g->p[1] != ']') {
                b = (unsigned char)g->p[1];
                g->p += 2;
            }
            for (k = a; k <= b; k++) {
                set_bit(g->in[i].set, k);
                set_bit(g->in[i].set, low_ascii(k));    /* ignore case */
                set_bit(g->in[i].set, up_ascii(k));
            }
        }
        if (*g->p == ']')
            g->p++;
        else
            g->bad = 1;
        if (neg)
            for (k = 0; k < 32; k++)
                g->in[i].set[k] = (unsigned char)~g->in[i].set[k];
        return;
    case '.':
        g->p++;
        emit(g, op_any);
        return;
    case '^':
        g->p++;
        emit(g, op_bol);
        return;
    case '$':
        g->p++;
        emit(g, op_eol);
        return;
    case '\\':
        g->p++;
        if (!*g->p) {
            g->bad = 1;
            return;
        }
        i = emit(g, op_char);
        g->in[i].c = (unsigned char)*g->p;
        g->p++;
        return;
    }
    i = emit(g, op_char);
    g->in[i].c = (unsigned char)*g->p;
    g->p++;
}

static void compile_seq(struct prog *g)
{
    int start, i, j;

    while (*g->p && *g->p != '|' && *g->p != ')') {
        start = g->len;
        compile_atom(g);
        switch (*g->p) {
        case '*':      /* L: split body, out; body; jmp L */
            g->p++;
            insert(g, start);
            g->in[start].op = op_split;
            j = emit(g, op_jmp);
            g->in[j].x = start;
            g->in[start].x = start + 1;
            g->in[start].y = g->len;
            break;
        case '+':      /* body; split body, out */
            g->p++;
            i = emit(g, op_split);
            g->in[i].x = start;
            g->in[i].y = g->len;
            break;
        case '?':      /* split body, out; body */
            g->p++;
            insert(g, start);
            g->in[start].op = op_split;
            g->in[start].x = start + 1;
            g->in[start].y = g->len;
            break;
        }
    }
}

static void compile_alt(struct prog *g)
{
    int start, j, split;

    start = g->len;
    compile_seq(g);
    while (*g->p == '|') {
        g->p++;
        /* split first, second; first; jmp end; second */
        insert(g, start);
        split = start;
        g->in[split].op = op_split;
        g->in[split].x = split + 1;
        j = emit(g, op_jmp);
        g->in[split].y = g->len;
        compile_seq(g);
        g->in[j].x = g->len;
    }
}

static int compile(struct prog *g, const char *pat)
{
    memset(g, 0, sizeof(*g));
    g->p = pat;
    compile_alt(g);
    emit(g, op_match);
    if (*g->p)
        g->bad = 1;
    return g->bad ? -1 : 0;
}

static int run(struct prog *g, int pc, const char *s, int n, int sp,
    int *caps, int end)
{
    int res;
    struct inst *in;
    int old, k, ok, c2[CAPS];

    for (;;) {
        in = &g->in[pc];
        switch (in->op) {
        case op_char:
            /* Plover compiles the rules with re.IGNORECASE */
            if (sp >= n || low_ascii((unsigned char)s[sp]) !=
                low_ascii(in->c))
            {
                return 0;
            }
            sp++;
            pc++;
            break;
        case op_any:
            if (sp >= n)
                return 0;
            sp++;
            pc++;
            break;
        case op_class:
            if (sp >= n)
                return 0;
            k = (unsigned char)s[sp];
            if (!(in->set[k >> 3] & (1 << (k & 7))))
                return 0;
            sp++;
            pc++;
            break;
        case op_bol:
            if (sp != 0)
                return 0;
            pc++;
            break;
        case op_eol:
            if (sp != n)
                return 0;
            pc++;
            break;
        case op_jmp:
            pc = in->x;
            break;
        case op_split:
            res = run(g, in->x, s, n, sp, caps, end);
            if (res)
                return 1;
            pc = in->y;
            break;
        case op_save:
            if (in->n >= CAPS) {
                pc++;
                break;
            }
            old = caps[in->n];
            caps[in->n] = sp;
            res = run(g, pc + 1, s, n, sp, caps, end);
            if (res)
                return 1;
            caps[in->n] = old;
            return 0;
        case op_notbehind:
            ok = 0;
            for (k = sp; k >= 0 && !ok; k--) {
                memset(c2, -1, sizeof(c2));
                ok = run(g, in->x, s, n, k, c2, sp);
            }
            if (ok)
                return 0;
            pc = in->y;
            break;
        case op_match:
            if (end >= 0 && sp != end)
                return 0;
            return 1;
        default:
            return 0;
        }
    }
}

/* Match the whole of s (as the rules are anchored) and expand repl. */
static int prog_match(struct prog *g, const char *s, char *out, int size,
    const char *repl)
{
    int res;
    int caps[CAPS], i, n, k, len;

    for (i = 0; i < CAPS; i++)
        caps[i] = -1;
    res = run(g, 0, s, strlen(s), 0, caps, -1);
    if (!res)
        return 0;
    if (!repl)
        return 1;
    n = 0;
    for (i = 0; repl[i] && n < size - 1; i++) {
        if (repl[i] == '\\' && repl[i + 1] >= '0' && repl[i + 1] <= '9') {
            k = repl[i + 1] - '0';
            i++;
            if (2 * k + 1 < CAPS && caps[2 * k] >= 0 &&
                caps[2 * k + 1] >= caps[2 * k])
            {
                len = caps[2 * k + 1] - caps[2 * k];
                if (n + len > size - 1)
                    len = size - 1 - n;
                memcpy(out + n, s + caps[2 * k], len);
                n += len;
            }
            continue;
        }
        out[n] = repl[i];
        n++;
    }
    out[n] = 0;
    return 1;
}

int rx_match(const char *pat, const char *s, char *out, int size,
    const char *repl)
{
    int res;
    static struct prog g;

    res = compile(&g, pat);
    if (res < 0)
        return -1;
    return prog_match(&g, s, out, size, repl);
}

static struct prog *progs;

static void compile_rules(void)
{
    int res;
    int i;

    progs = malloc(NUM_RULES * sizeof(*progs));
    if (!progs) {
        say("out of memory", 0);
        exit(1);
    }
    for (i = 0; i < NUM_RULES; i++) {
        res = compile(&progs[i], rules[i][0]);
        if (res < 0)
            say("bad spelling rule: %s", rules[i][0]);
    }
}

/* --- the word list (Plover's american_english_words.txt) ------------ */

extern const char words_txt[];    /* "word rank\n", sorted, words.c */
extern const long words_len;

static long *word_at;
static long nwords;

static void index_words(void)
{
    long i, n;

    n = 0;
    for (i = 0; i < words_len; i++)
        if (words_txt[i] == '\n')
            n++;
    word_at = malloc((n + 1) * sizeof(long));
    if (!word_at) {
        say("out of memory", 0);
        exit(1);
    }
    nwords = 0;
    word_at[0] = 0;
    for (i = 0; i < words_len; i++)
        if (words_txt[i] == '\n') {
            nwords++;
            word_at[nwords] = i + 1;
        }
}

static int cmp_word(const char *w, long at)
{
    const unsigned char *a, *b;

    a = (const unsigned char *)w;
    b = (const unsigned char *)words_txt + at;
    while (*a && *a == *b) {
        a++;
        b++;
    }
    if (!*a && *b == ' ')
        return 0;
    if (*b == ' ')
        return 1;
    return (int)*a - (int)*b;
}

int words_rank(const char *w)
{
    long lo, hi, mid;
    int c, r;
    const char *p;

    if (!word_at)
        index_words();
    lo = 0;
    hi = nwords - 1;
    while (lo <= hi) {
        mid = (lo + hi) / 2;
        c = cmp_word(w, word_at[mid]);
        if (!c) {
            p = words_txt + word_at[mid];
            while (*p != ' ')
                p++;
            r = 0;
            for (p++; *p >= '0' && *p <= '9'; p++)
                r = r * 10 + (*p - '0');
            return r;
        }
        if (c < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    return -1;
}

/* --- add_suffix (Plover's orthography.py) ----------------------------- */

#define CAND_MAX 64

struct cands {
    char w[CAND_MAX][WORD_MAX];
    int n;
};

static void from_rules(const char *word, const char *suffix, int check,
    struct cands *c)
{
    char s[WORD_MAX * 3], o[WORD_MAX];
    int i, res;

    if (!progs)
        compile_rules();
    sprintf(s, "%.*s ^ %.*s", WORD_MAX - 1, word, WORD_MAX - 1, suffix);
    for (i = 0; i < NUM_RULES && c->n < CAND_MAX; i++) {
        if (progs[i].bad)
            continue;
        res = prog_match(&progs[i], s, o, sizeof(o), rules[i][1]);
        if (!res)
            continue;
        if (check && words_rank(o) < 0)
            continue;
        scopy(c->w[c->n], o, WORD_MAX);
        c->n++;
    }
}

static void one_suffix(const char *word, const char *suffix, char *out,
    int size)
{
    static struct cands c;
    char simple[WORD_MAX * 2];
    const char *alias;
    int i, best, rb, r;

    c.n = 0;
    alias = 0;
    if (!strcmp(suffix, "able"))
        alias = "ible";
    else if (!strcmp(suffix, "ability"))
        alias = "ibility";
    if (alias)
        from_rules(word, alias, 1, &c);
    sprintf(simple, "%.*s%.*s", WORD_MAX - 1, word, WORD_MAX - 1, suffix);
    if (words_rank(simple) >= 0 && c.n < CAND_MAX) {
        scopy(c.w[c.n], simple, WORD_MAX);
        c.n++;
    }
    from_rules(word, suffix, 1, &c);
    if (c.n) {
        /* the most common; the first of equals */
        best = 0;
        rb = words_rank(c.w[0]);
        for (i = 1; i < c.n; i++) {
            r = words_rank(c.w[i]);
            if (r < rb) {
                rb = r;
                best = i;
            }
        }
        scopy(out, c.w[best], size);
        return;
    }
    from_rules(word, suffix, 0, &c);
    if (c.n) {
        scopy(out, c.w[0], size);
        return;
    }
    scopy(out, simple, size);
}

void add_suffix(const char *word, const char *suffix, char *out, int size)
{
    char s[TEXT_MAX];
    const char *sp;

    sp = strchr(suffix, ' ');
    if (!sp) {
        one_suffix(word, suffix, out, size);
        return;
    }
    scopy(s, suffix, sizeof(s));
    if (sp - suffix < (int)sizeof(s))
        s[sp - suffix] = 0;
    one_suffix(word, s, out, size);
    scat(out, sp, size);
}
