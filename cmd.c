#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "kspkan.h"

/* load index and registry, find the game version; 0 if usable */
int ctx_ready(struct ctx *c)
{
    int rc;

    rc = index_load(c);
    if (rc < 0) {
        fprintf(stderr, "kspkan: no metadata yet; run: kspkan update\n");
        return -1;
    }
    registry_load(c);
    ksp_detect(c);
    return 0;
}

/* file: a CKAN-meta tarball already on disk (fetched some other way,
   for example over Tor), or NULL to download c->repo */
int cmd_update(struct ctx *c, const char *file)
{
    char *part, *text;
    unsigned char *tar;
    long len, tlen;
    int rc, count;

    rc = make_dirs(c->shared);
    if (rc < 0) {
        fprintf(stderr, "kspkan: cannot create %s\n", c->shared);
        return 1;
    }
    if (file != NULL) {
        printf("reading %s\n", file);
        text = read_file(file, &len, 1L << 28);
        if (text == NULL) {
            fprintf(stderr, "kspkan: cannot read %s\n", file);
            return 1;
        }
    } else {
        part = shared_path(c, "meta.tar.gz.part");
        printf("fetching %s\n", c->repo);
        rc = fetch_url(c->repo, part, 0);
        if (rc < 0) {
            fprintf(stderr, "kspkan: download failed\n");
            free(part);
            return 1;
        }
        text = read_file(part, &len, 1L << 28);
        unlink(part);
        free(part);
        if (text == NULL) {
            fprintf(stderr, "kspkan: cannot read download\n");
            return 1;
        }
    }
    tar = gunzip_alloc((unsigned char *)text, len, 1L << 30, &tlen);
    free(text);
    if (tar == NULL) {
        fprintf(stderr, "kspkan: bad or oversized archive\n");
        return 1;
    }
    count = index_update(c, tar, tlen);
    free(tar);
    if (count < 0) {
        fprintf(stderr, "kspkan: cannot write the index\n");
        return 1;
    }
    printf("indexed %d module versions\n", count);
    return 0;
}

int cmd_search(struct ctx *c, const char *term)
{
    struct query q;
    struct summary *s;
    struct inst *m;
    int *order;
    int i, shown, mode;

    query_parse(&q, term);
    mode = q.sort >= 0 ? q.sort : (c->bypop ? SORT_POP : SORT_NAME);
    order = summary_order(c, mode);
    shown = 0;
    for (i = 0; i < c->ix.ns; i++) {
        s = &c->ix.s[order[i]];
        if (!query_match(c, &q, order[i])) {
            continue;
        }
        m = registry_find(&c->reg, s->id);
        printf("%c %-30s %8ld  %-10s  %s\n", m != NULL ? 'i' : ' ',
            s->id, s->downloads, s->date, s->name);
        shown++;
    }
    fprintf(stderr, "%d found, sorted by %s\n", shown, sort_names[mode]);
    free(order);
    query_free(&q);
    return 0;
}

static void show_rel(struct jnode *j, const char *key)
{
    struct jnode *a, *r;
    const char *nm, *v;

    a = json_get(j, key);
    if (a == NULL || a->type != J_ARR) {
        return;
    }
    printf("%s:\n", key);
    for (r = a->child; r != NULL; r = r->next) {
        nm = json_str(r, "name");
        v = json_str(r, "version");
        if (nm != NULL) {
            printf("  %s%s%s\n", nm, v ? " = " : "", v ? v : "");
        } else if (json_get(r, "any_of") != NULL) {
            printf("  (one of several)\n");
        }
    }
}

/* "needed by: a, b" and a newline, or nothing when nothing needs id */
static void print_needers(struct ctx *c, const char *id, const char *label)
{
    struct strs by;
    int i;

    by.v = NULL;
    by.n = 0;
    by.cap = 0;
    needed_by(c, id, &by);
    if (by.n == 0) {
        return;
    }
    fputs(label, stdout);
    for (i = 0; i < by.n; i++) {
        printf("%s%s", i > 0 ? ", " : "", by.v[i]);
    }
    printf("\n");
    strs_free(&by);
}

