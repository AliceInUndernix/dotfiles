#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "kspkan.h"

#define MAXCKAN (4L << 20)

/* ---- sorting (merge sort on string pointers) ---- */

static void msort(char **a, char **t, int n, int (*cmp)(const char *,
    const char *))
{
    int mid, i, j, k;

    if (n < 2) {
        return;
    }
    mid = n / 2;
    msort(a, t, mid, cmp);
    msort(a + mid, t, n - mid, cmp);
    i = 0;
    j = mid;
    k = 0;
    while (i < mid && j < n) {
        if (cmp(a[i], a[j]) <= 0) {
            t[k] = a[i];
            i++;
        } else {
            t[k] = a[j];
            j++;
        }
        k++;
    }
    while (i < mid) {
        t[k] = a[i];
        i++;
        k++;
    }
    while (j < n) {
        t[k] = a[j];
        j++;
        k++;
    }
    for (i = 0; i < n; i++) {
        a[i] = t[i];
    }
}

void sort_strs(char **a, int n, int (*cmp)(const char *, const char *))
{
    char **t;

    t = (char **)xalloc((long)n * (long)sizeof(char *) + 8);
    msort(a, t, n, cmp);
    free(t);
}

/* compares "id<TAB>version..." lines: id ascending, version descending */
static int line_cmp(const char *a, const char *b)
{
    char va[128], vb[128];
    int i, r;

    i = 0;
    while (a[i] != 0 && a[i] != '\t' && a[i] == b[i]) {
        i++;
    }
    if (a[i] != b[i]) {
        if (a[i] == '\t' || a[i] == 0) {
            if (b[i] == '\t' || b[i] == 0) {
                return 0;
            }
            return -1;
        }
        if (b[i] == '\t' || b[i] == 0) {
            return 1;
        }
        return (unsigned char)a[i] < (unsigned char)b[i] ? -1 : 1;
    }
    a += i + 1;
    b += i + 1;
    for (i = 0; i < 127 && a[i] != 0 && a[i] != '\t'; i++) {
        va[i] = a[i];
    }
    va[i] = 0;
    for (i = 0; i < 127 && b[i] != 0 && b[i] != '\t'; i++) {
        vb[i] = b[i];
    }
    vb[i] = 0;
    r = version_compare(va, vb);
    return -r;
}

static int plain_cmp(const char *a, const char *b)
{
    return strcmp(a, b);
}

/* ---- helpers ---- */

static int split_tabs(char *line, char **f, int max)
{
    int n;

    n = 0;
    f[n] = line;
    n++;
    while (*line && n < max) {
        if (*line == '\t') {
            *line = 0;
            f[n] = line + 1;
            n++;
        }
        line++;
    }
    return n;
}

static void add_field(struct buf *b, const char *s, long max, int last)
{
    long i;
    char c;

    if (s != NULL) {
        for (i = 0; s[i] != 0 && i < max; i++) {
            c = s[i];
            if ((unsigned char)c < 32) {
                c = ' ';
            }
            buf_add(b, &c, 1);
        }
    }
    buf_add(b, last ? "\n" : "\t", 1);
}

static int bool_is_true(const struct jnode *n)
{
    return n != NULL && n->type == J_BOOL && strcmp(n->str, "true") == 0;
}

static int name_ok(const char *s)
{
    int i;

    if (s[0] == 0 || s[0] == '.') {
        return 0;
    }
    for (i = 0; s[i]; i++) {
        if (!((s[i] >= 'a' && s[i] <= 'z') ||
            (s[i] >= 'A' && s[i] <= 'Z') || is_digit(s[i]) ||
            s[i] == '.' || s[i] == '_' || s[i] == '-' || s[i] == '+' ||
            s[i] == '~'))
        {
            return 0;
        }
    }
    return 1;
}

static void join_provides(struct buf *b, struct jnode *arr)
{
    struct jnode *c;
    int first, i;
    char t;

    first = 1;
    if (arr == NULL || arr->type != J_ARR) {
        return;
    }
    for (c = arr->child; c != NULL; c = c->next) {
        if (c->type != J_STR) {
            continue;
        }
        if (!first) {
            buf_add(b, ",", 1);
        }
        first = 0;
        for (i = 0; c->str[i]; i++) {
            t = c->str[i];
            if (t == ',' || (unsigned char)t < 32) {
                t = ' ';
            }
            buf_add(b, &t, 1);
        }
    }
}

