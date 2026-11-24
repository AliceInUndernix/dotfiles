#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <termios.h>
#include <sys/ioctl.h>
#include "kspkan.h"

#define KEY_UP 1000
#define KEY_DOWN 1001
#define KEY_PGUP 1002
#define KEY_PGDN 1003
#define KEY_HOME 1004
#define KEY_END 1005
#define KEY_ESC 27
#define KEY_ENTER 13

static struct termios saved_term;
static int term_saved;

static void raw_on(void)
{
    struct termios t;

    if (!term_saved) {
        tcgetattr(0, &saved_term);
        term_saved = 1;
    }
    t = saved_term;
    t.c_lflag &= ~(unsigned int)(ECHO | ICANON | ISIG);
    t.c_iflag &= ~(unsigned int)(IXON | ICRNL);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSAFLUSH, &t);
    fputs("\033[?1049h", stdout);
    fflush(stdout);
}

static void raw_off(void)
{
    fputs("\033[0m\033[?25h\033[?1049l", stdout);
    fflush(stdout);
    if (term_saved) {
        tcsetattr(0, TCSAFLUSH, &saved_term);
    }
}

static int byte_ready(void)
{
    struct pollfd p;

    p.fd = 0;
    p.events = POLLIN;
    p.revents = 0;
    return poll(&p, 1, 40) > 0;
}

static int read_key(void)
{
    unsigned char ch, s[4];
    long r;

    r = (long)read(0, &ch, 1);
    if (r <= 0) {
        return -1;
    }
    if (ch != 27) {
        return ch;
    }
    if (!byte_ready()) {
        return KEY_ESC;
    }
    r = (long)read(0, s, 1);
    if (r <= 0 || (s[0] != '[' && s[0] != 'O')) {
        return KEY_ESC;
    }
    if (!byte_ready()) {
        return KEY_ESC;
    }
    r = (long)read(0, s, 1);
    if (r <= 0) {
        return KEY_ESC;
    }
    if (s[0] == 'A') {
        return KEY_UP;
    }
    if (s[0] == 'B') {
        return KEY_DOWN;
    }
    if (s[0] == 'H') {
        return KEY_HOME;
    }
    if (s[0] == 'F') {
        return KEY_END;
    }
    if (s[0] >= '1' && s[0] <= '8') {
        r = (long)read(0, &ch, 1);
        if (s[0] == '5') {
            return KEY_PGUP;
        }
        if (s[0] == '6') {
            return KEY_PGDN;
        }
        if (s[0] == '1' || s[0] == '7') {
            return KEY_HOME;
        }
        return KEY_END;
    }
    return 0;
}

struct view {
    int *rows;
    int nrows;
    char *marks;
    struct entry **newest;
    int cur;
    int top;
    int sort;
    int usedsort;
    int onlyinst;
    int onlycompat;
    char find[128];
};

/* newest stable release that fits the game, per summary line */
static void view_build(struct ctx *c, struct view *v)
{
    struct entry *e;
    int i, k, cnt;

    free(v->rows);
    free(v->marks);
    free(v->newest);
    v->rows = (int *)xalloc((long)(c->ix.ns + 1) * (long)sizeof(int));
    v->marks = (char *)xalloc((long)c->ix.ns + 1);
    memset(v->marks, 0, (unsigned long)c->ix.ns + 1);
    v->newest = (struct entry **)xalloc((long)(c->ix.ns + 1) *
        (long)sizeof(struct entry *));
    for (i = 0; i < c->ix.ns; i++) {
        v->newest[i] = NULL;
        e = index_find(&c->ix, c->ix.s[i].id, &cnt);
        for (k = 0; e != NULL && k < cnt; k++) {
            if (strcmp(e[k].kind, "dlc") == 0) {
                continue;
            }
            if ((c->testing || strcmp(e[k].status, "stable") == 0) &&
                entry_compat(c, &e[k]))
            {
                v->newest[i] = &e[k];
                break;
            }
        }
    }
    v->cur = 0;
    v->top = 0;
}