int cmd_show(struct ctx *c, const char *id)
{
    struct entry *e;
    struct jnode *j, *a;
    struct inst *m;
    const char *v;
    int n, i;

    e = index_find(&c->ix, id, &n);
    if (e == NULL) {
        fprintf(stderr, "kspkan: no such module: %s\n", id);
        return 1;
    }
    j = entry_json(c, e);
    if (j == NULL) {
        fprintf(stderr, "kspkan: cannot read metadata\n");
        return 1;
    }
    printf("%s  %s\n", id, json_str(j, "name") ? json_str(j, "name") : "");
    v = json_str(j, "abstract");
    if (v != NULL) {
        printf("%s\n", v);
    }
    printf("newest: %s\n", e->version);
    m = registry_find(&c->reg, id);
    if (m != NULL) {
        printf("installed: %s, %s\n", m->version, m->autoinst ?
            "as a dependency" : "chosen by you");
        print_needers(c, id, "needed by: ");
    }
    if (c->kspver != NULL) {
        printf("game: %s\n", c->kspver);
    }
    printf("versions:");
    for (i = 0; i < n && i < 12; i++) {
        printf(" %s%s", e[i].version, entry_compat(c, &e[i]) ? "" : "(x)");
    }
    printf("%s\n", n > 12 ? " ..." : "");
    v = json_str(j, "license");
    if (v != NULL) {
        printf("license: %s\n", v);
    }
    show_rel(j, "depends");
    show_rel(j, "conflicts");
    show_rel(j, "recommends");
    show_rel(j, "suggests");
    show_rel(j, "supports");
    a = json_get(j, "resources");
    v = json_str(a, "homepage");
    if (v != NULL) {
        printf("homepage: %s\n", v);
    }
    return 0;
}

/* the installed modules: those you chose, then the dependencies, each
   with what needs it.  The module list is used when there is one. */
int cmd_list(struct ctx *c)
{
    struct inst *m;
    int i, pass;

    if (c->ix.n == 0 && index_load(c) < 0) {
        memset(&c->ix, 0, sizeof(c->ix));
    }
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < c->reg.n; i++) {
            m = &c->reg.v[i];
            if (m->autoinst != pass) {
                continue;
            }
            printf("%s %s%s", m->id, m->version,
                m->autoinst ? " (dependency)" : "");
            if (m->autoinst && is_orphan(c, m->id) && c->ix.n > 0) {
                printf(" nothing needs it (kspkan autoremove)");
            }
            print_needers(c, m->id, " needed by ");
            if (needed_by(c, m->id, NULL) == 0) {
                printf("\n");
            }
        }
    }
    return 0;
}

/* keep: the modules become your choice (autoremove leaves them);
   auto: they become dependencies (autoremove takes them when nothing
   needs them) */
int cmd_keep(struct ctx *c, char **ids, int n, int autoinst)
{
    struct inst *m;
    int i, rv;

    rv = 0;
    for (i = 0; i < n; i++) {
        m = registry_find(&c->reg, ids[i]);
        if (m == NULL) {
            fprintf(stderr, "kspkan: not installed: %s\n", ids[i]);
            rv = 1;
            continue;
        }
        m->autoinst = autoinst;
        printf("%s: %s\n", ids[i], autoinst ? "now a dependency" :
            "now chosen by you");
    }
    if (registry_save(c) < 0) {
        fprintf(stderr, "kspkan: cannot save what is installed\n");
        return 1;
    }
    return rv;
}

static int confirm(struct ctx *c)
{
    char line[16];
    char *p;

    if (c->assumeyes) {
        return 1;
    }
    printf("proceed? [y/N] ");
    fflush(stdout);
    p = fgets(line, sizeof(line), stdin);
    if (p == NULL) {
        return 0;
    }
    return line[0] == 'y' || line[0] == 'Y';
}

static int is_wanted(char **ids, int n, const char *id)
{
    long len;
    int i;

    for (i = 0; i < n; i++) {
        len = (long)strlen(id);
        if (strncmp(ids[i], id, (unsigned long)len) == 0 &&
            (ids[i][len] == 0 || ids[i][len] == '='))
        {
            return 1;
        }
    }
    return 0;
}

/* download, then install each entry; old copies named in replace are
   removed just before the new files go in */