/* string, or array of strings, joined with sep */
static void join_strings(struct buf *b, struct jnode *n, const char *sep)
{
    struct jnode *c;
    int first;

    if (n == NULL) {
        return;
    }
    if (n->type == J_STR) {
        buf_adds(b, n->str);
        return;
    }
    first = 1;
    for (c = n->child; c != NULL && n->type == J_ARR; c = c->next) {
        if (c->type != J_STR) {
            continue;
        }
        if (!first) {
            buf_adds(b, sep);
        }
        first = 0;
        buf_adds(b, c->str);
    }
}

static void join_depnames(struct buf *b, struct jnode *arr)
{
    struct jnode *el, *alt;
    const char *nm;
    int first;

    if (arr == NULL || arr->type != J_ARR) {
        return;
    }
    first = 1;
    for (el = arr->child; el != NULL; el = el->next) {
        nm = json_str(el, "name");
        alt = json_get(el, "any_of");
        if (nm == NULL && alt != NULL && alt->child != NULL) {
            nm = json_str(alt->child, "name");
        }
        if (nm == NULL) {
            continue;
        }
        if (!first) {
            buf_adds(b, ",");
        }
        first = 0;
        buf_adds(b, nm);
    }
}

/* author, tags, license, date, install size, depends: tab separated,
   ending the line */
static void add_summary_extras(struct buf *line, struct jnode *j)
{
    struct buf t;
    struct jnode *n;
    char date[16];
    const char *d;

    t.p = NULL;
    t.n = 0;
    t.cap = 0;
    buf_add(&t, "", 0);
    join_strings(&t, json_get(j, "author"), ", ");
    add_field(line, t.p, 100, 0);
    buf_free(&t);
    t.p = NULL;
    t.n = 0;
    t.cap = 0;
    buf_add(&t, "", 0);
    join_strings(&t, json_get(j, "tags"), ",");
    add_field(line, t.p, 100, 0);
    buf_free(&t);
    t.p = NULL;
    t.n = 0;
    t.cap = 0;
    buf_add(&t, "", 0);
    join_strings(&t, json_get(j, "license"), ",");
    add_field(line, t.p, 60, 0);
    buf_free(&t);
    d = json_str(j, "release_date");
    date[0] = 0;
    if (d != NULL && strlen(d) >= 10) {
        memcpy(date, d, 10);
        date[10] = 0;
    }
    add_field(line, date, 10, 0);
    n = json_get(j, "install_size");
    add_field(line, n != NULL && n->type == J_NUM ? n->str : "0", 15, 0);
    t.p = NULL;
    t.n = 0;
    t.cap = 0;
    buf_add(&t, "", 0);
    join_depnames(&t, json_get(j, "depends"));
    add_field(line, t.p, 300, 1);
    buf_free(&t);
}

/* mode 0: drop repeated identical lines; mode 1: keep only the first
   line of each identifier (lines are sorted, newest version first) */
static int write_joined(struct ctx *c, const char *rel, char **v, int n,
    int mode)
{
    struct buf b;
    char *p;
    const char *x, *y;
    int i, k, rc, same;

    b.p = NULL;
    b.n = 0;
    b.cap = 0;
    buf_add(&b, "", 0);
    for (i = 0; i < n; i++) {
        same = 0;
        if (i > 0 && mode == 0) {
            same = (strcmp(v[i - 1], v[i]) == 0);
        } else if (i > 0) {
            x = v[i - 1];
            y = v[i];
            k = 0;
            while (x[k] != 0 && x[k] != '\t' && x[k] == y[k]) {
                k++;
            }
            same = (x[k] == '\t' && y[k] == '\t');
        }
        if (!same) {
            buf_adds(&b, v[i]);
        }
    }
    p = shared_path(c, rel);
    rc = write_file(p, b.p, b.n);
    free(p);
    buf_free(&b);
    return rc;
}

/* ---- update ---- */

