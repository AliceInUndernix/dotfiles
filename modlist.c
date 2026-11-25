#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "kspkan.h"

/* A modlist is a plain text file, one module you chose per line:
       id version
   Lines starting with # are comments.  The dependencies are not in it;
   they are worked out again when the list is installed.  Modlists are
   kept in modlists/ next to the module list, or anywhere when the name
   has a / in it. */

#define ML_MAX (1L << 20)

static void names_set(struct ctx *c, const char *name, int add);

char *modlist_path(struct ctx *c, const char *name)
{
    char *dir, *p;

    if (strchr(name, '/') != NULL) {
        return xstrdup(name);
    }
    if (!name_valid(name)) {
        return NULL;
    }
    dir = shared_path(c, "modlists");
    p = (char *)xalloc((long)(strlen(dir) + strlen(name)) + 2);
    strcpy(p, dir);
    strcat(p, "/");
    strcat(p, name);
    free(dir);
    return p;
}

void modlist_add(struct modlist *ml, const char *id, const char *ver)
{
    struct mlent *nv;
    int i, ncap;

    if (ml->n == ml->cap) {
        ncap = ml->cap ? ml->cap * 2 : 32;
        nv = (struct mlent *)xalloc((long)ncap * (long)sizeof(*nv));
        for (i = 0; i < ml->n; i++) {
            nv[i] = ml->v[i];
        }
        free(ml->v);
        ml->v = nv;
        ml->cap = ncap;
    }
    ml->v[ml->n].id = xstrdup(id);
    ml->v[ml->n].version = xstrdup(ver);
    ml->n++;
}

void modlist_free(struct modlist *ml)
{
    int i;

    for (i = 0; i < ml->n; i++) {
        free(ml->v[i].id);
        free(ml->v[i].version);
    }
    free(ml->v);
    ml->v = NULL;
    ml->n = 0;
    ml->cap = 0;
}

/* 0 if read; the list may be empty */
int modlist_read(struct ctx *c, const char *name, struct modlist *ml)
{
    char *p, *text, *line, *nl, *sp, *ver;
    long len;

    ml->v = NULL;
    ml->n = 0;
    ml->cap = 0;
    p = modlist_path(c, name);
    if (p == NULL) {
        return -1;
    }
    text = read_file(p, &len, ML_MAX);
    free(p);
    if (text == NULL) {
        return -1;
    }
    line = text;
    while (*line) {
        nl = strchr(line, '\n');
        if (nl != NULL) {
            *nl = 0;
        }
        while (*line == ' ' || *line == '\t') {
            line++;
        }
        if (line[0] != '#' && line[0] != 0) {
            ver = "";
            sp = line;
            while (*sp && *sp != ' ' && *sp != '\t' && *sp != '\r') {
                sp++;
            }
            if (*sp) {
                *sp = 0;
                ver = sp + 1;
                while (*ver == ' ' || *ver == '\t') {
                    ver++;
                }
                sp = ver;
                while (*sp && *sp != ' ' && *sp != '\t' && *sp != '\r') {
                    sp++;
                }
                *sp = 0;
            }
            modlist_add(ml, line, ver);
        }
        if (nl == NULL) {
            break;
        }
        line = nl + 1;
    }
    free(text);
    return 0;
}

/* write a modlist under a name; note, if not NULL, is a comment line */
int modlist_store(struct ctx *c, const char *name, struct modlist *ml,
    const char *note)
{
    struct buf b;
    char *p, *dir;
    int i, rc;

    p = modlist_path(c, name);
    if (p == NULL) {
        return -1;
    }
    b.p = NULL;
    b.n = 0;
    b.cap = 0;
    buf_add(&b, "", 0);
    buf_adds(&b, "# kspkan modlist: the modules chosen, one \"id version\""
        " a line\n");
    if (note != NULL) {
        buf_adds(&b, "# ");
        buf_adds(&b, note);
        buf_adds(&b, "\n");
    }
    if (c->kspver != NULL) {
        buf_adds(&b, "# game ");
        buf_adds(&b, c->kspver);
        buf_adds(&b, "\n");
    }
    for (i = 0; i < ml->n; i++) {
        buf_adds(&b, ml->v[i].id);
        if (ml->v[i].version[0] != 0) {
            buf_adds(&b, " ");
            buf_adds(&b, ml->v[i].version);
        }
        buf_adds(&b, "\n");
    }
    if (strchr(name, '/') == NULL) {
        dir = shared_path(c, "modlists");
        make_dirs(dir);
        free(dir);
    }
    rc = write_file(p, b.p, b.n);
    free(p);
    buf_free(&b);
    if (rc == 0) {
        names_set(c, name, 1);
    }
    return rc;
}