static int install_entries(struct ctx *c, struct entry **ents, int n,
    char **wanted, int nwanted)
{
    struct archive a;
    struct plan p;
    struct inst *old;
    char err[512];
    char **paths;
    unsigned char *zip;
    long zlen;
    int i, rc, autoinst, rv;

    rv = 0;
    paths = (char **)xalloc((long)(n + 1) * (long)sizeof(char *));
    for (i = 0; i < n; i++) {
        paths[i] = NULL;
    }
    for (i = 0; i < n; i++) {
        if (strcmp(ents[i]->kind, "metapackage") == 0) {
            continue;
        }
        rc = module_download(c, ents[i], &paths[i], err, sizeof(err));
        if (rc < 0) {
            fprintf(stderr, "kspkan: %s\n", err);
            rv = 1;
            goto done;
        }
    }
    for (i = 0; i < n; i++) {
        autoinst = !is_wanted(wanted, nwanted, ents[i]->id);
        old = registry_find(&c->reg, ents[i]->id);
        if (old != NULL && !old->autoinst) {
            autoinst = 0;
        }
        if (paths[i] == NULL) {
            if (old != NULL) {
                registry_drop(&c->reg, ents[i]->id);
            }
            registry_add(&c->reg, ents[i]->id, ents[i]->version, autoinst);
            continue;
        }
        zip = (unsigned char *)read_file(paths[i], &zlen, 1L << 31);
        if (zip == NULL || zip_open(&a, zip, zlen) < 0) {
            fprintf(stderr, "kspkan: cannot read the archive of %s\n",
                ents[i]->id);
            free(zip);
            rv = 1;
            goto done;
        }
        rc = install_plan(&a, entry_json(c, ents[i]), ents[i]->id, &p, err,
            sizeof(err));
        if (rc == 0) {
            /* check before the old copy goes, so a refused upgrade
               leaves the old version installed */
            rc = install_check(c, &p, ents[i]->id, err, sizeof(err));
            if (rc < 0) {
                plan_free(&p);
            }
        }
        if (rc == 0) {
            if (old != NULL) {
                remove_module(c, ents[i]->id);
            }
            rc = install_extract(c, &a, &p, ents[i]->id,
                ents[i]->version, autoinst, err, sizeof(err));
            plan_free(&p);
        }
        archive_close(&a);
        free(zip);
        if (rc < 0) {
            fprintf(stderr, "kspkan: %s: %s\n", ents[i]->id, err);
            registry_save(c);
            rv = 1;
            goto done;
        }
        printf("installed %s %s\n", ents[i]->id, ents[i]->version);
        registry_save(c);
    }
    registry_save(c);
done:
    for (i = 0; i < n; i++) {
        free(paths[i]);
    }
    free(paths);
    return rv;
}

/* show the modules recommended and suggested by the plan and let the
   user pick; returns the wanted list extended by the picks (malloc'd
   array of pointers, strings are not copied) */
static char **choose_extras(struct ctx *c, struct entry **ents, int nents,
    char **ids, int n, int *nout)
{
    struct strs rec, sug, all;
    struct summary *s;
    char line[256];
    char **res;
    char *tok, *p;
    int i, k, pick, nrec, nres, takeall, takerec;

    rec.v = sug.v = all.v = NULL;
    rec.n = sug.n = all.n = rec.cap = sug.cap = all.cap = 0;
    collect_extras(c, ents, nents, "recommends", &rec);
    collect_extras(c, ents, nents, "suggests", &sug);
    nrec = rec.n;
    for (i = 0; i < sug.n; i++) {
        for (k = 0; k < rec.n && strcmp(rec.v[k], sug.v[i]) != 0; k++) {
            continue;
        }
        if (k < nrec) {
            continue;
        }
        strs_add(&rec, xstrdup(sug.v[i]));
    }
    strs_free(&sug);
    res = (char **)xalloc((long)(n + rec.n + 1) * (long)sizeof(char *));
    for (i = 0; i < n; i++) {
        res[i] = ids[i];
    }
    nres = n;
    *nout = n;
    if (rec.n == 0) {
        strs_free(&rec);
        free(all.v);
        return res;
    }
    if (c->assumeyes || !isatty(0)) {
        for (i = 0; i < rec.n; i++) {
            if ((i < nrec && c->withrec) || c->withsug) {
                res[nres] = xstrdup(rec.v[i]);
                nres++;
            }
        }
        *nout = nres;
        strs_free(&rec);
        free(all.v);
        return res;
    }
    printf("\nThese work well with what you chose:\n");
    for (i = 0; i < rec.n; i++) {
        s = summary_find(&c->ix, rec.v[i]);
        printf("  %2d) %-28.28s %-10s %.34s\n", i + 1, rec.v[i],
            i < nrec ? "recommended" : "suggested", s ? s->name : "");
    }
    printf("Install which too?  numbers (1 3), r = all recommended, "
        "a = all, Enter = none: ");
    fflush(stdout);
    p = fgets(line, sizeof(line), stdin);
    if (p == NULL) {
        line[0] = 0;
    }
    takeall = 0;
    takerec = 0;
    for (p = line; *p; p++) {
        if (*p == 'a' || *p == 'A') {
            takeall = 1;
        } else if (*p == 'r' || *p == 'R') {
            takerec = 1;
        }
    }
    for (i = 0; i < rec.n; i++) {
        pick = takeall || (takerec && i < nrec);
        for (tok = line; *tok && !pick; tok++) {
            if (is_digit(*tok) && (tok == line || !is_digit(tok[-1]))) {
                k = 0;
                p = tok;
                while (is_digit(*p) && k < 1000) {
                    k = k * 10 + (*p - '0');
                    p++;
                }
                pick = (k == i + 1);
            }
        }
        if (pick) {
            res[nres] = xstrdup(rec.v[i]);
            nres++;
        }
    }
    *nout = nres;
    strs_free(&rec);
    free(all.v);
    return res;
}

