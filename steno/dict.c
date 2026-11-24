/* Dictionaries: steno's plain files, and Dotterel's built-in ones.
 *
 *   NAME.dict    text -> outlines
 *   NAME.mode    plover-modal-dictionary: "entry", "dict" and "exit"
 *                tables and four switches
 *   dotterel-movement, dotterel-modifiers, dotterel-raw
 *                built in (their data is in dotterel.c)
 *
 * Plover's JSON is read only to convert it (steno -C).
 * The first dictionary given has the highest priority, as in Plover's
 * list. Every outline becomes strokes (key sets) when it is read, so a
 * lookup compares numbers, not text. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "steno.h"

#define DICT_MAX 32
#define MODS_MAX 16

enum { dk_json, dk_modal, dk_mods };

struct table {
    long *keys;           /* strokes of every entry, one after another */
    char *vals;           /* translations, each ending in 0 */
    long nkeys, ckeys, nvals, cvals;
    long *slot;           /* hash slots: entry number + 1 (0 = empty)
                             in the low 24 bits, 8 bits of the hash
                             above (SLOT_TAG) */
    long *ent_key;        /* entry -> offset in keys (count first) */
    long *ent_val;        /* entry -> offset in vals */
    long nent, cent, cval, nslot;
    int longest;
    /* by text, for dict_outlines: made when first asked, after a change */
    long *thead;          /* text hash -> first entry + 1, 0 = none */
    long *tnext;          /* entry -> next entry with that hash + 1 */
    long nthead;          /* 0: not made */
};

struct modal {
    struct table entry, dict, exit;
    int exclude_entry, exit_on_mismatch, exit_on_match, ignore_folding;
    int new_stroke, ignore, to_activate, matched, to_deactivate;
    int activated, dormant, dormant_buffer;
    long current;
};

struct modkey {
    long st;
    char key[COMBO_MAX];
};

struct mods {
    struct modkey *keys;
    int nkeys, ckeys;
    long mst[MODS_MAX];    /* the modifier strokes, and their keys: */
    int mbits[MODS_MAX];   /* 1 control, 2 alt, 4 shift, 8 super */
    int nmod;
    char out[COMBO_MAX + 64];
};

struct dict {
    char path[1024];
    int kind;
    int enabled;
    int raw;               /* dotterel-raw */
    int builtin;           /* from dotterel.c, no file */
    int longest;
    struct table t;
    struct modal *m;
    struct mods *x;
    long mtime, size;      /* to see when the file changes */
    char name[1024];       /* as the settings file names it */
    int user;              /* a "user" line, not "dict" */
};

static struct dict dicts[DICT_MAX];
static int ndicts;
static int raw_on;
static int raw_saved[DICT_MAX];
static int mods_stale;     /* the modifiers' keys must be found again */
static long dict_gen;      /* changes with any dictionary or its place */

/* --- strokes ------------------------------------------------------ */

static const char key_letters[] = "#STKPWHRAO*EUFRPBLGTSDZ";

/* next_key[c][pos]: the first key from pos on with the letter c, + 1
 * (0: none), so a letter is found in one step */
static unsigned char next_key[128][NUM_KEYS + 1];
static int next_made;

static void make_next_key(void)
{
    int c, pos, k;

    for (c = 0; c < 128; c++) {
        for (pos = 0; pos <= NUM_KEYS; pos++) {
            next_key[c][pos] = 0;
            for (k = pos; k < NUM_KEYS; k++) {
                if (key_letters[k] == c) {
                    next_key[c][pos] = (unsigned char)(k + 1);
                    break;
                }
            }
        }
    }
    next_made = 1;
}

long stroke_parse(const char *s, int n)
{
    long st;
    int i, k, pos;
    unsigned char c;

    if (!next_made)
        make_next_key();
    st = 0;
    pos = 0;
    for (i = 0; i < n; i++) {
        c = (unsigned char)s[i];
        if (c == '-') {
            if (pos < 11)
                pos = 11;
            continue;
        }
        if (c >= 128)
            return -1;
        k = next_key[c][pos];
        if (!k)
            return -1;
        st |= 1L << (k - 1);
        pos = k;
    }
    return st;
}

void stroke_text(long st, char *out)
{
    int k, n;

    n = 0;
    for (k = 0; k < 10; k++)
        if (st & (1L << k)) {
            out[n] = key_letters[k];
            n++;
        }
    /* no hyphen when A, O, *, E or U shows where the hands meet */
    if (!(st & 0x1f00L) && (st & 0x7fe000L)) {
        out[n] = '-';
        n++;
    }
    for (k = 10; k < NUM_KEYS; k++)
        if (st & (1L << k)) {
            out[n] = key_letters[k];
            n++;
        }
    out[n] = 0;
}

int outline_parse(const char *s, long *st, int max)
{
    int n, i, j;

    n = 0;
    i = 0;
    for (;;) {
        j = i;
        while (s[j] && s[j] != '/')
            j++;
        if (n == max)
            return -1;
        st[n] = stroke_parse(s + i, j - i);
        if (st[n] < 0)
            return -1;
        n++;
        if (!s[j])
            break;
        i = j + 1;
    }
    return n;
}

/* --- hash tables ---------------------------------------------------- */

static unsigned long hash(const long *st, int n)
{
    unsigned long h;
    int i;

    h = 2166136261UL;
    for (i = 0; i < n; i++) {
        h ^= (unsigned long)st[i];
        h *= 16777619UL;
        h &= 0xffffffffUL;
    }
    h ^= (unsigned long)n;
    return h;
}

static void *grow(void *p, long *cap, long need, long size)
{
    void *q;
    long c;

    if (need <= *cap)
        return p;
    c = *cap ? *cap : 256;
    while (c < need)
        c *= 2;
    q = malloc(c * size);
    if (!q) {
        say("out of memory", 0);
        die(1);
    }
    if (p) {
        memcpy(q, p, *cap * size);
        free(p);
    }
    *cap = c;
    return q;
}

/* An entry and 8 bits of its outline's hash in one slot: an outline
 * is read only when those are the same, so a miss rarely touches it */
#define SLOT_ENTRY 0xffffffL
#define SLOT_TAG(h) ((long)(((h) >> 24) & 0x7f) << 24)

static long find(struct table *t, const long *st, int n)
{
    unsigned long h;
    long i, e, tag;
    long *k;

    if (!t->nslot)
        return -1;
    h = hash(st, n);
    i = (long)(h & (unsigned long)(t->nslot - 1));
    tag = SLOT_TAG(h);
    while (t->slot[i]) {
        if ((t->slot[i] & ~SLOT_ENTRY) == tag) {
            e = (t->slot[i] & SLOT_ENTRY) - 1;
            k = t->keys + t->ent_key[e];
            if (k[0] == n && !memcmp(k + 1, st, n * sizeof(long)))
                return e;
        }
        i++;
        if (i == t->nslot)
            i = 0;
    }
    return -1;
}

static void rehash(struct table *t)
{
    long i, e, n;
    unsigned long h;
    long *k;

    n = 1024;                  /* a power of two: & for % */
    while (n < t->nslot * 2 || n < t->nent * 2 + 16)
        n *= 2;
    free(t->slot);
    t->slot = malloc(n * sizeof(long));
    if (!t->slot) {
        say("out of memory", 0);
        die(1);
    }
    memset(t->slot, 0, n * sizeof(long));
    t->nslot = n;
    for (e = 0; e < t->nent; e++) {
        k = t->keys + t->ent_key[e];
        h = hash(k + 1, (int)k[0]);
        i = (long)(h & (unsigned long)(n - 1));
        while (t->slot[i]) {
            i++;
            if (i == n)
                i = 0;
        }
        t->slot[i] = (e + 1) | SLOT_TAG(h);
    }
}

