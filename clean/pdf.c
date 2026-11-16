/* PDF: read every object in the file (also those packed in object
 * streams), keep only those the document still uses, and write a new
 * plain PDF from them. What goes: the document information (author,
 * program, title, dates), XMP, program data (PieceInfo), dates and
 * authors of comments, the file ID, and every earlier version of the
 * file that edits left inside it. Pages, fonts and pictures are copied
 * byte for byte. Encrypted and signed PDFs are left alone. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clean.h"

#define MAX_OBJECTS 8000000L
#define MAX_UNPACK (64L * 1024 * 1024)     /* one object stream */
#define MAX_UNPACK_ALL (256L * 1024 * 1024)
#define MAX_DEPTH 100

/* ------------------------------------------------------------ memory */

struct block {
    struct block *next;
    long used;
    long size;
};

static struct block *blocks;

static void *get_mem(long n)
{
    struct block *b;
    long size;
    void *p;

    n = (n + 15) & ~15L;
    if (!blocks || blocks->used + n > blocks->size) {
        size = n > 1048576 - 64 ? n + 64 : 1048576;
        b = malloc(size);
        if (!b) {
            fprintf(stderr, "clean: out of memory\n");
            exit(1);
        }
        b->next = blocks;
        b->used = 64;
        b->size = size;
        blocks = b;
    }
    p = (char *)blocks + blocks->used;
    blocks->used += n;
    return p;
}

static void free_mem(void)
{
    struct block *b;

    while (blocks) {
        b = blocks->next;
        free(blocks);
        blocks = b;
    }
}

/* ------------------------------------------------------------ tokens */

enum {
    t_end, t_num, t_str, t_hex, t_name, t_word,
    t_open_arr, t_close_arr, t_open_dict, t_close_dict, t_bad
};

struct lex {
    const unsigned char *p;
    long n;
    long pos;
};

struct tok {
    int type;
    long start;
    long len;
};

static int is_space(int c)
{
    return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32;
}

static int is_delim(int c)
{
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' ||
        c == ']' || c == '{' || c == '}' || c == '/' || c == '%';
}

static void skip_space(struct lex *x)
{
    for (;;) {
        while (x->pos < x->n && is_space(x->p[x->pos]))
            x->pos++;
        if (x->pos >= x->n || x->p[x->pos] != '%')
            return;
        while (x->pos < x->n && x->p[x->pos] != 10 && x->p[x->pos] != 13)
            x->pos++;
    }
}

static void next(struct lex *x, struct tok *t)
{
    const unsigned char *p;
    long i, depth;
    int c, num;

    skip_space(x);
    p = x->p;
    i = x->pos;
    t->start = i;
    t->type = t_end;
    if (i >= x->n) {
        t->len = 0;
        return;
    }
    c = p[i];
    if (c == '[' || c == ']') {
        t->type = c == '[' ? t_open_arr : t_close_arr;
        i++;
    } else if (c == '<' && i + 1 < x->n && p[i + 1] == '<') {
        t->type = t_open_dict;
        i += 2;
    } else if (c == '>' && i + 1 < x->n && p[i + 1] == '>') {
        t->type = t_close_dict;
        i += 2;
    } else if (c == '<') {
        t->type = t_hex;
        while (i < x->n && p[i] != '>')
            i++;
        if (i < x->n)
            i++;
    } else if (c == '(') {
        t->type = t_str;
        depth = 0;
        while (i < x->n) {
            if (p[i] == '\\') {
                i += 2;
                continue;
            }
            if (p[i] == '(')
                depth++;
            if (p[i] == ')')
                depth--;
            i++;
            if (depth == 0)
                break;
        }
        if (i > x->n)
            i = x->n;
    } else if (c == '/') {
        t->type = t_name;
        i++;
        while (i < x->n && !is_space(p[i]) && !is_delim(p[i]))
            i++;
    } else if (is_delim(c)) {
        t->type = t_bad;
        i++;
    } else {
        num = 1;
        while (i < x->n && !is_space(p[i]) && !is_delim(p[i])) {
            if (!((p[i] >= '0' && p[i] <= '9') || p[i] == '.' ||
                p[i] == '-' || p[i] == '+'))
            {
                num = 0;
            }
            i++;
        }
        t->type = num ? t_num : t_word;
    }
    t->len = i - t->start;
    x->pos = i;
}

