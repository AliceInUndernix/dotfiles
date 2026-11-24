#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "kspkan.h"

#define MAX_FILE (1L << 30)

static void set_msg(char *err, int sz, const char *a, const char *b)
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

struct stanza {
    const char *find;
    const char *find_rx;
    const char *file;
    const char *to;
    const char *as;
    int matchfiles;
    const struct jnode *filter;
    const struct jnode *filter_rx;
    const struct jnode *only;
    const struct jnode *only_rx;
};

/* a string or an array of strings: does any element match path? */
static int list_hits(const struct jnode *n, const char *path, int rx)
{
    const struct jnode *c;

    if (n == NULL) {
        return 0;
    }
    if (n->type == J_STR) {
        if (rx) {
            return rx_match(n->str, path) > 0;
        }
        return strstr(path, n->str) != NULL;
    }
    if (n->type == J_ARR) {
        for (c = n->child; c != NULL; c = c->next) {
            if (c->type == J_STR && list_hits(c, path, rx)) {
                return 1;
            }
        }
    }
    return 0;
}

static int under(const char *path, const char *root)
{
    long rl;

    rl = (long)strlen(root);
    if (strncmp(path, root, (unsigned long)rl) != 0) {
        return 0;
    }
    return path[rl] == 0 || path[rl] == '/';
}

static const char *base_of(const char *p)
{
    const char *s;

    s = strrchr(p, '/');
    return s != NULL ? s + 1 : p;
}

/* does path equal find, or end with "/" and find? */
static int ends_with_path(const char *path, const char *find)
{
    long pl, fl;

    pl = (long)strlen(path);
    fl = (long)strlen(find);
    while (fl > 0 && find[fl - 1] == '/') {
        fl--;
    }
    if (fl == 0 || fl > pl) {
        return 0;
    }
    if (strncmp(path + pl - fl, find, (unsigned long)fl) != 0) {
        return 0;
    }
    return fl == pl || path[pl - fl - 1] == '/';
}

/* the node (directory or file path inside the archive) a stanza names */
static char *find_root(struct archive *a, char **names,
    const struct stanza *st)
{
    char *best, *prefix;
    const char *nm, *prev, *q;
    long plen;
    int i, depth, bestdepth, isdir, hit;

    if (st->file != NULL) {
        for (i = 0; i < a->n; i++) {
            if (names[i] != NULL && under(names[i], st->file)) {
                return xstrdup(st->file);
            }
        }
        return NULL;
    }
    best = NULL;
    bestdepth = 1 << 20;
    prev = "";
    for (i = 0; i < a->n; i++) {
        nm = names[i];
        if (nm == NULL) {
            continue;
        }
        depth = 0;
        q = nm;
        for (;;) {
            while (*q && *q != '/') {
                q++;
            }
            depth++;
            plen = (long)(q - nm);
            isdir = (*q == '/') || a->e[i].isdir;
            hit = 0;
            if (depth < bestdepth && (isdir || st->matchfiles) &&
                !(*q == '/' && strncmp(prev, nm, (unsigned long)plen) == 0
                && prev[plen] == '/'))
            {
                prefix = xstrndup(nm, plen);
                if (st->find != NULL) {
                    hit = ends_with_path(prefix, st->find);
                } else {
                    hit = rx_match(st->find_rx, prefix) > 0;
                }
                if (hit) {
                    free(best);
                    best = prefix;
                    bestdepth = depth;
                } else {
                    free(prefix);
                }
            }
            if (*q == 0) {
                break;
            }
            q++;
        }
        prev = nm;
    }
    return best;
}

static void plan_add(struct plan *p, int ent, char *dest)
{
    struct pitem *nv;
    int i, ncap;

    for (i = 0; i < p->n; i++) {
        if (strcmp(p->v[i].dest, dest) == 0) {
            free(dest);
            return;
        }
    }
    if (p->n == p->cap) {
        ncap = p->cap ? p->cap * 2 : 64;
        nv = (struct pitem *)xalloc((long)ncap * (long)sizeof(*nv));
        for (i = 0; i < p->n; i++) {
            nv[i] = p->v[i];
        }
        free(p->v);
        p->v = nv;
        p->cap = ncap;
    }
    p->v[p->n].ent = ent;
    p->v[p->n].dest = dest;
    p->n++;
}