static void refilter(struct ctx *c, struct view *v)
{
    struct summary *s;
    struct query q;
    int *order;
    int i, j, keep;

    keep = v->nrows > 0 ? v->rows[v->cur] : -1;
    query_parse(&q, v->find);
    v->usedsort = q.sort >= 0 ? q.sort : v->sort;
    order = summary_order(c, v->usedsort);
    v->nrows = 0;
    for (j = 0; j < c->ix.ns; j++) {
        i = order[j];
        s = &c->ix.s[i];
        if (v->onlycompat && v->newest[i] == NULL &&
            registry_find(&c->reg, s->id) == NULL)
        {
            continue;
        }
        if (v->onlyinst && registry_find(&c->reg, s->id) == NULL) {
            continue;
        }
        if (!query_match(c, &q, i)) {
            continue;
        }
        v->rows[v->nrows] = i;
        v->nrows++;
    }
    free(order);
    query_free(&q);
    v->cur = 0;
    for (j = 0; j < v->nrows; j++) {
        if (v->rows[j] == keep) {
            v->cur = j;
        }
    }
    v->top = 0;
}

/* ---- colours: the habamax scheme, as 256 colour codes ---- */

#define CL_TEXT 0
#define CL_DIM 1
#define CL_TITLE 2
#define CL_INST 3
#define CL_UPD 4
#define CL_ADD 5
#define CL_DEL 6
#define CL_KEY 7
#define CL_LINK 8
#define CL_SORT 9

static int tui_color = 1;

static const char *cl_rgb[] = {
    "\033[22;39m",          /* text: the terminal's own colour */
    "\033[22;38;5;243m",    /* dim: #767676 */
    "\033[1;38;5;180m",     /* title: #d7af87 */
    "\033[22;38;5;109m",    /* installed: #87afaf */
    "\033[22;38;5;137m",    /* update: #af875f */
    "\033[22;38;5;108m",    /* to install: #87af87 */
    "\033[22;38;5;131m",    /* to remove: #af5f5f */
    "\033[22;38;5;110m",    /* key: #87afd7 */
    "\033[22;38;5;67m",     /* link: #5f87af */
    "\033[1;38;5;180m"      /* sorted column */
};

static const char *cl_mono[] = {
    "\033[22m", "\033[22m", "\033[1m", "\033[22m", "\033[1m", "\033[1m",
    "\033[1m", "\033[1m", "\033[22m", "\033[1m"
};

#define BG_BAR 1
#define BG_SEL 2

/* one screen line: text with colours, cut and padded to a width */
struct line {
    struct buf b;
    int vis;
    int width;
    int bg;
};

static void ln_bg(struct line *l)
{
    if (l->bg == BG_BAR) {
        buf_adds(&l->b, tui_color ? "\033[48;5;236m" : "\033[7m");
    } else if (l->bg == BG_SEL) {
        buf_adds(&l->b, tui_color ? "\033[48;5;238m" : "\033[7m");
    }
}

static void ln_start(struct line *l, int width, int bg)
{
    l->b.p = NULL;
    l->b.n = 0;
    l->b.cap = 0;
    buf_add(&l->b, "", 0);
    l->vis = 0;
    l->width = width;
    l->bg = bg;
    buf_adds(&l->b, "\033[0m");
    ln_bg(l);
}

static void ln_color(struct line *l, int cl)
{
    buf_adds(&l->b, tui_color ? cl_rgb[cl] : cl_mono[cl]);
}

/* add printable text, at most max characters (max < 0: no limit) */
static void ln_text(struct line *l, const char *s, int max)
{
    char t;
    int i;

    for (i = 0; s[i] && l->vis < l->width && (max < 0 || i < max); i++) {
        t = s[i];
        if ((unsigned char)t < 32 || (unsigned char)t > 126) {
            t = '?';
        }
        buf_add(&l->b, &t, 1);
        l->vis++;
    }
}

static void ln_pad(struct line *l, int upto)
{
    while (l->vis < upto && l->vis < l->width) {
        buf_add(&l->b, " ", 1);
        l->vis++;
    }
}

/* a field of fixed width, cut or padded; right aligned if right */
static void ln_field(struct line *l, int cl, const char *s, int w,
    int right)
{
    int n, start;

    n = (int)strlen(s);
    if (n > w) {
        n = w;
    }
    start = l->vis;
    ln_color(l, cl);
    if (right) {
        ln_pad(l, start + w - n);
    }
    ln_text(l, s, n);
    ln_pad(l, start + w);
}