/* a module the user names on purpose is no longer "only a
   dependency", so autoremove and removing its parent keep it */
static void mark_wanted(struct ctx *c, char **ids, int n)
{
    struct inst *m;
    char *id;
    int i, changed;

    changed = 0;
    for (i = 0; i < n; i++) {
        if (strchr(ids[i], '=') != NULL) {
            id = xstrndup(ids[i], (long)(strchr(ids[i], '=') - ids[i]));
        } else {
            id = xstrdup(ids[i]);
        }
        m = registry_find(&c->reg, id);
        if (m != NULL && m->autoinst) {
            m->autoinst = 0;
            changed = 1;
            printf("%s is now kept as installed by you\n", id);
        }
        free(id);
    }
    if (changed) {
        registry_save(c);
    }
}

int cmd_install(struct ctx *c, char **ids, int n)
{
    struct entry **ents, **ents2;
    char **want, **skip;
    char err[512];
    int nents, nents2, nwant, nskip, i, rc;

    if (c->kspver == NULL && !c->anyver) {
        fprintf(stderr, "kspkan: game version unknown; use -k VERSION "
            "or -a\n");
        return 1;
    }
    skip = (char **)xalloc((long)(n + 1) * (long)sizeof(char *));
    nskip = 0;
    for (i = 0; i < n; i++) {
        if (strchr(ids[i], '=') != NULL) {
            skip[nskip] = xstrndup(ids[i], (long)(strchr(ids[i], '=') -
                ids[i]));
            if (registry_find(&c->reg, skip[nskip]) != NULL) {
                nskip++;
            } else {
                free(skip[nskip]);
            }
        }
    }
    rc = resolve(c, ids, n, skip, nskip, &ents, &nents, err, sizeof(err));
    if (rc < 0) {
        fprintf(stderr, "kspkan: %s\n", err);
        return 1;
    }
    want = choose_extras(c, ents, nents, ids, n, &nwant);
    if (nwant > n) {
        rc = resolve(c, want, nwant, skip, nskip, &ents2, &nents2, err,
            sizeof(err));
        if (rc < 0) {
            fprintf(stderr, "kspkan: cannot add those: %s\n", err);
            nwant = n;
        } else {
            free(ents);
            ents = ents2;
            nents = nents2;
        }
    }
    mark_wanted(c, ids, n);
    if (nents == 0) {
        printf("nothing to do\n");
        return 0;
    }
    printf("will install:\n");
    for (i = 0; i < nents; i++) {
        printf("  %s %s%s\n", ents[i]->id, ents[i]->version,
            is_wanted(want, nwant, ents[i]->id) ? "" : "  (needed)");
    }
    if (!confirm(c)) {
        free(ents);
        return 1;
    }
    rc = install_entries(c, ents, nents, want, nwant);
    free(ents);
    return rc;
}

