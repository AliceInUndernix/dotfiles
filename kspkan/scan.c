#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "kspkan.h"

/* Finding the mods that are already in the game, put there by CKAN or
   by hand, and keeping them in the registry marked "found":

     1. CKAN/registry.json, when CKAN looked after this game: each mod
        CKAN installed, its version, whether it was a dependency, and
        its files, as long as they are still there.
     2. The rest of GameData against the module list: a module whose
        install stanzas would have made directories or files that are
        all there.  Its version comes from a file name with the
        version in it (ModuleManager.4.2.3.dll) or a KSP-AVC .version
        file; else it is not known ("0") and upgrade puts the newest
        in.  Where several modules would make the same directory, the
        one that fits the game, then the most downloaded, is taken,
        and the others are named.
     3. What is left: the DLCs, and directories CKAN does not know
        (Principia, say), which are only reported and left alone.

   Found files are only recorded, never moved.  The scan runs by hand
   (kspkan scan) and on its own when GameData or CKAN/registry.json has
   changed since the last one (the "scanned" stamp in the state
   directory). */

#define SC_MAXT 8
#define SC_DEPTH 24

struct cand {
    struct summary *s;
    struct entry *e;
    char *t[SC_MAXT];
    int nt;
    char *ver;
    int dropped;
};

static char *cat3(const char *a, const char *b, const char *cc)
{
    char *p;

    p = (char *)xalloc((long)(strlen(a) + strlen(b) + strlen(cc)) + 1);
    strcpy(p, a);
    strcat(p, b);
    strcat(p, cc);
    return p;
}

/* 1 directory, 2 file, 0 neither (symbolic links are neither) */
static int kind_of(struct ctx *c, const char *rel)
{
    struct stat st;
    char *p;
    int rc;

    p = cat3(c->game, "/", rel);
    rc = lstat(p, &st);
    free(p);
    if (rc < 0) {
        return 0;
    }
    if (S_ISDIR(st.st_mode)) {
        return 1;
    }
    return S_ISREG(st.st_mode) ? 2 : 0;
}

/* does any installed module have rel, or a file under it? */
static int owned(struct ctx *c, const char *rel)
{
    long n;
    int i, k;
    char *f;

    n = (long)strlen(rel);
    for (i = 0; i < c->reg.n; i++) {
        for (k = 0; k < c->reg.v[i].files.n; k++) {
            f = c->reg.v[i].files.v[k];
            if (strncmp(f, rel, (unsigned long)n) == 0 &&
                (f[n] == 0 || f[n] == '/'))
            {
                return 1;
            }
        }
    }
    return 0;
}

/* the files under the game-relative directory rel, game-relative */
static void walk(struct ctx *c, const char *rel, struct strs *out,
    int depth)
{
    DIR *d;
    struct dirent *de;
    char *p, *sub;
    int k;

    if (depth > SC_DEPTH) {
        return;
    }
    p = cat3(c->game, "/", rel);
    d = opendir(p);
    free(p);
    if (d == NULL) {
        return;
    }
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        sub = cat3(rel, "/", de->d_name);
        k = kind_of(c, sub);
        if (k == 1) {
            walk(c, sub, out, depth + 1);
            free(sub);
        } else if (k == 2) {
            strs_add(out, sub);
        } else {
            free(sub);
        }
    }
    closedir(d);
}

/* ---- 1. CKAN/registry.json ---- */

