#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kspkan.h"

#define QT_TEXT 0
#define QT_AUTHOR 1
#define QT_TAG 2
#define QT_LICENSE 3
#define QT_NEEDS 4
#define QT_DLMIN 5
#define QT_DLMAX 6
#define QT_AFTER 7
#define QT_BEFORE 8
#define QT_IS 9
#define QT_SIZEMAX 10

/* "1234", "10k", "2M" */
long parse_count(const char *s)
{
    long v;

    v = 0;
    while (is_digit(*s)) {
        v = v * 10 + (*s - '0');
        s++;
    }
    if (*s == 'k' || *s == 'K') {
        v *= 1000;
    } else if (*s == 'm' || *s == 'M') {
        v *= 1000000;
    }
    return v;
}

void fmt_size(long n, char *out)
{
    if (n <= 0) {
        strcpy(out, "?");
    } else if (n < 1024L * 1024L) {
        sprintf(out, "%ld KB", n / 1024 + 1);
    } else if (n < 1024L * 1024L * 1024L) {
        sprintf(out, "%ld.%ld MB", n / (1024L * 1024L),
            (n % (1024L * 1024L)) * 10 / (1024L * 1024L));
    } else {
        sprintf(out, "%ld.%ld GB", n / (1024L * 1024L * 1024L),
            (n % (1024L * 1024L * 1024L)) * 10 / (1024L * 1024L * 1024L));
    }
}

static int has_text(const char *hay, const char *needle)
{
    long n, i, k;
    char a, b;

    n = (long)strlen(needle);
    if (n == 0) {
        return 1;
    }
    for (i = 0; hay[i]; i++) {
        for (k = 0; k < n; k++) {
            a = hay[i + k];
            b = needle[k];
            if (a >= 'A' && a <= 'Z') {
                a = (char)(a + 32);
            }
            if (b >= 'A' && b <= 'Z') {
                b = (char)(b + 32);
            }
            if (a != b) {
                break;
            }
        }
        if (k == n) {
            return 1;
        }
        if (hay[i + k] == 0) {
            break;
        }
    }
    return 0;
}

static int starts(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* Words are ANDed.  A leading - negates a word.  Forms:
   author:X tag:X license:X needs:X dl>N dl<N after:DATE before:DATE
   size<N is:installed is:update sort:pop|name|new|small|big */
void query_parse(struct query *q, const char *text)
{
    char *p, *w, *e;
    int neg, kind;
    const char *val;

    q->n = 0;
    q->sort = -1;
    q->copy = xstrdup(text);
    p = q->copy;
    while (*p && q->n < Q_MAX) {
        while (*p == ' ') {
            p++;
        }
        if (*p == 0) {
            break;
        }
        w = p;
        while (*p && *p != ' ') {
            p++;
        }
        e = p;
        if (*p) {
            p++;
        }
        *e = 0;
        neg = 0;
        if (w[0] == '-' && w[1] != 0) {
            neg = 1;
            w++;
        }
        kind = QT_TEXT;
        val = w;
        if (starts(w, "sort:")) {
            val = w + 5;
            if (starts(val, "pop") || starts(val, "down")) {
                q->sort = SORT_POP;
            } else if (starts(val, "new") || starts(val, "date")) {
                q->sort = SORT_NEW;
            } else if (starts(val, "small")) {
                q->sort = SORT_SMALL;
            } else if (starts(val, "big") || starts(val, "large")) {
                q->sort = SORT_BIG;
            } else if (starts(val, "name")) {
                q->sort = SORT_NAME;
            }
            continue;
        } else if (starts(w, "author:")) {
            kind = QT_AUTHOR;
            val = w + 7;
        } else if (starts(w, "tag:")) {
            kind = QT_TAG;
            val = w + 4;
        } else if (starts(w, "license:")) {
            kind = QT_LICENSE;
            val = w + 8;
        } else if (starts(w, "needs:")) {
            kind = QT_NEEDS;
            val = w + 6;
        } else if (starts(w, "is:")) {
            kind = QT_IS;
            val = w + 3;
        } else if (starts(w, "after:")) {
            kind = QT_AFTER;
            val = w + 6;
        } else if (starts(w, "before:")) {
            kind = QT_BEFORE;
            val = w + 7;
        } else if (starts(w, "dl>")) {
            kind = QT_DLMIN;
            val = w + 3;
        } else if (starts(w, "dl<")) {
            kind = QT_DLMAX;
            val = w + 3;
        } else if (starts(w, "size<")) {
            kind = QT_SIZEMAX;
            val = w + 5;
        }
        if (val[0] == 0) {
            continue;
        }
        q->t[q->n].kind = kind;
        q->t[q->n].neg = neg;
        q->t[q->n].text = (char *)val;
        q->t[q->n].num = parse_count(val);
        q->n++;
    }
}

void query_free(struct query *q)
{
    free(q->copy);
    q->copy = NULL;
    q->n = 0;
}

/* sizes are given in megabytes: size<5 means under 5 MB */
static int term_hit(struct ctx *c, const struct qterm *t,
    const struct summary *s)
{
    struct inst *m;
    struct entry *e;
    int cnt;

    switch (t->kind) {
    case QT_AUTHOR:
        return has_text(s->author, t->text);
    case QT_TAG:
        return has_text(s->tags, t->text);
    case QT_LICENSE:
        return has_text(s->license, t->text);
    case QT_NEEDS:
        return has_text(s->deps, t->text);
    case QT_DLMIN:
        return s->downloads > t->num;
    case QT_DLMAX:
        return s->downloads < t->num;
    case QT_AFTER:
        return strcmp(s->date, t->text) >= 0 && s->date[0] != 0;
    case QT_BEFORE:
        return strcmp(s->date, t->text) < 0 && s->date[0] != 0;
    case QT_SIZEMAX:
        return s->size > 0 && s->size < t->num * 1024L * 1024L;
    case QT_IS:
        m = registry_find(&c->reg, s->id);
        if (starts(t->text, "inst")) {
            return m != NULL;
        }
        if (starts(t->text, "new") || starts(t->text, "avail")) {
            return m == NULL;
        }
        if (starts(t->text, "upd") || starts(t->text, "upg")) {
            e = index_find(&c->ix, s->id, &cnt);
            return m != NULL && e != NULL &&
                version_compare(e->version, m->version) > 0;
        }
        return 0;
    default:
        return has_text(s->id, t->text) || has_text(s->name, t->text) ||
            has_text(s->abstract, t->text) || has_text(s->tags, t->text);
    }
}

int query_match(struct ctx *c, const struct query *q, int sumidx)
{
    int i, hit;

    for (i = 0; i < q->n; i++) {
        hit = term_hit(c, &q->t[i], &c->ix.s[sumidx]);
        if (q->t[i].neg) {
            hit = !hit;
        }
        if (!hit) {
            return 0;
        }
    }
    return 1;
}