int cmd_remove(struct ctx *c, char **ids, int n)
{
    struct strs gone;
    char err[512];
    int i, rc;

    gone.v = NULL;
    gone.n = 0;
    gone.cap = 0;
    rc = plan_removal(c, ids, n, &gone, err, sizeof(err));
    if (rc < 0) {
        fprintf(stderr, "kspkan: %s\n", err);
        return 1;
    }
    if (gone.n == 0) {
        printf("nothing to remove\n");
        strs_free(&gone);
        return 0;
    }
    printf("will remove:\n");
    for (i = 0; i < gone.n; i++) {
        printf("  %s%s\n", gone.v[i], is_wanted(ids, n, gone.v[i]) ? "" :
            "  (no longer needed)");
    }
    if (!confirm(c)) {
        strs_free(&gone);
        return 1;
    }
    for (i = 0; i < gone.n; i++) {
        remove_module(c, gone.v[i]);
        printf("removed %s\n", gone.v[i]);
    }
    registry_save(c);
    strs_free(&gone);
    return 0;
}

/* installed modules whose metadata names a successor */
static void note_replacements(struct ctx *c)
{
    struct entry *e;
    struct jnode *r;
    int i, k, cnt;

    for (i = 0; i < c->reg.n; i++) {
        e = index_find(&c->ix, c->reg.v[i].id, &cnt);
        for (k = 0; e != NULL && k < cnt; k++) {
            if (strcmp(e[k].version, c->reg.v[i].version) != 0) {
                continue;
            }
            r = json_get(entry_json(c, &e[k]), "replaced_by");
            if (json_str(r, "name") != NULL) {
                printf("note: %s is replaced by %s (kspkan install %s)\n",
                    c->reg.v[i].id, json_str(r, "name"),
                    json_str(r, "name"));
            }
        }
    }
}

int cmd_upgrade(struct ctx *c, char **ids, int n)
{
    struct entry **ents, **todo;
    struct entry *e;
    struct strs targets;
    char err[512];
    int nents, i, k, cnt, ntodo, rc;

    note_replacements(c);
    targets.v = NULL;
    targets.n = 0;
    targets.cap = 0;
    for (i = 0; i < c->reg.n; i++) {
        if (n == 0 || is_wanted(ids, n, c->reg.v[i].id)) {
            strs_add(&targets, xstrdup(c->reg.v[i].id));
        }
    }
    if (targets.n == 0) {
        printf("nothing installed to upgrade\n");
        return 0;
    }
    rc = resolve(c, targets.v, targets.n, targets.v, targets.n, &ents,
        &nents, err, sizeof(err));
    if (rc < 0) {
        fprintf(stderr, "kspkan: %s\n", err);
        strs_free(&targets);
        return 1;
    }
    todo = (struct entry **)xalloc((long)(nents + 1) *
        (long)sizeof(struct entry *));
    ntodo = 0;
    for (i = 0; i < nents; i++) {
        e = ents[i];
        cnt = 0;
        for (k = 0; k < c->reg.n; k++) {
            if (strcmp(c->reg.v[k].id, e->id) == 0 &&
                strcmp(c->reg.v[k].version, e->version) == 0)
            {
                cnt = 1;
            }
        }
        if (!cnt) {
            todo[ntodo] = e;
            ntodo++;
        }
    }
    if (ntodo == 0) {
        printf("everything is up to date\n");
        rc = 0;
    } else {
        printf("will install:\n");
        for (i = 0; i < ntodo; i++) {
            printf("  %s %s\n", todo[i]->id, todo[i]->version);
        }
        if (confirm(c)) {
            rc = install_entries(c, todo, ntodo, targets.v, targets.n);
        } else {
            rc = 1;
        }
    }
    free(todo);
    free(ents);
    strs_free(&targets);
    return rc;
}

/* delete the downloaded archives (and any unfinished downloads) */
int cmd_clean(struct ctx *c)
{
    char *full, *part;
    int i, rc, count;

    count = 0;
    for (i = 0; i < c->ix.n; i++) {
        full = cache_file(c, &c->ix.e[i]);
        part = (char *)xalloc((long)strlen(full) + 8);
        strcpy(part, full);
        strcat(part, ".part");
        rc = unlink(full);
        if (rc == 0) {
            count++;
        }
        rc = unlink(part);
        if (rc == 0) {
            count++;
        }
        free(part);
        free(full);
    }
    printf("removed %d downloaded archives\n", count);
    return 0;
}