static void ln_put(struct line *l, struct buf *out, int last)
{
    ln_pad(l, l->width);
    buf_adds(&l->b, "\033[0m");
    if (!last) {
        buf_adds(&l->b, "\r\n");
    }
    buf_add(out, l->b.p, l->b.n);
    buf_free(&l->b);
}

static void fmt_count(long n, char *out)
{
    if (n <= 0) {
        strcpy(out, "-");
    } else if (n < 1000) {
        sprintf(out, "%ld", n);
    } else if (n < 1000000) {
        sprintf(out, "%ld.%ldk", n / 1000, (n % 1000) / 100);
    } else {
        sprintf(out, "%ld.%ldM", n / 1000000, (n % 1000000) / 100000);
    }
}

/* how many characters of s fit on one line of width cols */
static int wrap_take(const char *s, int cols)
{
    int n, last;

    n = (int)strlen(s);
    if (n <= cols) {
        return n;
    }
    last = cols;
    while (last > 0 && s[last] != ' ') {
        last--;
    }
    return last > 0 ? last : cols;
}

static void join_names(struct buf *b, struct jnode *arr, int cols)
{
    struct jnode *el, *alt;
    const char *nm;
    int first;

    first = 1;
    if (arr == NULL || arr->type != J_ARR) {
        return;
    }
    for (el = arr->child; el != NULL && b->n < cols; el = el->next) {
        nm = json_str(el, "name");
        alt = json_get(el, "any_of");
        if (nm == NULL && alt != NULL && alt->child != NULL) {
            nm = json_str(alt->child, "name");
        }
        if (nm == NULL) {
            continue;
        }
        if (!first) {
            buf_adds(b, ", ");
        }
        first = 0;
        buf_adds(b, nm);
    }
}

static void authors_of(struct buf *b, struct jnode *j)
{
    struct jnode *a, *el;
    int first;

    a = json_get(j, "author");
    if (a == NULL) {
        return;
    }
    if (a->type == J_STR) {
        buf_adds(b, a->str);
        return;
    }
    first = 1;
    for (el = a->child; el != NULL && a->type == J_ARR; el = el->next) {
        if (el->type != J_STR) {
            continue;
        }
        if (!first) {
            buf_adds(b, ", ");
        }
        first = 0;
        buf_adds(b, el->str);
    }
}

/* "label value" pair, the label dim */
static void ln_pair(struct line *l, const char *label, const char *value,
    int cl)
{
    ln_color(l, CL_DIM);
    ln_text(l, label, -1);
    ln_color(l, value[0] == 0 || strcmp(value, "-") == 0 ? CL_DIM : cl);
    ln_text(l, value[0] ? value : "-", -1);
    ln_text(l, "   ", -1);
}

static void ln_list(struct line *l, const char *label, struct jnode *j,
    const char *key, int cols)
{
    struct buf t;

    t.p = NULL;
    t.n = 0;
    t.cap = 0;
    buf_add(&t, "", 0);
    join_names(&t, json_get(j, key), cols);
    ln_pair(l, label, t.p, CL_TEXT);
    buf_free(&t);
}