void plan_free(struct plan *p)
{
    int i;

    for (i = 0; i < p->n; i++) {
        free(p->v[i].dest);
    }
    free(p->v);
    p->v = NULL;
    p->n = 0;
    p->cap = 0;
}

static int bool_of(const struct jnode *o, const char *key)
{
    struct jnode *n;

    n = json_get(o, key);
    return n != NULL && n->type == J_BOOL && strcmp(n->str, "true") == 0;
}

static int do_stanza(struct archive *a, char **names, const struct stanza *st,
    struct plan *p, char *err, int errsz)
{
    char *root, *dest;
    const char *name, *path, *to;
    int i, rl;

    if (st->to == NULL || !path_is_safe(st->to) ||
        (st->as != NULL && (strchr(st->as, '/') != NULL ||
        !path_is_safe(st->as))))
    {
        set_msg(err, errsz, "bad install_to or as in the metadata", "");
        return -1;
    }
    if ((st->find != NULL) + (st->find_rx != NULL) + (st->file != NULL) !=
        1)
    {
        set_msg(err, errsz, "install stanza needs one of find, ",
            "find_regexp, file");
        return -1;
    }
    root = find_root(a, names, st);
    if (root == NULL) {
        set_msg(err, errsz, "the archive has no ",
            st->find ? st->find : (st->file ? st->file : st->find_rx));
        return -1;
    }
    name = st->as != NULL ? st->as : base_of(root);
    to = st->to;
    if (strcmp(to, "GameRoot") == 0) {
        to = "";
    } else if (strncmp(to, "GameRoot/", 9) == 0) {
        to += 9;
    }
    rl = (int)strlen(root);
    for (i = 0; i < a->n; i++) {
        path = names[i];
        if (path == NULL || a->e[i].isdir || !under(path, root)) {
            continue;
        }
        if (list_hits(st->filter, path, 0) ||
            list_hits(st->filter_rx, path, 1))
        {
            continue;
        }
        if ((st->only != NULL || st->only_rx != NULL) &&
            !list_hits(st->only, path, 0) &&
            !list_hits(st->only_rx, path, 1))
        {
            continue;
        }
        dest = (char *)xalloc((long)(strlen(to) + strlen(name) +
            strlen(path)) + 4);
        dest[0] = 0;
        if (to[0] != 0) {
            strcpy(dest, to);
            strcat(dest, "/");
        }
        strcat(dest, name);
        strcat(dest, path + rl);
        if (!path_is_safe(dest)) {
            free(dest);
            continue;
        }
        plan_add(p, i, dest);
    }
    free(root);
    return 0;
}

int install_plan(struct archive *a, struct jnode *mod, const char *id,
    struct plan *p, char *err, int errsz)
{
    struct stanza st;
    struct jnode *inst, *el;
    char **names;
    long n;
    int i, rc, any;

    p->v = NULL;
    p->n = 0;
    p->cap = 0;
    names = (char **)xalloc((long)(a->n + 1) * (long)sizeof(char *));
    for (i = 0; i < a->n; i++) {
        names[i] = NULL;
        n = (long)strlen(a->e[i].name);
        while (n > 0 && a->e[i].name[n - 1] == '/') {
            n--;
        }
        if (n > 0) {
            names[i] = xstrndup(a->e[i].name, n);
            if (!path_is_safe(names[i])) {
                free(names[i]);
                names[i] = NULL;
            }
        }
    }
    rc = 0;
    inst = json_get(mod, "install");
    if (inst == NULL || inst->type != J_ARR || inst->child == NULL) {
        memset(&st, 0, sizeof(st));
        st.find = id;
        st.to = "GameData";
        rc = do_stanza(a, names, &st, p, err, errsz);
    } else {
        for (el = inst->child; el != NULL && rc == 0; el = el->next) {
            memset(&st, 0, sizeof(st));
            st.find = json_str(el, "find");
            st.find_rx = json_str(el, "find_regexp");
            st.file = json_str(el, "file");
            st.to = json_str(el, "install_to");
            st.as = json_str(el, "as");
            st.matchfiles = bool_of(el, "find_matches_files");
            st.filter = json_get(el, "filter");
            st.filter_rx = json_get(el, "filter_regexp");
            st.only = json_get(el, "include_only");
            st.only_rx = json_get(el, "include_only_regexp");
            rc = do_stanza(a, names, &st, p, err, errsz);
        }
    }
    for (i = 0; i < a->n; i++) {
        free(names[i]);
    }
    free(names);
    any = (p->n > 0);
    if (rc == 0 && !any) {
        set_msg(err, errsz, "nothing in the archive matches the ",
            "install rules");
        rc = -1;
    }
    if (rc < 0) {
        plan_free(p);
    }
    return rc;
}

