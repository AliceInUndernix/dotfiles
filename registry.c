#include <stdlib.h>
#include <string.h>
#include "kspkan.h"

char *state_path(struct ctx *c, const char *rel)
{
    char *p;

    p = (char *)xalloc((long)(strlen(c->state) + strlen(rel)) + 2);
    strcpy(p, c->state);
    strcat(p, "/");
    strcat(p, rel);
    return p;
}

char *shared_path(struct ctx *c, const char *rel)
{
    char *p;

    p = (char *)xalloc((long)(strlen(c->shared) + strlen(rel)) + 2);
    strcpy(p, c->shared);
    strcat(p, "/");
    strcat(p, rel);
    return p;
}

void registry_clear(struct registry *r)
{
    while (r->n > 0) {
        free(r->v[r->n - 1].id);
        free(r->v[r->n - 1].version);
        strs_free(&r->v[r->n - 1].files);
        r->n--;
    }
}

struct inst *registry_find(struct registry *r, const char *id)
{
    int i;

    for (i = 0; i < r->n; i++) {
        if (strcmp(r->v[i].id, id) == 0) {
            return &r->v[i];
        }
    }
    return NULL;
}

struct inst *registry_add(struct registry *r, const char *id,
    const char *version, int autoinst)
{
    struct inst *nv;
    struct inst *m;
    int i, ncap;

    if (r->n == r->cap) {
        ncap = r->cap ? r->cap * 2 : 16;
        nv = (struct inst *)xalloc((long)ncap * (long)sizeof(*nv));
        for (i = 0; i < r->n; i++) {
            nv[i] = r->v[i];
        }
        free(r->v);
        r->v = nv;
        r->cap = ncap;
    }
    m = &r->v[r->n];
    r->n++;
    m->id = xstrdup(id);
    m->version = xstrdup(version);
    m->autoinst = autoinst;
    m->found = 0;
    m->files.v = NULL;
    m->files.n = 0;
    m->files.cap = 0;
    return m;
}

void registry_drop(struct registry *r, const char *id)
{
    int i;

    for (i = 0; i < r->n; i++) {
        if (strcmp(r->v[i].id, id) == 0) {
            break;
        }
    }
    if (i == r->n) {
        return;
    }
    free(r->v[i].id);
    free(r->v[i].version);
    strs_free(&r->v[i].files);
    for (; i + 1 < r->n; i++) {
        r->v[i] = r->v[i + 1];
    }
    r->n--;
}

const char *registry_owner(struct registry *r, const char *path)
{
    int i, k;

    for (i = 0; i < r->n; i++) {
        for (k = 0; k < r->v[i].files.n; k++) {
            if (strcmp(r->v[i].files.v[k], path) == 0) {
                return r->v[i].id;
            }
        }
    }
    return NULL;
}

/* lines: "M<TAB>id<TAB>version<TAB>auto" then "F<TAB>path" lines;
   auto is 0 or 1, with an f after it for a module found in the game
   (put there by CKAN or by hand) rather than installed by kspkan */
int registry_load(struct ctx *c)
{
    char *p, *text, *line, *nl, *f1, *f2, *f3;
    struct inst *cur;
    long len;

    c->reg.v = NULL;
    c->reg.n = 0;
    c->reg.cap = 0;
    p = state_path(c, "installed");
    text = read_file(p, &len, 1L << 26);
    free(p);
    if (text == NULL) {
        return 0;
    }
    cur = NULL;
    line = text;
    while (*line) {
        nl = strchr(line, '\n');
        if (nl != NULL) {
            *nl = 0;
        }
        if (line[0] == 'M' && line[1] == '\t') {
            cur = NULL;
            f1 = line + 2;
            f2 = strchr(f1, '\t');
            f3 = NULL;
            if (f2 != NULL) {
                *f2 = 0;
                f2++;
                f3 = strchr(f2, '\t');
            }
            if (f3 != NULL) {
                *f3 = 0;
                f3++;
                cur = registry_add(&c->reg, f1, f2, f3[0] == '1');
                cur->found = f3[0] != 0 && f3[1] == 'f';
            }
        } else if (line[0] == 'F' && line[1] == '\t' && cur != NULL) {
            strs_add(&cur->files, xstrdup(line + 2));
        }
        if (nl == NULL) {
            break;
        }
        line = nl + 1;
    }
    free(text);
    return 0;
}

int registry_save(struct ctx *c)
{
    struct buf b;
    char *p;
    int i, k, rc;

    b.p = NULL;
    b.n = 0;
    b.cap = 0;
    buf_add(&b, "", 0);
    for (i = 0; i < c->reg.n; i++) {
        buf_adds(&b, "M\t");
        buf_adds(&b, c->reg.v[i].id);
        buf_adds(&b, "\t");
        buf_adds(&b, c->reg.v[i].version);
        buf_adds(&b, c->reg.v[i].autoinst ? "\t1" : "\t0");
        buf_adds(&b, c->reg.v[i].found ? "f\n" : "\n");
        for (k = 0; k < c->reg.v[i].files.n; k++) {
            buf_adds(&b, "F\t");
            buf_adds(&b, c->reg.v[i].files.v[k]);
            buf_adds(&b, "\n");
        }
    }
    make_dirs(c->state);
    p = state_path(c, "installed");
    rc = write_file(p, b.p, b.n);
    free(p);
    buf_free(&b);
    /* what kspkan itself put in is no reason to scan again */
    scan_stamp(c);
    return rc;
}