/* the detail pane: "lines" lines, the most useful kept when short */
static void draw_detail(struct ctx *c, struct view *v, struct buf *out,
    int lines, int cols)
{
    static const int rank[8] = { 0, 1, 4, 2, 6, 3, 5, 7 };
    struct line ln[8];
    struct summary *s;
    struct entry *e;
    struct jnode *j;
    struct inst *m;
    struct buf t;
    char num[32], sz[32];
    const char *ab, *hp;
    int i, n, y, cnt;

    if (lines <= 0) {
        return;
    }
    for (y = 0; y < 8; y++) {
        ln_start(&ln[y], cols, 0);
    }
    if (v->nrows > 0) {
        i = v->rows[v->cur];
        s = &c->ix.s[i];
        e = v->newest[i] != NULL ? v->newest[i] : index_find(&c->ix, s->id,
            &cnt);
        j = e != NULL ? entry_json(c, e) : NULL;
        m = registry_find(&c->reg, s->id);
        ln_color(&ln[0], CL_TITLE);
        ln_text(&ln[0], s->name, -1);
        if (j != NULL && json_get(j, "author") != NULL) {
            t.p = NULL;
            t.n = 0;
            t.cap = 0;
            buf_add(&t, "", 0);
            authors_of(&t, j);
            ln_color(&ln[0], CL_DIM);
            ln_text(&ln[0], "  by ", -1);
            ln_color(&ln[0], CL_TEXT);
            ln_text(&ln[0], t.p, -1);
            buf_free(&t);
        }
        fmt_count(s->downloads, num);
        ln_pair(&ln[1], "id ", s->id, CL_INST);
        ln_pair(&ln[1], "for your game ", v->newest[i] != NULL ?
            v->newest[i]->version : "none", CL_TEXT);
        ln_pair(&ln[1], "installed ", m != NULL ? m->version : "no",
            m != NULL ? CL_INST : CL_TEXT);
        ln_pair(&ln[1], "downloads ", num, CL_TEXT);
        fmt_size(s->size, sz);
        ln_pair(&ln[2], "license ", s->license, CL_TEXT);
        ln_pair(&ln[2], "released ", s->date, CL_TEXT);
        ln_pair(&ln[2], "size ", strcmp(sz, "?") == 0 ? "-" : sz, CL_TEXT);
        ln_pair(&ln[2], "tags ", s->tags, CL_TEXT);
        ab = j != NULL ? json_str(j, "abstract") : s->abstract;
        if (ab == NULL) {
            ab = "";
        }
        n = wrap_take(ab, cols);
        ln_text(&ln[3], ab, n);
        ab += n;
        while (*ab == ' ') {
            ab++;
        }
        n = wrap_take(ab, cols);
        ln_text(&ln[4], ab, n);
        ln_list(&ln[5], "needs ", j, "depends", cols);
        ln_list(&ln[6], "recommended ", j, "recommends", cols / 2);
        ln_list(&ln[6], "suggested ", j, "suggests", cols / 2);
        hp = json_str(json_get(j, "resources"), "homepage");
        ln_color(&ln[7], CL_DIM);
        ln_text(&ln[7], "page ", -1);
        ln_color(&ln[7], CL_LINK);
        ln_text(&ln[7], hp != NULL ? hp : "-", -1);
    }
    for (y = 0; y < 8; y++) {
        if (rank[y] < lines) {
            ln_put(&ln[y], out, 0);
        } else {
            buf_free(&ln[y].b);
        }
    }
}

/* mark the recommended modules of the selected one for installing */
static int mark_recommended(struct ctx *c, struct view *v)
{
    struct jnode *arr, *el;
    struct summary *s;
    struct entry *e;
    const char *nm;
    int i, count;

    if (v->nrows == 0) {
        return 0;
    }
    i = v->rows[v->cur];
    e = v->newest[i];
    if (e == NULL) {
        return 0;
    }
    arr = json_get(entry_json(c, e), "recommends");
    count = 0;
    if (arr == NULL || arr->type != J_ARR) {
        return 0;
    }
    for (el = arr->child; el != NULL; el = el->next) {
        nm = json_str(el, "name");
        if (nm == NULL) {
            continue;
        }
        s = summary_find(&c->ix, nm);
        if (s == NULL || registry_find(&c->reg, nm) != NULL) {
            continue;
        }
        if (!v->marks[s - c->ix.s]) {
            v->marks[s - c->ix.s] = 1;
            count++;
        }
    }
    return count;
}

static void key_hint(struct line *l, const char *key, const char *what)
{
    ln_color(l, CL_KEY);
    ln_text(l, key, -1);
    ln_color(l, CL_DIM);
    ln_text(l, " ", -1);
    ln_text(l, what, -1);
    ln_text(l, "   ", -1);
}

