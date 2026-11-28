#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "kspkan.h"

#define MAX_STEPS 20000L
#define MAX_ALTS 64
#define MAX_CANDS 256

struct req {
    const char *name;
    const char *exact;
    const char *minv;
    const char *maxv;
    const struct jnode *anyof;
    const char *from;
};

struct goal {
    struct req r;
    struct goal *next;
};

struct solver {
    struct ctx *c;
    struct entry **chosen;
    int nchosen;
    int cap;
    long steps;
    char *err;
    int errsz;
    char **want;
    int nwant;
    char choices[200];
    char clash[240];
};

static void set_err(struct solver *s, const char *a, const char *b,
    const char *cc, const char *d)
{
    int n;

    if (s->err[0] != 0) {
        return;
    }
    s->err[0] = 0;
    n = 0;
    while (*a && n < s->errsz - 1) {
        s->err[n] = *a;
        n++;
        a++;
    }
    while (*b && n < s->errsz - 1) {
        s->err[n] = *b;
        n++;
        b++;
    }
    while (*cc && n < s->errsz - 1) {
        s->err[n] = *cc;
        n++;
        cc++;
    }
    while (*d && n < s->errsz - 1) {
        s->err[n] = *d;
        n++;
        d++;
    }
    s->err[n] = 0;
}

static int has_provide(const struct entry *e, const char *name)
{
    const char *p;
    long n;

    n = (long)strlen(name);
    p = e->provides;
    while (*p) {
        if (strncmp(p, name, (unsigned long)n) == 0 &&
            (p[n] == ',' || p[n] == 0))
        {
            return 1;
        }
        while (*p && *p != ',') {
            p++;
        }
        if (*p == ',') {
            p++;
        }
    }
    return 0;
}

static int version_ok(const char *ver, const struct req *r)
{
    if (r->exact != NULL && version_compare(ver, r->exact) != 0) {
        return 0;
    }
    if (r->minv != NULL && version_compare(ver, r->minv) < 0) {
        return 0;
    }
    if (r->maxv != NULL && version_compare(ver, r->maxv) > 0) {
        return 0;
    }
    return 1;
}

/* does module m answer to the name in r? */
static int answers(const struct entry *m, const struct req *r)
{
    if (r->name == NULL) {
        return 0;
    }
    if (strcmp(m->id, r->name) == 0) {
        return version_ok(m->version, r);
    }
    return has_provide(m, r->name);
}

static int alts_of(const struct req *r, struct req *out, int max)
{
    const struct jnode *c;
    int n;

    if (r->anyof == NULL) {
        out[0] = *r;
        return 1;
    }
    n = 0;
    for (c = r->anyof->child; c != NULL && n < max; c = c->next) {
        if (c->type != J_OBJ) {
            continue;
        }
        out[n].name = json_str(c, "name");
        out[n].exact = json_str(c, "version");
        out[n].minv = json_str(c, "min_version");
        out[n].maxv = json_str(c, "max_version");
        out[n].anyof = NULL;
        out[n].from = r->from;
        n++;
    }
    return n;
}

static struct goal *goal_new(const struct jnode *el, const char *from,
    struct goal *next)
{
    struct goal *g;

    g = (struct goal *)xalloc((long)sizeof(*g));
    g->r.name = json_str(el, "name");
    g->r.exact = json_str(el, "version");
    g->r.minv = json_str(el, "min_version");
    g->r.maxv = json_str(el, "max_version");
    g->r.anyof = NULL;
    if (json_get(el, "any_of") != NULL) {
        g->r.anyof = json_get(el, "any_of");
    }
    g->r.from = from;
    g->next = next;
    return g;
}

/* goals for the depends of module e, placed before rest */
static struct goal *depends_goals(struct solver *s, struct entry *e,
    struct goal *rest)
{
    struct jnode *j, *d, *el;
    struct goal *head, *tail, *g;