static int tok_is(struct lex *x, struct tok *t, const char *w)
{
    long n;

    n = strlen(w);
    return t->len == n && !memcmp(x->p + t->start, w, n);
}

/* tok_int: the token as a whole number >= 0, or -1 */
static long tok_int(const unsigned char *p, long len)
{
    long v, i;

    if (len < 1 || len > 15)
        return -1;
    v = 0;
    for (i = 0; i < len; i++) {
        if (p[i] < '0' || p[i] > '9')
            return -1;
        v = v * 10 + (p[i] - '0');
    }
    return v;
}

/* ------------------------------------------------------------ values */

enum { v_atom, v_ref, v_array, v_dict };

struct val {
    int type;
    int atom;                   /* the token type of an atom */
    const unsigned char *raw;   /* an atom's text */
    long len;
    long ref;                   /* v_ref: the object number */
    struct val **kids;          /* array items; dict: key, value, ... */
    long nk;
    long ck;
};

static void add_kid(struct val *v, struct val *k)
{
    struct val **n;

    if (v->nk == v->ck) {
        v->ck = v->ck ? v->ck * 2 : 8;
        n = get_mem(v->ck * (long)sizeof *n);
        if (v->nk)
            memcpy(n, v->kids, v->nk * sizeof *n);
        v->kids = n;
    }
    v->kids[v->nk] = k;
    v->nk++;
}

static int is_name(struct val *v, const char *name)
{
    long n;

    n = strlen(name);
    return v && v->type == v_atom && v->len == n &&
        !memcmp(v->raw, name, n);
}

static struct val *dict_get(struct val *d, const char *key)
{
    long i;

    if (!d || d->type != v_dict)
        return 0;
    for (i = 0; i + 1 < d->nk; i += 2) {
        if (is_name(d->kids[i], key))
            return d->kids[i + 1];
    }
    return 0;
}

static long val_int(struct val *v)
{
    if (!v || v->type != v_atom || v->atom != t_num)
        return -1;
    return tok_int(v->raw, v->len);
}

static struct val *parse(struct lex *x, int depth);

static struct val *new_val(int type)
{
    struct val *v;

    v = get_mem(sizeof *v);
    memset(v, 0, sizeof *v);
    v->type = type;
    return v;
}

static struct val *parse(struct lex *x, int depth)
{
    struct tok t, t2, t3;
    struct val *v, *k;
    long save;

    save = x->pos;
    next(x, &t);
    if (depth > MAX_DEPTH)
        return 0;
    switch (t.type) {
    case t_end: case t_close_arr: case t_close_dict: case t_bad:
        x->pos = save;
        return 0;
    case t_open_arr:
        v = new_val(v_array);
        for (;;) {
            k = parse(x, depth + 1);
            if (!k)
                break;
            add_kid(v, k);
        }
        next(x, &t);
        if (t.type != t_close_arr)
            x->pos = t.start;
        return v;
    case t_open_dict:
        v = new_val(v_dict);
        for (;;) {
            save = x->pos;
            next(x, &t);
            if (t.type != t_name) {
                if (t.type != t_close_dict)
                    x->pos = save;
                break;
            }
            k = new_val(v_atom);
            k->atom = t_name;
            k->raw = x->p + t.start;
            k->len = t.len;
            add_kid(v, k);
            k = parse(x, depth + 1);
            if (!k) {
                k = new_val(v_atom);
                k->atom = t_word;
                k->raw = (const unsigned char *)"null";
                k->len = 4;
            }
            add_kid(v, k);
        }
        return v;
    case t_word:
        if (tok_is(x, &t, "endobj") || tok_is(x, &t, "stream") ||
            tok_is(x, &t, "endstream") || tok_is(x, &t, "obj") ||
            tok_is(x, &t, "R"))
        {
            x->pos = save;
            return 0;
        }
        break;
    case t_num:
        if (tok_int(x->p + t.start, t.len) < 0)
            break;
        save = x->pos;
        next(x, &t2);
        if (t2.type == t_num && tok_int(x->p + t2.start, t2.len) >= 0) {
            next(x, &t3);
            if (tok_is(x, &t3, "R")) {
                v = new_val(v_ref);
                v->ref = tok_int(x->p + t.start, t.len);
                return v;
            }
        }
        x->pos = save;
        break;
    }
    v = new_val(v_atom);
    v->atom = t.type;
    v->raw = x->p + t.start;
    v->len = t.len;
    return v;
}