/* the modules you chose, into the named modlist */
int modlist_write(struct ctx *c, const char *name, int *count)
{
    struct modlist ml;
    int i, rc;

    ml.v = NULL;
    ml.n = 0;
    ml.cap = 0;
    for (i = 0; i < c->reg.n; i++) {
        if (!c->reg.v[i].autoinst) {
            modlist_add(&ml, c->reg.v[i].id, c->reg.v[i].version);
        }
    }
    *count = ml.n;
    rc = modlist_store(c, name, &ml, NULL);
    modlist_free(&ml);
    return rc;
}

static int name_cmp(const char *a, const char *b)
{
    return strcmp(a, b);
}

/* the names of the saved modlists, sorted, from modlists/.names (kept
   by save and delete, so that no directory has to be read) */
int modlist_names(struct ctx *c, struct strs *out)
{
    char *p, *text, *line, *nl, *f;
    long len;

    p = shared_path(c, "modlists/.names");
    text = read_file(p, &len, ML_MAX);
    free(p);
    if (text == NULL) {
        return 0;
    }
    line = text;
    while (*line) {
        nl = strchr(line, '\n');
        if (nl != NULL) {
            *nl = 0;
        }
        f = modlist_path(c, line);
        if (f != NULL && access(f, R_OK) == 0) {
            strs_add(out, xstrdup(line));
        }
        free(f);
        if (nl == NULL) {
            break;
        }
        line = nl + 1;
    }
    free(text);
    sort_strs(out->v, out->n, name_cmp);
    return out->n;
}

/* add or drop a name in modlists/.names */
static void names_set(struct ctx *c, const char *name, int add)
{
    struct strs names;
    struct buf b;
    char *p;
    int i;

    if (strchr(name, '/') != NULL) {
        return;
    }
    names.v = NULL;
    names.n = 0;
    names.cap = 0;
    modlist_names(c, &names);
    b.p = NULL;
    b.n = 0;
    b.cap = 0;
    buf_add(&b, "", 0);
    for (i = 0; i < names.n; i++) {
        if (strcmp(names.v[i], name) != 0) {
            buf_adds(&b, names.v[i]);
            buf_adds(&b, "\n");
        }
    }
    if (add) {
        buf_adds(&b, name);
        buf_adds(&b, "\n");
    }
    p = shared_path(c, "modlists/.names");
    write_file(p, b.p, b.n);
    free(p);
    buf_free(&b);
    strs_free(&names);
}

static int ml_list(struct ctx *c)
{
    struct strs names;
    struct modlist ml;
    int i;

    names.v = NULL;
    names.n = 0;
    names.cap = 0;
    modlist_names(c, &names);
    if (names.n == 0) {
        printf("no modlists yet (kspkan modlist save NAME)\n");
    }
    for (i = 0; i < names.n; i++) {
        if (modlist_read(c, names.v[i], &ml) == 0) {
            printf("%-24s %d modules\n", names.v[i], ml.n);
            modlist_free(&ml);
        }
    }
    strs_free(&names);
    return 0;
}

/* each line of the modlist and how it stands in this game */
static int ml_show(struct ctx *c, struct modlist *ml)
{
    struct inst *m;
    int i, k, chosen;

    for (i = 0; i < ml->n; i++) {
        m = registry_find(&c->reg, ml->v[i].id);
        printf("%c %-30s %-12s ", m != NULL ? 'i' : ' ', ml->v[i].id,
            ml->v[i].version);
        if (m == NULL) {
            printf("%s\n", summary_find(&c->ix, ml->v[i].id) != NULL ?
                "not installed" : "not in the module list");
        } else if (strcmp(m->version, ml->v[i].version) != 0 &&
            ml->v[i].version[0] != 0)
        {
            printf("installed %s\n", m->version);
        } else {
            printf("installed%s\n", m->autoinst ? " as a dependency" : "");
        }
    }
    for (i = 0; i < c->reg.n; i++) {
        if (c->reg.v[i].autoinst) {
            continue;
        }
        chosen = 0;
        for (k = 0; k < ml->n; k++) {
            if (strcmp(ml->v[k].id, c->reg.v[i].id) == 0) {
                chosen = 1;
            }
        }
        if (!chosen) {
            printf("- %-30s %-12s installed, not in the list\n",
                c->reg.v[i].id, c->reg.v[i].version);
        }
    }
    return 0;
}

