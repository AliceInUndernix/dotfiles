/* xml: edits XML text by rules, without building a tree: elements that
 * go (with what is in them), text that is replaced, attributes that go
 * or get a new value. Used for SVG and for the XML parts of office
 * documents and EPUB. Bytes are bytes: no encoding is assumed. */
#include <string.h>

#include "clean.h"

static int starts(const unsigned char *p, long n, const char *w)
{
    long k;

    k = strlen(w);
    return k <= n && !memcmp(p, w, k);
}

/* find: where w starts in p[i..n), or -1 */
long xml_find(const unsigned char *p, long n, long i, const char *w)
{
    long k;

    k = strlen(w);
    for (; i + k <= n; i++) {
        if (p[i] == (unsigned char)w[0] && !memcmp(p + i, w, k))
            return i;
    }
    return -1;
}

static int is_white(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* tag_end: the index after the '>' of the tag that starts at i */
static long tag_end(const unsigned char *p, long n, long i)
{
    int q;

    q = 0;
    for (; i < n; i++) {
        if (q) {
            if (p[i] == q)
                q = 0;
        } else if (p[i] == '"' || p[i] == '\'') {
            q = p[i];
        } else if (p[i] == '>') {
            return i + 1;
        }
    }
    return -1;
}

static long name_len(const unsigned char *p, long n, long i)
{
    long k;

    k = i;
    while (k < n && !is_white(p[k]) && p[k] != '>' && p[k] != '/' &&
        p[k] != '=')
    {
        k++;
    }
    return k - i;
}

/* name_is: a rule's name ending in '*' takes any name it starts */
static int name_is(const unsigned char *p, long len, const char *w)
{
    long k;

    k = strlen(w);
    if (k > 0 && w[k - 1] == '*')
        return len >= k - 1 && !memcmp(p, w, k - 1);
    return len == k && !memcmp(p, w, len);
}

static const struct xrule *rule_for(const struct xrule *r, int what,
    const unsigned char *p, long len)
{
    for (; r->what; r++) {
        if (r->what == what && name_is(p, len, r->name))
            return r;
    }
    return 0;
}

/* put_value: the new value of an attribute; x_target keeps only the
 * last part of a file: address (a path on someone's computer) */
static void put_value(const struct xrule *r, const unsigned char *v,
    long vn, int q, struct buf *out)
{
    long k, last;

    buf_byte(out, q);
    if (r->what == x_target) {
        last = 0;
        for (k = 0; k < vn; k++) {
            if (v[k] == '/' || v[k] == '\\')
                last = k + 1;
        }
        buf_put(out, v + last, vn - last);
    } else {
        buf_str(out, r->value);
    }
    buf_byte(out, q);
}

/* changes: what a rule would do to the value v */
static int changes(const struct xrule *r, const unsigned char *v, long vn)
{
    long k;

    if (r->what == x_attr)
        return 1;
    if (r->what == x_target) {
        if (!starts(v, vn, "file:"))
            return 0;
        for (k = 0; k < vn; k++) {
            if (v[k] == '/' || v[k] == '\\')
                return 1;
        }
        return 0;
    }
    return vn != (long)strlen(r->value) || memcmp(v, r->value, vn);
}

/* put_tag: the start tag p[i..e), with its attributes by the rules */
static void put_tag(const struct xrule *rules, const unsigned char *p,
    long i, long e, struct buf *out)
{
    const struct xrule *r;
    long k, a, len, v, vs, ve;
    int q;

    k = i + 1 + name_len(p, e, i + 1);
    buf_put(out, p + i, k - i);
    while (k < e) {
        a = k;
        while (k < e && is_white(p[k]))
            k++;
        len = name_len(p, e, k);
        if (len == 0) {
            buf_put(out, p + a, e - a);
            return;
        }
        v = k + len;
        vs = -1;
        ve = -1;
        q = '"';
        while (v < e && is_white(p[v]))
            v++;
        if (v < e && p[v] == '=') {
            v++;
            while (v < e && is_white(p[v]))
                v++;
            if (v < e && (p[v] == '"' || p[v] == '\'')) {
                q = p[v];
                vs = v + 1;
                v++;
                while (v < e && p[v] != q)
                    v++;
                ve = v;
                if (v < e)
                    v++;
            }
        }
        r = rule_for(rules, x_attr, p + k, len);
        if (!r)
            r = rule_for(rules, x_aval, p + k, len);
        if (!r)
            r = rule_for(rules, x_target, p + k, len);
        if (r && vs >= 0 && changes(r, p + vs, ve - vs)) {
            note(r->note);
            if (r->what != x_attr) {
                buf_put(out, p + a, vs - 1 - a);
                put_value(r, p + vs, ve - vs, q, out);
            }
        } else {
            buf_put(out, p + a, v - a);
        }
        k = v;
    }
}

/* trim_line: spaces before an element that goes go with it */
static void trim_line(struct buf *out)
{
    while (out->n > 0 && (out->p[out->n - 1] == ' ' ||
        out->p[out->n - 1] == '\t'))
    {
        out->n--;
    }
}

/* elem_end: where the element whose start tag is p[i..e) ends */
static long elem_end(const unsigned char *p, long n, long i, long e)
{
    char close[64];
    long len, c;

    if (p[e - 2] == '/')
        return e;
    len = name_len(p, n, i + 1);
    if (len > 60)
        return -1;
    close[0] = '<';
    close[1] = '/';
    memcpy(close + 2, p + i + 1, len);
    close[2 + len] = 0;
    c = xml_find(p, n, e, close);
    while (c >= 0 && !(c + 2 + len < n && (p[c + 2 + len] == '>' ||
        is_white(p[c + 2 + len]))))
    {
        c = xml_find(p, n, c + 1, close);
    }
    if (c < 0)
        return -1;
    return tag_end(p, n, c);
}

int xml_edit(const struct xrule *rules, const unsigned char *p, long n,
    struct buf *out)
{
    const struct xrule *r;
    long i, e, len, end, c;

    i = 0;
    while (i < n) {
        if (p[i] != '<') {
            buf_byte(out, p[i]);
            i++;
            continue;
        }
        if (starts(p + i, n - i, "<![CDATA[")) {
            e = xml_find(p, n, i, "]]>");
            e = e < 0 ? n : e + 3;
            buf_put(out, p + i, e - i);
            i = e;
            continue;
        }
        if (starts(p + i, n - i, "<!--")) {
            e = xml_find(p, n, i + 4, "-->");
            if (e < 0)
                return -1;
            e += 3;
            r = rule_for(rules, x_comments, (const unsigned char *)"", 0);
            if (r) {
                note(r->note);
                trim_line(out);
            } else {
                buf_put(out, p + i, e - i);
            }
            i = e;
            continue;
        }
        e = tag_end(p, n, i);
        if (e < 0)
            return -1;
        if (i + 1 >= n || p[i + 1] == '?' || p[i + 1] == '!' ||
            p[i + 1] == '/')
        {
            buf_put(out, p + i, e - i);
            i = e;
            continue;
        }
        len = name_len(p, n, i + 1);
        r = rule_for(rules, x_elem, p + i + 1, len);
        if (r) {
            end = elem_end(p, n, i, e);
            if (end < 0)
                return -1;
            if (!r->value || xml_find(p, end, i, r->value) >= 0) {
                note(r->note);
                trim_line(out);
                i = end;
                continue;
            }
        }
        put_tag(rules, p, i, e, out);
        r = rule_for(rules, x_text, p + i + 1, len);
        if (r && p[e - 2] != '/') {
            end = elem_end(p, n, i, e);
            if (end < 0)
                return -1;
            c = end - 1;
            while (c > e && p[c] != '<')
                c--;
            if (xml_find(p, c, e, "<") < 0 && changes(r, p + e, c - e)) {
                note(r->note);
                buf_str(out, r->value);
                buf_put(out, p + c, end - c);
                i = end;
                continue;
            }
        }
        i = e;
    }
    return 0;
}