static void draw_row(struct ctx *c, struct view *v, struct buf *out,
    int idx, int cols, int idw, int verw, int wide)
{
    struct line l;
    struct summary *s;
    struct inst *m;
    char dl[32], sz[32];
    int i, st, dim;

    i = v->rows[idx];
    s = &c->ix.s[i];
    m = registry_find(&c->reg, s->id);
    ln_start(&l, cols, idx == v->cur ? BG_SEL : 0);
    dim = (v->newest[i] == NULL && m == NULL);
    st = ' ';
    if (m != NULL) {
        st = 'i';
        if (v->newest[i] != NULL &&
            version_compare(v->newest[i]->version, m->version) > 0)
        {
            st = 'u';
        }
    }
    ln_text(&l, " ", -1);
    if (v->marks[i]) {
        ln_color(&l, m != NULL ? CL_DEL : CL_ADD);
        ln_text(&l, m != NULL ? "x" : "+", -1);
    } else {
        ln_text(&l, " ", -1);
    }
    ln_color(&l, st == 'u' ? CL_UPD : CL_INST);
    ln_text(&l, st == 'u' ? "u " : (st == 'i' ? "i " : "  "), -1);
    ln_field(&l, dim ? CL_DIM : (m != NULL ? CL_INST : CL_TEXT), s->id,
        idw, 0);
    ln_text(&l, " ", -1);
    ln_field(&l, CL_DIM, v->newest[i] ? v->newest[i]->version : "-", verw,
        0);
    fmt_count(s->downloads, dl);
    ln_field(&l, CL_DIM, dl, 7, 1);
    ln_text(&l, "  ", -1);
    if (wide) {
        ln_field(&l, CL_DIM, s->date[0] ? s->date : "-", 10, 0);
        ln_text(&l, "  ", -1);
        fmt_size(s->size, sz);
        ln_field(&l, CL_DIM, strcmp(sz, "?") == 0 ? "-" : sz, 8, 1);
        ln_text(&l, "  ", -1);
    }
    ln_color(&l, dim ? CL_DIM : CL_TEXT);
    ln_text(&l, s->name, -1);
    ln_put(&l, out, 0);
}