    j = entry_json(s->c, e);
    d = json_get(j, "depends");
    if (d == NULL || d->type != J_ARR) {
        return rest;
    }
    head = NULL;
    tail = NULL;
    for (el = d->child; el != NULL; el = el->next) {
        if (el->type != J_OBJ) {
            continue;
        }
        g = goal_new(el, e->id, NULL);
        if (tail == NULL) {
            head = g;
        } else {
            tail->next = g;
        }
        tail = g;
    }
    if (tail == NULL) {
        return rest;
    }
    tail->next = rest;
    return head;
}

static void goals_free(struct goal *g, struct goal *stop)
{
    struct goal *n;

    while (g != NULL && g != stop) {
        n = g->next;
        free(g);
        g = n;
    }
}

static int satisfied(struct solver *s, const struct req *alt)
{
    int i;

    for (i = 0; i < s->nchosen; i++) {
        if (answers(s->chosen[i], alt)) {
            return 1;
        }
    }
    return 0;
}

static int id_chosen(struct solver *s, const char *id)
{
    int i;

    for (i = 0; i < s->nchosen; i++) {
        if (strcmp(s->chosen[i]->id, id) == 0) {
            return 1;
        }
    }
    return 0;
}

/* is id one of the modules asked for (as id or id=version), or one
   picked on the terminal for a name several modules provide? */
static int conflicts_chosen(struct solver *s, struct entry *cand);

static int asked_for(struct solver *s, const char *id)
{
    long n;
    int i;

    for (i = 0; i < s->c->picks.n; i++) {
        if (strcmp(s->c->picks.v[i], id) == 0) {
            return 1;
        }
    }
    n = (long)strlen(id);
    for (i = 0; i < s->nwant; i++) {
        if (strncmp(s->want[i], id, (unsigned long)n) == 0 &&
            (s->want[i][n] == 0 || s->want[i][n] == '='))
        {
            return 1;
        }
    }
    return 0;
}

/* the usable versions of one module for alt, newest first, added to
   out from n on; the new count */
static int usable(struct solver *s, const struct req *alt, const char *id,
    struct entry **out, int n)
{
    struct ctx *c;
    struct entry *e;
    int cnt, i;

    c = s->c;
    e = index_find(&c->ix, id, &cnt);
    if (e == NULL || id_chosen(s, e->id)) {
        return n;
    }
    for (i = 0; i < cnt && n < MAX_CANDS; i++) {
        if (strcmp(e[i].kind, "dlc") == 0) {
            continue;
        }
        if (!c->testing && alt->exact == NULL &&
            strcmp(e[i].status, "stable") != 0)
        {
            continue;
        }
        if (!entry_compat(c, &e[i])) {
            continue;
        }
        if (strcmp(e[i].id, alt->name) == 0 &&
            !version_ok(e[i].version, alt))
        {
            continue;
        }
        if (strcmp(e[i].id, alt->name) != 0 &&
            !has_provide(&e[i], alt->name))
        {
            continue;
        }
        out[n] = &e[i];
        n++;
    }
    return n;
}

/* several modules can answer alt: ask which on the terminal (only
   when one is there and -y was not given).  The index of the one
   picked, or -1. */