static int from_ckan(struct ctx *c, struct strs *said)
{
    struct jnode *root, *im, *m, *src, *ai, *files, *f;
    struct inst *in;
    struct strs keep;
    const char *ver;
    char *p, *text;
    long len;
    int n;

    p = cat3(c->game, "/", "CKAN/registry.json");
    text = read_file(p, &len, 1L << 27);
    free(p);
    if (text == NULL) {
        return 0;
    }
    root = json_parse(text, len);
    free(text);
    im = json_get(root, "installed_modules");
    n = 0;
    for (m = im != NULL ? im->child : NULL; m != NULL; m = m->next) {
        if (m->key == NULL || registry_find(&c->reg, m->key) != NULL) {
            continue;
        }
        src = json_get(m, "source_module");
        ver = json_str(src, "version");
        if (ver == NULL) {
            ver = "0";
        }
        keep.v = NULL;
        keep.n = 0;
        keep.cap = 0;
        files = json_get(m, "installed_files");
        for (f = files != NULL ? files->child : NULL; f != NULL;
            f = f->next)
        {
            if (f->key != NULL && path_is_safe(f->key) &&
                kind_of(c, f->key) == 2 &&
                registry_owner(&c->reg, f->key) == NULL)
            {
                strs_add(&keep, xstrdup(f->key));
            }
        }
        if (keep.n == 0) {
            strs_free(&keep);
            continue;
        }
        ai = json_get(m, "auto_installed");
        in = registry_add(&c->reg, m->key, ver, ai != NULL &&
            ai->type == J_BOOL && strcmp(ai->str, "true") == 0);
        in->found = 1;
        in->files = keep;
        strs_add(said, cat3(m->key, " ", ver));
        n++;
    }
    json_free(root);
    return n;
}

/* ---- 2. GameData against the module list ---- */

static const char *base_of(const char *s)
{
    const char *b;

    b = strrchr(s, '/');
    return b != NULL ? b + 1 : s;
}

/* the first entry of the game-relative directory dir that pat
   matches, malloc'd; NULL if none */
static char *rx_entry(struct ctx *c, const char *dir, const char *pat)
{
    DIR *d;
    struct dirent *de;
    char *p, *r;

    p = cat3(c->game, "/", dir);
    d = opendir(p);
    free(p);
    if (d == NULL) {
        return NULL;
    }
    r = NULL;
    while (r == NULL && (de = readdir(d)) != NULL) {
        if (de->d_name[0] != '.' && rx_match(pat, de->d_name)) {
            r = xstrdup(de->d_name);
        }
    }
    closedir(d);
    return r;
}

/* the GameData paths the install stanzas of e make (no stanzas: the
   directory named after the module, as CKAN does); -1 if e cannot be
   told (nothing in GameData, too many) */
static int targets(struct ctx *c, struct entry *e, char **t, int *nt)
{
    struct jnode *j, *ins, *st;
    const char *to, *name, *as;
    char *found;
    int had, bad;

    *nt = 0;
    found = NULL;
    had = e->j != NULL;
    j = entry_json(c, e);
    ins = json_get(j, "install");
    bad = 0;
    if (ins == NULL) {
        t[0] = cat3("GameData", "/", e->id);
        *nt = 1;
    } else if (ins->type != J_ARR) {
        bad = 1;
    }
    for (st = bad || ins == NULL ? NULL : ins->child; st != NULL && !bad;
        st = st->next)
    {
        to = json_str(st, "install_to");
        if (to == NULL || strncmp(to, "GameData", 8) != 0 ||
            (to[8] != 0 && to[8] != '/'))
        {
            continue;
        }
        name = json_str(st, "find");
        if (name == NULL) {
            name = json_str(st, "file");
        }
        as = json_str(st, "as");
        if (name == NULL && json_str(st, "find_regexp") != NULL &&
            path_is_safe(to))
        {
            free(found);
            found = rx_entry(c, to, json_str(st, "find_regexp"));
            name = found;
        }
        if (name == NULL || *nt == SC_MAXT) {
            bad = 1;
            break;
        }
        name = as != NULL ? as : base_of(name);
        if (name[0] == 0 || strcmp(name, "..") == 0 ||
            strcmp(name, ".") == 0 || strchr(name, '/') != NULL ||
            !path_is_safe(to))
        {
            bad = 1;
            break;
        }
        t[*nt] = cat3(to, "/", name);
        (*nt)++;
    }
    free(found);
    /* thousands of modules are looked at: their metadata is not kept */
    if (!had && e->j != NULL) {
        json_free(e->j);
        e->j = NULL;
    }
    if (*nt == 0) {
        bad = 1;
    }
    while (bad && *nt > 0) {
        (*nt)--;
        free(t[*nt]);
    }
    return bad ? -1 : 0;
}