static void draw(struct ctx *c, struct view *v, int rows, int cols,
    const char *mode)
{
    struct buf b;
    struct line l;
    char tmp[64];
    int i, y, nmarks, avail, detail, idw, verw, wide, so;
    long w;

    b.p = NULL;
    b.n = 0;
    b.cap = 0;
    buf_add(&b, "", 0);
    nmarks = 0;
    for (i = 0; i < c->ix.ns; i++) {
        if (v->marks[i]) {
            nmarks++;
        }
    }
    detail = rows >= 28 ? 8 : (rows >= 24 ? 7 : (rows >= 16 ? 4 : 0));
    avail = rows - 4 - detail - (mode[0] != 0 ? 1 : 0);
    if (avail < 1) {
        detail = 0;
        avail = rows - 4 - (mode[0] != 0 ? 1 : 0);
    }
    wide = cols >= 100;
    idw = cols >= 110 ? 34 : 26;
    verw = wide ? 12 : 10;
    so = v->usedsort;
    buf_adds(&b, "\033[H");

    /* title bar */
    ln_start(&l, cols, BG_BAR);
    ln_color(&l, CL_TITLE);
    ln_text(&l, " kspkan", -1);
    if (c->instname != NULL) {
        ln_color(&l, CL_TEXT);
        ln_text(&l, " [", -1);
        ln_text(&l, c->instname, -1);
        ln_text(&l, "]", -1);
    }
    ln_text(&l, "   ", -1);
    ln_pair(&l, "order ", sort_names[so], CL_SORT);
    ln_pair(&l, "game ", c->kspver ? c->kspver : "?", CL_TEXT);
    sprintf(tmp, "%d", v->nrows);
    ln_pair(&l, "shown ", tmp, CL_TEXT);
    if (nmarks > 0) {
        sprintf(tmp, "%d", nmarks);
        ln_pair(&l, "marked ", tmp, CL_ADD);
    }
    if (v->onlyinst) {
        ln_pair(&l, "", "installed only", CL_UPD);
    }
    if (!v->onlycompat) {
        ln_pair(&l, "", "incompatible too", CL_UPD);
    }
    if (c->listdays >= 14) {
        ln_pair(&l, "", "list is old: press u", CL_DEL);
    }
    if (v->find[0] != 0 && mode[0] == 0) {
        ln_pair(&l, "search ", v->find, CL_KEY);
    }
    ln_put(&l, &b, 0);

    /* column headings */
    ln_start(&l, cols, 0);
    ln_text(&l, "     ", -1);
    ln_field(&l, so == SORT_NAME ? CL_SORT : CL_DIM, so == SORT_NAME ?
        "identifier <" : "identifier", idw, 0);
    ln_text(&l, " ", -1);
    ln_field(&l, CL_DIM, "version", verw, 0);
    ln_field(&l, so == SORT_POP ? CL_SORT : CL_DIM, so == SORT_POP ?
        "> downl." : "downl.", 7, 1);
    ln_text(&l, "  ", -1);
    if (wide) {
        ln_field(&l, so == SORT_NEW ? CL_SORT : CL_DIM, so == SORT_NEW ?
            "date >" : "date", 10, 0);
        ln_text(&l, "  ", -1);
        ln_field(&l, so == SORT_SMALL || so == SORT_BIG ? CL_SORT : CL_DIM,
            so == SORT_SMALL ? "< size" : (so == SORT_BIG ? "> size" :
            "size"), 8, 1);
        ln_text(&l, "  ", -1);
    }
    ln_color(&l, CL_DIM);
    ln_text(&l, "name", -1);
    ln_put(&l, &b, 0);

    /* the list */
    if (v->cur < v->top) {
        v->top = v->cur;
    }
    if (v->cur >= v->top + avail) {
        v->top = v->cur - avail + 1;
    }
    for (y = 0; y < avail; y++) {
        if (v->top + y < v->nrows) {
            draw_row(c, v, &b, v->top + y, cols, idw, verw, wide);
        } else {
            ln_start(&l, cols, 0);
            if (y == 0) {
                ln_color(&l, CL_DIM);
                ln_text(&l, c->ix.ns == 0 ?
                    "   no module list yet: press u to fetch it" :
                    "   nothing matches; Esc clears the search", -1);
            }
            ln_put(&l, &b, 0);
        }
    }

    /* separator with the position in the list */
    ln_start(&l, cols, 0);
    ln_color(&l, CL_DIM);
    sprintf(tmp, " %d of %d ", v->nrows > 0 ? v->cur + 1 : 0, v->nrows);
    ln_text(&l, "----", -1);
    ln_text(&l, tmp, -1);
    while (l.vis < cols) {
        ln_text(&l, "-", -1);
    }
    ln_put(&l, &b, 0);
    draw_detail(c, v, &b, detail, cols);

    /* bottom bar */
    if (mode[0] != 0) {
        ln_start(&l, cols, 0);
        ln_color(&l, CL_DIM);
        ln_text(&l, " words  -word  author:x  tag:x  license:x  needs:x  "
            "dl>5k  after:2024-01-01  size<5  is:installed  is:update  "
            "sort:new|pop|small|big", -1);
        ln_put(&l, &b, 0);
        ln_start(&l, cols, BG_BAR);
        ln_color(&l, CL_TITLE);
        ln_text(&l, " search ", -1);
        ln_color(&l, CL_TEXT);
        ln_text(&l, v->find, -1);
        ln_text(&l, "_", -1);
        ln_color(&l, CL_DIM);
        ln_text(&l, "    Enter keeps it, Esc clears it", -1);
        ln_put(&l, &b, 1);
    } else {
        ln_start(&l, cols, BG_BAR);
        ln_text(&l, " ", -1);
        key_hint(&l, "Up/Dn", "move");
        key_hint(&l, "Space", "mark");
        key_hint(&l, "Enter", "apply");
        key_hint(&l, "/", "search");
        key_hint(&l, "s", "order");
        key_hint(&l, "i", "installed");
        key_hint(&l, "c", "compatible");
        key_hint(&l, "r", "recommended");
        key_hint(&l, "?", "help");
        key_hint(&l, "q", "quit");
        ln_put(&l, &b, 1);
    }
    w = (long)write(1, b.p, (unsigned long)b.n);
    (void)w;
    buf_free(&b);
}