/* install what the modlist has and the game has not; pin: the versions
   written in it, else the newest that fits.  sync: then remove the
   modules you chose that are not in it. */
static int ml_install(struct ctx *c, struct modlist *ml, int pin, int sync)
{
    struct strs want, gone;
    char *s;
    int i, k, rv, inlist;

    want.v = gone.v = NULL;
    want.n = gone.n = want.cap = gone.cap = 0;
    for (i = 0; i < ml->n; i++) {
        if (summary_find(&c->ix, ml->v[i].id) == NULL) {
            fprintf(stderr, "kspkan: %s is not in the module list; "
                "skipped\n", ml->v[i].id);
            continue;
        }
        if (registry_find(&c->reg, ml->v[i].id) != NULL && (!pin ||
            ml->v[i].version[0] == 0 || strcmp(registry_find(&c->reg,
            ml->v[i].id)->version, ml->v[i].version) == 0))
        {
            continue;
        }
        s = (char *)xalloc((long)(strlen(ml->v[i].id) +
            strlen(ml->v[i].version)) + 2);
        strcpy(s, ml->v[i].id);
        if (pin && ml->v[i].version[0] != 0) {
            strcat(s, "=");
            strcat(s, ml->v[i].version);
        }
        strs_add(&want, s);
    }
    if (sync) {
        for (i = 0; i < c->reg.n; i++) {
            if (c->reg.v[i].autoinst) {
                continue;
            }
            inlist = 0;
            for (k = 0; k < ml->n; k++) {
                if (strcmp(ml->v[k].id, c->reg.v[i].id) == 0) {
                    inlist = 1;
                }
            }
            if (!inlist) {
                strs_add(&gone, xstrdup(c->reg.v[i].id));
            }
        }
    }
    rv = 0;
    if (gone.n > 0) {
        rv = cmd_remove(c, gone.v, gone.n);
    }
    if (want.n > 0 && rv == 0) {
        rv = cmd_install(c, want.v, want.n);
    }
    if (want.n == 0 && gone.n == 0) {
        printf("the game already has everything in the modlist\n");
    }
    strs_free(&want);
    strs_free(&gone);
    return rv;
}

/* kspkan modlist import FILE [NAME]: a list CKAN wrote, kept as a
   kspkan modlist (NAME, else made from the file name) */