static int ask_provider(struct solver *s, const struct req *alt,
    char **ids, int n)
{
    struct summary *sm;
    char line[32];
    char *p, *no;
    int k, v;

    if (s->c->assumeyes || !isatty(0) || !isatty(2)) {
        return -1;
    }
    /* asked already and answered none: not again while backtracking */
    for (k = 0; k < s->c->picks.n; k++) {
        if (s->c->picks.v[k][0] == '!' &&
            strcmp(s->c->picks.v[k] + 1, alt->name) == 0)
        {
            return -1;
        }
    }
    fprintf(stderr, "%s%s%s%s can come from any of these:\n", alt->name,
        alt->from != NULL ? ", needed by " : "",
        alt->from != NULL ? alt->from : "", alt->from != NULL ? "," : "");
    for (k = 0; k < n; k++) {
        sm = summary_find(&s->c->ix, ids[k]);
        fprintf(stderr, "  %2d  %-40s  %.50s\n", k + 1, ids[k],
            sm != NULL && sm->abstract != NULL ? sm->abstract : "");
    }
    fprintf(stderr, "which one? [1-%d, Enter for none] ", n);
    fflush(stderr);
    p = fgets(line, sizeof(line), stdin);
    v = 0;
    for (k = 0; p != NULL && is_digit(line[k]) && k < 6; k++) {
        v = v * 10 + (line[k] - '0');
    }
    if (v < 1 || v > n) {
        no = (char *)xalloc((long)strlen(alt->name) + 2);
        strcpy(no, "!");
        strcat(no, alt->name);
        strs_add(&s->c->picks, no);
        return -1;
    }
    strs_add(&s->c->picks, xstrdup(ids[v - 1]));
    return v - 1;
}

/* candidates answering alt, newest first.  Returns the count.  A
   virtual name that several modules can answer in this game sets
   *ambig and lists them in s->choices, unless just one of them is
   among the modules asked for */
static int candidates(struct solver *s, const struct req *alt,
    struct entry **out, int *ambig)
{
    struct ctx *c;
    struct entry *e;
    char *ids[16];
    char *line, *tab;
    long nl;
    int cnt, n, i, k, nids, same, nuse, asked, m, nv, ok, hadclash;

    c = s->c;
    *ambig = 0;
    e = index_find(&c->ix, alt->name, &cnt);
    if (e != NULL) {
        return usable(s, alt, e->id, out, 0);
    }
    nids = 0;
    nl = (long)strlen(alt->name);
    for (i = 0; i < c->ix.np; i++) {
        line = c->ix.prov[i];
        if (strncmp(line, alt->name, (unsigned long)nl) != 0 ||
            line[nl] != '\t')
        {
            continue;
        }
        tab = line + nl + 1;
        same = 0;
        for (k = 0; k < nids; k++) {
            if (strcmp(ids[k], tab) == 0) {
                same = 1;
            }
        }
        if (!same && nids < 16) {
            ids[nids] = tab;
            nids++;
        }
    }
    /* keep the modules that have a usable version; of those, the ones
       asked for by name win */
    nuse = 0;
    asked = 0;
    for (k = 0; k < nids; k++) {
        nv = usable(s, alt, ids[k], out, 0);
        /* one that clashes with what is chosen already is no choice */
        hadclash = s->clash[0] != 0;
        ok = 0;
        for (i = 0; i < nv; i++) {
            if (!conflicts_chosen(s, out[i])) {
                ok = 1;
            }
        }
        if (!hadclash) {
            s->clash[0] = 0;
        }
        if (!ok) {
            continue;
        }
        ids[nuse] = ids[k];
        nuse++;
        if (asked_for(s, ids[k])) {
            asked++;
        }
    }
    if (asked > 0) {
        m = 0;
        for (k = 0; k < nuse; k++) {
            if (asked_for(s, ids[k])) {
                ids[m] = ids[k];
                m++;
            }
        }
        nuse = m;
    }
    if (nuse > 1) {
        k = ask_provider(s, alt, ids, nuse);
        if (k >= 0) {
            return usable(s, alt, ids[k], out, 0);
        }
        *ambig = 1;
        s->choices[0] = 0;
        for (k = 0; k < nuse; k++) {
            if (strlen(s->choices) + strlen(ids[k]) + 3 <
                sizeof(s->choices))
            {
                strcat(s->choices, k > 0 ? ", " : "");
                strcat(s->choices, ids[k]);
            }
        }
        return 0;
    }
    n = 0;
    for (k = 0; k < nuse; k++) {
        n = usable(s, alt, ids[k], out, n);
    }
    return n;
}