/* ------------------------------------------------------------ objects */

struct obj {
    struct val *v;
    const unsigned char *data;  /* stream data, or 0 */
    long dlen;
    long where;                 /* where it was defined: newest wins */
    int defs;
    int reach;
    long newnum;
};

static struct obj *objs;
static long nobjs;
static struct val *trailer;
static long trailer_where;
static long unpacked;

static int set_obj(long num, struct val *v, long where)
{
    struct obj *o;
    long n;

    if (num < 0 || num >= MAX_OBJECTS)
        return -1;
    if (num >= nobjs) {
        n = nobjs ? nobjs : 1024;
        while (n <= num)
            n *= 2;
        o = get_mem(n * (long)sizeof *o);
        memset(o, 0, n * sizeof *o);
        if (nobjs)
            memcpy(o, objs, nobjs * sizeof *o);
        objs = o;
        nobjs = n;
    }
    o = &objs[num];
    o->defs++;
    if (o->v && o->where > where)
        return 0;
    o->v = v;
    o->data = 0;
    o->dlen = 0;
    o->where = where;
    return 1;
}

static long find_word(const unsigned char *p, long n, long from,
    const char *w)
{
    long wl, i;

    wl = strlen(w);
    for (i = from; i + wl <= n; i++) {
        if (p[i] == w[0] && !memcmp(p + i, w, wl))
            return i;
    }
    return -1;
}

/* stream: the data after "stream", up to its end */
static void read_stream(struct lex *x, struct obj *o)
{
    const unsigned char *p;
    long start, len, e;

    p = x->p;
    start = x->pos;
    if (start < x->n && p[start] == 13)
        start++;
    if (start < x->n && p[start] == 10)
        start++;
    len = val_int(dict_get(o->v, "/Length"));
    if (len >= 0 && start + len <= x->n) {
        e = start + len;
        while (e < x->n && is_space(p[e]))
            e++;
        if (e + 9 <= x->n && !memcmp(p + e, "endstream", 9)) {
            o->data = p + start;
            o->dlen = len;
            x->pos = e + 9;
            return;
        }
    }
    e = find_word(p, x->n, start, "endstream");
    if (e < 0)
        e = x->n;
    x->pos = e + 9 > x->n ? x->n : e + 9;
    if (e > start && p[e - 1] == 10)
        e--;
    if (e > start && p[e - 1] == 13)
        e--;
    o->data = p + start;
    o->dlen = e - start;
}

static void maybe_trailer(struct val *d, long where)
{
    if (dict_get(d, "/Root") && where >= trailer_where) {
        trailer = d;
        trailer_where = where;
    }
}