static void help_screen(int cols)
{
    static const char *keys[] = {
        "Up Down  j k", "move one line",
        "PgUp PgDn", "move a page (also ctrl-u, ctrl-d)",
        "Home End  g G", "first and last line",
        "/", "search; words are ANDed, a leading - excludes a word",
        "", "author:x  tag:x  license:x  needs:x",
        "", "dl>5k  dl<100  after:2024-01-01  before:...  size<5 (MB)",
        "", "is:installed  is:update  is:new",
        "", "sort:pop  sort:new  sort:small  sort:big  sort:name",
        "Esc", "clear the search",
        "s", "next order: name, popular, newest, small, large",
        "i", "show only installed modules",
        "c", "show or hide modules that do not fit your game",
        "Space", "mark: + will be installed, x will be removed",
        "r", "mark the recommended modules of this one",
        "Enter  a", "carry out the marks (asks before it acts)",
        "U", "upgrade everything installed",
        "u", "fetch a new module list",
        "Tab", "switch to the next game instance",
        "q", "quit",
        NULL
    };
    struct buf b;
    struct line l;
    int i;
    long w;

    b.p = NULL;
    b.n = 0;
    b.cap = 0;
    buf_add(&b, "", 0);
    buf_adds(&b, "\033[H\033[0m\033[2J");
    ln_start(&l, cols, BG_BAR);
    ln_color(&l, CL_TITLE);
    ln_text(&l, " kspkan keys", -1);
    ln_put(&l, &b, 0);
    ln_start(&l, cols, 0);
    ln_put(&l, &b, 0);
    for (i = 0; keys[i] != NULL; i += 2) {
        ln_start(&l, cols, 0);
        ln_text(&l, "  ", -1);
        ln_field(&l, CL_KEY, keys[i], 16, 0);
        ln_color(&l, CL_TEXT);
        ln_text(&l, keys[i + 1], -1);
        ln_put(&l, &b, 0);
    }
    ln_start(&l, cols, 0);
    ln_put(&l, &b, 0);
    ln_start(&l, cols, 0);
    ln_text(&l, "  ", -1);
    ln_color(&l, CL_INST);
    ln_text(&l, "i", -1);
    ln_color(&l, CL_DIM);
    ln_text(&l, " installed   ", -1);
    ln_color(&l, CL_UPD);
    ln_text(&l, "u", -1);
    ln_color(&l, CL_DIM);
    ln_text(&l, " newer version available   ", -1);
    ln_color(&l, CL_ADD);
    ln_text(&l, "+", -1);
    ln_color(&l, CL_DIM);
    ln_text(&l, " to install   ", -1);
    ln_color(&l, CL_DEL);
    ln_text(&l, "x", -1);
    ln_color(&l, CL_DIM);
    ln_text(&l, " to remove", -1);
    ln_put(&l, &b, 0);
    ln_start(&l, cols, 0);
    ln_put(&l, &b, 0);
    ln_start(&l, cols, 0);
    ln_color(&l, CL_DIM);
    ln_text(&l, "  press any key to go back", -1);
    ln_put(&l, &b, 1);
    w = (long)write(1, b.p, (unsigned long)b.n);
    if (w >= 0) {
        read_key();
    }
    buf_free(&b);
}

static void pause_for_enter(void)
{
    char line[8];
    char *p;

    printf("\npress Enter to return ");
    fflush(stdout);
    p = fgets(line, sizeof(line), stdin);
    (void)p;
}

static void apply(struct ctx *c, struct view *v)
{
    struct strs ins, rem;
    int i;

    ins.v = rem.v = NULL;
    ins.n = rem.n = ins.cap = rem.cap = 0;
    for (i = 0; i < c->ix.ns; i++) {
        if (!v->marks[i]) {
            continue;
        }
        if (registry_find(&c->reg, c->ix.s[i].id) != NULL) {
            strs_add(&rem, xstrdup(c->ix.s[i].id));
        } else {
            strs_add(&ins, xstrdup(c->ix.s[i].id));
        }
        v->marks[i] = 0;
    }
    if (ins.n == 0 && rem.n == 0) {
        return;
    }
    raw_off();
    if (rem.n > 0) {
        cmd_remove(c, rem.v, rem.n);
    }
    if (ins.n > 0) {
        cmd_install(c, ins.v, ins.n);
    }
    pause_for_enter();
    strs_free(&ins);
    strs_free(&rem);
    raw_on();
}

static void do_update(struct ctx *c, struct view *v)
{
    int rc;

    raw_off();
    cmd_update(c);
    memset(&c->ix, 0, sizeof(c->ix));
    rc = index_load(c);
    if (rc < 0) {
        memset(&c->ix, 0, sizeof(c->ix));
    }
    view_build(c, v);
    v->nrows = 0;
    refilter(c, v);
    pause_for_enter();
    raw_on();
}

static void next_instance(struct ctx *c)
{
    int i;

    for (i = 0; i < c->inames.n; i++) {
        if (c->instname != NULL &&
            strcmp(c->inames.v[i], c->instname) == 0)
        {
            break;
        }
    }
    i = (i + 1) % c->inames.n;
    inst_select(c, c->inames.v[i]);
}

static void do_upgrade(struct ctx *c)
{
    raw_off();
    cmd_upgrade(c, NULL, 0);
    pause_for_enter();
    raw_on();
}