static char *join_path(const char *a, const char *b)
{
    char *p;

    p = (char *)xalloc((long)(strlen(a) + strlen(b)) + 2);
    strcpy(p, a);
    strcat(p, "/");
    strcat(p, b);
    return p;
}

static void prune_dirs(struct ctx *c, const char *rel)
{
    char *p, *full, *slash;
    int rc;

    p = xstrdup(rel);
    for (;;) {
        slash = strrchr(p, '/');
        if (slash == NULL) {
            break;
        }
        *slash = 0;
        if (strchr(p, '/') == NULL) {
            break;
        }
        full = join_path(c->game, p);
        rc = rmdir(full);
        free(full);
        if (rc < 0) {
            break;
        }
    }
    free(p);
}

int remove_module(struct ctx *c, const char *id)
{
    struct inst *m;
    char *full;
    int i;

    m = registry_find(&c->reg, id);
    if (m == NULL) {
        return -1;
    }
    for (i = 0; i < m->files.n; i++) {
        if (!path_is_safe(m->files.v[i])) {
            continue;
        }
        full = join_path(c->game, m->files.v[i]);
        unlink(full);
        free(full);
    }
    for (i = 0; i < m->files.n; i++) {
        if (path_is_safe(m->files.v[i])) {
            prune_dirs(c, m->files.v[i]);
        }
    }
    registry_drop(&c->reg, id);
    return 0;
}

/* refuse a plan whose files would clobber a directory, another
   module's file or (without -f) a file kspkan did not install */
int install_check(struct ctx *c, struct plan *p, const char *id,
    char *err, int errsz)
{
    struct stat st;
    const char *owner;
    char *full;
    int i, rc;

    for (i = 0; i < p->n; i++) {
        full = join_path(c->game, p->v[i].dest);
        rc = lstat(full, &st);
        free(full);
        if (rc < 0) {
            continue;
        }
        owner = registry_owner(&c->reg, p->v[i].dest);
        if (S_ISDIR(st.st_mode)) {
            set_msg(err, errsz, "a directory is in the way: ",
                p->v[i].dest);
            return -1;
        }
        if (owner != NULL && strcmp(owner, id) != 0) {
            set_msg(err, errsz, p->v[i].dest, " belongs to another module");
            return -1;
        }
        if (owner == NULL && !c->force) {
            set_msg(err, errsz, "file exists, not installed by kspkan "
                "(use -f): ", p->v[i].dest);
            return -1;
        }
    }
    return 0;
}

int install_extract(struct ctx *c, struct archive *a, struct plan *p,
    const char *id, const char *version, int autoinst, char *err,
    int errsz)
{
    struct inst *m;
    unsigned char *data;
    char *full, *dir, *slash;
    long len;
    int i, rc;

    rc = install_check(c, p, id, err, errsz);
    if (rc < 0) {
        return -1;
    }
    m = registry_find(&c->reg, id);
    if (m != NULL) {
        registry_drop(&c->reg, id);
    }
    m = registry_add(&c->reg, id, version, autoinst);
    for (i = 0; i < p->n; i++) {
        data = archive_read(a, p->v[i].ent, MAX_FILE, &len);
        if (data == NULL) {
            set_msg(err, errsz, "cannot read from the archive: ",
                p->v[i].dest);
            rc = -1;
            goto quit;
        }
        dir = xstrdup(p->v[i].dest);
        slash = strrchr(dir, '/');
        if (slash != NULL) {
            *slash = 0;
            rc = safe_make_dirs(c->game, dir);
        } else {
            rc = 0;
        }
        free(dir);
        full = join_path(c->game, p->v[i].dest);
        if (rc == 0) {
            rc = write_file(full, (char *)data, len);
        }
        free(full);
        free(data);
        if (rc < 0) {
            set_msg(err, errsz, "cannot write ", p->v[i].dest);
            goto quit;
        }
        strs_add(&m->files, xstrdup(p->v[i].dest));
    }
    rc = 0;
quit:
    if (rc < 0) {
        remove_module(c, id);
    }
    return rc;
}