/* are all the targets there, and nobody's? */
static int all_there(struct ctx *c, char **t, int nt)
{
    int i;

    for (i = 0; i < nt; i++) {
        if (kind_of(c, t[i]) == 0 || owned(c, t[i])) {
            return 0;
        }
    }
    return 1;
}

/* "v1.2.3", "2:1.2.3" and "1.2.3.0" all read as 1 2 3 0 */
static void nums(const char *s, long *v)
{
    int i;

    for (i = 0; i < 4; i++) {
        v[i] = 0;
    }
    if (strchr(s, ':') != NULL) {
        s = strchr(s, ':') + 1;
    }
    while (*s && !is_digit(*s)) {
        s++;
    }
    for (i = 0; i < 4 && is_digit(*s); i++) {
        while (is_digit(*s)) {
            v[i] = v[i] * 10 + (*s - '0');
            s++;
        }
        if (*s != '.' && *s != '_' && *s != '-') {
            break;
        }
        s++;
    }
}

/* the version of a KSP-AVC .version file under the targets, as
   "major.minor.patch.build", into out; 0 if none */
static int avc_version(struct ctx *c, char **t, int nt, char *out)
{
    struct strs files;
    struct jnode *j, *v;
    char *p, *text;
    const char *s;
    long len, n[4];
    int i, k, ok;
    char *keys[4];

    keys[0] = "MAJOR";
    keys[1] = "MINOR";
    keys[2] = "PATCH";
    keys[3] = "BUILD";
    files.v = NULL;
    files.n = 0;
    files.cap = 0;
    for (i = 0; i < nt; i++) {
        if (kind_of(c, t[i]) == 1) {
            walk(c, t[i], &files, 0);
        }
    }
    ok = 0;
    for (i = 0; i < files.n && !ok; i++) {
        len = (long)strlen(files.v[i]);
        if (len < 9 || strcmp(files.v[i] + len - 8, ".version") != 0) {
            continue;
        }
        p = cat3(c->game, "/", files.v[i]);
        text = read_file(p, &len, 1L << 16);
        free(p);
        if (text == NULL) {
            continue;
        }
        j = json_parse(text, len);
        free(text);
        v = json_get(j, "VERSION");
        if (v != NULL && v->type == J_OBJ) {
            for (k = 0; k < 4; k++) {
                s = json_get(v, keys[k]) != NULL &&
                    json_get(v, keys[k])->str != NULL ?
                    json_get(v, keys[k])->str : "0";
                n[k] = 0;
                while (is_digit(*s)) {
                    n[k] = n[k] * 10 + (*s - '0');
                    s++;
                }
            }
            sprintf(out, "%ld.%ld.%ld.%ld", n[0], n[1], n[2], n[3]);
            ok = 1;
        } else if (v != NULL && v->type == J_STR && strlen(v->str) < 40) {
            strcpy(out, v->str);
            ok = 1;
        }
        json_free(j);
    }
    strs_free(&files);
    return ok;
}

/* the version a found module most likely has: the newest one whose
   number is in a found file's name, or that matches its .version
   file; NULL if none can be told */
