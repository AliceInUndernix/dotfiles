#include <stdlib.h>
#include <string.h>
#include "kspkan.h"

#define JSON_DEPTH 32

struct jp {
    const char *s;
    long n;
    long i;
    int err;
};

static struct jnode *jp_value(struct jp *p, int depth);

static void jp_ws(struct jp *p)
{
    while (p->i < p->n && is_space(p->s[p->i])) {
        p->i++;
    }
}

static struct jnode *jp_new(int type)
{
    struct jnode *n;

    n = (struct jnode *)xalloc((long)sizeof(*n));
    n->type = type;
    n->key = NULL;
    n->str = NULL;
    n->child = NULL;
    n->next = NULL;
    return n;
}

void json_free(struct jnode *n)
{
    struct jnode *next;

    while (n != NULL) {
        next = n->next;
        json_free(n->child);
        free(n->key);
        free(n->str);
        free(n);
        n = next;
    }
}

static int jp_hex(struct jp *p)
{
    int v, i, c;

    v = 0;
    for (i = 0; i < 4; i++) {
        if (p->i >= p->n) {
            p->err = 1;
            return 0;
        }
        c = (unsigned char)p->s[p->i];
        p->i++;
        v = v * 16;
        if (c >= '0' && c <= '9') {
            v += c - '0';
        } else if (c >= 'a' && c <= 'f') {
            v += c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            v += c - 'A' + 10;
        } else {
            p->err = 1;
            return 0;
        }
    }
    return v;
}

static void jp_utf8(struct buf *b, long c)
{
    char t[4];

    if (c < 0x80) {
        t[0] = (char)c;
        buf_add(b, t, 1);
    } else if (c < 0x800) {
        t[0] = (char)(0xc0 | (c >> 6));
        t[1] = (char)(0x80 | (c & 0x3f));
        buf_add(b, t, 2);
    } else if (c < 0x10000) {
        t[0] = (char)(0xe0 | (c >> 12));
        t[1] = (char)(0x80 | ((c >> 6) & 0x3f));
        t[2] = (char)(0x80 | (c & 0x3f));
        buf_add(b, t, 3);
    } else {
        t[0] = (char)(0xf0 | (c >> 18));
        t[1] = (char)(0x80 | ((c >> 12) & 0x3f));
        t[2] = (char)(0x80 | ((c >> 6) & 0x3f));
        t[3] = (char)(0x80 | (c & 0x3f));
        buf_add(b, t, 4);
    }
}

/* parses a string at p->i (after the opening quote); malloc'd result */
static char *jp_string(struct jp *p)
{
    struct buf b;
    char *r;
    long c, lo;
    char ch;

    b.p = NULL;
    b.n = 0;
    b.cap = 0;
    buf_add(&b, "", 0);
    for (;;) {
        if (p->i >= p->n) {
            p->err = 1;
            break;
        }
        ch = p->s[p->i];
        p->i++;
        if (ch == '"') {
            break;
        }
        if ((unsigned char)ch < 32) {
            p->err = 1;
            break;
        }
        if (ch != '\\') {
            buf_add(&b, &ch, 1);
            continue;
        }
        if (p->i >= p->n) {
            p->err = 1;
            break;
        }
        ch = p->s[p->i];
        p->i++;
        if (ch == 'n') {
            buf_add(&b, "\n", 1);
        } else if (ch == 't') {
            buf_add(&b, "\t", 1);
        } else if (ch == 'r') {
            buf_add(&b, "\r", 1);
        } else if (ch == 'b') {
            buf_add(&b, "\b", 1);
        } else if (ch == 'f') {
            buf_add(&b, "\f", 1);
        } else if (ch == '"' || ch == '\\' || ch == '/') {
            buf_add(&b, &ch, 1);
        } else if (ch == 'u') {
            c = jp_hex(p);
            if (c >= 0xd800 && c < 0xdc00 && p->i + 1 < p->n &&
                p->s[p->i] == '\\' && p->s[p->i + 1] == 'u')
            {
                p->i += 2;
                lo = jp_hex(p);
                c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00);
            }
            if (p->err) {
                break;
            }
            jp_utf8(&b, c);
        } else {
            p->err = 1;
            break;
        }
    }
    r = b.p;
    if (p->err) {
        free(b.p);
        return NULL;
    }
    return r;
}

