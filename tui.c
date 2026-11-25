#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/times.h>
#include "kspkan.h"

#define KEY_UP 1000
#define KEY_DOWN 1001
#define KEY_PGUP 1002
#define KEY_PGDN 1003
#define KEY_HOME 1004
#define KEY_END 1005
#define KEY_ESC 27
#define KEY_ENTER 13
#define KEY_MOUSE 1010

/* the last mouse event: button (0 left, 1 middle, 2 right, 64 wheel
   up, 65 wheel down), column and row from 1, and whether pressed */
static int mouse_b, mouse_x, mouse_y, mouse_down;

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
    fputs("\033[?1049h\033[?1000h\033[?1006h", stdout);
    fflush(stdout);
}

static void raw_off(void)
{
    fputs("\033[?1006l\033[?1000l\033[0m\033[?25h\033[?1049l", stdout);
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

/* a key read after Esc that was not part of an escape sequence */
static int pending_key = -1;

/* the rest of "ESC [ < b ; x ; y M" (pressed) or "... m" (let go) */
static int read_mouse(void)
{
    unsigned char ch;
    int v[3], k;
    long r;

    v[0] = v[1] = v[2] = 0;
    k = 0;
    for (;;) {
        if (!byte_ready()) {
            return 0;
        }
        r = (long)read(0, &ch, 1);
        if (r <= 0) {
            return 0;
        }
        if (ch >= '0' && ch <= '9' && v[k] < 10000) {
            v[k] = v[k] * 10 + (ch - '0');
        } else if (ch == ';' && k < 2) {
            k++;
        } else if (ch == 'M' || ch == 'm') {
            break;
        } else {
            return 0;
        }
    }
    mouse_b = v[0] & ~(4 | 8 | 16 | 32);
    mouse_x = v[1];
    mouse_y = v[2];
    mouse_down = ch == 'M';
    return KEY_MOUSE;
}

static int read_key(void)
{
    unsigned char ch, s[4];
    long r;

    if (pending_key >= 0) {
        r = pending_key;
        pending_key = -1;
        return (int)r;
    }
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
    if (r <= 0) {
        return KEY_ESC;
    }
    if (s[0] != '[' && s[0] != 'O') {
        pending_key = s[0];
        return KEY_ESC;
    }
    if (!byte_ready()) {
        return KEY_ESC;
    }
    r = (long)read(0, s, 1);
    if (r <= 0) {
        return KEY_ESC;
    }
    if (s[0] == '<') {
        return read_mouse();
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

/* what the list shows: the i key goes through these in turn */
#define SH_ALL 0
#define SH_INST 1
#define SH_CHOSEN 2
#define SH_DEP 3
#define SH_ORPHAN 4
#define SH_NOT 5
#define SH_UPD 6
#define SH_MARKED 7
#define SH_KINDS 8

static const char *show_names[SH_KINDS] = {
    "all", "installed", "chosen by you", "dependencies",
    "unneeded dependencies", "not installed", "updates", "marked"
};

/* where the description goes: the p key goes through these */
#define PANE_BOTTOM 0
#define PANE_RIGHT 1
#define PANE_OFF 2

static const char *pane_names[3] = { "bottom", "right", "off" };

struct view {
    int *rows;
    int nrows;
    char *marks;
    struct entry **newest;
    int cur;
    int top;
    int sort;
    int usedsort;
    int show;
    int onlycompat;
    int pane;
    int pscroll;
    int pshown;
    char find[128];
    char input[256];
    char msg[200];
    char hint[400];
};

/* the list that o opened, waiting for "add on top" or "replace" */
static struct modlist pend;
static char pend_name[100];

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

/* whether summary line i passes the i key's filter */
static int shows(struct ctx *c, struct view *v, int i)
{
    struct inst *m;
    const char *id;

    id = c->ix.s[i].id;
    m = registry_find(&c->reg, id);
    switch (v->show) {
    case SH_INST:
        return m != NULL;
    case SH_CHOSEN:
        return m != NULL && !m->autoinst;
    case SH_DEP:
        return m != NULL && m->autoinst;
    case SH_ORPHAN:
        return m != NULL && is_orphan(c, id);
    case SH_NOT:
        return m == NULL;
    case SH_UPD:
        return m != NULL && v->newest[i] != NULL &&
            version_compare(v->newest[i]->version, m->version) > 0;
    case SH_MARKED:
        return v->marks[i];
    default:
        return 1;
    }
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
        if (!shows(c, v, i)) {
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
        ln_pair(&ln[1], "installed ", m == NULL ? "no" : m->version,
            m != NULL ? CL_INST : CL_TEXT);
        if (m != NULL) {
            ln_pair(&ln[1], "", m->autoinst ? (needed_by(c, s->id, NULL) >
                0 ? "as a dependency" : "as a dependency, unneeded") :
                "chosen by you", CL_INST);
        }
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

/* ---- the right pane: everything about the module, wrapped ---- */

struct pane {
    struct line *l;
    int n;
    int cap;
    int width;
};

static struct line *pn_new(struct pane *p)
{
    struct line *nv;
    int i, ncap;

    if (p->n == p->cap) {
        ncap = p->cap ? p->cap * 2 : 32;
        nv = (struct line *)xalloc((long)ncap * (long)sizeof(*nv));
        for (i = 0; i < p->n; i++) {
            nv[i] = p->l[i];
        }
        free(p->l);
        p->l = nv;
        p->cap = ncap;
    }
    ln_start(&p->l[p->n], p->width, 0);
    p->n++;
    return &p->l[p->n - 1];
}

static void pn_free(struct pane *p)
{
    int i;

    for (i = 0; i < p->n; i++) {
        buf_free(&p->l[i].b);
    }
    free(p->l);
    p->l = NULL;
    p->n = 0;
    p->cap = 0;
}

/* how much of s goes on a line of cols: up to a newline, else cut at
   the last space that fits */
static int pn_take(const char *s, int cols)
{
    int n, last;

    for (n = 0; s[n] && s[n] != '\n' && s[n] != '\r'; n++) {
        continue;
    }
    if (n <= cols) {
        return n;
    }
    last = cols;
    while (last > 0 && s[last] != ' ') {
        last--;
    }
    return last > 0 ? last : cols;
}

/* label (dim) then text in colour cl, wrapped; following lines are
   indented by two.  Newlines in the text start new lines. */
static void pn_text(struct pane *p, const char *label, const char *text,
    int cl)
{
    struct line *l;
    int n, room, blank;

    l = pn_new(p);
    ln_color(l, CL_DIM);
    ln_text(l, label, -1);
    if (text == NULL || text[0] == 0) {
        ln_text(l, "-", -1);
        return;
    }
    blank = 0;
    for (;;) {
        room = p->width - l->vis;
        if (room < 8) {
            room = 8;
        }
        n = pn_take(text, room);
        ln_color(l, cl);
        ln_text(l, text, n);
        text += n;
        while (*text == ' ') {
            text++;
        }
        if (*text == '\r') {
            text++;
        }
        if (*text == '\n') {
            text++;
            if (*text == '\n' || *text == '\r') {
                blank = 1;
            }
            while (*text == '\n' || *text == '\r' || *text == ' ') {
                text++;
            }
        }
        if (*text == 0) {
            break;
        }
        if (blank) {
            pn_new(p);
            blank = 0;
        }
        l = pn_new(p);
        ln_text(l, label[0] != 0 ? "  " : "", -1);
    }
}

static void pn_names(struct pane *p, const char *label, struct jnode *j,
    const char *key)
{
    struct buf t;
    struct jnode *a;

    a = json_get(j, key);
    if (a == NULL || a->type != J_ARR || a->child == NULL) {
        return;
    }
    t.p = NULL;
    t.n = 0;
    t.cap = 0;
    buf_add(&t, "", 0);
    join_names(&t, a, 4000);
    pn_text(p, label, t.p, CL_TEXT);
    buf_free(&t);
}

static void pn_link(struct pane *p, struct jnode *res, const char *key,
    const char *label)
{
    const char *u;
    struct line *l;
    int n;

    u = json_str(res, key);
    if (u == NULL) {
        return;
    }
    l = pn_new(p);
    ln_color(l, CL_DIM);
    ln_text(l, label, -1);
    ln_color(l, CL_LINK);
    for (;;) {
        n = p->width - l->vis;
        ln_text(l, u, n);
        if ((int)strlen(u) <= n) {
            break;
        }
        u += n;
        l = pn_new(p);
        ln_color(l, CL_LINK);
        ln_text(l, "  ", -1);
    }
}

static void pane_build(struct ctx *c, struct view *v, struct pane *p)
{
    struct summary *s;
    struct entry *e;
    struct jnode *j;
    struct inst *m;
    struct strs by;
    struct buf t;
    struct line *l;
    char num[32], sz[32];
    const char *d;
    int i, k, cnt;

    if (v->nrows == 0) {
        return;
    }
    i = v->rows[v->cur];
    s = &c->ix.s[i];
    e = v->newest[i] != NULL ? v->newest[i] : index_find(&c->ix, s->id,
        &cnt);
    j = e != NULL ? entry_json(c, e) : NULL;
    m = registry_find(&c->reg, s->id);
    pn_text(p, "", s->name, CL_TITLE);
    t.p = NULL;
    t.n = 0;
    t.cap = 0;
    buf_add(&t, "", 0);
    authors_of(&t, j);
    if (t.n > 0) {
        pn_text(p, "by ", t.p, CL_TEXT);
    }
    buf_free(&t);
    pn_new(p);
    pn_text(p, "id          ", s->id, CL_INST);
    pn_text(p, "for you     ", v->newest[i] != NULL ?
        v->newest[i]->version : "none fits your game", CL_TEXT);
    if (m == NULL) {
        pn_text(p, "installed   ", "no", CL_TEXT);
    } else {
        l = pn_new(p);
        ln_color(l, CL_DIM);
        ln_text(l, "installed   ", -1);
        ln_color(l, CL_INST);
        ln_text(l, m->found && strcmp(m->version, "0") == 0 ?
            "version not known" : m->version, -1);
        ln_text(l, m->autoinst ? ", as a dependency" : ", chosen by you",
            -1);
        if (m->found) {
            ln_text(l, ", found in the game (not put there by kspkan)",
                -1);
        }
        by.v = NULL;
        by.n = 0;
        by.cap = 0;
        needed_by(c, s->id, &by);
        t.p = NULL;
        t.n = 0;
        t.cap = 0;
        buf_add(&t, "", 0);
        for (k = 0; k < by.n; k++) {
            if (k > 0) {
                buf_adds(&t, ", ");
            }
            buf_adds(&t, by.v[k]);
        }
        if (by.n > 0) {
            pn_text(p, "needed by   ", t.p, CL_TEXT);
        } else if (m->autoinst) {
            pn_text(p, "needed by   ", "nothing (autoremove takes it)",
                CL_UPD);
        }
        buf_free(&t);
        strs_free(&by);
    }
    fmt_count(s->downloads, num);
    pn_text(p, "downloads   ", num, CL_TEXT);
    fmt_size(s->size, sz);
    pn_text(p, "size        ", strcmp(sz, "?") == 0 ? "-" : sz, CL_TEXT);
    pn_text(p, "released    ", s->date, CL_TEXT);
    pn_text(p, "license     ", s->license, CL_TEXT);
    pn_text(p, "tags        ", s->tags, CL_TEXT);
    pn_new(p);
    d = j != NULL ? json_str(j, "abstract") : NULL;
    pn_text(p, "", d != NULL ? d : s->abstract, CL_TEXT);
    d = j != NULL ? json_str(j, "description") : NULL;
    if (d != NULL && d[0] != 0) {
        pn_new(p);
        pn_text(p, "", d, CL_TEXT);
    }
    if (j == NULL) {
        return;
    }
    pn_new(p);
    pn_names(p, "needs       ", j, "depends");
    pn_names(p, "recommends  ", j, "recommends");
    pn_names(p, "suggests    ", j, "suggests");
    pn_names(p, "supports    ", j, "supports");
    pn_names(p, "conflicts   ", j, "conflicts");
    pn_link(p, json_get(j, "resources"), "homepage", "page        ");
    pn_link(p, json_get(j, "resources"), "repository", "source      ");
    pn_link(p, json_get(j, "resources"), "spacedock", "spacedock   ");
    pn_link(p, json_get(j, "resources"), "bugtracker", "bugs        ");
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

/* where draw put the list: its first screen row, how many rows, its
   width, and whether the pane is on the right */
static int lay_y0, lay_avail, lay_lw, lay_right;

/* a second click on the same row soon after the first */
static int dbl_click(int row)
{
    static long last;
    static int lastrow = -1;
    struct tms t;
    long now;
    int dbl;

    /* times() counts in hundredths of a second on Linux */
    now = (long)times(&t);
    dbl = row == lastrow && now - last >= 0 && now - last < 40;
    last = now;
    lastrow = dbl ? -1 : row;
    return dbl;
}

/* where the key hints of the bottom bar are, so that a click on one
   works as its key */
struct hintpos {
    int x0;
    int x1;
    int key;
};

static struct hintpos hints[24];
static int nhints;
static int hint_rec;

static int key_code(const char *k)
{
    if (strcmp(k, "Space") == 0) {
        return ' ';
    }
    if (strncmp(k, "Enter", 5) == 0) {
        return KEY_ENTER;
    }
    if (strcmp(k, "Tab") == 0) {
        return 9;
    }
    if (strncmp(k, "Esc", 3) == 0) {
        return KEY_ESC;
    }
    if (strchr(k, '/') != NULL && k[1] != 0) {
        return 0;
    }
    return (unsigned char)k[0];
}

static void key_hint(struct line *l, const char *key, const char *what)
{
    if (hint_rec && nhints < 24) {
        hints[nhints].x0 = l->vis + 1;
        hints[nhints].key = key_code(key);
    }
    ln_color(l, CL_KEY);
    ln_text(l, key, -1);
    ln_color(l, CL_DIM);
    ln_text(l, " ", -1);
    ln_text(l, what, -1);
    if (hint_rec && nhints < 24) {
        hints[nhints].x1 = l->vis;
        nhints++;
    }
    ln_text(l, "   ", -1);
}

/* the key of the hint at column x of the bottom bar, or 0 */
static int hint_at(int x)
{
    int i;

    for (i = 0; i < nhints; i++) {
        if (x >= hints[i].x0 && x <= hints[i].x1) {
            return hints[i].key;
        }
    }
    return 0;
}

/* one line of the list; with nl 0 the line is left open, for the right
   pane to follow on the same screen line */
static void draw_row(struct ctx *c, struct view *v, struct buf *out,
    int idx, int cols, int idw, int verw, int wide, int nl)
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
    /* i: chosen by you, d: a dependency, f: found in the game (put
       there by CKAN or by hand); then u: a newer one fits */
    ln_color(&l, CL_INST);
    ln_text(&l, m == NULL ? " " : (m->found ? "f" : (m->autoinst ? "d" :
        "i")), -1);
    ln_color(&l, CL_UPD);
    ln_text(&l, st == 'u' ? "u " : "  ", -1);
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
    ln_put(&l, out, !nl);
}

/* the separator and line k of the right pane, ending the screen line */
static void pane_line(struct pane *p, int k, struct buf *out)
{
    struct line l;

    buf_adds(out, "\033[0m");
    buf_adds(out, tui_color ? cl_rgb[CL_DIM] : cl_mono[CL_DIM]);
    buf_adds(out, "|");
    if (p != NULL && k >= 0 && k < p->n) {
        ln_put(&p->l[k], out, 0);
    } else {
        ln_start(&l, p != NULL ? p->width : 0, 0);
        ln_put(&l, out, 0);
    }
}

/* the bottom bar: the input line of a prompt, a message, or the keys */
static void draw_bottom(struct ctx *c, struct view *v, struct buf *b,
    int cols, const char *mode)
{
    struct strs names;
    struct line l;
    int i;

    if (mode[0] == '/') {
        ln_start(&l, cols, 0);
        ln_color(&l, CL_DIM);
        ln_text(&l, " words  -word  author:x  tag:x  license:x  needs:x  "
            "dl>5k  after:2024-01-01  size<5  is:installed  is:chosen  "
            "is:dep  is:orphan  is:found  is:update  "
            "sort:new|pop|small|big", -1);
        ln_put(&l, b, 0);
        ln_start(&l, cols, BG_BAR);
        ln_color(&l, CL_TITLE);
        ln_text(&l, " search ", -1);
        ln_color(&l, CL_TEXT);
        ln_text(&l, v->find, -1);
        ln_text(&l, "_", -1);
        ln_color(&l, CL_DIM);
        ln_text(&l, "    Enter keeps it, Esc clears it", -1);
        ln_put(&l, b, 1);
        return;
    }
    if (mode[0] == 'h') {
        ln_start(&l, cols, 0);
        ln_color(&l, CL_TEXT);
        ln_text(&l, " ", -1);
        ln_text(&l, v->msg[0] ? v->msg : pend_name, -1);
        ln_put(&l, b, 0);
        ln_start(&l, cols, BG_BAR);
        ln_color(&l, CL_TITLE);
        ln_text(&l, " apply it how?  ", -1);
        hint_rec = 1;
        nhints = 0;
        key_hint(&l, "a", "add on top of what the game has");
        key_hint(&l, "r", "replace: also mark for removal the mods you "
            "chose that it lacks");
        key_hint(&l, "Esc", "leave it");
        hint_rec = 0;
        ln_put(&l, b, 1);
        return;
    }
    if (mode[0] == 'w' || mode[0] == 'o') {
        ln_start(&l, cols, 0);
        if (v->hint[0] != 0) {
            ln_color(&l, CL_KEY);
            ln_text(&l, " ", -1);
            ln_text(&l, v->hint, -1);
        } else {
            names.v = NULL;
            names.n = 0;
            names.cap = 0;
            modlist_names(c, &names);
            ln_color(&l, CL_DIM);
            ln_text(&l, mode[0] == 'o' ? " a modlist, or a file: kspkan "
                "modlist, .ckan, registry.json, CKAN's txt md csv tsv;"
                " Tab completes;" : " a modlist name, or a path with / "
                "for a file; Tab completes;", -1);
            ln_text(&l, names.n > 0 ? " modlists: " : " no modlists yet",
                -1);
            for (i = 0; i < names.n; i++) {
                ln_color(&l, CL_KEY);
                ln_text(&l, names.v[i], -1);
                ln_text(&l, "  ", -1);
            }
            strs_free(&names);
        }
        ln_put(&l, b, 0);
        ln_start(&l, cols, BG_BAR);
        ln_color(&l, CL_TITLE);
        ln_text(&l, mode[0] == 'w' ? " export the mods you chose to " :
            " open (import) ", -1);
        ln_color(&l, CL_TEXT);
        ln_text(&l, v->input, -1);
        ln_text(&l, "_", -1);
        ln_color(&l, CL_DIM);
        ln_text(&l, "    Tab: complete  Enter: go  Esc: back", -1);
        ln_put(&l, b, 1);
        return;
    }
    ln_start(&l, cols, BG_BAR);
    if (v->msg[0] != 0) {
        ln_color(&l, CL_TEXT);
        ln_text(&l, " ", -1);
        ln_text(&l, v->msg, -1);
        ln_put(&l, b, 1);
        return;
    }
    ln_text(&l, " ", -1);
    hint_rec = 1;
    nhints = 0;
    key_hint(&l, "j/k", "move");
    key_hint(&l, "Space", "mark");
    key_hint(&l, "Enter", "apply");
    key_hint(&l, "/", "search");
    key_hint(&l, "i", "show");
    key_hint(&l, "s", "order");
    key_hint(&l, "p", "pane");
    key_hint(&l, "o", "import");
    key_hint(&l, "w", "export");
    key_hint(&l, "Tab", "games");
    key_hint(&l, "?", "help");
    key_hint(&l, "q", "quit");
    hint_rec = 0;
    ln_put(&l, b, 1);
}

static void draw(struct ctx *c, struct view *v, int rows, int cols,
    const char *mode)
{
    struct buf b;
    struct line l;
    struct pane p;
    char tmp[64];
    int i, y, nmarks, avail, detail, idw, verw, wide, so, right, pw, lw;
    int bars, h;
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
    right = v->pane == PANE_RIGHT && cols >= 90;
    pw = 0;
    if (right) {
        pw = cols * 2 / 5;
        if (pw < 34) {
            pw = 34;
        }
        if (pw > 72) {
            pw = 72;
        }
    }
    lw = right ? cols - pw - 1 : cols;
    bars = 2 + (mode[0] != 0 ? 1 : 0);
    detail = 0;
    if (v->pane == PANE_BOTTOM || (v->pane == PANE_RIGHT && !right)) {
        detail = rows >= 28 ? 8 : (rows >= 24 ? 7 : (rows >= 16 ? 4 : 0));
    }
    avail = rows - bars - 2 - detail;
    if (avail < 1) {
        detail = 0;
        avail = rows - bars - 2;
    }
    if (avail < 1) {
        avail = 1;
    }
    wide = lw >= 100;
    idw = lw >= 110 ? 34 : (lw >= 80 ? 26 : (lw - 25) / 2);
    if (idw < 12) {
        idw = 12;
    }
    verw = wide ? 12 : 10;
    so = v->usedsort;
    p.l = NULL;
    p.n = 0;
    p.cap = 0;
    p.width = pw;
    h = avail + 2;
    if (right) {
        pane_build(c, v, &p);
        if (v->pscroll > p.n - h) {
            v->pscroll = p.n - h;
        }
        if (v->pscroll < 0) {
            v->pscroll = 0;
        }
    }
    v->pshown = right ? p.n - h : 0;
    lay_y0 = 3;
    lay_avail = avail;
    lay_lw = lw;
    lay_right = right;
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
    ln_pair(&l, "show ", show_names[v->show], v->show == SH_ALL ?
        CL_TEXT : CL_UPD);
    ln_pair(&l, "order ", sort_names[so], CL_SORT);
    ln_pair(&l, "game ", c->kspver ? c->kspver : "?", CL_TEXT);
    sprintf(tmp, "%d", v->nrows);
    ln_pair(&l, "shown ", tmp, CL_TEXT);
    if (nmarks > 0) {
        sprintf(tmp, "%d", nmarks);
        ln_pair(&l, "marked ", tmp, CL_ADD);
    }
    if (!v->onlycompat) {
        ln_pair(&l, "", "incompatible too", CL_UPD);
    }
    if (c->listdays >= 14) {
        ln_pair(&l, "", "list is old: press u", CL_DEL);
    }
    if (v->find[0] != 0 && mode[0] != '/') {
        ln_pair(&l, "search ", v->find, CL_KEY);
    }
    ln_put(&l, &b, 0);

    /* column headings */
    ln_start(&l, lw, 0);
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
    ln_put(&l, &b, right);
    if (right) {
        pane_line(&p, v->pscroll, &b);
    }

    /* the list */
    if (v->cur < v->top) {
        v->top = v->cur;
    }
    if (v->cur >= v->top + avail) {
        v->top = v->cur - avail + 1;
    }
    for (y = 0; y < avail; y++) {
        if (v->top + y < v->nrows) {
            draw_row(c, v, &b, v->top + y, lw, idw, verw, wide, !right);
        } else {
            ln_start(&l, lw, 0);
            if (y == 0) {
                ln_color(&l, CL_DIM);
                ln_text(&l, c->ix.ns == 0 ?
                    "   no module list yet: press u to fetch it" :
                    "   nothing here; Esc clears the search, i shows "
                    "more", -1);
            }
            ln_put(&l, &b, right);
        }
        if (right) {
            pane_line(&p, v->pscroll + 1 + y, &b);
        }
    }

    /* separator with the position in the list */
    ln_start(&l, lw, 0);
    ln_color(&l, CL_DIM);
    sprintf(tmp, " %d of %d ", v->nrows > 0 ? v->cur + 1 : 0, v->nrows);
    ln_text(&l, "----", -1);
    ln_text(&l, tmp, -1);
    while (l.vis < lw) {
        ln_text(&l, "-", -1);
    }
    ln_put(&l, &b, right);
    if (right) {
        pane_line(&p, v->pscroll + 1 + avail, &b);
    }
    pn_free(&p);
    draw_detail(c, v, &b, detail, cols);
    draw_bottom(c, v, &b, cols, mode);
    w = (long)write(1, b.p, (unsigned long)b.n);
    (void)w;
    buf_free(&b);
}

static void help_screen(int rows, int cols)
{
    static const char *keys[] = {
        "Up Down  j k", "move one line",
        "PgUp PgDn", "move a page (also ctrl-u, ctrl-d)",
        "Home End  g G", "first and last line",
        "/", "search; words are ANDed, a leading - excludes a word",
        "", "author:x  tag:x  license:x  needs:x",
        "", "dl>5k  dl<100  after:2024-01-01  before:...  size<5 (MB)",
        "", "is:installed is:chosen is:dep is:needed is:orphan is:found",
        "", "is:update",
        "", "is:new   sort:pop  sort:new  sort:small  sort:big  sort:name",
        "Esc", "clear the search, then show all again",
        "i  I", "show: all, installed, chosen by you, dependencies,",
        "", "unneeded dependencies, not installed, updates, marked",
        "s  S", "order: name, popular, newest, small, large",
        "c", "show or hide modules that do not fit your game",
        "p", "description pane: at the bottom, on the right, off",
        "J K", "scroll the right pane (also ctrl-e, ctrl-y)",
        "Space", "mark: + will be installed, x will be removed",
        "r", "mark the recommended modules of this one",
        "C", "clear all marks",
        "Enter  a", "carry out the marks (asks before it acts); what",
        "", "cannot go in stays marked, and marks are kept on quit",
        "e", "installed: make it your choice, or a dependency",
        "o  O", "import: a modlist, kspkan modlist file, .ckan modpack,",
        "", "CKAN/registry.json, or a list CKAN saved; Tab completes;",
        "", "then a: add on top, r: replace what you chose",
        "w", "export the mods you chose: a modlist name, or a path",
        "U", "upgrade everything installed",
        "u", "fetch a new module list",
        "Tab", "the games on this computer (found on their own)",
        "mouse", "wheel scrolls the list or the right pane; click",
        "", "picks; double click, right click or the mark column",
        "", "marks; the keys in the bottom bar can be clicked;",
        "", "Shift with the mouse selects text in most terminals",
        "q", "quit",
        NULL
    };
    struct buf b;
    struct line l;
    int i, nkeys, per, start, key;
    long w;

    nkeys = 0;
    while (keys[nkeys * 2] != NULL) {
        nkeys++;
    }
    per = rows - 3;
    if (per < 1) {
        per = 1;
    }
    for (start = 0; start < nkeys; start += per) {
        b.p = NULL;
        b.n = 0;
        b.cap = 0;
        buf_add(&b, "", 0);
        buf_adds(&b, "\033[H\033[0m\033[2J");
        ln_start(&l, cols, BG_BAR);
        ln_color(&l, CL_TITLE);
        ln_text(&l, " kspkan keys     ", -1);
        ln_color(&l, CL_INST);
        ln_text(&l, "i", -1);
        ln_color(&l, CL_DIM);
        ln_text(&l, " chosen by you  ", -1);
        ln_color(&l, CL_INST);
        ln_text(&l, "d", -1);
        ln_color(&l, CL_DIM);
        ln_text(&l, " a dependency  ", -1);
        ln_color(&l, CL_UPD);
        ln_text(&l, "u", -1);
        ln_color(&l, CL_DIM);
        ln_text(&l, " newer one fits  ", -1);
        ln_color(&l, CL_ADD);
        ln_text(&l, "+", -1);
        ln_color(&l, CL_DIM);
        ln_text(&l, " to install  ", -1);
        ln_color(&l, CL_DEL);
        ln_text(&l, "x", -1);
        ln_color(&l, CL_DIM);
        ln_text(&l, " to remove", -1);
        ln_put(&l, &b, 0);
        ln_start(&l, cols, 0);
        ln_put(&l, &b, 0);
        for (i = start; i < nkeys && i < start + per; i++) {
            ln_start(&l, cols, 0);
            ln_text(&l, "  ", -1);
            ln_field(&l, CL_KEY, keys[i * 2], 16, 0);
            ln_color(&l, CL_TEXT);
            ln_text(&l, keys[i * 2 + 1], -1);
            ln_put(&l, &b, 0);
        }
        ln_start(&l, cols, 0);
        ln_color(&l, CL_DIM);
        ln_text(&l, start + per < nkeys ? "  any key: more   q: back" :
            "  press any key to go back", -1);
        ln_put(&l, &b, 1);
        w = (long)write(1, b.p, (unsigned long)b.n);
        buf_free(&b);
        if (w < 0) {
            break;
        }
        key = read_key();
        if (key == 'q' || key == KEY_ESC || key < 0) {
            break;
        }
    }
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

/* the marks are kept in the game's state ("marks", one id a line),
   so that quitting, switching games, an update or a failed install
   never loses them */
static void marks_save(struct ctx *c, struct view *v)
{
    struct buf b;
    char *p;
    int i;

    if (c->state == NULL || v->marks == NULL) {
        return;
    }
    b.p = NULL;
    b.n = 0;
    b.cap = 0;
    buf_add(&b, "", 0);
    for (i = 0; i < c->ix.ns; i++) {
        if (v->marks[i]) {
            buf_adds(&b, c->ix.s[i].id);
            buf_adds(&b, "\n");
        }
    }
    p = state_path(c, "marks");
    if (b.n == 0) {
        unlink(p);
    } else {
        write_file(p, b.p, b.n);
    }
    free(p);
    buf_free(&b);
}

static void marks_load(struct ctx *c, struct view *v)
{
    struct summary *s;
    char *p, *text, *line, *nl;
    long len;

    if (c->state == NULL) {
        return;
    }
    p = state_path(c, "marks");
    text = read_file(p, &len, 1L << 22);
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
        s = summary_find(&c->ix, line);
        if (s != NULL) {
            v->marks[s - c->ix.s] = 1;
        }
        if (nl == NULL) {
            break;
        }
        line = nl + 1;
    }
    free(text);
}

static int count_marks(struct ctx *c, struct view *v)
{
    int i, n;

    n = 0;
    for (i = 0; i < c->ix.ns; i++) {
        if (v->marks[i]) {
            n++;
        }
    }
    return n;
}

/* carry out the marks.  A mark goes only once its module really is in
   (or out); the rest stay marked, so nothing chosen is lost when
   something clashes or the answer is no.  What fits is installed even
   when some marked module cannot go in. */
static void apply(struct ctx *c, struct view *v)
{
    struct strs ins, rem;
    char *wasin;
    int i, npicks, done, kept, have;

    ins.v = rem.v = NULL;
    ins.n = rem.n = ins.cap = rem.cap = 0;
    wasin = (char *)xalloc((long)c->ix.ns + 1);
    for (i = 0; i < c->ix.ns; i++) {
        wasin[i] = registry_find(&c->reg, c->ix.s[i].id) != NULL;
        if (!v->marks[i]) {
            continue;
        }
        if (wasin[i]) {
            strs_add(&rem, xstrdup(c->ix.s[i].id));
        } else {
            strs_add(&ins, xstrdup(c->ix.s[i].id));
        }
    }
    if (ins.n == 0 && rem.n == 0) {
        free(wasin);
        sprintf(v->msg, "nothing marked: Space marks a module");
        return;
    }
    marks_save(c, v);
    raw_off();
    if (rem.n > 0) {
        cmd_remove(c, rem.v, rem.n);
    }
    npicks = c->picks.n;
    if (ins.n > 0) {
        fit_wants(c, &ins);
    }
    if (ins.n > 0) {
        cmd_install(c, ins.v, ins.n);
    }
    while (c->picks.n > npicks) {
        c->picks.n--;
        free(c->picks.v[c->picks.n]);
    }
    done = kept = 0;
    for (i = 0; i < c->ix.ns; i++) {
        if (!v->marks[i]) {
            continue;
        }
        have = registry_find(&c->reg, c->ix.s[i].id) != NULL;
        if (have != wasin[i]) {
            v->marks[i] = 0;
            done++;
        } else {
            kept++;
        }
    }
    marks_save(c, v);
    if (kept > 0) {
        printf("\n%d done; %d still marked (the reasons are above); "
            "they stay marked until they go in or you clear them\n",
            done, kept);
    }
    pause_for_enter();
    strs_free(&ins);
    strs_free(&rem);
    free(wasin);
    raw_on();
    if (kept > 0) {
        sprintf(v->msg, "%d done, %d still marked: i shows marked, "
            "C clears", done, kept);
    }
}

static void do_update(struct ctx *c, struct view *v)
{
    int rc;

    marks_save(c, v);
    raw_off();
    cmd_update(c, NULL);
    memset(&c->ix, 0, sizeof(c->ix));
    rc = index_load(c);
    if (rc < 0) {
        memset(&c->ix, 0, sizeof(c->ix));
    }
    view_build(c, v);
    marks_load(c, v);
    v->nrows = 0;
    refilter(c, v);
    pause_for_enter();
    raw_on();
}

/* a mouse event in the list view: the wheel scrolls the list, or the
   right pane under the pointer; a click picks a row, a click in the
   mark column, a double click or a right click marks it; a click on a
   key in the bottom bar works as that key.  The key to act on, or 0. */
static int mouse_main(struct ctx *c, struct view *v, int rows)
{
    int idx, step;

    if (!mouse_down) {
        return 0;
    }
    if (mouse_b == 64 || mouse_b == 65) {
        step = mouse_b == 64 ? -3 : 3;
        if (lay_right && mouse_x > lay_lw + 1) {
            v->pscroll += step;
            if (v->pscroll > v->pshown) {
                v->pscroll = v->pshown;
            }
            if (v->pscroll < 0) {
                v->pscroll = 0;
            }
            return 0;
        }
        v->cur += step;
        if (v->cur >= v->nrows) {
            v->cur = v->nrows - 1;
        }
        if (v->cur < 0) {
            v->cur = 0;
        }
        return 0;
    }
    if (mouse_y == rows && mouse_b == 0) {
        return hint_at(mouse_x);
    }
    if (mouse_y == 2 && mouse_b == 0) {
        return 's';
    }
    if (mouse_y < lay_y0 || mouse_y >= lay_y0 + lay_avail ||
        (lay_right && mouse_x > lay_lw))
    {
        return 0;
    }
    idx = v->top + mouse_y - lay_y0;
    if (idx >= v->nrows || (mouse_b != 0 && mouse_b != 2)) {
        return 0;
    }
    v->cur = idx;
    if (mouse_b == 2 || mouse_x <= 3 || dbl_click(idx)) {
        v->marks[v->rows[idx]] = (char)!v->marks[v->rows[idx]];
        marks_save(c, v);
    }
    return 0;
}

/* Tab: the games on this computer, found on their own, with what is
   in each; Enter switches to the one under the cursor */
static int games_screen(struct ctx *c, int rows, int cols)
{
    struct gameinfo *gi;
    struct buf b;
    struct line l;
    char tmp[200];
    int i, n, cur, key, y, top, per, pick, rescan;
    long w;

    pick = -1;
    rescan = 1;
    gi = NULL;
    n = 0;
    cur = 0;
    top = 0;
    for (;;) {
        if (rescan) {
            for (i = 0; i < n; i++) {
                game_info_free(&gi[i]);
            }
            free(gi);
            games_autoadd(c, 1);
            n = c->inames.n;
            gi = (struct gameinfo *)xalloc((long)(n + 1) *
                (long)sizeof(*gi));
            for (i = 0; i < n; i++) {
                game_info(c, i, &gi[i]);
                if (c->instname != NULL &&
                    strcmp(c->inames.v[i], c->instname) == 0 && rescan == 1)
                {
                    cur = i;
                }
            }
            rescan = 0;
        }
        per = (rows - 4) / 3;
        if (per < 1) {
            per = 1;
        }
        if (cur < top) {
            top = cur;
        }
        if (cur >= top + per) {
            top = cur - per + 1;
        }
        b.p = NULL;
        b.n = 0;
        b.cap = 0;
        buf_add(&b, "", 0);
        buf_adds(&b, "\033[H\033[0m\033[2J");
        ln_start(&l, cols, BG_BAR);
        ln_color(&l, CL_TITLE);
        ln_text(&l, " kspkan games   ", -1);
        ln_color(&l, CL_DIM);
        sprintf(tmp, "%d found on this computer", n);
        ln_text(&l, tmp, -1);
        ln_put(&l, &b, 0);
        ln_start(&l, cols, 0);
        ln_put(&l, &b, 0);
        y = 2;
        for (i = top; i < n && i < top + per; i++) {
            ln_start(&l, cols, i == cur ? BG_SEL : 0);
            ln_color(&l, CL_INST);
            ln_text(&l, c->instname != NULL &&
                strcmp(c->instname, c->inames.v[i]) == 0 ? " * " : "   ",
                -1);
            ln_color(&l, CL_TITLE);
            ln_text(&l, c->inames.v[i], -1);
            ln_color(&l, CL_DIM);
            ln_text(&l, "  game ", -1);
            ln_color(&l, CL_TEXT);
            ln_text(&l, gi[i].version != NULL ? gi[i].version : "?", -1);
            ln_color(&l, CL_DIM);
            ln_text(&l, "  ", -1);
            ln_color(&l, CL_LINK);
            ln_text(&l, c->igames.v[i], -1);
            ln_put(&l, &b, 0);
            ln_start(&l, cols, i == cur ? BG_SEL : 0);
            ln_color(&l, CL_DIM);
            sprintf(tmp, "     %d chosen, %d dependencies, %d found; "
                "%d folders in GameData; DLC: ", gi[i].chosen, gi[i].deps,
                gi[i].found, gi[i].folders);
            ln_text(&l, tmp, -1);
            ln_color(&l, gi[i].dlcs[0] ? CL_ADD : CL_DIM);
            ln_text(&l, gi[i].dlcs[0] ? gi[i].dlcs : "none", -1);
            ln_put(&l, &b, 0);
            ln_start(&l, cols, 0);
            ln_put(&l, &b, 0);
            y += 3;
        }
        if (n == 0) {
            ln_start(&l, cols, 0);
            ln_color(&l, CL_DIM);
            ln_text(&l, "   no game found; from a shell: kspkan add NAME "
                "GAMEDIR", -1);
            ln_put(&l, &b, 0);
            y++;
        }
        for (; y < rows - 1; y++) {
            ln_start(&l, cols, 0);
            ln_put(&l, &b, 0);
        }
        ln_start(&l, cols, BG_BAR);
        ln_text(&l, " ", -1);
        hint_rec = 1;
        nhints = 0;
        key_hint(&l, "j/k", "move");
        key_hint(&l, "Enter", "use this game");
        key_hint(&l, "r", "search again");
        key_hint(&l, "Esc q", "back");
        hint_rec = 0;
        ln_put(&l, &b, 1);
        w = (long)write(1, b.p, (unsigned long)b.n);
        buf_free(&b);
        if (w < 0) {
            break;
        }
        key = read_key();
        if (key == KEY_MOUSE) {
            key = 0;
            if (mouse_down && (mouse_b == 64 || mouse_b == 65)) {
                key = mouse_b == 64 ? 'k' : 'j';
            } else if (mouse_down && mouse_b == 0 && mouse_y == rows) {
                key = hint_at(mouse_x);
            } else if (mouse_down && mouse_b == 0 && mouse_y >= 3 &&
                top + (mouse_y - 3) / 3 < n)
            {
                cur = top + (mouse_y - 3) / 3;
                key = dbl_click(cur) ? KEY_ENTER : 0;
            }
        }
        if (key < 0 || key == 'q' || key == KEY_ESC || key == 9) {
            break;
        }
        if ((key == 'j' || key == KEY_DOWN) && cur + 1 < n) {
            cur++;
        } else if ((key == 'k' || key == KEY_UP) && cur > 0) {
            cur--;
        } else if (key == 'g' || key == KEY_HOME) {
            cur = 0;
        } else if ((key == 'G' || key == KEY_END) && n > 0) {
            cur = n - 1;
        } else if (key == 'r') {
            rescan = 2;
        } else if ((key == KEY_ENTER || key == 10) && n > 0) {
            pick = cur;
            break;
        }
    }
    for (i = 0; i < n; i++) {
        game_info_free(&gi[i]);
    }
    free(gi);
    return pick;
}

/* make game i the current one: its own installed list and marks */
static void switch_game(struct ctx *c, struct view *v, int i)
{
    int found;

    if (c->instname != NULL && strcmp(c->inames.v[i], c->instname) == 0) {
        return;
    }
    marks_save(c, v);
    inst_select(c, c->inames.v[i]);
    found = c->ix.ns > 0 ? scan_run(c, 2) : 0;
    view_build(c, v);
    marks_load(c, v);
    v->nrows = 0;
    refilter(c, v);
    sprintf(v->msg, "now %.40s: %d mods installed%s", c->instname,
        c->reg.n, found > 0 ? ", some found in the game (f)" : "");
}

static void do_upgrade(struct ctx *c)
{
    raw_off();
    cmd_upgrade(c, NULL, 0);
    pause_for_enter();
    raw_on();
}

/* the pane setting, kept in "settings" next to the module list */
static void settings_load(struct ctx *c, struct view *v)
{
    char *p, *text;
    long len;
    int i;

    p = shared_path(c, "settings");
    text = read_file(p, &len, 4096);
    free(p);
    if (text == NULL) {
        return;
    }
    for (i = 0; i < 3; i++) {
        if (strstr(text, "pane ") != NULL &&
            strncmp(strstr(text, "pane ") + 5, pane_names[i],
            strlen(pane_names[i])) == 0)
        {
            v->pane = i;
        }
    }
    free(text);
}

static void settings_save(struct ctx *c, struct view *v)
{
    char line[32], *p;

    sprintf(line, "pane %s\n", pane_names[v->pane]);
    make_dirs(c->shared);
    p = shared_path(c, "settings");
    write_file(p, line, (long)strlen(line));
    free(p);
}

/* "~/x" made into a path under HOME; malloc'd */
static char *expand_home(const char *s)
{
    const char *home;
    char *p;

    home = getenv("HOME");
    if (s[0] == '~' && (s[1] == '/' || s[1] == 0) && home != NULL) {
        p = (char *)xalloc((long)(strlen(home) + strlen(s)) + 2);
        strcpy(p, home);
        strcat(p, s + 1);
        return p;
    }
    return xstrdup(s);
}

/* o: a saved modlist, a kspkan modlist file, a .ckan modpack, a CKAN
   registry.json (or the game folder holding CKAN/), or a mod list CKAN
   saved.  Then the h prompt asks how to apply it. */
static int open_list(struct ctx *c, struct view *v, const char *arg)
{
    struct stat st;
    const char *kind;
    char *path, *reg, *saved;
    int rc;

    path = expand_home(arg);
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        reg = (char *)xalloc((long)strlen(path) + 24);
        strcpy(reg, path);
        strcat(reg, "/CKAN/registry.json");
        if (access(reg, R_OK) != 0) {
            sprintf(v->msg, "%.60s is a directory; give a file in it "
                "(Tab lists them)", arg);
            free(reg);
            free(path);
            return -1;
        }
        free(path);
        path = reg;
    }
    modlist_free(&pend);
    rc = modlist_open(c, path, &pend, &saved, &kind);
    if (rc == -2) {
        sprintf(v->msg, "no mods found in %.60s", arg);
    } else if (rc < 0) {
        sprintf(v->msg, "no modlist or file %.60s", arg);
    }
    if (rc < 0) {
        free(path);
        return -1;
    }
    sprintf(pend_name, "%.60s", saved != NULL ? saved : arg);
    if (saved != NULL) {
        sprintf(v->msg, "%s, %d mods, kept as the modlist %s", kind,
            pend.n, pend_name);
    } else {
        sprintf(v->msg, "%s, %d mods", kind, pend.n);
    }
    free(saved);
    free(path);
    return 0;
}

/* mark what the opened list has and the game has not; replace: also
   mark for removal the mods you chose that the list does not have */
static void apply_list(struct ctx *c, struct view *v, int replace)
{
    struct summary *s;
    struct inst *m;
    int i, k, marked, have, unknown, gone, inlist;

    marked = have = unknown = gone = 0;
    for (i = 0; i < pend.n; i++) {
        s = summary_find(&c->ix, pend.v[i].id);
        if (s == NULL) {
            unknown++;
        } else if (registry_find(&c->reg, pend.v[i].id) != NULL) {
            have++;
            v->marks[s - c->ix.s] = 0;
        } else {
            v->marks[s - c->ix.s] = 1;
            marked++;
        }
    }
    for (i = 0; replace && i < c->reg.n; i++) {
        m = &c->reg.v[i];
        s = summary_find(&c->ix, m->id);
        if (m->autoinst || s == NULL || (strlen(m->id) > 4 &&
            strcmp(m->id + strlen(m->id) - 4, "-DLC") == 0))
        {
            continue;
        }
        inlist = 0;
        for (k = 0; k < pend.n; k++) {
            if (strcmp(pend.v[k].id, m->id) == 0) {
                inlist = 1;
            }
        }
        if (!inlist) {
            v->marks[s - c->ix.s] = 1;
            gone++;
        }
    }
    marks_save(c, v);
    sprintf(v->msg, "%.40s: + %d to install, x %d to remove, %d already "
        "in, %d unknown; Enter carries it out", pend_name, marked, gone,
        have, unknown);
    modlist_free(&pend);
    if (marked + gone > 0) {
        v->show = SH_MARKED;
        refilter(c, v);
    }
}

/* w: the modules you chose, as a saved modlist (a plain name) or a
   file (a path; a directory gets a file named after the game) */
static void save_modlist(struct ctx *c, struct view *v, const char *arg)
{
    struct stat st;
    char *path, *dir;
    int rc, count;

    if (strchr(arg, '/') == NULL && arg[0] != '~') {
        if (!name_valid(arg)) {
            sprintf(v->msg, "a name of letters, digits, _ - . please, "
                "or a path with a /");
            return;
        }
        path = xstrdup(arg);
    } else {
        path = expand_home(arg);
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            dir = path;
            path = (char *)xalloc((long)strlen(dir) + 64);
            strcpy(path, dir);
            free(dir);
            strcat(path, "/");
            strcat(path, c->instname != NULL ? c->instname : "modlist");
            strcat(path, ".kspkan");
        }
    }
    rc = modlist_write(c, path, &count);
    if (rc < 0) {
        sprintf(v->msg, "could not write %.60s", path);
    } else if (strchr(path, '/') == NULL) {
        sprintf(v->msg, "saved the %d modules you chose as the modlist "
            "%.60s", count, path);
    } else {
        sprintf(v->msg, "wrote the %d modules you chose to %.100s",
            count, path);
    }
    free(path);
}

/* ---- Tab completion of paths and modlist names ---- */

static int has_prefix(const char *s, const char *p)
{
    return strncmp(s, p, strlen(p)) == 0;
}

/* complete the last word of t (at most max chars); the choices, when
   there are several, go to v->hint */
static void complete(struct ctx *c, struct view *v, char *t, int max)
{
    struct strs cand;
    struct stat st;
    DIR *d;
    struct dirent *de;
    char *slash, *dir, *full, *pre;
    int i, n, k, isdir;
    long dl;

    cand.v = NULL;
    cand.n = 0;
    cand.cap = 0;
    v->hint[0] = 0;
    slash = strrchr(t, '/');
    pre = slash != NULL ? slash + 1 : t;
    if (slash == NULL && t[0] != '~') {
        modlist_names(c, &cand);
        k = 0;
        for (i = 0; i < cand.n; i++) {
            if (has_prefix(cand.v[i], t)) {
                cand.v[k] = cand.v[i];
                k++;
            } else {
                free(cand.v[i]);
            }
        }
        cand.n = k;
        dir = xstrdup(".");
    } else if (slash == NULL) {
        strcpy(t, "~/");
        return;
    } else {
        dl = (long)(slash - t);
        full = xstrndup(t, dl == 0 ? 1 : dl);
        dir = expand_home(full);
        free(full);
    }
    d = opendir(dir);
    while (d != NULL && (de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0
            || (de->d_name[0] == '.' && pre[0] != '.') ||
            !has_prefix(de->d_name, pre))
        {
            continue;
        }
        full = (char *)xalloc((long)(strlen(dir) +
            strlen(de->d_name)) + 4);
        strcpy(full, dir);
        strcat(full, "/");
        strcat(full, de->d_name);
        isdir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        free(full);
        full = (char *)xalloc((long)strlen(de->d_name) + 4);
        strcpy(full, de->d_name);
        if (isdir) {
            strcat(full, "/");
        }
        strs_add(&cand, full);
    }
    if (d != NULL) {
        closedir(d);
    }
    free(dir);
    if (cand.n == 0) {
        sprintf(v->hint, "nothing starts with %.60s", pre);
        strs_free(&cand);
        return;
    }
    /* the longest start that all choices share */
    n = (int)strlen(cand.v[0]);
    for (i = 1; i < cand.n; i++) {
        for (k = 0; k < n && cand.v[i][k] == cand.v[0][k]; k++) {
            continue;
        }
        n = k;
    }
    if ((int)(pre - t) + n < max) {
        memcpy(pre, cand.v[0], (unsigned long)n);
        pre[n] = 0;
    }
    if (cand.n > 1) {
        sort_strs(cand.v, cand.n, strcmp);
        for (i = 0; i < cand.n; i++) {
            if (strlen(v->hint) + strlen(cand.v[i]) + 3 >=
                sizeof(v->hint))
            {
                strcat(v->hint, "...");
                break;
            }
            strcat(v->hint, cand.v[i]);
            strcat(v->hint, "  ");
        }
    }
    strs_free(&cand);
}

/* e: a module you chose becomes a dependency, and the other way */
static void toggle_chosen(struct ctx *c, struct view *v)
{
    struct inst *m;

    if (v->nrows == 0) {
        return;
    }
    m = registry_find(&c->reg, c->ix.s[v->rows[v->cur]].id);
    if (m == NULL) {
        sprintf(v->msg, "not installed");
        return;
    }
    m->autoinst = !m->autoinst;
    if (registry_save(c) < 0) {
        sprintf(v->msg, "could not save the change");
        return;
    }
    sprintf(v->msg, "%.60s is now %s", m->id, m->autoinst ?
        "a dependency (autoremove takes it when nothing needs it)" :
        "chosen by you");
}

/* a key while a prompt (search, modlist name) is open */
static void prompt_key(struct ctx *c, struct view *v, char *mode, int key)
{
    char *t;
    int n, max;

    if (mode[0] == 'h') {
        if (key == 'a' || key == 'A') {
            apply_list(c, v, 0);
            mode[0] = 0;
        } else if (key == 'r' || key == 'R') {
            apply_list(c, v, 1);
            mode[0] = 0;
        } else if (key == KEY_ESC || key == 'q') {
            modlist_free(&pend);
            sprintf(v->msg, "left as it was; %.60s is kept as a modlist "
                "if it was imported", pend_name);
            mode[0] = 0;
        } else {
            sprintf(v->msg, "a adds on top, r replaces, Esc leaves it");
        }
        return;
    }
    t = mode[0] == '/' ? v->find : v->input;
    max = mode[0] == '/' ? 120 : 250;
    n = (int)strlen(t);
    if (key != 9) {
        v->hint[0] = 0;
    }
    if (key == KEY_ENTER || key == 10) {
        if (mode[0] == 'w' && n > 0) {
            save_modlist(c, v, t);
            mode[0] = 0;
        } else if (mode[0] == 'o' && n > 0) {
            if (open_list(c, v, t) == 0) {
                mode[0] = 'h';
            } else {
                /* stay, so that the path can be put right */
                strcpy(v->hint, v->msg);
                v->msg[0] = 0;
            }
        } else {
            mode[0] = 0;
        }
    } else if (key == 9 && t != v->find) {
        complete(c, v, t, max);
    } else if (key == KEY_ESC) {
        if (mode[0] == '/') {
            t[0] = 0;
            refilter(c, v);
        }
        mode[0] = 0;
    } else if (key == 127 || key == 8) {
        if (n > 0) {
            t[n - 1] = 0;
        }
        if (mode[0] == '/') {
            refilter(c, v);
        }
    } else if (key == 21) {
        t[0] = 0;
        if (mode[0] == '/') {
            refilter(c, v);
        }
    } else if (key == 23 && t != v->find) {
        /* ctrl-w: back to the previous / */
        while (n > 0 && t[n - 1] == '/') {
            n--;
        }
        while (n > 0 && t[n - 1] != '/') {
            n--;
        }
        t[n] = 0;
    } else if (key == KEY_UP && v->cur > 0) {
        v->cur--;
    } else if (key == KEY_DOWN && v->cur + 1 < v->nrows) {
        v->cur++;
    } else if (key >= 32 && key < 127 && n < max) {
        t[n] = (char)key;
        t[n + 1] = 0;
        if (mode[0] == '/') {
            refilter(c, v);
        }
    }
}

int cmd_tui(struct ctx *c)
{
    struct view v;
    struct winsize ws;
    char mode[4];
    int key, rows, cols, rc, page, running, i, lastrow, found;

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
    found = c->ix.ns > 0 ? scan_run(c, 2) : 0;
    tui_color = !c->mono && getenv("NO_COLOR") == NULL &&
        (getenv("TERM") == NULL || strcmp(getenv("TERM"), "dumb") != 0);
    memset(&v, 0, sizeof(v));
    v.onlycompat = 1;
    v.pane = PANE_BOTTOM;
    settings_load(c, &v);
    if (found > 0) {
        sprintf(v.msg, "found %d mod%s already in the game, marked f "
            "(I: show them all)", found, found == 1 ? "" : "s");
    }
    view_build(c, &v);
    marks_load(c, &v);
    if (count_marks(c, &v) > 0 && v.msg[0] == 0) {
        sprintf(v.msg, "%d still marked from last time: Enter carries "
            "them out, C clears them", count_marks(c, &v));
    }
    refilter(c, &v);
    mode[0] = 0;
    running = 1;
    lastrow = -1;
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
        if (page < 1) {
            page = 1;
        }
        /* a new module in the pane starts at its top */
        if (v.nrows > 0 && v.rows[v.cur] != lastrow) {
            v.pscroll = 0;
            lastrow = v.rows[v.cur];
        }
        draw(c, &v, rows, cols, mode);
        key = read_key();
        if (key < 0) {
            break;
        }
        if (key == KEY_MOUSE && (!mouse_down || mouse_b == 1)) {
            continue;
        }
        v.msg[0] = 0;
        if (key == KEY_MOUSE && mode[0] != 0) {
            if (!mouse_down || mouse_b != 0 || mouse_y != rows ||
                mode[0] != 'h')
            {
                continue;
            }
            key = hint_at(mouse_x);
        } else if (key == KEY_MOUSE) {
            key = mouse_main(c, &v, rows);
            if (key == 0) {
                continue;
            }
        }
        if (mode[0] != 0) {
            prompt_key(c, &v, mode, key);
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
        } else if (key == 'J' || key == 5) {
            if (v.pscroll < v.pshown) {
                v.pscroll++;
            }
        } else if (key == 'K' || key == 25) {
            if (v.pscroll > 0) {
                v.pscroll--;
            }
        } else if (key == '/') {
            strcpy(mode, "/");
        } else if (key == 'w' || key == 'W') {
            strcpy(mode, "w");
            v.input[0] = 0;
            v.hint[0] = 0;
            if (c->instname != NULL) {
                strncpy(v.input, c->instname, sizeof(v.input) - 1);
                v.input[sizeof(v.input) - 1] = 0;
            }
        } else if (key == 'o' || key == 'O') {
            strcpy(mode, "o");
            v.input[0] = 0;
            v.hint[0] = 0;
        } else if (key == KEY_ESC && v.find[0] != 0) {
            v.find[0] = 0;
            refilter(c, &v);
        } else if (key == KEY_ESC && v.show != SH_ALL) {
            v.show = SH_ALL;
            refilter(c, &v);
        } else if (key == 's') {
            v.sort = (v.sort + 1) % SORT_KINDS;
            refilter(c, &v);
            v.cur = 0;
            v.top = 0;
        } else if (key == 'S') {
            v.sort = (v.sort + SORT_KINDS - 1) % SORT_KINDS;
            refilter(c, &v);
            v.cur = 0;
            v.top = 0;
        } else if (key == 'i') {
            v.show = (v.show + 1) % SH_KINDS;
            refilter(c, &v);
        } else if (key == 'I') {
            v.show = (v.show + SH_KINDS - 1) % SH_KINDS;
            refilter(c, &v);
        } else if (key == 'c') {
            v.onlycompat = !v.onlycompat;
            refilter(c, &v);
        } else if (key == 'p') {
            v.pane = (v.pane + 1) % 3;
            if (v.pane == PANE_RIGHT && cols < 90) {
                sprintf(v.msg, "the right pane needs 90 columns; "
                    "this is %d", cols);
            }
            settings_save(c, &v);
        } else if (key == 'e') {
            toggle_chosen(c, &v);
            refilter(c, &v);
        } else if (key == '?') {
            help_screen(rows, cols);
        } else if (key == ' ' && v.nrows > 0) {
            i = v.rows[v.cur];
            v.marks[i] = (char)!v.marks[i];
            marks_save(c, &v);
            if (v.cur + 1 < v.nrows) {
                v.cur++;
            }
        } else if (key == 'C') {
            memset(v.marks, 0, (unsigned long)c->ix.ns + 1);
            marks_save(c, &v);
            if (v.show == SH_MARKED) {
                refilter(c, &v);
            }
        } else if (key == KEY_ENTER || key == 10 || key == 'a') {
            apply(c, &v);
            refilter(c, &v);
        } else if (key == 'r') {
            sprintf(v.msg, "%d recommended modules marked",
                mark_recommended(c, &v));
            marks_save(c, &v);
        } else if (key == 'U') {
            do_upgrade(c);
            refilter(c, &v);
        } else if (key == 9) {
            i = games_screen(c, rows, cols);
            if (i >= 0) {
                switch_game(c, &v, i);
            }
        } else if (key == 'u') {
            do_update(c, &v);
        }
    }
    raw_off();
    return 0;
}