/* does the conflicts list of a name any module in s->chosen? */
static int conflicts_of(struct solver *s, struct entry *a, struct entry *b)
{
    struct jnode *j, *arr, *el;
    struct req r;

    if (strcmp(a->id, b->id) == 0) {
        return 0;
    }
    j = entry_json(s->c, a);
    arr = json_get(j, "conflicts");
    if (arr == NULL || arr->type != J_ARR) {
        return 0;
    }
    for (el = arr->child; el != NULL; el = el->next) {
        r.name = json_str(el, "name");
        r.exact = json_str(el, "version");
        r.minv = json_str(el, "min_version");
        r.maxv = json_str(el, "max_version");
        r.anyof = NULL;
        r.from = NULL;
        if (answers(b, &r)) {
            return 1;
        }
    }
    return 0;
}

static int conflicts_chosen(struct solver *s, struct entry *cand)
{
    int i;

    for (i = 0; i < s->nchosen; i++) {
        if (conflicts_of(s, cand, s->chosen[i]) ||
            conflicts_of(s, s->chosen[i], cand))
        {
            if (s->clash[0] == 0) {
                sprintf(s->clash, ": %.60s %.30s conflicts with %.60s "
                    "%.30s", cand->id, cand->version, s->chosen[i]->id,
                    s->chosen[i]->version);
            }
            return 1;
        }
    }
    return 0;
}

static int solve(struct solver *s, struct goal *g)
{
    struct req alts[MAX_ALTS];
    struct entry *cand[MAX_CANDS];
    struct goal *ng;
    int nalt, a, nc, i, ok, ambig, any;

    if (g == NULL) {
        return 1;
    }
    s->steps++;
    if (s->steps > MAX_STEPS) {
        set_err(s, "the dependencies are too tangled to solve", "", "", "");
        return 0;
    }
    nalt = alts_of(&g->r, alts, MAX_ALTS);
    if (nalt == 0) {
        alts[0].name = NULL;
    }
    for (a = 0; a < nalt; a++) {
        if (alts[a].name != NULL && satisfied(s, &alts[a])) {
            return solve(s, g->next);
        }
    }
    any = 0;
    for (a = 0; a < nalt; a++) {
        if (alts[a].name == NULL) {
            continue;
        }
        nc = candidates(s, &alts[a], cand, &ambig);
        if (ambig) {
            set_err(s, alts[a].name, " is provided by several modules (",
                s->choices, "); name the one you want with it, or run "
                "this on a terminal to be asked");
            continue;
        }
        for (i = 0; i < nc; i++) {
            any = 1;
            if (conflicts_chosen(s, cand[i])) {
                continue;
            }
            if (s->nchosen >= s->cap) {
                set_err(s, "too many modules", "", "", "");
                return 0;
            }
            s->chosen[s->nchosen] = cand[i];
            s->nchosen++;
            ng = depends_goals(s, cand[i], g->next);
            ok = solve(s, ng);
            goals_free(ng, g->next);
            if (ok) {
                return 1;
            }
            s->nchosen--;
        }
    }
    if (!any) {
        set_err(s, "nothing compatible with this game provides ",
            alts[0].name ? alts[0].name : "?",
            g->r.from ? ", needed by " : "", g->r.from ? g->r.from : "");
    } else {
        set_err(s, "conflict while choosing ",
            alts[0].name ? alts[0].name : "?", s->clash, "");
    }
    return 0;
}

static int in_list(char **v, int n, const char *id)
{
    int i;

    for (i = 0; i < n; i++) {
        if (strcmp(v[i], id) == 0) {
            return 1;
        }
    }
    return 0;
}