int index_update(struct ctx *c, const unsigned char *tar, long len)
{
    struct archive a;
    struct strs lines, sums, provs, cnts;
    struct jnode *j, *pv, *ch;
    struct buf b, path;
    unsigned char *data;
    char *name, *slash, *meta, *idp;
    const char *id, *ver, *kind;
    long n, nlen;
    int i, count, rc;

    rc = tar_open(&a, tar, len);
    if (rc < 0) {
        return -1;
    }
    lines.v = sums.v = provs.v = NULL;
    lines.n = sums.n = provs.n = 0;
    lines.cap = sums.cap = provs.cap = 0;
    cnts.v = NULL;
    cnts.n = 0;
    cnts.cap = 0;
    meta = shared_path(c, "meta");
    make_dirs(meta);
    count = 0;
    for (i = 0; i < a.n; i++) {
        if (a.e[i].isdir) {
            continue;
        }
        name = strchr(a.e[i].name, '/');
        if (name == NULL) {
            continue;
        }
        name++;
        nlen = (long)strlen(name);
        if (strcmp(name, "download_counts.json") == 0) {
            data = archive_read(&a, i, MAXCKAN, &n);
            if (data != NULL) {
                j = json_parse((char *)data, n);
                if (j != NULL && j->type == J_OBJ) {
                    for (ch = j->child; ch != NULL; ch = ch->next) {
                        if (ch->type != J_NUM || ch->key == NULL) {
                            continue;
                        }
                        b.p = NULL;
                        b.n = 0;
                        b.cap = 0;
                        buf_add(&b, "", 0);
                        add_field(&b, ch->key, 200, 0);
                        add_field(&b, ch->str, 20, 1);
                        strs_add(&cnts, xstrdup(b.p));
                        buf_free(&b);
                    }
                }
                json_free(j);
                free(data);
            }
            continue;
        }
        if (nlen < 6 || strcmp(name + nlen - 5, ".ckan") != 0) {
            continue;
        }
        slash = strchr(name, '/');
        if (slash == NULL || strchr(slash + 1, '/') != NULL) {
            continue;
        }
        idp = xstrndup(name, (long)(slash - name));
        if (!name_ok(idp) || !name_ok(slash + 1)) {
            free(idp);
            continue;
        }
        data = archive_read(&a, i, MAXCKAN, &n);
        if (data == NULL) {
            free(idp);
            continue;
        }
        j = json_parse((char *)data, n);
        id = json_str(j, "identifier");
        ver = json_str(j, "version");
        if (j == NULL || id == NULL || ver == NULL || strcmp(id, idp) != 0) {
            json_free(j);
            free(data);
            free(idp);
            continue;
        }
        path.p = NULL;
        path.n = 0;
        path.cap = 0;
        buf_adds(&path, meta);
        buf_adds(&path, "/");
        buf_adds(&path, idp);
        make_dirs(path.p);
        buf_adds(&path, "/");
        buf_adds(&path, slash + 1);
        write_file(path.p, (char *)data, n);
        buf_free(&path);
        kind = json_str(j, "kind");
        b.p = NULL;
        b.n = 0;
        b.cap = 0;
        buf_add(&b, "", 0);
        add_field(&b, id, 200, 0);
        add_field(&b, ver, 100, 0);
        add_field(&b, slash + 1, 200, 0);
        add_field(&b, json_str(j, "ksp_version"), 40, 0);
        add_field(&b, json_str(j, "ksp_version_min"), 40, 0);
        add_field(&b, json_str(j, "ksp_version_max"), 40, 0);
        add_field(&b, kind != NULL ? kind : "package", 20, 0);
        add_field(&b, json_str(j, "release_status") != NULL ?
            json_str(j, "release_status") : "stable", 20, 0);
        pv = json_get(j, "provides");
        join_provides(&b, pv);
        buf_adds(&b, "\t");
        buf_adds(&b, bool_is_true(json_get(j, "ksp_version_strict")) ?
            "1" : "0");
        buf_adds(&b, "\n");
        strs_add(&lines, xstrdup(b.p));
        buf_free(&b);
        b.p = NULL;
        b.n = 0;
        b.cap = 0;
        buf_add(&b, "", 0);
        add_field(&b, id, 200, 0);
        add_field(&b, ver, 100, 0);
        add_field(&b, json_str(j, "name"), 100, 0);
        add_field(&b, json_str(j, "abstract"), 160, 0);
        add_summary_extras(&b, j);
        strs_add(&sums, xstrdup(b.p));
        buf_free(&b);
        if (pv != NULL && pv->type == J_ARR) {
            for (ch = pv->child; ch != NULL; ch = ch->next) {
                if (ch->type == J_STR) {
                    b.p = NULL;
                    b.n = 0;
                    b.cap = 0;
                    buf_add(&b, "", 0);
                    add_field(&b, ch->str, 200, 0);
                    add_field(&b, id, 200, 1);
                    strs_add(&provs, xstrdup(b.p));
                    buf_free(&b);
                }
            }
        }
        count++;
        json_free(j);
        free(data);
        free(idp);
    }
    free(meta);
    archive_close(&a);
    sort_strs(lines.v, lines.n, line_cmp);
    sort_strs(sums.v, sums.n, line_cmp);
    sort_strs(provs.v, provs.n, plain_cmp);
    sort_strs(cnts.v, cnts.n, plain_cmp);
    rc = write_joined(c, "index", lines.v, lines.n, 0);
    if (rc == 0) {
        rc = write_joined(c, "summary", sums.v, sums.n, 1);
    }
    if (rc == 0) {
        rc = write_joined(c, "provides", provs.v, provs.n, 0);
    }
    if (rc == 0) {
        rc = write_joined(c, "counts", cnts.v, cnts.n, 0);
    }
    strs_free(&lines);
    strs_free(&sums);
    strs_free(&provs);
    strs_free(&cnts);
    if (rc < 0) {
        return -1;
    }
    return count;
}