static int hex_equal(const char *a, const char *b)
{
    char x, y;

    while (*a && *b) {
        x = *a;
        y = *b;
        if (x >= 'A' && x <= 'Z') {
            x = (char)(x + 32);
        }
        if (y >= 'A' && y <= 'Z') {
            y = (char)(y + 32);
        }
        if (x != y) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static int hash_matches(const char *path, struct jnode *hashes)
{
    char hex[65];
    const char *want;

    want = json_str(hashes, "sha256");
    if (want != NULL) {
        if (hash_file(path, "sha256", hex) < 0) {
            return 0;
        }
        return hex_equal(hex, want);
    }
    want = json_str(hashes, "sha1");
    if (want != NULL) {
        if (hash_file(path, "sha1", hex) < 0) {
            return 0;
        }
        return hex_equal(hex, want);
    }
    return 0;
}

/* where the archive of a module version is kept */
char *cache_file(struct ctx *c, struct entry *e)
{
    char *dir, *name, *full;
    int i;

    dir = shared_path(c, "cache");
    name = (char *)xalloc((long)(strlen(e->id) + strlen(e->version)) + 16);
    strcpy(name, e->id);
    strcat(name, "-");
    strcat(name, e->version);
    for (i = 0; name[i]; i++) {
        if (!((name[i] >= 'a' && name[i] <= 'z') ||
            (name[i] >= 'A' && name[i] <= 'Z') || is_digit(name[i]) ||
            name[i] == '.' || name[i] == '-'))
        {
            name[i] = '_';
        }
    }
    strcat(name, ".zip");
    full = join_path(dir, name);
    free(dir);
    free(name);
    return full;
}

/* fetch the module's archive into the cache, checking its hash; every
   download address is tried in turn */
int module_download(struct ctx *c, struct entry *e, char **path,
    char *err, int errsz)
{
    struct jnode *j, *dl, *hashes, *u;
    struct stat st;
    char *dir, *full, *part;
    const char *url;
    int rc;

    j = entry_json(c, e);
    dl = json_get(j, "download");
    hashes = json_get(j, "download_hash");
    if (dl == NULL || (dl->type != J_STR && dl->type != J_ARR) ||
        hashes == NULL || (json_str(hashes, "sha256") == NULL &&
        json_str(hashes, "sha1") == NULL))
    {
        set_msg(err, errsz, "no download or no hash in the metadata of ",
            e->id);
        return -1;
    }
    dir = shared_path(c, "cache");
    make_dirs(dir);
    free(dir);
    full = cache_file(c, e);
    rc = stat(full, &st);
    if (rc == 0 && hash_matches(full, hashes)) {
        *path = full;
        return 0;
    }
    part = (char *)xalloc((long)strlen(full) + 8);
    strcpy(part, full);
    strcat(part, ".part");
    printf("downloading %s %s\n", e->id, e->version);
    set_msg(err, errsz, "no usable download address for ", e->id);
    u = dl->type == J_ARR ? dl->child : dl;
    for (; u != NULL; u = u->next) {
        if (u->type != J_STR) {
            continue;
        }
        url = u->str;
        rc = fetch_url(url, part, 0);
        if (rc < 0) {
            set_msg(err, errsz, "download failed: ", url);
            continue;
        }
        if (!hash_matches(part, hashes)) {
            unlink(part);
            set_msg(err, errsz, "the checksum does not match for ", e->id);
            continue;
        }
        rc = rename(part, full);
        if (rc < 0) {
            set_msg(err, errsz, "cannot store the download of ", e->id);
            break;
        }
        free(part);
        *path = full;
        return 0;
    }
    free(part);
    free(full);
    return -1;
}
