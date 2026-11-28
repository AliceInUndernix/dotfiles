#include <string.h>
#include "kspkan.h"

/* Regular expressions for CKAN find_regexp and filter_regexp.
   Supported: literals . [] [^] \s \d \w (and capitals) ( ) (?: ) | * + ?
   ^ $ and a leading (?i).  Matching is a search, as in .NET IsMatch.
   Run time is linear in the text, so hostile patterns cannot hang us. */

#define RX_MAXPAT 256
#define RX_MAXNODE 600
#define RX_MAXPROG 1400
#define RX_MAXCLASS 40

#define N_CHAR 0
#define N_ANY 1
#define N_CLASS 2
#define N_CAT 3
#define N_ALT 4
#define N_STAR 5
#define N_PLUS 6
#define N_QUEST 7
#define N_BOL 8
#define N_EOL 9
#define N_EMPTY 10

#define I_CHAR 0
#define I_ANY 1
#define I_CLASS 2
#define I_SPLIT 3
#define I_JMP 4
#define I_BOL 5
#define I_EOL 6
#define I_MATCH 7

struct rnode {
    int type;
    int arg;
    int left;
    int right;
};

struct rinst {
    int op;
    int x;
    int y;
};

struct rx {
    const char *p;
    int i;
    int err;
    int icase;
    struct rnode nodes[RX_MAXNODE];
    int nn;
    unsigned char cls[RX_MAXCLASS][32];
    int ncls;
    struct rinst prog[RX_MAXPROG];
    int pc;
};

static int rx_lower(int c)
{
    if (c >= 'A' && c <= 'Z') {
        return c + 32;
    }
    return c;
}

static int rx_node(struct rx *r, int type, int arg, int left, int rr)
{
    int n;

    if (r->nn >= RX_MAXNODE) {
        r->err = 1;
        return 0;
    }
    n = r->nn;
    r->nn++;
    r->nodes[n].type = type;
    r->nodes[n].arg = arg;
    r->nodes[n].left = left;
    r->nodes[n].right = rr;
    return n;
}

static void cls_set(unsigned char *c, int ch)
{
    c[ch >> 3] |= (unsigned char)(1 << (ch & 7));
}

static int cls_has(const unsigned char *c, int ch)
{
    return (c[ch >> 3] >> (ch & 7)) & 1;
}