/* ---- load ---- */

/* compare an identifier with the part of a "id<TAB>count" line before
   the tab */
static int id_vs_line(const char *id, const char *line)
{
    int i;

    for (i = 0; id[i] != 0 && id[i] == line[i]; i++) {
        continue;
    }
    if (id[i] == 0 && line[i] == '\t') {
        return 0;
    }
    if (line[i] == '\t' || line[i] == 0) {
        return 1;
    }
    if (id[i] == 0) {
        return -1;
    }
    return (unsigned char)id[i] < (unsigned char)line[i] ? -1 : 1;
}

static void load_counts(struct ctx *c)
{
    char *p, *text, *line, *nl, **v;
    long len, n, val;
    int i, lo, hi, mid, r, nlines;

    p = shared_path(c, "counts");
    text = read_file(p, &len, 1L << 24);
    free(p);
    if (text == NULL) {
        return;
    }
    nlines = 0;
    for (i = 0; text[i]; i++) {
        if (text[i] == '\n') {
            nlines++;
        }
    }
    v = (char **)xalloc((long)(nlines + 1) * (long)sizeof(char *));
    line = text;
    for (i = 0; i < nlines; i++) {
        nl = strchr(line, '\n');
        *nl = 0;
        v[i] = line;
        line = nl + 1;
    }
    for (i = 0; i < c->ix.ns; i++) {
        lo = 0;
        hi = nlines;
        while (lo < hi) {
            mid = (lo + hi) / 2;
            r = id_vs_line(c->ix.s[i].id, v[mid]);
            if (r == 0) {
                p = strchr(v[mid], '\t') + 1;
                val = 0;
                for (n = 0; is_digit(p[n]) && n < 15; n++) {
                    val = val * 10 + (p[n] - '0');
                }
                c->ix.s[i].downloads = val;
                break;
            }
            if (r > 0) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
    }
    free(v);
    free(text);
}

const char *sort_names[] = {
    "name", "most downloaded", "newest", "smallest", "largest"
};

/* nonzero if a may stay in front of b */
static int sort_before(const struct summary *s, int a, int b, int mode)
{
    int r;

    if (mode == SORT_POP) {
        return s[a].downloads >= s[b].downloads;
    }
    if (mode == SORT_NEW) {
        r = strcmp(s[a].date, s[b].date);
        return r >= 0;
    }
    if (mode == SORT_SMALL || mode == SORT_BIG) {
        if (s[a].size == 0 || s[b].size == 0) {
            return s[b].size == 0;
        }
        if (mode == SORT_SMALL) {
            return s[a].size <= s[b].size;
        }
        return s[a].size >= s[b].size;
    }
    return a <= b;
}

static void msort_int(int *a, int *t, int n, const struct summary *s,
    int mode)
{
    int mid, i, j, k;

    if (n < 2) {
        return;
    }
    mid = n / 2;
    msort_int(a, t, mid, s, mode);
    msort_int(a + mid, t, n - mid, s, mode);
    i = 0;
    j = mid;
    k = 0;
    while (i < mid && j < n) {
        if (sort_before(s, a[i], a[j], mode)) {
            t[k] = a[i];
            i++;
        } else {
            t[k] = a[j];
            j++;
        }
        k++;
    }
    while (i < mid) {
        t[k] = a[i];
        i++;
        k++;
    }
    while (j < n) {
        t[k] = a[j];
        j++;
        k++;
    }
    for (i = 0; i < n; i++) {
        a[i] = t[i];
    }
}

/* summary indexes in the order of a sort mode (SORT_*) */
int *summary_order(struct ctx *c, int mode)
{
    int *a, *t;
    int i;

    a = (int *)xalloc((long)(c->ix.ns + 1) * (long)sizeof(int));
    for (i = 0; i < c->ix.ns; i++) {
        a[i] = i;
    }
    if (mode != SORT_NAME) {
        t = (int *)xalloc((long)(c->ix.ns + 1) * (long)sizeof(int));
        msort_int(a, t, c->ix.ns, c->ix.s, mode);
        free(t);
    }
    return a;
}

int index_load(struct ctx *c)
{
    struct stat sb;
    struct index *ix;
    char *p, *line, *nl, *f[10];
    long len;
    int i, k, nf;

    ix = &c->ix;
    memset(ix, 0, sizeof(*ix));
    p = shared_path(c, "index");
    ix->text = read_file(p, &len, 1L << 28);
    free(p);
    if (ix->text == NULL) {
        return -1;
    }
    ix->n = 0;
    for (i = 0; ix->text[i]; i++) {
        if (ix->text[i] == '\n') {
            ix->n++;
        }
    }
    ix->e = (struct entry *)xalloc((long)(ix->n + 1) *
        (long)sizeof(struct entry));
    line = ix->text;
    for (i = 0; i < ix->n; i++) {
        nl = strchr(line, '\n');
        *nl = 0;
        nf = split_tabs(line, f, 10);
        while (nf < 10) {
            f[nf] = nl;
            nf++;
        }
        ix->e[i].id = f[0];
        ix->e[i].version = f[1];
        ix->e[i].file = f[2];
        ix->e[i].kexact = f[3];
        ix->e[i].kmin = f[4];
        ix->e[i].kmax = f[5];
        ix->e[i].kind = f[6];
        ix->e[i].status = f[7];
        ix->e[i].provides = f[8];
        ix->e[i].strict = f[9];
        ix->e[i].j = NULL;
        line = nl + 1;
    }
    p = shared_path(c, "summary");
    ix->stext = read_file(p, &len, 1L << 27);
    free(p);
    if (ix->stext == NULL) {
        return -1;
    }
    ix->ns = 0;
    for (i = 0; ix->stext[i]; i++) {
        if (ix->stext[i] == '\n') {
            ix->ns++;
        }
    }
    ix->s = (struct summary *)xalloc((long)(ix->ns + 1) *
        (long)sizeof(struct summary));
    line = ix->stext;
    for (i = 0; i < ix->ns; i++) {
        nl = strchr(line, '\n');
        *nl = 0;
        nf = split_tabs(line, f, 10);
        while (nf < 10) {
            f[nf] = nl;
            nf++;
        }
        ix->s[i].id = f[0];
        ix->s[i].name = f[2];
        ix->s[i].abstract = f[3];
        ix->s[i].version = f[1];
        ix->s[i].downloads = 0;
        ix->s[i].author = f[4];
        ix->s[i].tags = f[5];
        ix->s[i].license = f[6];
        ix->s[i].date = f[7];
        ix->s[i].size = 0;
        for (k = 0; is_digit(f[8][k]) && k < 15; k++) {
            ix->s[i].size = ix->s[i].size * 10 + (f[8][k] - '0');
        }
        ix->s[i].deps = f[9];
        line = nl + 1;
    }
    load_counts(c);
    p = shared_path(c, "index");
    k = stat(p, &sb);
    if (k == 0) {
        c->listdays = ((long)time(NULL) - (long)sb.st_mtime) / 86400;
    }
    free(p);
    p = shared_path(c, "provides");
    ix->ptext = read_file(p, &len, 1L << 26);
    free(p);
    if (ix->ptext == NULL) {
        return -1;
    }
    ix->np = 0;
    for (i = 0; ix->ptext[i]; i++) {
        if (ix->ptext[i] == '\n') {
            ix->np++;
        }
    }
    ix->prov = (char **)xalloc((long)(ix->np + 1) * (long)sizeof(char *));
    line = ix->ptext;
    for (i = 0; i < ix->np; i++) {
        nl = strchr(line, '\n');
        *nl = 0;
        ix->prov[i] = line;
        line = nl + 1;
    }
    return 0;
}

/* first entry (newest version) for id, and how many versions exist */
struct entry *index_find(struct index *ix, const char *id, int *count)
{
    int lo, hi, mid, r, n;

    lo = 0;
    hi = ix->n;
    while (lo < hi) {
        mid = (lo + hi) / 2;
        r = strcmp(ix->e[mid].id, id);
        if (r < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    n = 0;
    while (lo + n < ix->n && strcmp(ix->e[lo + n].id, id) == 0) {
        n++;
    }
    if (count != NULL) {
        *count = n;
    }
    if (n == 0) {
        return NULL;
    }
    return &ix->e[lo];
}

struct summary *summary_find(struct index *ix, const char *id)
{
    int lo, hi, mid, r;

    lo = 0;
    hi = ix->ns;
    while (lo < hi) {
        mid = (lo + hi) / 2;
        r = strcmp(ix->s[mid].id, id);
        if (r == 0) {
            return &ix->s[mid];
        }
        if (r < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

/* ---- game version compatibility ---- */

static int game_compat(struct ctx *c, struct entry *e);

/* compare g with v over the components that v has */
static int comp_cmp(const char *g, const char *v)
{
    long a, b;

    while (*v) {
        if (!is_digit(*v)) {
            return 0;
        }
        a = 0;
        while (is_digit(*g)) {
            a = a * 10 + (*g - '0');
            g++;
        }
        b = 0;
        while (is_digit(*v)) {
            b = b * 10 + (*v - '0');
            v++;
        }
        if (a != b) {
            return a < b ? -1 : 1;
        }
        if (*g == '.') {
            g++;
        }
        if (*v == '.') {
            v++;
        }
    }
    return 0;
}

/* compare g with v over the components both have: "1.12" against
   "1.12.4" is equal, so "1.12" stands for every 1.12.x */
static int part_cmp(const char *g, const char *v)
{
    long a, b;

    while (*v && *g) {
        if (!is_digit(*v) || !is_digit(*g)) {
            return 0;
        }
        a = 0;
        while (is_digit(*g)) {
            a = a * 10 + (*g - '0');
            g++;
        }
        b = 0;
        while (is_digit(*v)) {
            b = b * 10 + (*v - '0');
            v++;
        }
        if (a != b) {
            return a < b ? -1 : 1;
        }
        if (*g == '.') {
            g++;
        }
        if (*v == '.') {
            v++;
        }
    }
    return 0;
}

/* does e fit one of the other game versions you allowed (kspkan
   compat)? */
static int extra_compat(struct ctx *c, struct entry *e)
{
    const char *x;
    int i;

    for (i = 0; i < c->compat.n; i++) {
        x = c->compat.v[i];
        if (e->kexact[0] != 0) {
            if (part_cmp(x, e->kexact) == 0) {
                return 1;
            }
            continue;
        }
        if (e->kmin[0] != 0 && part_cmp(x, e->kmin) < 0) {
            continue;
        }
        if (e->kmax[0] != 0 && part_cmp(x, e->kmax) > 0) {
            continue;
        }
        return 1;
    }
    return 0;
}

int entry_compat(struct ctx *c, struct entry *e)
{
    if (c->anyver || c->kspver == NULL) {
        return 1;
    }
    if (game_compat(c, e)) {
        return 1;
    }
    return c->compat.n > 0 && extra_compat(c, e);
}

/* does e fit the game's own version? */
static int game_compat(struct ctx *c, struct entry *e)
{
    if (e->kexact[0] != 0) {
        if (strcmp(e->kexact, "any") == 0) {
            return 1;
        }
        if (e->strict[0] == '1') {
            return version_compare(c->kspver, e->kexact) == 0;
        }
        return comp_cmp(c->kspver, e->kexact) == 0;
    }
    if (e->kmin[0] != 0 && comp_cmp(c->kspver, e->kmin) < 0) {
        return 0;
    }
    if (e->kmax[0] != 0 && comp_cmp(c->kspver, e->kmax) > 0) {
        return 0;
    }
    return 1;
}

struct jnode *entry_json(struct ctx *c, struct entry *e)
{
    char *rel, *p, *text;
    long len;

    if (e->j != NULL) {
        return e->j;
    }
    rel = (char *)xalloc((long)(strlen(e->id) + strlen(e->file)) + 8);
    strcpy(rel, "meta/");
    strcat(rel, e->id);
    strcat(rel, "/");
    strcat(rel, e->file);
    p = shared_path(c, rel);
    free(rel);
    text = read_file(p, &len, MAXCKAN);
    free(p);
    if (text == NULL) {
        return NULL;
    }
    e->j = json_parse(text, len);
    free(text);
    return e->j;
}

/* ---- finding the game version ---- */

static char *find_version(const char *text)
{
    const char *p;
    char tmp[32];
    int n, dots;

    for (p = text; *p; p++) {
        if ((p[0] == 'v' || p[0] == 'V') && strncmp(p + 1, "ersion", 6) == 0)
        {
            p += 7;
            while (*p == ' ' || *p == '=' || *p == ':' || *p == '\t') {
                p++;
            }
            if (!is_digit(*p)) {
                continue;
            }
            n = 0;
            dots = 0;
            while (n < 30 && (is_digit(*p) || *p == '.')) {
                if (*p == '.') {
                    dots++;
                    if (dots == 3) {
                        break;
                    }
                }
                tmp[n] = *p;
                n++;
                p++;
            }
            tmp[n] = 0;
            return xstrdup(tmp);
        }
    }
    return NULL;
}

/* the other game versions you allowed for this game, from "compat"
   in its state directory: one a line, as 1.12 or 1.11.2 */
void compat_load(struct ctx *c)
{
    char *p, *text, *line, *nl;

    strs_free(&c->compat);
    p = state_path(c, "compat");
    text = read_file(p, NULL, 4096);
    free(p);
    if (text == NULL) {
        return;
    }
    line = text;
    while (*line) {
        nl = strchr(line, '\n');
        if (nl != NULL) {
            *nl = 0;
        }
        if (is_digit(line[0]) && strlen(line) < 16) {
            strs_add(&c->compat, xstrdup(line));
        }
        if (nl == NULL) {
            break;
        }
        line = nl + 1;
    }
    free(text);
}

/* the game version written in a game folder, or NULL; malloc'd */
char *game_version(const char *dir)
{
    static const char *names[] = { "buildID.txt", "readme.txt", NULL };
    char *p, *text, *v;
    int i;

    for (i = 0; names[i] != NULL; i++) {
        p = (char *)xalloc((long)(strlen(dir) + 32));
        strcpy(p, dir);
        strcat(p, "/");
        strcat(p, names[i]);
        text = read_file(p, NULL, 1L << 20);
        free(p);
        if (text != NULL) {
            v = find_version(text);
            free(text);
            if (v != NULL) {
                return v;
            }
        }
    }
    return NULL;
}

int ksp_detect(struct ctx *c)
{
    compat_load(c);
    if (c->kspver != NULL) {
        return 0;
    }
    c->kspver = game_version(c->game);
    return c->kspver != NULL ? 0 : -1;
}