/* scan: every "N G obj" and "trailer" in the file */
static void scan(const unsigned char *p, long n)
{
    struct lex x;
    struct tok t, t2, t3;
    struct val *v;
    long num, save, where;
    int r;

    x.p = p;
    x.n = n;
    x.pos = 0;
    for (;;) {
        next(&x, &t);
        if (t.type == t_end)
            break;
        if (tok_is(&x, &t, "trailer")) {
            v = parse(&x, 0);
            if (v && v->type == v_dict)
                maybe_trailer(v, t.start);
            continue;
        }
        num = t.type == t_num ? tok_int(p + t.start, t.len) : -1;
        if (num < 0)
            continue;
        save = x.pos;
        next(&x, &t2);
        if (t2.type != t_num || tok_int(p + t2.start, t2.len) < 0) {
            x.pos = save;
            continue;
        }
        next(&x, &t3);
        if (!tok_is(&x, &t3, "obj")) {
            x.pos = save;
            continue;
        }
        where = t.start;
        v = parse(&x, 0);
        if (!v) {
            v = new_val(v_atom);
            v->atom = t_word;
            v->raw = (const unsigned char *)"null";
            v->len = 4;
        }
        r = set_obj(num, v, where);
        save = x.pos;
        next(&x, &t2);
        if (tok_is(&x, &t2, "stream")) {
            if (r == 1) {
                read_stream(&x, &objs[num]);
            } else {
                struct obj tmp;

                memset(&tmp, 0, sizeof tmp);
                tmp.v = v;
                read_stream(&x, &tmp);
            }
            save = x.pos;
            next(&x, &t2);
        }
        if (!tok_is(&x, &t2, "endobj"))
            x.pos = save;
        if (r == 1 && is_name(dict_get(v, "/Type"), "/XRef"))
            maybe_trailer(v, where);
    }
}

/* lengths: a stream whose /Length is "N 0 R" gets that length */
static void lengths(const unsigned char *end)
{
    struct val *l;
    long i, len;
    struct obj *o;

    for (i = 0; i < nobjs; i++) {
        o = &objs[i];
        if (!o->data)
            continue;
        l = dict_get(o->v, "/Length");
        if (!l || l->type != v_ref || l->ref < 0 || l->ref >= nobjs)
            continue;
        len = val_int(objs[l->ref].v);
        if (len >= 0 && o->data + len <= end)
            o->dlen = len;
    }
}

/* unpack: the objects inside object streams */
static int unpack(void)
{
    struct buf b;
    struct lex x;
    struct tok t, t2;
    struct obj *o;
    struct val *v, *f;
    long i, k, count, first, num, off, save, nnow, ow;

    nnow = nobjs;
    for (i = 0; i < nnow; i++) {
        o = &objs[i];
        if (!o->data || !is_name(dict_get(o->v, "/Type"), "/ObjStm"))
            continue;
        f = dict_get(o->v, "/Filter");
        if (f && f->type == v_array && f->nk == 1)
            f = f->kids[0];
        if (!f || !is_name(f, "/FlateDecode") ||
            dict_get(o->v, "/DecodeParms"))
        {
            why = "a PDF packed in a way clean can't unpack";
            return -1;
        }
        memset(&b, 0, sizeof b);
        if (inflate_zlib(o->data, o->dlen, &b, MAX_UNPACK) < 0 ||
            unpacked + b.n > MAX_UNPACK_ALL)
        {
            buf_free(&b);
            why = "a PDF with a broken or too big object stream";
            return -1;
        }
        unpacked += b.n;
        /* keep the unpacked bytes as long as the values point there */
        x.p = get_mem(b.n + 1);
        memcpy((unsigned char *)x.p, b.p, b.n);
        x.n = b.n;
        buf_free(&b);
        count = val_int(dict_get(o->v, "/N"));
        first = val_int(dict_get(o->v, "/First"));
        ow = o->where;  /* set_obj may move objs: o is not used below */
        if (count < 0 || first < 0 || first > x.n)
            continue;
        x.pos = 0;
        for (k = 0; k < count; k++) {
            next(&x, &t);
            next(&x, &t2);
            num = tok_int(x.p + t.start, t.len);
            off = tok_int(x.p + t2.start, t2.len);
            if (num < 0 || off < 0 || first + off >= x.n)
                break;
            save = x.pos;
            x.pos = first + off;
            v = parse(&x, 0);
            x.pos = save;
            if (v && set_obj(num, v, ow) < 0)
                break;
        }
    }
    return 0;
}

/* ------------------------------------------------------------ what goes */

static const char *markup[] = {
    "/Text", "/FreeText", "/Line", "/Square", "/Circle", "/Polygon",
    "/PolyLine", "/Highlight", "/Underline", "/Squiggly", "/StrikeOut",
    "/Stamp", "/Caret", "/Ink", "/FileAttachment", "/Sound", "/Redact", 0
};

static int signed_pdf;