/* is the DLC directory present? */
static int dlc_present(struct ctx *c, const char *id)
{
    struct stat st;
    char *p, *base;
    long n;
    int rc;

    n = (long)strlen(id);
    if (n < 5 || strcmp(id + n - 4, "-DLC") != 0) {
        return 0;
    }
    base = xstrndup(id, n - 4);
    p = (char *)xalloc((long)(strlen(c->game) + n) + 32);
    strcpy(p, c->game);
    strcat(p, "/GameData/SquadExpansion/");
    strcat(p, base);
    rc = stat(p, &st);
    free(p);
    free(base);
    return rc == 0 && S_ISDIR(st.st_mode);
}

/* want: "id" or "id=version".  skip: installed ids to treat as absent */
int resolve(struct ctx *c, char **want, int nwant, char **skip, int nskip,
    struct entry ***out, int *nout, char *err, int errsz)
{
    struct solver s;
    struct goal *goals, *g;
    struct entry *e, *fx;
    struct entry **res;
    char *eq;
    int i, k, cnt, nfixed, ok, nlocal;
    struct entry *locals;

    err[0] = 0;
    s.c = c;
    s.cap = c->reg.n + c->ix.n / 100 + 600;
    s.chosen = (struct entry **)xalloc((long)s.cap *
        (long)sizeof(struct entry *));
    s.nchosen = 0;
    s.steps = 0;
    s.err = err;
    s.errsz = errsz;
    s.want = want;
    s.nwant = nwant;
    s.choices[0] = 0;
    s.clash[0] = 0;
    locals = (struct entry *)xalloc((long)(c->reg.n + 1) *
        (long)sizeof(struct entry));
    nlocal = 0;
    for (i = 0; i < c->reg.n; i++) {
        if (in_list(skip, nskip, c->reg.v[i].id)) {
            continue;
        }
        fx = NULL;
        e = index_find(&c->ix, c->reg.v[i].id, &cnt);
        for (k = 0; e != NULL && k < cnt; k++) {
            if (strcmp(e[k].version, c->reg.v[i].version) == 0) {
                fx = &e[k];
            }
        }
        if (fx == NULL) {
            fx = &locals[nlocal];
            nlocal++;
            memset(fx, 0, sizeof(*fx));
            fx->id = c->reg.v[i].id;
            fx->version = c->reg.v[i].version;
            fx->file = fx->kexact = fx->kmin = fx->kmax = "";
            fx->kind = "package";
            fx->status = "stable";
            fx->provides = "";
            fx->strict = "";
        }
        s.chosen[s.nchosen] = fx;
        s.nchosen++;
    }
    for (i = 0; i < c->ix.n; i++) {
        e = &c->ix.e[i];
        if (strcmp(e->kind, "dlc") == 0 && dlc_present(c, e->id) &&
            !id_chosen(&s, e->id))
        {
            s.chosen[s.nchosen] = e;
            s.nchosen++;
        }
    }
    nfixed = s.nchosen;
    goals = NULL;
    for (i = nwant - 1; i >= 0; i--) {
        g = (struct goal *)xalloc((long)sizeof(*g));
        memset(g, 0, sizeof(*g));
        eq = strchr(want[i], '=');
        if (eq != NULL) {
            g->r.name = xstrndup(want[i], (long)(eq - want[i]));
            g->r.exact = eq + 1;
        } else {
            g->r.name = want[i];
        }
        g->r.from = NULL;
        g->next = goals;
        goals = g;
    }
    for (i = 0; i < nfixed; i++) {
        if (strcmp(s.chosen[i]->file, "") != 0) {
            goals = depends_goals(&s, s.chosen[i], goals);
        }
    }
    ok = solve(&s, goals);
    res = NULL;
    *nout = 0;
    if (ok) {
        res = (struct entry **)xalloc((long)(s.nchosen - nfixed + 1) *
            (long)sizeof(struct entry *));
        for (i = nfixed; i < s.nchosen; i++) {
            res[i - nfixed] = s.chosen[i];
        }
        *nout = s.nchosen - nfixed;
    } else if (err[0] == 0) {
        set_err(&s, "cannot satisfy the request", "", "", "");
    }
    *out = res;
    free(s.chosen);
    free(locals);
    return ok ? 0 : -1;
}