static int jp_lit(struct jp *p, const char *w)
{
    long n;

    n = (long)strlen(w);
    if (p->i + n <= p->n && memcmp(p->s + p->i, w, (unsigned long)n) == 0) {
        p->i += n;
        return 1;
    }
    return 0;
}

static struct jnode *jp_container(struct jp *p, int depth, int isobj)
{
    struct jnode *c, *item, *last;
    char close;

    close = isobj ? '}' : ']';
    c = jp_new(isobj ? J_OBJ : J_ARR);
    last = NULL;
    jp_ws(p);
    if (p->i < p->n && p->s[p->i] == close) {
        p->i++;
        return c;
    }
    for (;;) {
        jp_ws(p);
        item = NULL;
        if (isobj) {
            char *key;

            if (p->i >= p->n || p->s[p->i] != '"') {
                p->err = 1;
                break;
            }
            p->i++;
            key = jp_string(p);
            if (key == NULL) {
                break;
            }
            jp_ws(p);
            if (p->i >= p->n || p->s[p->i] != ':') {
                free(key);
                p->err = 1;
                break;
            }
            p->i++;
            item = jp_value(p, depth + 1);
            if (item == NULL) {
                free(key);
                break;
            }
            item->key = key;
        } else {
            item = jp_value(p, depth + 1);
            if (item == NULL) {
                break;
            }
        }
        if (last == NULL) {
            c->child = item;
        } else {
            last->next = item;
        }
        last = item;
        jp_ws(p);
        if (p->i >= p->n) {
            p->err = 1;
            break;
        }
        if (p->s[p->i] == ',') {
            p->i++;
        } else if (p->s[p->i] == close) {
            p->i++;
            return c;
        } else {
            p->err = 1;
            break;
        }
    }
    p->err = 1;
    json_free(c);
    return NULL;
}

static struct jnode *jp_value(struct jp *p, int depth)
{
    struct jnode *n;
    long st;
    char c;

    if (depth > JSON_DEPTH) {
        p->err = 1;
        return NULL;
    }
    jp_ws(p);
    if (p->i >= p->n) {
        p->err = 1;
        return NULL;
    }
    c = p->s[p->i];
    if (c == '{' || c == '[') {
        p->i++;
        return jp_container(p, depth, c == '{');
    }
    if (c == '"') {
        p->i++;
        n = jp_new(J_STR);
        n->str = jp_string(p);
        if (n->str == NULL) {
            free(n);
            return NULL;
        }
        return n;
    }
    if (jp_lit(p, "true") || jp_lit(p, "false")) {
        n = jp_new(J_BOOL);
        n->str = xstrdup(p->s[p->i - 1] == 'e' && p->s[p->i - 4] == 't' ?
            "true" : "false");
        return n;
    }
    if (jp_lit(p, "null")) {
        return jp_new(J_NULL);
    }
    st = p->i;
    while (p->i < p->n && (is_digit(p->s[p->i]) || p->s[p->i] == '-' ||
        p->s[p->i] == '+' || p->s[p->i] == '.' || p->s[p->i] == 'e' ||
        p->s[p->i] == 'E'))
    {
        p->i++;
    }
    if (p->i == st) {
        p->err = 1;
        return NULL;
    }
    n = jp_new(J_NUM);
    n->str = xstrndup(p->s + st, p->i - st);
    return n;
}

struct jnode *json_parse(const char *text, long len)
{
    struct jp p;
    struct jnode *n;

    p.s = text;
    p.n = len;
    p.i = 0;
    p.err = 0;
    n = jp_value(&p, 0);
    if (n == NULL) {
        return NULL;
    }
    jp_ws(&p);
    if (p.i != p.n) {
        json_free(n);
        return NULL;
    }
    return n;
}

struct jnode *json_get(const struct jnode *obj, const char *key)
{
    struct jnode *c;

    if (obj == NULL || obj->type != J_OBJ) {
        return NULL;
    }
    for (c = obj->child; c != NULL; c = c->next) {
        if (c->key != NULL && strcmp(c->key, key) == 0) {
            return c;
        }
    }
    return NULL;
}

const char *json_str(const struct jnode *obj, const char *key)
{
    struct jnode *c;

    c = json_get(obj, key);
    if (c == NULL || c->type != J_STR) {
        return NULL;
    }
    return c->str;
}