/* drop: 1 when key i of dict d goes; notes it when tell is set */
static int drop(struct val *d, long i, int tell)
{
    struct val *k, *v, *s;
    int j;

    k = d->kids[i];
    v = d->kids[i + 1];
    if (is_name(k, "/ByteRange"))
        signed_pdf = 1;
    if (is_name(k, "/Metadata")) {
        if (tell)
            note("XMP");
        return 1;
    }
    if (is_name(k, "/PieceInfo")) {
        if (tell)
            note("program data");
        return 1;
    }
    if (is_name(k, "/LastModified") ||
        ((is_name(k, "/CreationDate") || is_name(k, "/ModDate") ||
        is_name(k, "/M")) && v->type == v_atom &&
        (v->atom == t_str || v->atom == t_hex)))
    {
        if (tell)
            note("dates");
        return 1;
    }
    if (is_name(k, "/T") && dict_get(d, "/Rect") && !dict_get(d, "/FT")) {
        s = dict_get(d, "/Subtype");
        for (j = 0; markup[j]; j++) {
            if (is_name(s, markup[j])) {
                if (tell)
                    note("comment authors");
                return 1;
            }
        }
    }
    return 0;
}

/* walk: marks the objects v points to, and notes what goes; a stream's
 * /Length is written as a number, so its object isn't needed */
static void walk(struct val *v, long *stack, long *top, int stream)
{
    long i;

    if (!v)
        return;
    if (v->type == v_ref) {
        if (v->ref >= 0 && v->ref < nobjs && objs[v->ref].v &&
            !objs[v->ref].reach)
        {
            objs[v->ref].reach = 1;
            stack[*top] = v->ref;
            (*top)++;
        }
        return;
    }
    if (v->type == v_array) {
        for (i = 0; i < v->nk; i++)
            walk(v->kids[i], stack, top, 0);
    } else if (v->type == v_dict) {
        for (i = 0; i + 1 < v->nk; i += 2) {
            if (stream && is_name(v->kids[i], "/Length"))
                continue;
            if (!drop(v, i, 1))
                walk(v->kids[i + 1], stack, top, 0);
        }
    }
}

static void info_note(void)
{
    struct val *i;
    char d[104], w[24];
    long k, j, s;

    i = dict_get(trailer, "/Info");
    if (!i)
        return;
    if (i->type == v_ref)
        i = i->ref >= 0 && i->ref < nobjs ? objs[i->ref].v : 0;
    d[0] = 0;
    if (i && i->type == v_dict) {
        for (k = 0; k + 1 < i->nk; k += 2) {
            s = 0;
            for (j = 1; j < i->kids[k]->len && s < 20; j++) {
                if (i->kids[k]->raw[j] > 32 && i->kids[k]->raw[j] < 127) {
                    w[s] = (char)i->kids[k]->raw[j];
                    s++;
                }
            }
            w[s] = 0;
            if (strlen(d) + s + 3 < 100) {
                if (d[0])
                    strcat(d, ", ");
                strcat(d, w);
            }
        }
    }
    note2("document information", d);
}

/* ------------------------------------------------------------ writing */

static void put_val(struct buf *b, struct val *v, int is_stream_dict,
    long dlen)
{
    long i;
    int first;

    switch (v->type) {
    case v_atom:
        buf_put(b, v->raw, v->len);
        break;
    case v_ref:
        if (v->ref >= 0 && v->ref < nobjs && objs[v->ref].reach) {
            buf_num(b, objs[v->ref].newnum);
            buf_str(b, " 0 R");
        } else {
            buf_str(b, "null");
        }
        break;
    case v_array:
        buf_byte(b, '[');
        for (i = 0; i < v->nk; i++) {
            if (i)
                buf_byte(b, ' ');
            put_val(b, v->kids[i], 0, 0);
        }
        buf_byte(b, ']');
        break;
    case v_dict:
        buf_str(b, "<<");
        first = 1;
        for (i = 0; i + 1 < v->nk; i += 2) {
            if (drop(v, i, 0))
                continue;
            if (is_stream_dict && is_name(v->kids[i], "/Length"))
                continue;
            if (!first)
                buf_byte(b, ' ');
            first = 0;
            buf_put(b, v->kids[i]->raw, v->kids[i]->len);
            buf_byte(b, ' ');
            put_val(b, v->kids[i + 1], 0, 0);
        }
        if (is_stream_dict) {
            buf_str(b, first ? "/Length " : " /Length ");
            buf_num(b, dlen);
        }
        buf_str(b, ">>");
        break;
    }
}