int cmd_tui(struct ctx *c)
{
    struct view v;
    struct winsize ws;
    char mode[4];
    int key, rows, cols, rc, flen, page, running, i;

    if (!isatty(0) || !isatty(1)) {
        fprintf(stderr, "kspkan: full screen mode needs a terminal\n");
        return 2;
    }
    rc = index_load(c);
    if (rc < 0) {
        memset(&c->ix, 0, sizeof(c->ix));
    }
    registry_load(c);
    ksp_detect(c);
    tui_color = !c->mono && getenv("NO_COLOR") == NULL &&
        (getenv("TERM") == NULL || strcmp(getenv("TERM"), "dumb") != 0);
    memset(&v, 0, sizeof(v));
    v.onlycompat = 1;
    view_build(c, &v);
    refilter(c, &v);
    mode[0] = 0;
    running = 1;
    raw_on();
    while (running) {
        rows = 24;
        cols = 80;
        rc = ioctl(1, TIOCGWINSZ, &ws);
        if (rc == 0 && ws.ws_row > 4) {
            rows = ws.ws_row;
            cols = ws.ws_col;
        }
        page = rows - 8;
        draw(c, &v, rows, cols, mode);
        key = read_key();
        if (key < 0) {
            break;
        }
        if (mode[0] != 0) {
            flen = (int)strlen(v.find);
            if (key == KEY_ENTER || key == 10) {
                mode[0] = 0;
            } else if (key == KEY_ESC) {
                mode[0] = 0;
                v.find[0] = 0;
                refilter(c, &v);
            } else if (key == 127 || key == 8) {
                if (flen > 0) {
                    v.find[flen - 1] = 0;
                }
                refilter(c, &v);
            } else if (key == KEY_UP && v.cur > 0) {
                v.cur--;
            } else if (key == KEY_DOWN && v.cur + 1 < v.nrows) {
                v.cur++;
            } else if (key >= 32 && key < 127 && flen < 120) {
                v.find[flen] = (char)key;
                v.find[flen + 1] = 0;
                refilter(c, &v);
            }
            continue;
        }
        if (key == 'q' || key == 3) {
            running = 0;
        } else if (key == 'j' || key == KEY_DOWN) {
            if (v.cur + 1 < v.nrows) {
                v.cur++;
            }
        } else if (key == 'k' || key == KEY_UP) {
            if (v.cur > 0) {
                v.cur--;
            }
        } else if (key == 'g' || key == KEY_HOME) {
            v.cur = 0;
        } else if (key == 'G' || key == KEY_END) {
            v.cur = v.nrows > 0 ? v.nrows - 1 : 0;
        } else if (key == 4 || key == KEY_PGDN) {
            v.cur += page;
            if (v.cur >= v.nrows) {
                v.cur = v.nrows > 0 ? v.nrows - 1 : 0;
            }
        } else if (key == 21 || key == KEY_PGUP) {
            v.cur -= page;
            if (v.cur < 0) {
                v.cur = 0;
            }
        } else if (key == '/') {
            strcpy(mode, "/");
        } else if (key == KEY_ESC && v.find[0] != 0) {
            v.find[0] = 0;
            refilter(c, &v);
        } else if (key == 's') {
            v.sort = (v.sort + 1) % SORT_KINDS;
            refilter(c, &v);
            v.cur = 0;
            v.top = 0;
        } else if (key == 'i') {
            v.onlyinst = !v.onlyinst;
            refilter(c, &v);
        } else if (key == 'c') {
            v.onlycompat = !v.onlycompat;
            refilter(c, &v);
        } else if (key == '?') {
            help_screen(cols);
        } else if (key == ' ' && v.nrows > 0) {
            i = v.rows[v.cur];
            v.marks[i] = (char)!v.marks[i];
            if (v.cur + 1 < v.nrows) {
                v.cur++;
            }
        } else if (key == KEY_ENTER || key == 10 || key == 'a') {
            apply(c, &v);
            refilter(c, &v);
        } else if (key == 'r') {
            mark_recommended(c, &v);
        } else if (key == 'U') {
            do_upgrade(c);
            refilter(c, &v);
        } else if (key == 9 && c->inames.n > 1) {
            next_instance(c);
            view_build(c, &v);
            v.nrows = 0;
            refilter(c, &v);
        } else if (key == 'u') {
            do_update(c, &v);
        }
    }
    raw_off();
    return 0;
}