/* Later entries win, as when Python reads a JSON object. */
static void put(struct table *t, const long *st, int n, const char *v)
{
    long e, len, i;
    unsigned long h;

    len = strlen(v) + 1;
    t->nthead = 0;             /* the text index is stale */
    t->vals = grow(t->vals, &t->cvals, t->nvals + len, 1);
    memcpy(t->vals + t->nvals, v, len);
    e = find(t, st, n);
    if (e >= 0) {
        t->ent_val[e] = t->nvals;
        t->nvals += len;
        return;
    }
    if (t->nent + 1 >= SLOT_ENTRY) {
        say("too many entries in one dictionary", 0);
        return;
    }
    if (t->nent * 2 + 2 > t->nslot)
        rehash(t);
    t->keys = grow(t->keys, &t->ckeys, t->nkeys + n + 1, sizeof(long));
    t->keys[t->nkeys] = n;
    memcpy(t->keys + t->nkeys + 1, st, n * sizeof(long));
    t->ent_key = grow(t->ent_key, &t->cent, t->nent + 1, sizeof(long));
    t->ent_val = grow(t->ent_val, &t->cval, t->nent + 1, sizeof(long));
    t->ent_key[t->nent] = t->nkeys;
    t->ent_val[t->nent] = t->nvals;
    t->nkeys += n + 1;
    t->nvals += len;
    t->nent++;
    h = hash(st, n);
    i = (long)(h & (unsigned long)(t->nslot - 1));
    while (t->slot[i]) {
        i++;
        if (i == t->nslot)
            i = 0;
    }
    t->slot[i] = t->nent | SLOT_TAG(h);
    if (n > t->longest)
        t->longest = n;
}

static const char *get(struct table *t, const long *st, int n)
{
    long e;

    e = find(t, st, n);
    if (e < 0)
        return 0;
    return t->vals + t->ent_val[e];
}

/* --- JSON (what Plover writes; only what that needs) ------------- */

struct json {
    const char *s;
    long i, n;
    const char *path;
    char str[TEXT_MAX * 2];
};

static void json_ws(struct json *j)
{
    while (j->i < j->n && (j->s[j->i] == ' ' || j->s[j->i] == '\n' ||
        j->s[j->i] == '\r' || j->s[j->i] == '\t'))
    {
        j->i++;
    }
}