/* ---- removal: dependents and orphaned dependencies ---- */

static void set_msg_plain(char *err, int sz, const char *a, const char *b)
{
    int n;

    n = 0;
    while (*a && n < sz - 1) {
        err[n] = *a;
        n++;
        a++;
    }
    while (*b && n < sz - 1) {
        err[n] = *b;
        n++;
        b++;
    }
    err[n] = 0;
}

static int depends_on(struct ctx *c, struct entry *me, struct entry *target)
{
    struct jnode *j, *d, *el, *alt;
    struct req r;

    j = entry_json(c, me);
    d = json_get(j, "depends");
    if (d == NULL || d->type != J_ARR) {
        return 0;
    }
    for (el = d->child; el != NULL; el = el->next) {
        if (json_get(el, "any_of") != NULL) {
            for (alt = json_get(el, "any_of")->child; alt != NULL;
                alt = alt->next)
            {
                r.name = json_str(alt, "name");
                r.exact = r.minv = r.maxv = NULL;
                r.anyof = NULL;
                r.from = NULL;
                if (r.name != NULL && answers(target, &r)) {
                    return 1;
                }
            }
        } else {
            r.name = json_str(el, "name");
            r.exact = r.minv = r.maxv = NULL;
            r.anyof = NULL;
            r.from = NULL;
            if (r.name != NULL && answers(target, &r)) {
                return 1;
            }
        }
    }
    return 0;
}

static struct entry *reg_entry(struct ctx *c, struct inst *m)
{
    struct entry *e;
    int cnt, k;

    e = index_find(&c->ix, m->id, &cnt);
    for (k = 0; e != NULL && k < cnt; k++) {
        if (strcmp(e[k].version, m->version) == 0) {
            return &e[k];
        }
    }
    /* a version no longer in the list: what the newest needs, so what
       it depends on is not taken as unneeded (autoremove) */
    return cnt > 0 ? e : NULL;
}

/* ids to remove: those asked for, installed modules that need them, and
   automatically installed modules that no module you chose still needs */
int plan_removal(struct ctx *c, char **ids, int n, struct strs *out,
    char *err, int errsz)
{
    struct entry *me, *te;
    char *gone, *reach;
    int i, k, changed, total;

    err[0] = 0;
    total = c->reg.n;
    gone = (char *)xalloc((long)total + 1);
    reach = (char *)xalloc((long)total + 1);
    memset(gone, 0, (unsigned long)total + 1);
    for (i = 0; i < n; i++) {
        for (k = 0; k < total && strcmp(c->reg.v[k].id, ids[i]) != 0; k++) {
            continue;
        }
        if (k == total) {
            set_msg_plain(err, errsz, "not installed: ", ids[i]);
            free(gone);
            free(reach);
            return -1;
        }
        gone[k] = 1;
    }
    changed = 1;
    while (changed) {
        changed = 0;
        for (i = 0; i < total; i++) {
            if (gone[i]) {
                continue;
            }
            me = reg_entry(c, &c->reg.v[i]);
            for (k = 0; k < total && me != NULL; k++) {
                if (!gone[k] || k == i) {
                    continue;
                }
                te = reg_entry(c, &c->reg.v[k]);
                if (te != NULL && depends_on(c, me, te)) {
                    gone[i] = 1;
                    changed = 1;
                    break;
                }
            }
        }
    }
    for (i = 0; i < total; i++) {
        reach[i] = (char)(!gone[i] && !c->reg.v[i].autoinst);
    }
    changed = 1;
    while (changed) {
        changed = 0;
        for (i = 0; i < total; i++) {
            if (!reach[i]) {
                continue;
            }
            me = reg_entry(c, &c->reg.v[i]);
            for (k = 0; k < total && me != NULL; k++) {
                if (gone[k] || reach[k] || k == i) {
                    continue;
                }
                te = reg_entry(c, &c->reg.v[k]);
                if (te != NULL && depends_on(c, me, te)) {
                    reach[k] = 1;
                    changed = 1;
                }
            }
        }
    }
    for (i = 0; i < total; i++) {
        if (!gone[i] && !reach[i]) {
            gone[i] = 1;
        }
    }
    for (i = 0; i < total; i++) {
        if (gone[i]) {
            strs_add(out, xstrdup(c->reg.v[i].id));
        }
    }
    free(gone);
    free(reach);
    return 0;
}