static char *guess_version(struct ctx *c, struct cand *k)
{
    struct entry *e;
    char avc[96];
    long a[4], b[4];
    const char *nm;
    int cnt, i, j, hasavc;

    e = index_find(&c->ix, k->s->id, &cnt);
    for (i = 0; i < k->nt; i++) {
        if (kind_of(c, k->t[i]) != 2) {
            continue;
        }
        nm = base_of(k->t[i]);
        for (j = 0; j < cnt; j++) {
            nums(e[j].version, a);
            sprintf(avc, "%ld.%ld.%ld", a[0], a[1], a[2]);
            if (a[0] + a[1] + a[2] > 0 && strstr(nm, avc) != NULL) {
                return xstrdup(e[j].version);
            }
        }
    }
    hasavc = avc_version(c, k->t, k->nt, avc);
    if (!hasavc) {
        return NULL;
    }
    nums(avc, a);
    for (j = 0; j < cnt; j++) {
        nums(e[j].version, b);
        if (a[0] == b[0] && a[1] == b[1] && a[2] == b[2] &&
            (a[3] == b[3] || a[3] == 0 || b[3] == 0))
        {
            return xstrdup(e[j].version);
        }
    }
    return NULL;
}

/* is file under target t (or t itself)? */
static int under(const char *file, const char *t)
{
    long n;

    n = (long)strlen(t);
    return strncmp(file, t, (unsigned long)n) == 0 &&
        (file[n] == 0 || file[n] == '/');
}

/* candidate a is better than b for the same directory */
static int better(struct ctx *c, struct cand *a, struct cand *b)
{
    int ca, cb;

    ca = entry_compat(c, a->e);
    cb = entry_compat(c, b->e);
    if (ca != cb) {
        return ca;
    }
    return a->s->downloads > b->s->downloads;
}

static int from_gamedata(struct ctx *c, struct strs *said,
    struct strs *doubt)
{
    struct cand *cv;
    struct strs files, others;
    struct inst *in;
    struct entry *e;
    char line[400];
    long mine;
    int nc, i, k, a, b, cnt, n, deeper, same;