int clean_pdf(const unsigned char *in, long n, struct buf *out)
{
    struct val *root;
    long *stack, top, i, k, count, unused, xref, hl;
    long *where;
    char d[64];
    int r;

    objs = 0;
    nobjs = 0;
    trailer = 0;
    trailer_where = 0;
    unpacked = 0;
    signed_pdf = 0;
    r = -1;
    scan(in, n);
    if (!trailer || dict_get(trailer, "/Encrypt")) {
        why = trailer ? "an encrypted PDF" : "a broken PDF (no catalog)";
        goto quit;
    }
    lengths(in + n);
    if (unpack() < 0)
        goto quit;
    lengths(in + n);
    root = dict_get(trailer, "/Root");
    if (!root || root->type != v_ref || root->ref >= nobjs ||
        !objs[root->ref].v)
    {
        why = "a broken PDF (no catalog)";
        goto quit;
    }
    /* what is still used, from the catalog on */
    stack = get_mem(nobjs * (long)sizeof *stack);
    top = 0;
    walk(root, stack, &top, 0);
    while (top > 0) {
        top--;
        k = stack[top];
        walk(objs[k].v, stack, &top, objs[k].data != 0);
    }
    if (signed_pdf) {
        why = "a signed PDF: cleaning breaks the signature (which also "
            "names the signer)";
        goto quit;
    }
    info_note();
    if (dict_get(trailer, "/ID"))
        note("file ID");
    count = 0;
    unused = 0;
    for (i = 0; i < nobjs; i++) {
        if (objs[i].reach) {
            count++;
            objs[i].newnum = count;
        } else if (objs[i].v && !is_name(dict_get(objs[i].v, "/Type"),
            "/ObjStm") && !is_name(dict_get(objs[i].v, "/Type"), "/XRef"))
        {
            unused++;
        }
        if (objs[i].defs > 1)
            unused += objs[i].defs - 1;
    }
    if (unused > 0) {
        sprintf(d, "%ld", unused);
        note2("earlier versions and unused parts", d);
    }
    /* the new file */
    for (hl = 0; hl < n && hl < 12 && in[hl] != 10 && in[hl] != 13; hl++)
        ;
    buf_put(out, in, hl);
    buf_str(out, "\n%\342\343\317\323\n");
    where = get_mem((count + 1) * (long)sizeof *where);
    for (i = 0; i < nobjs; i++) {
        if (!objs[i].reach)
            continue;
        k = objs[i].newnum;
        where[k] = out->n;
        buf_num(out, k);
        buf_str(out, " 0 obj\n");
        if (objs[i].data && objs[i].v->type == v_dict) {
            put_val(out, objs[i].v, 1, objs[i].dlen);
            buf_str(out, "\nstream\n");
            buf_put(out, objs[i].data, objs[i].dlen);
            buf_str(out, "\nendstream");
        } else {
            put_val(out, objs[i].v, 0, 0);
        }
        buf_str(out, "\nendobj\n");
    }
    xref = out->n;
    buf_str(out, "xref\n0 ");
    buf_num(out, count + 1);
    buf_str(out, "\n0000000000 65535 f \n");
    for (k = 1; k <= count; k++) {
        sprintf(d, "%010ld 00000 n \n", where[k]);
        buf_str(out, d);
    }
    buf_str(out, "trailer\n<</Size ");
    buf_num(out, count + 1);
    buf_str(out, " /Root ");
    buf_num(out, objs[root->ref].newnum);
    buf_str(out, " 0 R>>\nstartxref\n");
    buf_num(out, xref);
    buf_str(out, "\n%%EOF\n");
    r = 0;
quit:
    free_mem();
    objs = 0;
    nobjs = 0;
    return r;
}