static int json_err(struct json *j, const char *what)
{
    char m[64];

    sprintf(m, "%.40s at byte %ld", what, j->i);
    fprintf(stderr, "steno: %s: %s\n", j->path, m);
    return -1;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int json_u4(struct json *j, long *u)
{
    int k, v;

    *u = 0;
    for (k = 0; k < 4; k++) {
        if (j->i >= j->n)
            return -1;
        v = hexval((unsigned char)j->s[j->i]);
        if (v < 0)
            return -1;
        *u = *u * 16 + v;
        j->i++;
    }
    return 0;
}

static int put_u8(char *o, long c)
{
    if (c < 0x80) {
        o[0] = (char)c;
        return 1;
    }
    if (c < 0x800) {
        o[0] = (char)(0xc0 | (c >> 6));
        o[1] = (char)(0x80 | (c & 0x3f));
        return 2;
    }
    if (c < 0x10000) {
        o[0] = (char)(0xe0 | (c >> 12));
        o[1] = (char)(0x80 | ((c >> 6) & 0x3f));
        o[2] = (char)(0x80 | (c & 0x3f));
        return 3;
    }
    o[0] = (char)(0xf0 | (c >> 18));
    o[1] = (char)(0x80 | ((c >> 12) & 0x3f));
    o[2] = (char)(0x80 | ((c >> 6) & 0x3f));
    o[3] = (char)(0x80 | (c & 0x3f));
    return 4;
}

/* A string into j->str. */
static int json_string(struct json *j)
{
    int n;
    int c;
    long u, u2;

    json_ws(j);
    if (j->i >= j->n || j->s[j->i] != '"')
        return json_err(j, "a string was expected");
    j->i++;
    n = 0;
    for (;;) {
        if (j->i >= j->n)
            return json_err(j, "unfinished string");
        c = (unsigned char)j->s[j->i];
        j->i++;
        if (c == '"')
            break;
        if (n > (int)sizeof(j->str) - 8)
            return json_err(j, "string too long");
        if (c != '\\') {
            j->str[n] = (char)c;
            n++;
            continue;
        }
        if (j->i >= j->n)
            return json_err(j, "unfinished string");
        c = j->s[j->i];
        j->i++;
        switch (c) {
        case 'n': j->str[n] = '\n'; n++; break;
        case 't': j->str[n] = '\t'; n++; break;
        case 'r': j->str[n] = '\r'; n++; break;
        case 'b': j->str[n] = '\b'; n++; break;
        case 'f': j->str[n] = '\f'; n++; break;
        case 'u':
            if (json_u4(j, &u) < 0)
                return json_err(j, "bad \\u");
            if (u >= 0xd800 && u < 0xdc00 && j->i + 6 <= j->n &&
                j->s[j->i] == '\\' && j->s[j->i + 1] == 'u')
            {
                j->i += 2;
                if (json_u4(j, &u2) < 0)
                    return json_err(j, "bad \\u");
                u = 0x10000 + ((u - 0xd800) << 10) + (u2 - 0xdc00);
            }
            n += put_u8(j->str + n, u);
            break;
        default:
            j->str[n] = (char)c;
            n++;
        }
    }
    j->str[n] = 0;
    return n;
}

static int json_char(struct json *j, int c)
{
    json_ws(j);
    if (j->i < j->n && j->s[j->i] == c) {
        j->i++;
        return 1;
    }
    return 0;
}

static void warn_key(const char *path, const char *key)
{
    if (verbose)
        fprintf(stderr, "steno: %s: not steno, left out: %s\n", path, key);
}

/* An object of strings, after its "{". */
static int json_table(struct json *j, struct table *t)
{
    int res;
    char key[TEXT_MAX * 2];
    long st[STROKE_MAX];
    int n;

    res = json_char(j, '}');
    if (res)
        return 0;
    for (;;) {
        res = json_string(j);
        if (res < 0)
            return -1;
        scopy(key, j->str, sizeof(key));
        res = json_char(j, ':');
        if (!res)
            return json_err(j, "':' was expected");
        res = json_string(j);
        if (res < 0)
            return -1;
        n = outline_parse(key, st, STROKE_MAX);
        if (n < 0)
            warn_key(j->path, key);
        else
            put(t, st, n, j->str);
        res = json_char(j, ',');
        if (res)
            continue;
        res = json_char(j, '}');
        if (res)
            return 0;
        return json_err(j, "',' or '}' was expected");
    }
}

static int json_bool(struct json *j, int *v)
{
    json_ws(j);
    if (j->i + 4 <= j->n && !strncmp(j->s + j->i, "true", 4)) {
        j->i += 4;
        *v = 1;
        return 0;
    }
    if (j->i + 5 <= j->n && !strncmp(j->s + j->i, "false", 5)) {
        j->i += 5;
        *v = 0;
        return 0;
    }
    return json_err(j, "true or false was expected");
}

static int load_json(struct dict *d, struct json *j)
{
    int res;

    res = json_char(j, '{');
    if (!res)
        return json_err(j, "'{' was expected");
    res = json_table(j, &d->t);
    if (res < 0)
        return -1;
    d->longest = d->t.longest;
    return 0;
}

static int load_modal(struct dict *d, struct json *j)
{
    int res;
    struct modal *m;
    char name[64];
    int *flag;

    m = malloc(sizeof(*m));
    if (!m)
        return -1;
    memset(m, 0, sizeof(*m));
    m->exit_on_mismatch = 1;
    m->ignore_folding = 1;
    m->new_stroke = 1;
    d->m = m;
    res = json_char(j, '{');
    if (!res)
        return json_err(j, "'{' was expected");
    res = json_char(j, '}');
    if (res)
        return 0;
    for (;;) {
        res = json_string(j);
        if (res < 0)
            return -1;
        scopy(name, j->str, sizeof(name));
        res = json_char(j, ':');
        if (!res)
            return json_err(j, "':' was expected");
        flag = 0;
        if (!strcmp(name, "exclude_entry"))
            flag = &m->exclude_entry;
        else if (!strcmp(name, "exit_on_mismatch"))
            flag = &m->exit_on_mismatch;
        else if (!strcmp(name, "exit_on_match"))
            flag = &m->exit_on_match;
        else if (!strcmp(name, "ignore_folding"))
            flag = &m->ignore_folding;
        if (flag) {
            res = json_bool(j, flag);
            if (res < 0)
                return -1;
        } else if (!strcmp(name, "entry") || !strcmp(name, "dict") ||
            !strcmp(name, "exit"))
        {
            res = json_char(j, '{');
            if (!res)
                return json_err(j, "only tables in the file itself");
            res = json_table(j, name[0] == 'e' && name[1] == 'n' ?
                &m->entry : name[0] == 'd' ? &m->dict : &m->exit);
            if (res < 0)
                return -1;
        } else {
            return json_err(j, "unknown setting");
        }
        res = json_char(j, ',');
        if (res)
            continue;
        res = json_char(j, '}');
        if (res)
            break;
        return json_err(j, "',' or '}' was expected");
    }
    d->longest = m->entry.longest;
    if (m->dict.longest > d->longest)
        d->longest = m->dict.longest;
    if (m->exit.longest > d->longest)
        d->longest = m->exit.longest;
    return 0;
}

/* --- the modal dictionary (plover-modal-dictionary) ---------------- */

static const char *modal_lookup(struct modal *m, const long *st, int n)
{
    const char *v;

    if (m->ignore || !n)
        return 0;
    if (m->new_stroke) {
        m->new_stroke = 0;
        m->current = st[n - 1];
    } else {
        m->matched = 0;
        if (st[n - 1] != m->current && m->ignore_folding) {
            m->ignore = 1;
            return 0;
        }
    }
    if (m->activated || (m->dormant && n > m->dormant_buffer)) {
        v = get(&m->exit, st, n);
        if (v)
            m->to_deactivate = 1;
        else
            v = get(&m->dict, st, n);
        if (v) {
            m->matched = 1;
            if (!m->activated)
                m->to_activate = 1;
            return v;
        }
    }
    if (!m->activated || !m->exclude_entry) {
        v = get(&m->entry, st, n);
        if (v) {
            m->matched = 1;
            m->to_activate = 1;
            return v;
        }
    }
    return 0;
}

static void modal_stroked(struct modal *m, int longest)
{
    if (m->activated) {
        if (m->to_deactivate || (m->exit_on_mismatch && !m->matched) ||
            (m->exit_on_match && m->matched))
        {
            m->activated = 0;
            m->dormant = 1;
            m->dormant_buffer = 1;
        }
    } else if (m->to_activate) {
        if (!m->to_deactivate)
            m->activated = 1;
    } else if (m->dormant) {
        m->dormant_buffer++;
        if (m->dormant_buffer >= longest) {
            m->dormant = 0;
            m->dormant_buffer = 0;
        }
    }
    m->new_stroke = 1;
    m->ignore = 0;
    m->to_activate = 0;
    m->matched = 0;
    m->to_deactivate = 0;
}

/* --- dotterel-modifiers, built in ----------------------------------- *
 * A modifier stroke (KHR Ctrl, SH Shift, THRA Alt, KPW Super, and the
 * chords for several), then a stroke that presses one key or types one
 * character: that key with the modifiers. The strokes and where the
 * keys come from are in dotterel.c. */

static int mod_of(struct mods *x, long st)
{
    int i;

    for (i = 0; i < x->nmod; i++)
        if (x->mst[i] == st)
            return x->mbits[i];
    return 0;
}

/* Remove {^} {-|} {~|} {>} {<}. */
static void strip_spacing(const char *s, char *o, int size)
{
    int n, len;

    n = 0;
    while (*s && n < size - 1) {
        len = 0;
        if (s[0] == '{' && s[1] == '^' && s[2] == '}')
            len = 3;
        else if (s[0] == '{' && (s[1] == '-' || s[1] == '~') &&
            s[2] == '|' && s[3] == '}')
        {
            len = 4;
        } else if (s[0] == '{' && (s[1] == '>' || s[1] == '<') &&
            s[2] == '}')
        {
            len = 3;
        }
        if (len) {
            s += len;
            continue;
        }
        o[n] = *s;
        n++;
        s++;
    }
    o[n] = 0;
}

static const char *char_key(int c)
{
    switch (c) {
    case '\n': return "Return";
    case '\t': return "Tab";
    case ' ': return "space";
    case '.': return "period";
    case ',': return "comma";
    case '/': return "slash";
    case ';': return "semicolon";
    case '\'': return "apostrophe";
    case '[': return "bracketleft";
    case ']': return "bracketright";
    case '\\': return "backslash";
    case '-': return "minus";
    case '=': return "equal";
    case '`': return "grave";
    }
    return 0;
}

static int is_alnum(int c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z');
}

/* The one key a translation presses or types, or 0. */
static int key_of(const char *tr, char *key)
{
    char r[TEXT_MAX];
    const char *k;
    int n, i, c, len;

    strip_spacing(tr, r, sizeof(r));
    n = strlen(r);
    /* {#Up}: one key, no modifiers inside */
    if (n > 3 && r[0] == '{' && r[1] == '#' && r[n - 1] == '}') {
        for (i = 2; i < n - 1; i++)
            if (r[i] == '{' || r[i] == '}' || r[i] == '(' ||
                r[i] == ')' || is_space_at(r + i, n - i))
            {
                break;
            }
        if (i == n - 1 && n - 3 < COMBO_MAX) {
            memcpy(key, r + 2, n - 3);
            key[n - 3] = 0;
            return 1;
        }
    }
    /* \{(?:&|\^?~?\|?)(.)\^?\}  or one character */
    c = -1;
    if (n >= 3 && r[0] == '{' && r[n - 1] == '}') {
        i = 1;
        if (r[i] == '&') {
            i++;
        } else {
            if (r[i] == '^')
                i++;
            if (r[i] == '~')
                i++;
            if (r[i] == '|')
                i++;
        }
        len = u8get(r + i, n - 1 - i, 0);
        if (len && (i + len == n - 1 ||
            (i + len == n - 2 && r[n - 2] == '^')))
        {
            c = len == 1 ? (unsigned char)r[i] : 256;
        }
    }
    if (c < 0) {
        len = u8get(r, n, 0);
        if (len && len == n)
            c = len == 1 ? (unsigned char)r[0] : 256;
    }
    if (c < 0 || c == 256)
        return 0;
    if (is_alnum(c)) {
        if (c >= 'A' && c <= 'Z')
            sprintf(key, "shift_l(%c)", c - 'A' + 'a');
        else
            sprintf(key, "%c", c);
        return 1;
    }
    k = char_key(c);
    if (!k)
        return 0;
    strcpy(key, k);
    return 1;
}

static void mods_add(struct mods *x, long st, const char *key)
{
    int i;
    long c;

    for (i = 0; i < x->nkeys; i++)
        if (x->keys[i].st == st)
            return;
    c = x->ckeys;
    x->keys = grow(x->keys, &c, x->nkeys + 1, sizeof(struct modkey));
    x->ckeys = (int)c;
    x->keys[x->nkeys].st = st;
    scopy(x->keys[x->nkeys].key, key, COMBO_MAX);
    x->nkeys++;
}

static void mods_read_table(struct mods *x, struct table *t)
{
    long e;
    long *k;
    char key[COMBO_MAX];

    for (e = 0; e < t->nent; e++) {
        k = t->keys + t->ent_key[e];
        if (k[0] != 1 || mod_of(x, k[1]))
            continue;
        if (key_of(t->vals + t->ent_val[e], key))
            mods_add(x, k[1], key);
    }
}


static const char *mods_lookup(struct mods *x, const long *st, int n)
{
    int bits, i, b;
    const char *key;
    char k[COMBO_MAX + 64], t[COMBO_MAX + 64];
    static const char *const names[] = { "control", "alt", "shift",
        "super" };

    for (i = 0; i < n - 1; i++)
        if (!mod_of(x, st[i]))
            return 0;
    if (mod_of(x, st[n - 1]))
        return "{#}";
    for (i = 0; i + 1 < n - 1; i++)
        if (st[i] == st[i + 1])
            return 0;
    if (n < 2)
        return 0;
    key = 0;
    for (i = 0; i < x->nkeys; i++)
        if (x->keys[i].st == st[n - 1])
            key = x->keys[i].key;
    if (!key)
        return 0;
    bits = 0;
    for (i = 0; i < n - 1; i++)
        bits |= mod_of(x, st[i]);
    scopy(k, key, sizeof(k));
    for (b = 3; b >= 0; b--)
        if (bits & (1 << b)) {
            sprintf(t, "%s_l(%.*s)", names[b], COMBO_MAX, k);
            scopy(k, t, sizeof(k));
        }
    sprintf(x->out, "{^}{#%.*s}{^}", COMBO_MAX, k);
    return x->out;
}

/* --- the collection --------------------------------------------- */

static int ends_with(const char *s, const char *e)
{
    int n, m;

    n = strlen(s);
    m = strlen(e);
    return n >= m && !strcmp(s + n - m, e);
}

static void file_stamp(const char *path, long *mtime, long *size)
{
    struct stat sb;

    *mtime = -1;
    *size = -1;
    if (stat(path, &sb) == 0) {
        *mtime = (long)sb.st_mtime;
        *size = (long)sb.st_size;
    }
}

static void table_free(struct table *t)
{
    free(t->keys);
    free(t->vals);
    free(t->slot);
    free(t->ent_key);
    free(t->ent_val);
    free(t->thead);
    free(t->tnext);
    memset(t, 0, sizeof(*t));
}

static void dict_free(struct dict *d)
{
    table_free(&d->t);
    if (d->m) {
        table_free(&d->m->entry);
        table_free(&d->m->dict);
        table_free(&d->m->exit);
        free(d->m);
        d->m = 0;
    }
    if (d->x) {
        free(d->x->keys);
        free(d->x);
        d->x = 0;
    }
}

/* --- steno's own format ----------------------------------------------
 *
 * One entry a line, fields between tabs:
 *
 *   NAME.dict   TEXT <tab> OUTLINE [OUTLINE...]
 *   NAME.mode   option <tab> NAME <tab> yes|no
 *               entry|dict|exit <tab> TEXT <tab> OUTLINE [OUTLINE...]
 *
 * Outlines are separated by spaces. In TEXT, \t is a tab, \n a newline,
 * \r a carriage return and \\ a backslash. Empty lines are skipped. */

static char *next_line(char **p)
{
    char *l, *e;

    l = *p;
    if (!l || !*l)
        return 0;
    e = strchr(l, '\n');
    if (e) {
        *e = 0;
        *p = e + 1;
    } else {
        *p = l + strlen(l);
    }
    e = l + strlen(l);
    if (e > l && e[-1] == '\r')
        e[-1] = 0;
    return l;
}

static int split_tabs(char *l, char **f, int max)
{
    int n;
    char *t;

    n = 0;
    f[n] = l;
    n++;
    while (n < max) {
        t = strchr(f[n - 1], '\t');
        if (!t)
            break;
        *t = 0;
        f[n] = t + 1;
        n++;
    }
    return n;
}

static void unescape(const char *s, char *o, int size)
{
    int n;

    n = 0;
    while (*s && n < size - 1) {
        if (s[0] == '\\' && s[1]) {
            if (s[1] == 'n' || s[1] == 't' || s[1] == 'r' ||
                s[1] == '\\')
            {
                o[n] = s[1] == 'n' ? '\n' : s[1] == 't' ? '\t' :
                    s[1] == 'r' ? '\r' : '\\';
                n++;
                s += 2;
                continue;
            }
        }
        o[n] = *s;
        n++;
        s++;
    }
    o[n] = 0;
}

static void bad_line(const char *path, int ln, const char *what)
{
    char m[1300];

    sprintf(m, "%.1000s:%d: %%s", path, ln);
    say(m, what);
}

/* every outline of a line, for text */
static void put_outlines(struct table *t, char *outlines, const char *text,
    const char *path, int ln)
{
    long st[STROKE_MAX];
    char *o, *e;
    int n, end;

    o = outlines;
    for (;;) {
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
        if (n < 0)
            bad_line(path, ln, "not an outline");
        else
            put(t, st, n, text);
        if (end)
            break;
        o = e + 1;
    }
}

static int load_dict_file(struct dict *d, char *buf)
{
    char *p, *l, *f[3], text[TEXT_MAX];
    int ln, n;
    long words;

    /* about as many entries as words: hash slots for them all at once,
     * not grown (and every entry hashed again) as they come */
    words = 0;
    for (p = buf; *p; p++)
        if (*p == ' ' || *p == '\t')
            words++;
    if (words * 2 + 16 > d->t.nslot) {
        d->t.nslot = words;
        rehash(&d->t);
    }
    p = buf;
    ln = 0;
    while ((l = next_line(&p)) != 0) {
        ln++;
        if (!l[0])
            continue;
        n = split_tabs(l, f, 2);
        if (n < 2) {
            bad_line(d->path, ln, "no tab between text and outlines");
            continue;
        }
        unescape(f[0], text, sizeof(text));
        put_outlines(&d->t, f[1], text, d->path, ln);
    }
    d->longest = d->t.longest;
    return 0;
}

static int load_mode_file(struct dict *d, char *buf)
{
    char *p, *l, *f[4], text[TEXT_MAX];
    struct modal *m;
    struct table *t;
    int ln, n, *flag;

    m = malloc(sizeof(*m));
    if (!m)
        return -1;
    memset(m, 0, sizeof(*m));
    m->exit_on_mismatch = 1;
    m->ignore_folding = 1;
    m->new_stroke = 1;
    d->m = m;
    p = buf;
    ln = 0;
    while ((l = next_line(&p)) != 0) {
        ln++;
        if (!l[0])
            continue;
        n = split_tabs(l, f, 3);
        if (n == 3 && !strcmp(f[0], "option")) {
            flag = !strcmp(f[1], "exclude_entry") ? &m->exclude_entry :
                !strcmp(f[1], "exit_on_mismatch") ? &m->exit_on_mismatch :
                !strcmp(f[1], "exit_on_match") ? &m->exit_on_match :
                !strcmp(f[1], "ignore_folding") ? &m->ignore_folding : 0;
            if (!flag)
                bad_line(d->path, ln, "unknown option");
            else
                *flag = !strcmp(f[2], "yes");
            continue;
        }
        t = n < 3 ? 0 : !strcmp(f[0], "entry") ? &m->entry :
            !strcmp(f[0], "dict") ? &m->dict :
            !strcmp(f[0], "exit") ? &m->exit : 0;
        if (!t) {
            bad_line(d->path, ln, "option, entry, dict or exit?");
            continue;
        }
        unescape(f[1], text, sizeof(text));
        put_outlines(t, f[2], text, d->path, ln);
    }
    d->longest = m->entry.longest;
    if (m->dict.longest > d->longest)
        d->longest = m->dict.longest;
    if (m->exit.longest > d->longest)
        d->longest = m->exit.longest;
    return 0;
}

static int load_into(struct dict *d, const char *path);
static int dict_find(const char *name);

/* the outlines of a built-in line, into t */
static void put_builtin(struct table *t, const char *text,
    const char *outlines)
{
    char o[256];

    scopy(o, outlines, sizeof(o));
    put_outlines(t, o, text, "dotterel.c", 0);
}

static int load_movement(struct dict *d)
{
    struct modal *m;
    const struct builtin_line *b;

    m = malloc(sizeof(*m));
    if (!m)
        return -1;
    memset(m, 0, sizeof(*m));
    m->exit_on_mismatch = 1;
    m->ignore_folding = 1;
    m->new_stroke = 1;
    d->m = m;
    for (b = dotterel_movement; b->kind; b++)
        put_builtin(b->kind == 'e' ? &m->entry : &m->dict, b->text,
            b->outlines);
    d->longest = m->entry.longest;
    if (m->dict.longest > d->longest)
        d->longest = m->dict.longest;
    return 0;
}

static int load_modifiers(struct dict *d)
{
    struct mods *x;
    int i;

    x = malloc(sizeof(*x));
    if (!x)
        return -1;
    memset(x, 0, sizeof(*x));
    d->x = x;
    d->longest = 4;
    for (i = 0; i < MODS_MAX && dotterel_mod_strokes[i]; i++) {
        x->mst[i] = stroke_parse(dotterel_mod_strokes[i],
            strlen(dotterel_mod_strokes[i]));
        x->mbits[i] = dotterel_mod_bits[i];
    }
    x->nmod = i;
    mods_stale = 1;
    return 0;
}

/* The keys of the modifiers, from the dictionaries dotterel.c names:
 * the ones in the list, or else their files. */
static void mods_find_keys(struct mods *x)
{
    static struct dict tmp;
    char file[300];
    int i, j, r;
    struct dict *d;

    x->nkeys = 0;
    for (i = 0; dotterel_mod_keys[i]; i++) {
        j = dict_find(dotterel_mod_keys[i]);
        d = j >= 0 ? &dicts[j] : 0;
        if (!d) {
            sprintf(file, "%.200s.dict", dotterel_mod_keys[i]);
            if (dict_is_builtin(dotterel_mod_keys[i]))
                scopy(file, dotterel_mod_keys[i], sizeof(file));
            r = load_into(&tmp, dict_dir_file(file));
            if (r < 0)
                continue;
            d = &tmp;
        }
        if (d->kind == dk_json)
            mods_read_table(x, &d->t);
        else if (d->kind == dk_modal)
            mods_read_table(x, &d->m->entry);
        if (d == &tmp)
            dict_free(&tmp);
    }
}

static void mods_refresh(void)
{
    int i;

    if (!mods_stale)
        return;
    mods_stale = 0;
    for (i = 0; i < ndicts; i++)
        if (dicts[i].kind == dk_mods)
            mods_find_keys(dicts[i].x);
}

int dict_is_builtin(const char *name)
{
    return !strcmp(name, "dotterel-movement") ||
        !strcmp(name, "dotterel-modifiers") ||
        !strcmp(name, "dotterel-raw");
}

static int load_builtin(struct dict *d, const char *name)
{
    d->builtin = 1;
    d->mtime = d->size = -1;
    if (!strcmp(name, "dotterel-movement")) {
        d->kind = dk_modal;
        return load_movement(d);
    }
    if (!strcmp(name, "dotterel-modifiers")) {
        d->kind = dk_mods;
        return load_modifiers(d);
    }
    d->kind = dk_json;
    d->raw = 1;
    put_builtin(&d->t, dotterel_raw_text, dotterel_raw_outline);
    d->longest = d->t.longest;
    return 0;
}

/* Plover's JSON (.json, .modal), for steno -C only */
static int load_plover(struct dict *d, const char *path)
{
    struct json j;
    char *buf;
    long size;
    int r;

    buf = read_file(path, &size);
    if (!buf) {
        say("can't read %s", path);
        return -1;
    }
    j.s = buf;
    j.i = 0;
    j.n = size;
    j.path = path;
    if (ends_with(path, ".modal")) {
        d->kind = dk_modal;
        r = load_modal(d, &j);
    } else {
        d->kind = dk_json;
        r = load_json(d, &j);
    }
    free(buf);
    return r;
}

static int base_is(const char *path, const char *name)
{
    const char *b;

    b = strrchr(path, '/');
    b = b ? b + 1 : path;
    return !strncmp(b, name, strlen(name));
}

static int load_into(struct dict *d, const char *path)
{
    char *buf;
    long size;
    int r;

    memset(d, 0, sizeof(*d));
    scopy(d->path, path, sizeof(d->path));
    d->enabled = 1;
    if (dict_is_builtin(path)) {
        r = load_builtin(d, path);
        if (r < 0)
            dict_free(d);
        return r;
    }
    d->raw = base_is(path, "dotterel-raw.");
    file_stamp(path, &d->mtime, &d->size);
    if (ends_with(path, ".mods") || ends_with(path, ".py")) {
        say("%s: the modifiers are built in now (dict "
            "dotterel-modifiers)", path);
        return -1;
    }
    if (ends_with(path, ".json") || ends_with(path, ".modal")) {
        say("%s is in Plover's format: steno -C DIR FILE converts it",
            path);
        return -1;
    }
    if (!ends_with(path, ".dict") && !ends_with(path, ".mode")) {
        say("not a .dict or .mode file, nor built in: %s", path);
        return -1;
    }
    buf = read_file(path, &size);
    if (!buf) {
        say("can't read %s", path);
        return -1;
    }
    if (ends_with(path, ".mode")) {
        d->kind = dk_modal;
        r = load_mode_file(d, buf);
    } else {
        d->kind = dk_json;
        r = load_dict_file(d, buf);
    }
    free(buf);
    if (r < 0) {
        dict_free(d);
        return -1;
    }
    return 0;
}

/* --- steno -C: Plover's files in steno's format ----------------------- */

static void write_text(FILE *f, const char *s)
{
    for (; *s; s++) {
        if (*s == '\\')
            fputs("\\\\", f);
        else if (*s == '\t')
            fputs("\\t", f);
        else if (*s == '\n')
            fputs("\\n", f);
        else if (*s == '\r')
            fputs("\\r", f);
        else
            fputc(*s, f);
    }
}

static unsigned long text_hash(const char *s)
{
    unsigned long h;

    h = 5381;
    while (*s) {
        h = h * 33 + (unsigned char)*s;
        s++;
    }
    return h;
}

/* Each text once, with all its outlines, in the order the texts first
 * come in the file. */
static int write_table(FILE *f, struct table *t, const char *prefix)
{
    long *slot, *next, *last, e, g, nslot, i, *k;
    unsigned long h;
    char s[32];
    int j;

    nslot = 16;
    while (nslot < t->nent * 2)
        nslot *= 2;
    slot = malloc(nslot * sizeof(long));
    next = malloc((t->nent + 1) * sizeof(long));
    last = malloc((t->nent + 1) * sizeof(long));
    if (!slot || !next || !last) {
        free(slot);
        free(next);
        free(last);
        return -1;
    }
    for (i = 0; i < nslot; i++)
        slot[i] = -1;
    for (e = 0; e < t->nent; e++) {
        next[e] = -1;
        last[e] = -2;          /* not a group's first */
        h = text_hash(t->vals + t->ent_val[e]) & (nslot - 1);
        for (;;) {
            g = slot[h];
            if (g < 0 || !strcmp(t->vals + t->ent_val[g],
                t->vals + t->ent_val[e]))
            {
                break;
            }
            h = (h + 1) & (nslot - 1);
        }
        if (g < 0) {
            slot[h] = e;
            last[e] = e;
        } else {
            next[last[g]] = e;
            last[g] = e;
        }
    }
    for (e = 0; e < t->nent; e++) {
        if (last[e] == -2)
            continue;
        fputs(prefix, f);
        write_text(f, t->vals + t->ent_val[e]);
        fputc('\t', f);
        for (g = e; g >= 0; g = next[g]) {
            k = t->keys + t->ent_key[g];
            if (g != e)
                fputc(' ', f);
            for (j = 0; j < k[0]; j++) {
                stroke_text(k[1 + j], s);
                if (j)
                    fputc('/', f);
                fputs(s, f);
            }
        }
        fputc('\n', f);
    }
    free(slot);
    free(next);
    free(last);
    return 0;
}

/* in (Plover's) -> outdir/name (steno's); 0 ok, 1 nothing to make
 * (built in) */
int dict_convert(const char *in, const char *outdir, char *made, int size)
{
    struct dict d;
    char base[256], tmp[1400], *dot;
    const char *b;
    FILE *f;
    int r, closed;

    b = strrchr(in, '/');
    b = b ? b + 1 : in;
    scopy(base, b, sizeof(base) - 8);
    dot = strrchr(base, '.');
    if (!dot) {
        say("what is %s? (.json or .modal)", in);
        return -1;
    }
    *dot = 0;
    if (dict_is_builtin(base))
        return 1;
    *dot = '.';
    memset(&d, 0, sizeof(d));
    scopy(d.path, in, sizeof(d.path));
    if (!strcmp(dot, ".modal")) {
        strcpy(dot, ".mode");
    } else if (!strcmp(dot, ".json")) {
        strcpy(dot, ".dict");
    } else {
        say("what is %s? (.json or .modal)", in);
        return -1;
    }
    r = load_plover(&d, in);
    if (r < 0) {
        dict_free(&d);
        return -1;
    }
    scopy(made, outdir, size - 256);
    scat(made, "/", size);
    scat(made, base, size);
    sprintf(tmp, "%.1300s.new", made);
    f = fopen(tmp, "w");
    if (!f) {
        say("can't write %s", tmp);
        dict_free(&d);
        return -1;
    }
    r = 0;
    if (d.kind == dk_modal) {
        fprintf(f, "option\texclude_entry\t%s\n",
            d.m->exclude_entry ? "yes" : "no");
        fprintf(f, "option\texit_on_mismatch\t%s\n",
            d.m->exit_on_mismatch ? "yes" : "no");
        fprintf(f, "option\texit_on_match\t%s\n",
            d.m->exit_on_match ? "yes" : "no");
        fprintf(f, "option\tignore_folding\t%s\n",
            d.m->ignore_folding ? "yes" : "no");
        r = write_table(f, &d.m->entry, "entry\t");
        if (r == 0)
            r = write_table(f, &d.m->dict, "dict\t");
        if (r == 0)
            r = write_table(f, &d.m->exit, "exit\t");
    } else {
        r = write_table(f, &d.t, "");
    }
    dict_free(&d);
    closed = fclose(f);
    if (closed != 0 || r < 0) {
        say("can't write %s", tmp);
        unlink(tmp);
        return -1;
    }
    r = rename(tmp, made);
    if (r < 0) {
        say("can't write %s", made);
        unlink(tmp);
        return -1;
    }
    return 0;
}

int dict_add(const char *path)
{
    int res;

    dict_gen++;
    if (ndicts == DICT_MAX) {
        say("too many dictionaries", 0);
        return -1;
    }
    res = load_into(&dicts[ndicts], path);
    if (res < 0)
        return -1;
    mods_stale = 1;
    if (raw_on) {
        /* in raw mode: on when it ends */
        raw_saved[ndicts] = 1;
        dicts[ndicts].enabled = dicts[ndicts].raw;
    }
    ndicts++;
    return 0;
}

/* Read again the dictionaries whose file changed (a word added with
 * steno -a, or by hand). A file that can't be read keeps the old one. */
void dict_reload(void)
{
    int res;
    static struct dict tmp;
    long mtime, size;
    int i, enabled;

    for (i = 0; i < ndicts; i++) {
        if (dicts[i].builtin)
            continue;
        file_stamp(dicts[i].path, &mtime, &size);
        if (mtime == dicts[i].mtime && size == dicts[i].size)
            continue;
        res = load_into(&tmp, dicts[i].path);
        if (res < 0) {
            dicts[i].mtime = mtime;
            dicts[i].size = size;
            continue;
        }
        enabled = dicts[i].enabled;
        dict_free(&dicts[i]);
        dicts[i] = tmp;
        dicts[i].enabled = enabled;
        mods_stale = 1;
        dict_gen++;
        if (verbose)
            say("read again: %s", dicts[i].path);
    }
}

const char *dict_path(int i)
{
    return i >= 0 && i < ndicts ? dicts[i].path : 0;
}

static int is_deleted(const char *v)
{
    const char *d = "{plover:deleted}";
    int i;

    for (i = 0; d[i]; i++)
        if (low_ascii((unsigned char)v[i]) != d[i])
            return 0;
    return v[i] == 0;
}

/* The translation of an outline in the plain (.json) dictionaries,
 * the way a lookup finds it, without touching the modal state. *at is
 * the dictionary it comes from. */
static const char *peek_at(const long *st, int n, int *at)
{
    int i;
    const char *v;

    for (i = 0; i < ndicts; i++) {
        if (!dicts[i].enabled || dicts[i].kind != dk_json)
            continue;
        v = get(&dicts[i].t, st, n);
        if (v) {
            *at = i;
            return is_deleted(v) ? 0 : v;
        }
    }
    return 0;
}

const char *dict_peek(const long *st, int n)
{
    int at;

    return peek_at(st, n, &at);
}

static int same_text(const char *a, const char *b, int fold)
{
    if (!fold)
        return !strcmp(a, b);
    while (*a && (*a == *b || ((*a | 32) == (*b | 32) &&
        (*a | 32) >= 'a' && (*a | 32) <= 'z')))
    {
        a++;
        b++;
    }
    return !*a && !*b;
}

#define FOUND_MAX 64

/* keys in the strokes of k (count first) */
static int outline_keys(const long *k)
{
    int i, n;
    long st;

    n = 0;
    for (i = 1; i <= k[0]; i++)
        for (st = k[i]; st; st &= st - 1)
            n++;
    return n;
}

/* which outline is shorter: fewer strokes, then fewer keys (count
 * first in both) */
static int outline_cmp(const long *a, const long *b)
{
    if (a[0] != b[0])
        return a[0] < b[0] ? -1 : 1;
    return outline_keys(a) - outline_keys(b);
}

/* the same for outlines written as text, KAT/-S; a bad one is last */
int outline_text_cmp(const char *a, const char *b)
{
    long x[STROKE_MAX + 1], y[STROKE_MAX + 1];

    x[0] = outline_parse(a, x + 1, STROKE_MAX);
    y[0] = outline_parse(b, y + 1, STROKE_MAX);
    if (x[0] <= 0 || y[0] <= 0)
        return (x[0] <= 0) - (y[0] <= 0);
    return outline_cmp(x, y);
}

/* a hash of a text, the same for texts that differ only in case */
static unsigned long fold_hash(const char *s)
{
    unsigned long h;

    h = 2166136261UL;
    for (; *s; s++) {
        h ^= (unsigned long)(*s >= 'A' && *s <= 'Z' ? *s + 32 :
            (unsigned char)*s);
        h *= 16777619UL;
        h &= 0xffffffffUL;
    }
    return h;
}

/* The entries of t by text: chains in entry order (the order a scan
 * would find them in), made again only after the table changed. */
static void text_index(struct table *t)
{
    long e, n, h;

    if (t->nthead || !t->nent)
        return;
    n = 1024;
    while (n < t->nent)
        n *= 2;
    free(t->thead);
    free(t->tnext);
    t->thead = malloc(n * sizeof(long));
    t->tnext = malloc(t->nent * sizeof(long));
    if (!t->thead || !t->tnext) {
        say("out of memory", 0);
        die(1);
    }
    memset(t->thead, 0, n * sizeof(long));
    for (e = t->nent - 1; e >= 0; e--) {
        h = (long)(fold_hash(t->vals + t->ent_val[e]) & (n - 1));
        t->tnext[e] = t->thead[h];
        t->thead[h] = e + 1;
    }
    t->nthead = n;
}

/* The outlines that write text, shortest first, as "KAT  KA*T". Only
 * ones a lookup would really reach (no higher dictionary has them).
 * When none writes it exactly, ones that differ only in case. One
 * pass over the entries. */
static int outlines_found(const char *text, char *out, int size, int max)
{
    unsigned long th;
    long e, *k, fk[FOUND_MAX];
    int fx[FOUND_MAX], nf, exact, i, j, n, pass, at, m;
    char one[STROKE_MAX * 32], s[32];
    struct table *t;
    const char *v;

    out[0] = 0;
    nf = 0;
    exact = 0;
    th = fold_hash(text);
    for (i = 0; i < ndicts; i++) {
        if (!dicts[i].enabled || dicts[i].kind != dk_json)
            continue;
        t = &dicts[i].t;
        text_index(t);
        if (!t->nthead)
            continue;
        e = t->thead[th & (t->nthead - 1)] - 1;
        for (; e >= 0 && nf < FOUND_MAX; e = t->tnext[e] - 1) {
            v = t->vals + t->ent_val[e];
            if (!same_text(v, text, 1))
                continue;
            m = !strcmp(v, text);
            if (exact && !m)
                continue;
            k = t->keys + t->ent_key[e];
            v = peek_at(k + 1, (int)k[0], &at);
            if (!v || at != i || !same_text(v, text, 1))
                continue;
            if (m && !exact) {
                exact = 1;
                nf = 0;        /* drop the ones that only fold */
            }
            fk[nf] = (long)(k - t->keys);
            fx[nf] = i;
            nf++;
        }
    }
    /* shortest first: fewest strokes, then fewest keys; the same:
     * dictionary order (stable) */
    for (i = 1; i < nf; i++) {
        for (j = i; j > 0; j--) {
            if (outline_cmp(dicts[fx[j - 1]].t.keys + fk[j - 1],
                dicts[fx[j]].t.keys + fk[j]) <= 0)
            {
                break;
            }
            e = fk[j];
            fk[j] = fk[j - 1];
            fk[j - 1] = e;
            at = fx[j];
            fx[j] = fx[j - 1];
            fx[j - 1] = at;
        }
    }
    n = 0;
    for (i = 0; i < nf && n < max; i++) {
        k = dicts[fx[i]].t.keys + fk[i];
        one[0] = 0;
        for (pass = 0; pass < k[0]; pass++) {
            stroke_text(k[1 + pass], s);
            if (pass)
                scat(one, "/", sizeof(one));
            scat(one, s, sizeof(one));
        }
        if (n)
            scat(out, "  ", size);
        scat(out, one, size);
        n++;
    }
    return n;
}

/* The last texts asked for, with their answers: after each stroke the
 * suggestions ask again for the words that were there before. Kept
 * until a dictionary changes. */
#define OUTLINES_CACHE 16
#define OUTLINES_TEXT 128
#define OUTLINES_OUT 512

struct asked {
    long gen;
    int max, n;
    char text[OUTLINES_TEXT];
    char out[OUTLINES_OUT];
};
static struct asked asked[OUTLINES_CACHE];
static int next_asked;

#define FORMS 32
#define ONE_MAX 256

/* a symbol on its own, as ., ?, -- (no letter, digit or space) */
static int is_symbol(const char *s)
{
    int n;

    for (n = 0; s[n]; n++) {
        if ((s[n] >= 'a' && s[n] <= 'z') || (s[n] >= 'A' && s[n] <= 'Z') ||
            (s[n] >= '0' && s[n] <= '9') || s[n] == ' ' || s[n] == '\t' ||
            s[n] == '{' || s[n] == '}')
        {
            return 0;
        }
    }
    return n > 0 && n < 16;
}

/* The text as asked, and the forms the dictionaries may have it in:
 * with typographic quotes (don't: don\342\200\231t, as -a saves it) and a
 * symbol as the meta that writes it ({.} for ., {?}, {^-^}). Their
 * outlines together, shortest first, each once. */
static int outlines_any(const char *text, char *out, int size, int max)
{
    static char forms[FORMS][TEXT_MAX];
    static char got[TEXT_MAX * 2];
    static char all[FOUND_MAX][ONE_MAX];
    char *p, *q;
    int nforms, i, j, k, n, len;

    static const char *const wrap[] = {
        "{%s}", "{^%s^}", "{^%s}", "{%s^}", "{~|%s^}", "{^~|%s}",
        "{^~|%s^}"
    };
    int nbase;

    nforms = 0;
    scopy(forms[nforms++], text, TEXT_MAX);
    typographic(text, forms[nforms], TEXT_MAX);
    if (strcmp(forms[nforms], text))
        nforms++;
    /* a straight quote alone: the typographic ones */
    if (!strcmp(text, "\"") || !strcmp(text, "'")) {
        scopy(forms[nforms++], text[0] == '"' ? "\342\200\234" :
            "\342\200\231", TEXT_MAX);
        if (text[0] == '"')
            scopy(forms[nforms++], "\342\200\235", TEXT_MAX);
    }
    /* a symbol: also as the metas that write it */
    nbase = nforms;
    for (i = 0; i < nbase; i++) {
        if (!is_symbol(forms[i]) || strlen(forms[i]) > 16)
            continue;
        for (j = 0; j < (int)(sizeof(wrap) / sizeof(wrap[0])) &&
            nforms < FORMS; j++)
        {
            sprintf(forms[nforms++], wrap[j], forms[i]);
        }
    }
    n = 0;
    for (i = 0; i < nforms; i++) {
        if (!outlines_found(forms[i], got, sizeof(got), FOUND_MAX))
            continue;
        for (p = got; *p && n < FOUND_MAX; p = q) {
            while (*p == ' ')
                p++;
            for (q = p; *q && *q != ' '; q++)
                ;
            len = (int)(q - p);
            if (!len || len >= ONE_MAX)
                continue;
            memcpy(all[n], p, len);
            all[n][len] = 0;
            for (j = 0; j < n && strcmp(all[j], all[n]); j++)
                ;
            if (j == n)
                n++;
        }
    }
    /* shortest first again, now across the forms (stable) */
    for (i = 1; i < n; i++) {
        for (j = i; j > 0 && outline_text_cmp(all[j - 1], all[j]) > 0; j--) {
            char t[ONE_MAX];
            scopy(t, all[j], ONE_MAX);
            scopy(all[j], all[j - 1], ONE_MAX);
            scopy(all[j - 1], t, ONE_MAX);
        }
    }
    out[0] = 0;
    k = 0;
    for (i = 0; i < n && k < max; i++) {
        if (k)
            scat(out, "  ", size);
        scat(out, all[i], size);
        k++;
    }
    return k;
}

/* Every entry a lookup reaches (no higher dictionary has its outline),
 * for the suggestions' guesses: fn(text, strokes, n, arg) for each,
 * until it gives nonzero. */
void dict_each(int (*fn)(const char *, const long *, int, void *),
    void *arg)
{
    struct table *t;
    const char *v;
    long e, *k;
    int i, at;

    for (i = 0; i < ndicts; i++) {
        if (!dicts[i].enabled || dicts[i].kind != dk_json)
            continue;
        t = &dicts[i].t;
        for (e = 0; e < t->nent; e++) {
            k = t->keys + t->ent_key[e];
            v = t->vals + t->ent_val[e];
            if (is_deleted(v) || !peek_at(k + 1, (int)k[0], &at) || at != i)
                continue;
            if (fn(v, k + 1, (int)k[0], arg))
                return;
        }
    }
}

long dict_generation(void)
{
    return dict_gen;
}

int dict_outlines(const char *text, char *out, int size, int max)
{
    struct asked *a;
    int i, n;

    for (i = 0; i < OUTLINES_CACHE; i++) {
        a = &asked[i];
        if (a->gen == dict_gen + 1 && a->max == max &&
            !strcmp(a->text, text))
        {
            scopy(out, a->out, size);
            return a->n;
        }
    }
    n = outlines_any(text, out, size, max);
    if ((int)strlen(text) < OUTLINES_TEXT && (int)strlen(out) <
        OUTLINES_OUT && (int)strlen(out) < size - 1)
    {
        a = &asked[next_asked];
        next_asked = (next_asked + 1) % OUTLINES_CACHE;
        a->gen = dict_gen + 1;
        a->max = max;
        a->n = n;
        scopy(a->text, text, OUTLINES_TEXT);
        scopy(a->out, out, OUTLINES_OUT);
    }
    return n;
}

/* Every dictionary that has the outline, highest first: "file: text"
 * lines, for steno -l and -a. */
int dict_explain(const long *st, int n, char *out, int size)
{
    int i, c;
    const char *v, *f;
    char line[TEXT_MAX + 256];

    out[0] = 0;
    c = 0;
    for (i = 0; i < ndicts; i++) {
        if (dicts[i].kind != dk_json)
            continue;
        v = get(&dicts[i].t, st, n);
        if (!v)
            continue;
        f = strrchr(dicts[i].path, '/');
        f = f ? f + 1 : dicts[i].path;
        sprintf(line, "  %.*s  (%.200s)\n", TEXT_MAX, v, f);
        scat(out, line, size);
        c++;
    }
    return c;
}

int dict_longest(void)
{
    int i, n;

    n = 0;
    for (i = 0; i < ndicts; i++)
        if (dicts[i].enabled && dicts[i].longest > n)
            n = dicts[i].longest;
    return n;
}

const char *dict_lookup(const long *st, int n)
{
    int i;
    const char *v;
    struct dict *d;

    if (n > dict_longest())
        return 0;
    for (i = 0; i < ndicts; i++) {
        d = &dicts[i];
        if (!d->enabled || n > d->longest)
            continue;
        if (d->kind == dk_json)
            v = get(&d->t, st, n);
        else if (d->kind == dk_modal)
            v = modal_lookup(d->m, st, n);
        else {
            mods_refresh();
            v = mods_lookup(d->x, st, n);
        }
        if (v)
            return is_deleted(v) ? 0 : v;
    }
    return 0;
}

void dict_stroked(void)
{
    int i;

    for (i = 0; i < ndicts; i++)
        if (dicts[i].kind == dk_modal)
            modal_stroked(dicts[i].m, dicts[i].longest);
}

/* RA*U: every dictionary off but dotterel-raw, and back. */
int dict_raw_toggle(void)
{
    int i, have;

    dict_gen++;
    have = 0;
    for (i = 0; i < ndicts; i++)
        if (dicts[i].raw)
            have = 1;
    if (!have)
        return -1;
    if (raw_on) {
        for (i = 0; i < ndicts; i++)
            dicts[i].enabled = raw_saved[i];
        raw_on = 0;
        return 0;
    }
    for (i = 0; i < ndicts; i++) {
        raw_saved[i] = dicts[i].enabled;
        dicts[i].enabled = dicts[i].raw;
    }
    raw_on = 1;
    return 1;
}

int dict_count(void)
{
    return ndicts;
}

long dict_entries(void)
{
    long n;
    int i;

    n = 0;
    for (i = 0; i < ndicts; i++) {
        if (dicts[i].kind == dk_json)
            n += dicts[i].t.nent;
        else if (dicts[i].kind == dk_modal)
            n += dicts[i].m->entry.nent + dicts[i].m->dict.nent +
                dicts[i].m->exit.nent;
    }
    return n;
}

/* --- for the status screen (tui.c) ----------------------------------- */

/* what the settings file calls it, and whether it is the user one */
void dict_set_origin(int i, const char *name, int user)
{
    if (i < 0 || i >= ndicts)
        return;
    scopy(dicts[i].name, name, sizeof(dicts[i].name));
    dicts[i].user = user;
}

const char *dict_name(int i, int *user)
{
    *user = dicts[i].user;
    return dicts[i].name[0] ? dicts[i].name : dicts[i].path;
}

/* on or off as you set it; in raw mode, as it will be after it */
int dict_enabled(int i)
{
    return raw_on ? raw_saved[i] : dicts[i].enabled;
}

void dict_set_enabled(int i, int on)
{
    dict_gen++;
    if (i < 0 || i >= ndicts)
        return;
    if (raw_on)
        raw_saved[i] = on;     /* raw mode keeps it as it is till it ends */
    else
        dicts[i].enabled = on;
}

int dict_raw_on(void)
{
    return raw_on;
}

/* 1 while a .modal dictionary is in its mode (Dotterel's movement) */
int dict_modal_active(void)
{
    int i;

    for (i = 0; i < ndicts; i++)
        if (dicts[i].kind == dk_modal && dicts[i].enabled &&
            dicts[i].m->activated)
        {
            return 1;
        }
    return 0;
}

const char *dict_kind(int i)
{
    if (dicts[i].kind == dk_mods)
        return "built in";
    if (dicts[i].kind == dk_modal)
        return dicts[i].builtin ? "modal, built in" : "modal";
    if (dicts[i].raw)
        return dicts[i].builtin ? "raw mode, built in" : "raw mode";
    return "dict";
}

long dict_size(int i)
{
    if (dicts[i].kind == dk_json)
        return dicts[i].t.nent;
    if (dicts[i].kind == dk_modal)
        return dicts[i].m->entry.nent + dicts[i].m->dict.nent +
            dicts[i].m->exit.nent;
    mods_refresh();
    return dicts[i].x ? dicts[i].x->nkeys : 0;
}

/* one place up (dir -1) or down (1) in the priority: 0, or -1 when
 * it can't go further */
int dict_move(int i, int dir)
{
    struct dict t;
    int j, k;

    dict_gen++;
    j = i + dir;
    if (i < 0 || i >= ndicts || j < 0 || j >= ndicts)
        return -1;
    t = dicts[i];
    dicts[i] = dicts[j];
    dicts[j] = t;
    k = raw_saved[i];
    raw_saved[i] = raw_saved[j];
    raw_saved[j] = k;
    return 0;
}

void dict_remove(int i)
{
    dict_gen++;
    if (i < 0 || i >= ndicts)
        return;
    dict_free(&dicts[i]);
    mods_stale = 1;
    for (; i + 1 < ndicts; i++) {
        dicts[i] = dicts[i + 1];
        raw_saved[i] = raw_saved[i + 1];
    }
    ndicts--;
    memset(&dicts[ndicts], 0, sizeof(dicts[0]));
}

/* the dictionary in the list that NAME (no extension) is, or -1 */
static int dict_find(const char *name)
{
    const char *b, *dot;
    int i, n;

    n = strlen(name);
    for (i = 0; i < ndicts; i++) {
        b = strrchr(dicts[i].path, '/');
        b = b ? b + 1 : dicts[i].path;
        dot = strrchr(b, '.');
        if (dicts[i].builtin)
            dot = b + strlen(b);
        if (dot && dot - b == n && !strncmp(b, name, n))
            return i;
    }
    return -1;
}