/* newest release of a real module that fits the game, or NULL */
static struct entry *newest_fit(struct ctx *c, const char *id)
{
    struct entry *e;
    int cnt, k;

    e = index_find(&c->ix, id, &cnt);
    for (k = 0; e != NULL && k < cnt; k++) {
        if (strcmp(e[k].kind, "dlc") == 0) {
            continue;
        }
        if ((c->testing || strcmp(e[k].status, "stable") == 0) &&
            entry_compat(c, &e[k]))
        {
            return &e[k];
        }
    }
    return NULL;
}

static int in_strs(const struct strs *s, const char *id)
{
    int i;

    for (i = 0; i < s->n; i++) {
        if (strcmp(s->v[i], id) == 0) {
            return 1;
        }
    }
    return 0;
}

/* identifiers named by the "recommends" or "suggests" lists of the given
   modules that can be installed here and are neither installed nor
   already in the plan.  An any_of group offers its first usable name. */
void collect_extras(struct ctx *c, struct entry **ents, int n,
    const char *key, struct strs *out)
{
    struct jnode *arr, *el, *alt;
    const char *nm;
    int i, k, planned;

    for (i = 0; i < n; i++) {
        arr = json_get(entry_json(c, ents[i]), key);
        if (arr == NULL || arr->type != J_ARR) {
            continue;
        }
        for (el = arr->child; el != NULL; el = el->next) {
            nm = json_str(el, "name");
            alt = json_get(el, "any_of");
            if (nm == NULL && alt != NULL) {
                for (alt = alt->child; alt != NULL; alt = alt->next) {
                    nm = json_str(alt, "name");
                    if (nm != NULL && newest_fit(c, nm) != NULL) {
                        break;
                    }
                    nm = NULL;
                }
            }
            if (nm == NULL || registry_find(&c->reg, nm) != NULL ||
                in_strs(out, nm) || newest_fit(c, nm) == NULL)
            {
                continue;
            }
            planned = 0;
            for (k = 0; k < n; k++) {
                if (strcmp(ents[k]->id, nm) == 0) {
                    planned = 1;
                }
            }
            if (!planned) {
                strs_add(out, xstrdup(nm));
            }
        }
    }
}

/* installed modules that depend on the installed module id; their ids
   go into out when it is not NULL.  Returns how many. */
int needed_by(struct ctx *c, const char *id, struct strs *out)
{
    struct inst *m;
    struct entry *te, *me;
    int i, n;

    m = registry_find(&c->reg, id);
    if (m == NULL || c->ix.n == 0) {
        return 0;
    }
    te = reg_entry(c, m);
    if (te == NULL) {
        return 0;
    }
    n = 0;
    for (i = 0; i < c->reg.n; i++) {
        if (strcmp(c->reg.v[i].id, id) == 0) {
            continue;
        }
        me = reg_entry(c, &c->reg.v[i]);
        if (me != NULL && depends_on(c, me, te)) {
            if (out != NULL) {
                strs_add(out, xstrdup(c->reg.v[i].id));
            }
            n++;
        }
    }
    return n;
}

/* a dependency that no installed module needs any more */
int is_orphan(struct ctx *c, const char *id)
{
    struct inst *m;

    m = registry_find(&c->reg, id);
    return m != NULL && m->autoinst && needed_by(c, id, NULL) == 0;
}