static int ml_import(struct ctx *c, const char *file, const char *name)
{
    struct ckanimp r;
    struct buf note;
    const char *base;
    char *made, *p;
    int i, unknown, rc;

    made = NULL;
    if (name == NULL) {
        made = ckan_list_name(file);
        name = made;
    }
    p = modlist_path(c, name);
    if (p == NULL) {
        fprintf(stderr, "kspkan: bad modlist name: %s (letters, digits, "
            "_ - . only; or a path with a /)\n", name);
        free(made);
        return 2;
    }
    rc = ckan_import(file, &r);
    if (rc < 0 || r.ml.n == 0) {
        if (rc < 0 && r.ml.n == 0 && strcmp(r.kind, "file") == 0) {
            fprintf(stderr, "kspkan: cannot read %s\n", file);
        } else {
            fprintf(stderr, "kspkan: no mods found in %s (read as a %s)\n",
                file, r.kind);
        }
        modlist_free(&r.ml);
        free(made);
        free(p);
        return 1;
    }
    base = strrchr(file, '/');
    base = base != NULL ? base + 1 : file;
    note.p = NULL;
    note.n = 0;
    note.cap = 0;
    buf_add(&note, "", 0);
    buf_adds(&note, "imported from the ");
    buf_adds(&note, r.kind);
    buf_adds(&note, " ");
    for (i = 0; base[i] && i < 60; i++) {
        buf_add(&note, (unsigned char)base[i] > 32 &&
            (unsigned char)base[i] < 127 ? base + i : "_", 1);
    }
    rc = modlist_store(c, name, &r.ml, note.p);
    buf_free(&note);
    if (rc < 0) {
        fprintf(stderr, "kspkan: cannot write %s\n", p);
        modlist_free(&r.ml);
        free(made);
        free(p);
        return 1;
    }
    unknown = 0;
    for (i = 0; i < r.ml.n; i++) {
        if (c->ix.ns > 0 && summary_find(&c->ix, r.ml.v[i].id) == NULL) {
            if (unknown == 0) {
                printf("not in the module list (yet):");
            }
            printf(" %s", r.ml.v[i].id);
            unknown++;
        }
    }
    if (unknown > 0) {
        printf("\n");
    }
    printf("imported %d mods from the %s into the modlist %s\n", r.ml.n,
        r.kind, name);
    if (r.deps > 0) {
        printf("left out %d mods CKAN installed as dependencies; they are "
            "worked out again\n", r.deps);
    }
    if (r.suggests > 0) {
        printf("left out %d suggested mods\n", r.suggests);
    }
    if (r.skipped > 0) {
        printf("passed over %d lines or entries that name no mod\n",
            r.skipped);
    }
    printf("next: kspkan modlist show %s, then kspkan modlist install %s "
        "(or pin)\n", name, name);
    modlist_free(&r.ml);
    free(made);
    free(p);
    return 0;
}

/* kspkan modlist [list | save NAME | show NAME | install NAME |
   pin NAME | sync NAME | delete NAME | import FILE [NAME]] */
int cmd_modlist(struct ctx *c, char **args, int n)
{
    struct modlist ml;
    const char *sub;
    char *p;
    int rc, count;

    sub = n > 0 ? args[0] : "list";
    if (strcmp(sub, "list") == 0 || strcmp(sub, "ls") == 0) {
        return ml_list(c);
    }
    if (n < 2) {
        fprintf(stderr, "usage: kspkan modlist [list | save NAME | show "
            "NAME | install NAME | pin NAME | sync NAME | delete NAME |\n"
            "       import FILE [NAME]]\n");
        return 2;
    }
    if (strcmp(sub, "import") == 0) {
        return ml_import(c, args[1], n > 2 ? args[2] : NULL);
    }
    p = modlist_path(c, args[1]);
    if (p == NULL) {
        fprintf(stderr, "kspkan: bad modlist name: %s (letters, digits, "
            "_ - . only; or a path with a /)\n", args[1]);
        return 2;
    }
    if (strcmp(sub, "save") == 0) {
        rc = modlist_write(c, args[1], &count);
        if (rc < 0) {
            fprintf(stderr, "kspkan: cannot write %s\n", p);
            free(p);
            return 1;
        }
        printf("saved %d modules to %s\n", count, p);
        free(p);
        return 0;
    }
    if (strcmp(sub, "delete") == 0 || strcmp(sub, "rm") == 0) {
        rc = unlink(p);
        if (rc < 0) {
            fprintf(stderr, "kspkan: no modlist %s\n", p);
            free(p);
            return 1;
        }
        names_set(c, args[1], 0);
        printf("deleted %s\n", p);
        free(p);
        return 0;
    }
    rc = modlist_read(c, args[1], &ml);
    if (rc < 0) {
        fprintf(stderr, "kspkan: cannot read %s\n", p);
        free(p);
        return 1;
    }
    free(p);
    if (strcmp(sub, "show") == 0) {
        rc = ml_show(c, &ml);
    } else if (strcmp(sub, "install") == 0) {
        rc = ml_install(c, &ml, 0, 0);
    } else if (strcmp(sub, "pin") == 0) {
        rc = ml_install(c, &ml, 1, 0);
    } else if (strcmp(sub, "sync") == 0) {
        rc = ml_install(c, &ml, 0, 1);
    } else {
        fprintf(stderr, "kspkan: no modlist command %s\n", sub);
        rc = 2;
    }
    modlist_free(&ml);
    return rc;
}