    if (c->ix.ns == 0) {
        return 0;
    }
    cv = (struct cand *)xalloc((long)c->ix.ns * (long)sizeof(*cv));
    nc = 0;
    for (i = 0; i < c->ix.ns; i++) {
        if (registry_find(&c->reg, c->ix.s[i].id) != NULL) {
            continue;
        }
        e = index_find(&c->ix, c->ix.s[i].id, &cnt);
        if (e == NULL || strcmp(e->kind, "dlc") == 0 ||
            strcmp(e->kind, "metapackage") == 0)
        {
            continue;
        }
        cv[nc].s = &c->ix.s[i];
        cv[nc].e = e;
        cv[nc].ver = NULL;
        cv[nc].dropped = 0;
        if (targets(c, e, cv[nc].t, &cv[nc].nt) < 0) {
            continue;
        }
        if (!all_there(c, cv[nc].t, cv[nc].nt)) {
            for (k = 0; k < cv[nc].nt; k++) {
                free(cv[nc].t[k]);
            }
            continue;
        }
        nc++;
    }
    /* the same directory from several modules: keep the best one */
    for (a = 0; a < nc; a++) {
        if (cv[a].dropped) {
            continue;
        }
        others.v = NULL;
        others.n = 0;
        others.cap = 0;
        for (b = 0; b < nc; b++) {
            if (b == a || cv[b].dropped) {
                continue;
            }
            same = 0;
            for (i = 0; i < cv[a].nt; i++) {
                for (k = 0; k < cv[b].nt; k++) {
                    if (strcmp(cv[a].t[i], cv[b].t[k]) == 0) {
                        same = 1;
                    }
                }
            }
            if (!same) {
                continue;
            }
            if (better(c, &cv[b], &cv[a])) {
                cv[a].dropped = 1;
                break;
            }
            cv[b].dropped = 1;
            strs_add(&others, xstrdup(cv[b].s->id));
        }
        if (!cv[a].dropped && others.n > 0) {
            sprintf(line, "%.60s: took %.60s; it could also be", cv[a].t[0],
                cv[a].s->id);
            for (k = 0; k < others.n; k++) {
                if (strlen(line) + strlen(others.v[k]) + 3 < sizeof(line)) {
                    strcat(line, " ");
                    strcat(line, others.v[k]);
                }
            }
            strs_add(doubt, xstrdup(line));
        }
        strs_free(&others);
    }
    /* record them; a file goes to the module with the deepest target */
    n = 0;
    for (a = 0; a < nc; a++) {
        if (cv[a].dropped) {
            continue;
        }
        files.v = NULL;
        files.n = 0;
        files.cap = 0;
        for (i = 0; i < cv[a].nt; i++) {
            if (kind_of(c, cv[a].t[i]) == 2) {
                strs_add(&files, xstrdup(cv[a].t[i]));
            } else {
                walk(c, cv[a].t[i], &files, 0);
            }
        }
        for (i = 0; i < files.n; i++) {
            mine = 0;
            for (k = 0; k < cv[a].nt; k++) {
                if (under(files.v[i], cv[a].t[k]) &&
                    (long)strlen(cv[a].t[k]) > mine)
                {
                    mine = (long)strlen(cv[a].t[k]);
                }
            }
            deeper = 0;
            for (b = 0; b < nc && !deeper; b++) {
                if (b == a || cv[b].dropped) {
                    continue;
                }
                for (k = 0; k < cv[b].nt; k++) {
                    if (under(files.v[i], cv[b].t[k]) &&
                        (long)strlen(cv[b].t[k]) > mine)
                    {
                        deeper = 1;
                    }
                }
            }
            if (deeper || registry_owner(&c->reg, files.v[i]) != NULL) {
                free(files.v[i]);
                files.v[i] = NULL;
            }
        }
        k = 0;
        for (i = 0; i < files.n; i++) {
            if (files.v[i] != NULL) {
                files.v[k] = files.v[i];
                k++;
            }
        }
        files.n = k;
        if (files.n == 0) {
            strs_free(&files);
            continue;
        }
        cv[a].ver = guess_version(c, &cv[a]);
        in = registry_add(&c->reg, cv[a].s->id,
            cv[a].ver != NULL ? cv[a].ver : "0", 0);
        in->found = 1;
        in->files = files;
        strs_add(said, cat3(cv[a].s->id, " ", cv[a].ver != NULL ?
            cv[a].ver : "(version not known)"));
        n++;
    }
    /* a found module that another one needs is a dependency */
    for (a = 0; a < nc; a++) {
        if (cv[a].dropped) {
            continue;
        }
        in = registry_find(&c->reg, cv[a].s->id);
        if (in != NULL && in->found && needed_by(c, in->id, NULL) > 0) {
            in->autoinst = 1;
        }
    }
    for (a = 0; a < nc; a++) {
        for (k = 0; k < cv[a].nt; k++) {
            free(cv[a].t[k]);
        }
        free(cv[a].ver);
    }
    free(cv);
    return n;
}

/* ---- 3. what is left ---- */

/* the top-level entries of GameData that no module has: directories,
   and .dll files (ModuleManager's caches and the like are not mods) */
void scan_others(struct ctx *c, struct strs *out)
{
    DIR *d;
    struct dirent *de;
    char *p, *rel;
    long n;
    int k;

    p = cat3(c->game, "/", "GameData");
    d = opendir(p);
    free(p);
    if (d == NULL) {
        return;
    }
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.' || strcmp(de->d_name, "Squad") == 0 ||
            strcmp(de->d_name, "SquadExpansion") == 0)
        {
            continue;
        }
        rel = cat3("GameData", "/", de->d_name);
        k = kind_of(c, rel);
        n = (long)strlen(de->d_name);
        if ((k == 1 || (k == 2 && n > 4 &&
            strcmp(de->d_name + n - 4, ".dll") == 0)) && !owned(c, rel))
        {
            strs_add(out, xstrdup(de->d_name));
        }
        free(rel);
    }
    closedir(d);
    sort_strs(out->v, out->n, strcmp);
}