static void cls_escape(unsigned char *c, int e)
{
    int ch, low, in;

    low = rx_lower(e);
    for (ch = 0; ch < 256; ch++) {
        in = 0;
        if (low == 's') {
            in = (ch == ' ' || (ch >= 9 && ch <= 13));
        } else if (low == 'd') {
            in = (ch >= '0' && ch <= '9');
        } else {
            in = (ch == '_' || (ch >= '0' && ch <= '9') ||
                (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'));
        }
        if (e != low) {
            in = !in;
        }
        if (in) {
            cls_set(c, ch);
        }
    }
}

static int rx_escaped_char(int e)
{
    if (e == 't') {
        return 9;
    }
    if (e == 'n') {
        return 10;
    }
    if (e == 'r') {
        return 13;
    }
    return e;
}

static int rx_is_class_escape(int e)
{
    return e == 's' || e == 'S' || e == 'd' || e == 'D' || e == 'w' ||
        e == 'W';
}

static int rx_alt(struct rx *r);

static int rx_class(struct rx *r)
{
    unsigned char *c;
    int neg, ch, hi, first, k, lo;

    if (r->ncls >= RX_MAXCLASS) {
        r->err = 1;
        return 0;
    }
    k = r->ncls;
    r->ncls++;
    c = r->cls[k];
    memset(c, 0, 32);
    neg = 0;
    if (r->p[r->i] == '^') {
        neg = 1;
        r->i++;
    }
    first = 1;
    while (r->p[r->i] != 0 && (r->p[r->i] != ']' || first)) {
        first = 0;
        ch = (unsigned char)r->p[r->i];
        r->i++;
        if (ch == '\\' && r->p[r->i] != 0) {
            ch = (unsigned char)r->p[r->i];
            r->i++;
            if (rx_is_class_escape(ch)) {
                cls_escape(c, ch);
                continue;
            }
            ch = rx_escaped_char(ch);
        }
        hi = ch;
        if (r->p[r->i] == '-' && r->p[r->i + 1] != ']' &&
            r->p[r->i + 1] != 0)
        {
            hi = (unsigned char)r->p[r->i + 1];
            r->i += 2;
            if (hi == '\\' && r->p[r->i] != 0) {
                hi = rx_escaped_char((unsigned char)r->p[r->i]);
                r->i++;
            }
        }
        for (lo = ch; lo <= hi; lo++) {
            cls_set(c, lo);
            if (r->icase) {
                cls_set(c, rx_lower(lo));
                if (lo >= 'a' && lo <= 'z') {
                    cls_set(c, lo - 32);
                }
            }
        }
    }
    if (r->p[r->i] != ']') {
        r->err = 1;
        return 0;
    }
    r->i++;
    if (neg) {
        for (ch = 0; ch < 32; ch++) {
            c[ch] = (unsigned char)~c[ch];
        }
    }
    return rx_node(r, N_CLASS, k, 0, 0);
}

static int rx_atom(struct rx *r)
{
    int ch, n, k;

    ch = (unsigned char)r->p[r->i];
    if (ch == '(') {
        r->i++;
        if (r->p[r->i] == '?' && r->p[r->i + 1] == ':') {
            r->i += 2;
        }
        n = rx_alt(r);
        if (r->p[r->i] != ')') {
            r->err = 1;
            return 0;
        }
        r->i++;
        return n;
    }
    r->i++;
    if (ch == '[') {
        return rx_class(r);
    }
    if (ch == '.') {
        return rx_node(r, N_ANY, 0, 0, 0);
    }
    if (ch == '^') {
        return rx_node(r, N_BOL, 0, 0, 0);
    }
    if (ch == '$') {
        return rx_node(r, N_EOL, 0, 0, 0);
    }
    if (ch == '\\') {
        ch = (unsigned char)r->p[r->i];
        if (ch == 0) {
            r->err = 1;
            return 0;
        }
        r->i++;
        if (rx_is_class_escape(ch)) {
            if (r->ncls >= RX_MAXCLASS) {
                r->err = 1;
                return 0;
            }
            k = r->ncls;
            r->ncls++;
            memset(r->cls[k], 0, 32);
            cls_escape(r->cls[k], ch);
            return rx_node(r, N_CLASS, k, 0, 0);
        }
        ch = rx_escaped_char(ch);
    }
    if (r->icase) {
        ch = rx_lower(ch);
    }
    return rx_node(r, N_CHAR, ch, 0, 0);
}

static int rx_rep(struct rx *r)
{
    int n, ch, type;

    n = rx_atom(r);
    for (;;) {
        ch = r->p[r->i];
        if (ch == '*') {
            type = N_STAR;
        } else if (ch == '+') {
            type = N_PLUS;
        } else if (ch == '?') {
            type = N_QUEST;
        } else {
            break;
        }
        r->i++;
        n = rx_node(r, type, 0, n, 0);
        if (r->p[r->i] == '?') {
            r->i++;
        }
        if (r->err) {
            break;
        }
    }
    return n;
}

static int rx_cat(struct rx *r)
{
    int n, m;

    n = rx_node(r, N_EMPTY, 0, 0, 0);
    while (!r->err && r->p[r->i] != 0 && r->p[r->i] != '|' &&
        r->p[r->i] != ')')
    {
        m = rx_rep(r);
        n = rx_node(r, N_CAT, 0, n, m);
    }
    return n;
}

static int rx_alt(struct rx *r)
{
    int n, m;

    n = rx_cat(r);
    while (!r->err && r->p[r->i] == '|') {
        r->i++;
        m = rx_cat(r);
        n = rx_node(r, N_ALT, 0, n, m);
    }
    return n;
}

static int rx_inst(struct rx *r, int op, int x, int y)
{
    int k;

    if (r->pc >= RX_MAXPROG) {
        r->err = 1;
        return 0;
    }
    k = r->pc;
    r->pc++;
    r->prog[k].op = op;
    r->prog[k].x = x;
    r->prog[k].y = y;
    return k;
}

static void rx_emit(struct rx *r, int n, int depth)
{
    struct rnode *nd;
    int s, j;

    if (r->err || depth > 500) {
        r->err = 1;
        return;
    }
    nd = &r->nodes[n];
    if (nd->type == N_CHAR) {
        rx_inst(r, I_CHAR, nd->arg, 0);
    } else if (nd->type == N_ANY) {
        rx_inst(r, I_ANY, 0, 0);
    } else if (nd->type == N_CLASS) {
        rx_inst(r, I_CLASS, nd->arg, 0);
    } else if (nd->type == N_BOL) {
        rx_inst(r, I_BOL, 0, 0);
    } else if (nd->type == N_EOL) {
        rx_inst(r, I_EOL, 0, 0);
    } else if (nd->type == N_CAT) {
        rx_emit(r, nd->left, depth + 1);
        rx_emit(r, nd->right, depth + 1);
    } else if (nd->type == N_ALT) {
        s = rx_inst(r, I_SPLIT, 0, 0);
        r->prog[s].x = r->pc;
        rx_emit(r, nd->left, depth + 1);
        j = rx_inst(r, I_JMP, 0, 0);
        r->prog[s].y = r->pc;
        rx_emit(r, nd->right, depth + 1);
        r->prog[j].x = r->pc;
    } else if (nd->type == N_STAR) {
        s = rx_inst(r, I_SPLIT, 0, 0);
        r->prog[s].x = r->pc;
        rx_emit(r, nd->left, depth + 1);
        rx_inst(r, I_JMP, s, 0);
        r->prog[s].y = r->pc;
    } else if (nd->type == N_PLUS) {
        s = r->pc;
        rx_emit(r, nd->left, depth + 1);
        j = rx_inst(r, I_SPLIT, s, 0);
        r->prog[j].y = r->pc;
    } else if (nd->type == N_QUEST) {
        s = rx_inst(r, I_SPLIT, 0, 0);
        r->prog[s].x = r->pc;
        rx_emit(r, nd->left, depth + 1);
        r->prog[s].y = r->pc;
    }
}

struct rxrun {
    int list[2][RX_MAXPROG];
    int cnt[2];
    int mark[RX_MAXPROG];
};

/* follow jumps and splits; returns 1 if the match state was reached */
static int rx_add(const struct rx *r, struct rxrun *u, int which, int pc,
    int gen, int atstart, int atend, int depth)
{
    const struct rinst *in;
    int hit;

    if (u->mark[pc] == gen || depth > RX_MAXPROG) {
        return 0;
    }
    u->mark[pc] = gen;
    in = &r->prog[pc];
    if (in->op == I_JMP) {
        return rx_add(r, u, which, in->x, gen, atstart, atend, depth + 1);
    }
    if (in->op == I_SPLIT) {
        hit = rx_add(r, u, which, in->x, gen, atstart, atend, depth + 1);
        if (rx_add(r, u, which, in->y, gen, atstart, atend, depth + 1)) {
            hit = 1;
        }
        return hit;
    }
    if (in->op == I_BOL) {
        if (atstart) {
            return rx_add(r, u, which, pc + 1, gen, atstart, atend,
                depth + 1);
        }
        return 0;
    }
    if (in->op == I_EOL) {
        if (atend) {
            return rx_add(r, u, which, pc + 1, gen, atstart, atend,
                depth + 1);
        }
        return 0;
    }
    if (in->op == I_MATCH) {
        return 1;
    }
    u->list[which][u->cnt[which]] = pc;
    u->cnt[which]++;
    return 0;
}

static int rx_run(const struct rx *r, const char *s)
{
    static struct rxrun u;
    int cur, nxt, i, c, pos, ok, len, k;

    len = (int)strlen(s);
    memset(u.mark, 0, sizeof(u.mark));
    cur = 0;
    u.cnt[0] = 0;
    for (pos = 0; pos <= len; pos++) {
        if (rx_add(r, &u, cur, 0, pos + 1, pos == 0, pos == len, 0)) {
            return 1;
        }
        if (pos == len) {
            break;
        }
        c = (unsigned char)s[pos];
        if (r->icase) {
            c = rx_lower(c);
        }
        nxt = 1 - cur;
        u.cnt[nxt] = 0;
        for (i = 0; i < u.cnt[cur]; i++) {
            k = u.list[cur][i];
            ok = 0;
            if (r->prog[k].op == I_CHAR) {
                ok = (r->prog[k].x == c);
            } else if (r->prog[k].op == I_ANY) {
                ok = 1;
            } else if (r->prog[k].op == I_CLASS) {
                ok = cls_has(r->cls[r->prog[k].x], (unsigned char)s[pos]);
            }
            if (ok) {
                if (rx_add(r, &u, nxt, k + 1, pos + 2, 0, pos + 1 == len,
                    0))
                {
                    return 1;
                }
            }
        }
        cur = nxt;
    }
    return 0;
}

int rx_match(const char *pat, const char *s)
{
    static struct rx r;
    int root;

    if (strlen(pat) > RX_MAXPAT) {
        return -1;
    }
    memset(&r, 0, sizeof(r));
    r.p = pat;
    if (strncmp(pat, "(?i)", 4) == 0) {
        r.icase = 1;
        r.i = 4;
    }
    root = rx_alt(&r);
    if (r.p[r.i] != 0) {
        return -1;
    }
    if (r.err) {
        return -1;
    }
    rx_emit(&r, root, 0);
    rx_inst(&r, I_MATCH, 0, 0);
    if (r.err) {
        return -1;
    }
    return rx_run(&r, s);
}