/* the DLCs in the game, as "MakingHistory-DLC" */
void scan_dlcs(struct ctx *c, struct strs *out)
{
    int i;
    char *rel;

    for (i = 0; i < c->ix.ns; i++) {
        if (strlen(c->ix.s[i].id) < 5 || strcmp(c->ix.s[i].id +
            strlen(c->ix.s[i].id) - 4, "-DLC") != 0)
        {
            continue;
        }
        rel = cat3("GameData/SquadExpansion/", c->ix.s[i].id, "");
        rel[strlen(rel) - 4] = 0;
        if (kind_of(c, rel) == 1) {
            strs_add(out, xstrdup(c->ix.s[i].id));
        }
        free(rel);
    }
}

/* ---- the stamp: when the last scan saw the game ---- */

static void stamp_of(struct ctx *c, char *out)
{
    struct stat st;
    char *p;
    long a, an, b, bn;

    a = an = b = bn = 0;
    p = cat3(c->game, "/", "GameData");
    if (stat(p, &st) == 0) {
        a = (long)st.st_mtim.tv_sec;
        an = (long)st.st_mtim.tv_nsec;
    }
    free(p);
    p = cat3(c->game, "/", "CKAN/registry.json");
    if (stat(p, &st) == 0) {
        b = (long)st.st_mtim.tv_sec;
        bn = (long)st.st_mtim.tv_nsec;
    }
    free(p);
    sprintf(out, "%ld.%ld %ld.%ld %d\n", a, an, b, bn, c->ix.ns);
}

void scan_stamp(struct ctx *c)
{
    char s[80], *p;

    stamp_of(c, s);
    p = state_path(c, "scanned");
    write_file(p, s, (long)strlen(s));
    free(p);
}

/* kspkan scan (quiet 0), and the scan on its own (quiet 1: only when
   the game has changed, and one line when something was found; 2: not
   even that).  The number of mods found. */
int scan_run(struct ctx *c, int quiet)
{
    struct strs said, doubt, others, dlcs;
    char now[80], *p, *was;
    long len;
    int n, i, k;

    k = 0;
    if (quiet && c->noscan) {
        return 0;
    }
    if (quiet) {
        stamp_of(c, now);
        p = state_path(c, "scanned");
        was = read_file(p, &len, 1024);
        free(p);
        if (was != NULL && strcmp(was, now) == 0) {
            free(was);
            return 0;
        }
        free(was);
    }
    said.v = doubt.v = others.v = dlcs.v = NULL;
    said.n = doubt.n = others.n = dlcs.n = 0;
    said.cap = doubt.cap = others.cap = dlcs.cap = 0;
    n = from_ckan(c, &said);
    n += from_gamedata(c, &said, &doubt);
    if (n > 0) {
        registry_save(c);
    }
    scan_stamp(c);
    if (quiet == 1 && n > 0) {
        fprintf(stderr, "kspkan: found %d mod%s already in the game (kspkan "
            "scan says which)\n", n, n == 1 ? "" : "s");
    }
    if (!quiet) {
        for (i = 0; i < c->reg.n; i++) {
            if (c->reg.v[i].found) {
                printf("found   %s %s%s\n", c->reg.v[i].id,
                    strcmp(c->reg.v[i].version, "0") == 0 ?
                    "(version not known)" : c->reg.v[i].version,
                    c->reg.v[i].autoinst ? " (dependency)" : "");
                k++;
            }
        }
        for (i = 0; i < doubt.n; i++) {
            printf("note    %s\n", doubt.v[i]);
        }
        scan_dlcs(c, &dlcs);
        for (i = 0; i < dlcs.n; i++) {
            printf("dlc     %s\n", dlcs.v[i]);
        }
        scan_others(c, &others);
        for (i = 0; i < others.n; i++) {
            printf("other   GameData/%s (not in the module list; left "
                "alone)\n", others.v[i]);
        }
        printf("%d mod%s marked as found, %d of them just now; kspkan "
            "can upgrade and remove them\n", k, k == 1 ? "" : "s", n);
    }
    strs_free(&said);
    strs_free(&doubt);
    strs_free(&others);
    strs_free(&dlcs);
    return n;
}
