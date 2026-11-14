/* finishing a site after wget: repair, duplicates, big files, links,
 * versions */
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "webarchiver.h"
#include "config.h"

static const char *page_exts[] = { ".html", ".htm", ".xhtml", ".shtml",
    ".css", 0 };
static const char *html_exts[] = { ".html", ".htm", ".xhtml", ".shtml",
    0 };

static int ends_ci(const char *s, const char **list)
{
    int i, ls, lx;

    ls = strlen(s);
    for (i = 0; list[i]; i++) {
        lx = strlen(list[i]);
        if (ls >= lx && str_ieq_n(s + ls - lx, list[i], lx))
            return 1;
    }
    return 0;
}

static int is_page(const char *path)
{
    return ends_ci(path, page_exts);
}

static int is_temp(const char *path)
{
    return ends_with(path, ".part") || ends_with(path, ".tmp");
}

/* "size mtime" of a file, for comparing with a snapshot */
static void file_stamp(const char *path, char *out)
{
    sprintf(out, "%lld %lld", file_size(path), file_mtime(path));
}

/* ------------------------------------------------------------ snapshot */

void pre_load(const char *d, struct table *pre)
{
    char *p;

    p = state_file(d, ".pre");
    tab_load(pre, p);
    free(p);
}

void pre_save(const char *d, struct table *pre)
{
    char *p;

    p = state_file(d, ".pre");
    tab_save(pre, p);
    free(p);
}

/* size and time of every file before wget runs */
void snapshot(const char *d, struct table *pre)
{
    struct strlist files;
    char stamp[64];
    char *dir, *r;
    int i;

    sl_init(&files);
    dir = domain_dir(d);
    list_files(dir, &files);
    free(dir);
    for (i = 0; i < files.n; i++) {
        file_stamp(files.v[i], stamp);
        r = rel_out(files.v[i]);
        tab_set(pre, r, stamp);
        free(r);
    }
    sl_free(&files);
}

/* wget --backups renames X to X.1 before overwriting X */
int is_backup(const char *rel, struct table *pre)
{
    const char *orig;
    char stamp[64];
    char *base, *p;
    int res;

    if (!ends_with(rel, ".1") || tab_get(pre, rel))
        return 0;
    base = xstrndup(rel, strlen(rel) - 2);
    orig = tab_get(pre, base);
    free(base);
    if (!orig)
        return 0;
    p = abs_out(rel);
    file_stamp(p, stamp);
    res = !strcmp(stamp, orig);
    free(p);
    return res;
}

/* ------------------------------------------------------------ repair */

/* undo half-finished work of an interrupted wget run */
void repair(const char *d, struct dstate *st, struct table *pre,
    struct urlmap *m)
{
    struct strlist files;
    struct table done;
    char stamp[64], num[32];
    const char *was;
    char *dir, *r, *b, *rb, *orig;
    int i, fixed;

    tab_init(&done);
    update_urlmap(d, m, st->log_offset, &done);
    if (!done.count && log_has_downloads(d, st->log_offset)) {
        say("  ", d, ": could not read the download log - skipping "
            "cleanup of unfinished files");
        tab_free(&done);
        return;
    }
    fixed = 0;
    sl_init(&files);
    dir = domain_dir(d);
    list_files(dir, &files);
    for (i = 0; i < files.n; i++) {
        r = rel_out(files.v[i]);
        if (ends_with(r, ".part") || is_backup(r, pre)) {
            free(r);
            continue;
        }
        file_stamp(files.v[i], stamp);
        was = tab_get(pre, r);
        if ((was && !strcmp(was, stamp)) || tab_get(&done, r)) {
            free(r);
            continue;
        }
        b = str2(files.v[i], ".1");
        rb = rel_out(b);
        if (is_file(b) && is_backup(rb, pre))
            rename(b, files.v[i]);  /* put back the old, complete copy */
        else
            unlink(files.v[i]);     /* half-downloaded new file */
        fixed++;
        free(rb);
        free(b);
        free(r);
    }
    sl_free(&files);
    /* a backup was made but the new file never written: restore it */
    sl_init(&files);
    list_files(dir, &files);
    for (i = 0; i < files.n; i++) {
        r = rel_out(files.v[i]);
        orig = xstrndup(files.v[i], strlen(files.v[i]) - 2);
        if (is_backup(r, pre) && !is_file(orig)) {
            rename(files.v[i], orig);
            fixed++;
        }
        free(orig);
        free(r);
    }
    sl_free(&files);
    free(dir);
    tab_free(&done);
    if (fixed) {
        sprintf(num, ": repaired %d unfinished file(s)", fixed);
        r = str2(num, " - they will be downloaded again");
        say("  ", d, r);
        free(r);
    }
}

/* old copies wget replaced: keep them in history if a version needs
 * them */
void process_backups(const char *d, struct table *pre,
    struct table *referenced, struct table *tree_shas)
{
    struct strlist files;
    char sha[65];
    char *dir, *r, *orig;
    int i;

    sl_init(&files);
    dir = domain_dir(d);
    list_files(dir, &files);
    free(dir);
    for (i = 0; i < files.n; i++) {
        r = rel_out(files.v[i]);
        if (!is_backup(r, pre)) {
            free(r);
            continue;
        }
        orig = xstrndup(files.v[i], strlen(files.v[i]) - 2);
        if (!is_file(orig)) {
            rename(files.v[i], orig);
        } else if (sha_file(files.v[i], sha) &&
            tab_get(referenced, sha) && !tab_get(tree_shas, sha))
        {
            store_object(files.v[i], sha, 1);
        } else {
            unlink(files.v[i]);
        }
        free(orig);
        free(r);
    }
    sl_free(&files);
}

/* ------------------------------------------------------------ dedupe */

/* absolute, normalised target of a symlink (one level) */
static char *link_target(const char *path)
{
    char tmp[4096];
    char *dir, *joined, *res;
    int n;

    n = readlink(path, tmp, sizeof(tmp) - 1);
    if (n <= 0)
        return 0;
    tmp[n] = 0;
    if (tmp[0] == '/')
        return norm_path(tmp);
    dir = xstrndup(path, strrchr(path, '/') - path);
    joined = path2(dir, tmp);
    res = norm_path(joined);
    free(joined);
    free(dir);
    return res;
}

/* delete files whose content already exists in the domain; links are
 * pointed at the kept copy later ("dup:<file>" in the index) */
static int dedupe(const char *d, struct table *index, struct table *cache,
    struct table *pre)
{
    struct strlist files, gone;
    const char *h, *k;
    char *dir, *r, *key, *dkey, *target, *kp, *tr;
    char sha[65];
    int i;

    sl_init(&files);
    sl_init(&gone);
    dir = domain_dir(d);
    list_files(dir, &files);
    for (i = 0; i < files.n; i++) {
        r = rel_out(files.v[i]);
        dkey = str2("dup:", r);
        if (is_link(files.v[i])) {
            target = link_target(files.v[i]);
            sl_add(&gone, files.v[i]);
            if (target && is_file(target) && starts_with(target, dir) &&
                target[strlen(dir)] == '/')
            {
                tr = rel_out(target);
                tab_set(index, dkey, tr);
                free(tr);
            }
            free(target);
        } else if (!is_temp(r) && !is_page(r) && !is_backup(r, pre)) {
            h = hash_of(cache, files.v[i]);
            strcpy(sha, h);
            key = str2("sha:", sha);
            k = tab_get(index, key);
            kp = k ? abs_out(k) : 0;
            if (k && strcmp(k, r) && is_file(kp) && !is_link(kp) &&
                !strcmp(hash_of(cache, kp), sha))
            {
                tab_set(index, dkey, k);
                sl_add(&gone, files.v[i]);
            } else {
                tab_set(index, key, r);
                tab_del(index, dkey);
            }
            free(kp);
            free(key);
        }
        free(dkey);
        free(r);
    }
    index_save(d, index);       /* record first, then delete */
    for (i = 0; i < gone.n; i++)
        unlink(gone.v[i]);
    i = gone.n;
    sl_free(&gone);
    sl_free(&files);
    free(dir);
    return i;
}

/* ------------------------------------------------------------ big files */

struct big {
    const char *d;
    struct table *index;
    struct table *referenced;
    struct table *cache;
    struct urlmap *map;
    struct table seen;      /* url -> local path ("" = not available) */
    int fetch;
    int recheck;
    int new_files;
    int dups;
    int updated;
};

static char *known(struct big *g, const char *key)
{
    const char *r;
    char *p;

    r = tab_get(g->index, key);
    if (!r)
        return 0;
    p = abs_out(r);
    if (is_file(p)) {
        free(p);
        return xstrdup(r);
    }
    free(p);
    return 0;
}

static char *known_global(const char *key)
{
    const char *r;
    char *p;

    r = global_get(key);
    if (!r)
        return 0;
    p = abs_out(r);
    if (is_file(p)) {
        free(p);
        return xstrdup(r);
    }
    free(p);
    return 0;
}

static void big_register(struct big *g, struct strlist *keys,
    const char *r)
{
    int i;

    for (i = 0; i < keys->n; i++) {
        tab_set(g->index, keys->v[i], r);
        if (starts_with(keys->v[i], "url:"))
            map_set(g->map, keys->v[i] + 4, r, 1);
    }
    index_save(g->d, g->index);
    global_set(keys, r);
}

/* where a big file goes: the domain folder, or _external/<host>/ */
static char *big_dest(const char *url, const char *d)
{
    char *host, *hd, *base, *path, *raw, *dest, *dir, *tmp;

    host = url_host(url);
    hd = host_dir(host);
    dir = domain_dir(d);
    if (!strcmp(hd, d)) {
        base = xstrdup(dir);
    } else {
        tmp = path2(dir, "_external");
        base = path2(tmp, hd);
        free(tmp);
    }
    raw = url_path(url);
    path = pct_unquote(raw[0] ? raw : "/index");
    tmp = str3(base, "/", path[0] == '/' ? path + 1 : path);
    dest = norm_path(tmp);
    if (!starts_with(dest, dir) || dest[strlen(dir)] != '/') {
        free(dest);
        dest = str2(dir, "/_external/file");
    }
    free(tmp);
    free(path);
    free(raw);
    free(base);
    free(dir);
    free(hd);
    free(host);
    return dest;
}

/* "name~1.ext" when name.ext is taken by a different file */
static char *free_name(const char *dest)
{
    char num[32];
    const char *dot, *slash;
    char *stem, *p;
    int n;

    slash = strrchr(dest, '/');
    dot = strrchr(dest, '.');
    if (!dot || (slash && dot < slash))
        dot = dest + strlen(dest);
    stem = xstrndup(dest, dot - dest);
    for (n = 1;; n++) {
        sprintf(num, "~%d", n);
        p = str3(stem, num, dot);
        if (!is_file(p))
            break;
        free(p);
    }
    free(stem);
    return p;
}

static void key_list(struct strlist *keys, const char *url,
    struct strlist *probed)
{
    char *k;
    int i;

    k = str2("url:", url);
    sl_add(keys, k);
    for (i = 0; i < probed->n; i++) {
        if (strcmp(probed->v[i], k))
            sl_add(keys, probed->v[i]);
    }
    free(k);
}

/* the local copy of a big file, downloading it only if it is new;
 * 0 if not available */
static char *big_get_new(struct big *g, const char *url)
{
    struct strlist probed, keys;
    char sha[65], line[64];
    char *k, *mine, *final, *size, *p, *dest, *res, *r;
    const char *same;
    int i, strong, matches, got;

    k = str2("url:", url);
    mine = known(g, k);
    free(k);
    if (mine && !g->recheck)
        return mine;
    if (!g->fetch || stop_requested())
        return mine;
    sl_init(&probed);
    if (!probe(url, &final, &size, &probed)) {
        sl_free(&probed);
        if (mine)
            return mine;
        say("  ! cannot check ", url, "");
        return 0;
    }
    sl_init(&keys);
    key_list(&keys, url, &probed);
    sl_free(&probed);
    res = 0;
    if (mine) {
        strong = 0;
        matches = 0;
        for (i = 0; i < keys.n; i++) {
            if (starts_with(keys.v[i], "url:"))
                continue;
            strong++;
            if (tab_get(g->index, keys.v[i]) &&
                !strcmp(tab_get(g->index, keys.v[i]), mine))
            {
                matches++;
            }
        }
        if (!strong || matches) {
            big_register(g, &keys, mine);
            res = mine;
            goto quit;
        }
        /* changed on the server: keep the old copy if history needs it */
        say("  ~ changed on server, updating: ", url, "");
        p = abs_out(mine);
        if (sha_file(p, sha) && tab_get(g->referenced, sha))
            store_object(p, sha, 1);
        else
            unlink(p);
        free(p);
        g->updated++;
    } else {
        for (i = 0; i < keys.n; i++) {
            r = known(g, keys.v[i]);
            if (r) {
                say("  = duplicate, not downloading: ", url, "");
                say("      same as ", r, "");
                g->dups++;
                big_register(g, &keys, r);
                res = r;
                goto quit;
            }
        }
        for (i = 0; i < keys.n; i++) {
            r = known_global(keys.v[i]);
            if (!r)
                continue;
            /* downloaded for another domain already: copy it */
            dest = big_dest(final, g->d);
            p = rel_out(dest);
            if (is_file(dest) && strcmp(p, r)) {
                free(p);
                p = free_name(dest);
                free(dest);
                dest = p;
                p = rel_out(dest);
            }
            say("  = already downloaded for another site, copying: ", url,
                "");
            {
                char *src, *dir;

                src = abs_out(r);
                dir = xstrndup(dest, strrchr(dest, '/') - dest);
                mkdirs(dir);
                copy_file(src, dest);
                free(dir);
                free(src);
            }
            g->dups++;
            big_register(g, &keys, p);
            free(dest);
            free(r);
            res = p;
            goto quit;
        }
    }
    dest = mine ? abs_out(mine) : big_dest(final, g->d);
    if (!mine && is_file(dest)) {
        p = free_name(dest);
        free(dest);
        dest = p;
    }
    if (size && size[0]) {
        human(parse_ll(size), line + 2);
        line[0] = ' ';
        line[1] = '(';
        strcat(line, ")");
    } else {
        line[0] = 0;
    }
    say("  + downloading ", url, line);
    got = download(final, dest, size, sha);
    if (got < 0) {
        say("  stopped while downloading ", url,
            " - it will continue next run");
        free(dest);
        goto quit;
    }
    if (!got) {
        say("  ! download failed (will continue next run) ", url, "");
        free(dest);
        goto quit;
    }
    p = str2(dest, ".part");
    k = str2("sha:", sha);
    same = tab_get(g->index, k);
    r = rel_out(dest);
    if (same && strcmp(same, r)) {
        char *sp;

        sp = abs_out(same);
        if (is_file(sp) && !strcmp(hash_of(g->cache, sp), sha)) {
            unlink(p);
            say("    identical to ", same, " - not kept twice");
            g->dups++;
            res = xstrdup(same);
            big_register(g, &keys, res);
            free(sp);
            free(r);
            free(k);
            free(p);
            free(dest);
            goto quit;
        }
        free(sp);
    }
    rename(p, dest);
    tab_set(g->index, k, r);
    g->new_files++;
    big_register(g, &keys, r);
    res = r;
    free(k);
    free(p);
    free(dest);
quit:
    if (res != mine)
        free(mine);
    sl_free(&keys);
    free(final);
    free(size);
    return res;
}

/* absolute path of the local copy, or 0 */
static char *big_get(struct big *g, const char *url)
{
    const char *seen;
    char *r, *p;

    seen = tab_get(&g->seen, url);
    if (seen)
        return seen[0] ? xstrdup(seen) : 0;
    r = big_get_new(g, url);
    p = r ? abs_out(r) : 0;
    tab_set(&g->seen, url, p ? p : "");
    free(r);
    return p;
}

/* on an update, ask the server about every big file once */
static void recheck_all(struct big *g)
{
    struct strlist keys;
    struct table done;
    const char *r;
    char *p;
    int i;

    sl_init(&keys);
    tab_keys(g->index, &keys);
    tab_init(&done);
    for (i = 0; i < keys.n; i++) {
        if (!starts_with(keys.v[i], "url:"))
            continue;
        r = tab_get(g->index, keys.v[i]);
        p = abs_out(r);
        if (!tab_get(&done, r) && is_file(p)) {
            tab_set(&done, r, "1");
            free(big_get(g, keys.v[i] + 4));
        }
        free(p);
    }
    tab_free(&done);
    sl_free(&keys);
}

/* ------------------------------------------------------------ links */

struct rewriter {
    const char *d;
    struct urlmap *map;
    struct table *index;
    struct big *big;
    char *base;     /* scheme://host of the site */
    char *host;
    int bad_links;
};

/* the kept copy of a deleted duplicate */
static char *canon(struct rewriter *w, const char *path)
{
    const char *k;
    char *r, *key, *p;

    r = rel_out(path);
    key = str2("dup:", r);
    k = tab_get(w->index, key);
    p = k ? abs_out(k) : 0;
    free(key);
    free(r);
    if (p && is_file(p))
        return p;
    free(p);
    return xstrdup(path);
}

static int same_site(const char *a, const char *b)
{
    if (starts_with(a, "www."))
        a += 4;
    if (starts_with(b, "www."))
        b += 4;
    return !strcmp(a, b);
}

/* the web address a local file came from (log, else its path) */
static char *page_url_for(struct rewriter *w, const char *page)
{
    const char *u;
    char *r, *inside, *dir, *q, *slash, *host, *res;

    r = rel_out(page);
    u = tab_get(&w->map->p, r);
    free(r);
    if (u)
        return xstrdup(u);
    dir = domain_dir(w->d);
    inside = rel_path(page, dir);
    free(dir);
    if (starts_with(inside, "_external/")) {
        slash = strchr(inside + 10, '/');
        if (slash) {
            host = xstrndup(inside + 10, slash - inside - 10);
            q = pct_quote(slash + 1, "/~@!$&'()*+,;=-._:");
            res = str3("https://", host, "/");
            r = str2(res, q);
            free(res);
            free(q);
            free(host);
            free(inside);
            return r;
        }
    }
    q = pct_quote(inside, "/~@!$&'()*+,;=-._:");
    res = str3(w->base, "/", q);
    free(q);
    free(inside);
    return res;
}

/* the local file for a URL of this site, by wget's naming rules */
static char *on_disk(struct rewriter *w, const char *url)
{
    static const char *suffix[] = { "", ".html", "/index.html", ".css", 0 };
    static const char *qsuffix[] = { "", ".html", ".css", 0 };
    char *host, *root, *raw, *path, *query, *cand, *full, *norm, *c, *tmp;
    int i;

    host = url_host(url);
    root = domain_dir(w->d);
    if (!same_site(host, w->host)) {
        tmp = path2(root, "_external");
        free(root);
        c = host_dir(host);
        root = path2(tmp, c);
        free(c);
        free(tmp);
    }
    raw = url_path(url);
    path = pct_unquote(raw[0] == '/' ? raw + 1 : raw);
    if (!path[0] || ends_with(path, "/")) {
        tmp = str2(path, "index.html");
        free(path);
        path = tmp;
    }
    query = url_query(url);
    full = 0;
    for (i = 0; query && qsuffix[i] && !full; i++) {
        cand = str3(path, "?", query);
        tmp = str3(root, "/", cand);
        c = str2(tmp, qsuffix[i]);
        norm = norm_path(c);
        if (starts_with(norm, root) && is_file(norm))
            full = canon(w, norm);
        free(norm);
        free(c);
        free(tmp);
        free(cand);
    }
    for (i = 0; suffix[i] && !full; i++) {
        tmp = str3(root, "/", path);
        c = str2(tmp, suffix[i]);
        norm = norm_path(c);
        if (starts_with(norm, root)) {
            cand = canon(w, norm);
            if (is_file(cand))
                full = cand;
            else
                free(cand);
        }
        free(norm);
        free(c);
        free(tmp);
    }
    free(query);
    free(path);
    free(raw);
    free(root);
    free(host);
    return full;
}

static char *lookup(struct rewriter *w, const char *url)
{
    const char *r;
    char *cands[3], *p;
    int i, n;

    n = strlen(url);
    cands[0] = xstrdup(url);
    cands[1] = n > 1 && url[n - 1] == '/' ? xstrndup(url, n - 1) :
        xstrdup(url);
    cands[2] = n > 0 && url[n - 1] == '/' ? xstrdup(url) : str2(url, "/");
    p = 0;
    for (i = 0; i < 3 && !p; i++) {
        r = tab_get(&w->map->u, cands[i]);
        if (r) {
            char *a;

            a = abs_out(r);
            p = canon(w, a);
            free(a);
            if (!is_file(p)) {
                free(p);
                p = 0;
            }
        }
    }
    for (i = 0; i < 3; i++)
        free(cands[i]);
    return p ? p : on_disk(w, url);
}

/* relative link from page_dir to target, percent-encoded */
static char *make_link(const char *target, const char *page_dir)
{
    char *r, *q, *colon, *slash, *res;

    r = rel_path(target, page_dir);
    q = pct_quote(r, "/~@!$&'()*+,;=-._");
    free(r);
    colon = strchr(q, ':');
    slash = strchr(q, '/');
    if (colon && (!slash || colon < slash)) {
        res = str2("./", q);
        free(q);
        return res;
    }
    return q;
}

static int is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

/* new value for one link, or 0 to leave it as it is */
static char *resolve(struct rewriter *w, const char *raw,
    const char *page_dir, const char *page_url, int is_html)
{
    static const char *skip[] = { "#", "data:", "mailto:", "javascript:",
        "tel:", "about:", "blob:", 0 };
    char *t, *base, *frag, *hash, *url, *p, *unq, *cand, *c, *res;
    const char *s;
    int i, abs, n;

    t = is_html ? html_unescape(raw) : xstrdup(raw);
    for (s = t; is_space((unsigned char)*s); s++)
        ;
    n = strlen(s);
    while (n > 0 && is_space((unsigned char)s[n - 1]))
        n--;
    base = xstrndup(s, n);
    free(t);
    res = 0;
    for (i = 0; skip[i]; i++) {
        if (str_ieq_n(base, skip[i], strlen(skip[i])))
            break;
    }
    if (!base[0] || skip[i])
        goto quit;
    hash = strchr(base, '#');
    frag = xstrdup(hash ? hash : "");
    if (hash)
        *hash = 0;
    if (!base[0]) {
        free(frag);
        goto quit;
    }
    if (base[0] == '/' && base[1] == '/') {
        char *scheme;

        scheme = page_url && strchr(page_url, ':') ?
            xstrndup(page_url, strchr(page_url, ':') - page_url) :
            xstrdup("https");
        p = str3(scheme, ":", base);
        free(scheme);
        free(base);
        base = p;
    }
    abs = url_has_scheme(base);
    if (abs && !url_is_http(base)) {
        free(frag);
        goto quit;
    }
    if (!abs && base[0] != '/') {
        /* a path that already points at a local file */
        unq = pct_unquote(base);
        for (i = 0; i < 2 && !res; i++) {
            if (i == 1 && strchr(unq, '?'))
                *strchr(unq, '?') = 0;
            else if (i == 1)
                break;
            cand = path2(page_dir, unq);
            p = norm_path(cand);
            c = canon(w, p);
            if (strcmp(c, p) || is_file(p)) {
                url = make_link(c, page_dir);
                res = str2(url, frag);
                free(url);
            }
            free(c);
            free(p);
            free(cand);
        }
        free(unq);
        if (res) {
            free(frag);
            goto quit;
        }
    }
    url = url_join(page_url, base);
    p = lookup(w, url);
    if (!p) {
        char *path;

        path = url_path(url);
        if (has_ext(path, &big_exts) || has_ext(path, &req_exts))
            p = big_get(w->big, url);  /* big file, or one wget missed */
        free(path);
    }
    if (p) {
        c = make_link(p, page_dir);
        res = str2(c, frag);
        free(c);
        free(p);
    } else if (!abs) {
        res = str2(url, frag);      /* not archived: the live site */
    }
    free(url);
    free(frag);
quit:
    free(base);
    return res;
}

/* a new value inside quotes in a page */
static void add_value(struct buf *b, const char *v, int is_html)
{
    for (; *v; v++) {
        if (*v == '"')
            buf_add(b, "%22");
        else if (*v == '\'')
            buf_add(b, "%27");
        else if (*v == '&' && is_html)
            buf_add(b, "&amp;");
        else
            buf_addc(b, *v);
    }
}

struct page {
    const char *dir;
    const char *url;
    int is_html;
};

static char *resolve_safe(struct rewriter *w, struct page *pg,
    const char *raw)
{
    if (strlen(raw) > 4000 || strchr(raw, '\n')) {
        w->bad_links++;
        return 0;
    }
    return resolve(w, raw, pg->dir, pg->url, pg->is_html);
}

static int name_at(const char *s, const char **names, int *len)
{
    int i, n;

    for (i = 0; names[i]; i++) {
        n = strlen(names[i]);
        if (str_ieq_n(s, names[i], n)) {
            const char *e;

            e = s + n;
            while (is_space((unsigned char)*e))
                e++;
            if (*e == '=') {
                *len = e + 1 - s;
                return 1;
            }
        }
    }
    return 0;
}

/* one pass over a page: attributes (srcset == 1: comma lists) */
static char *pass_attrs(struct rewriter *w, struct page *pg,
    const char *text, const char **names, int srcset)
{
    struct buf out;
    const char *s, *v, *e;
    char *val, *nv;
    int len, q, prev;

    buf_init(&out);
    s = text;
    while (*s) {
        prev = s > text ? (unsigned char)s[-1] : 0;
        if (!(prev && (is_space(prev) || prev == '"' || prev == '\'' ||
            prev == '/')) || !name_at(s, names, &len))
        {
            buf_addc(&out, *s);
            s++;
            continue;
        }
        v = s + len;
        while (is_space((unsigned char)*v))
            v++;
        q = *v == '"' || *v == '\'' ? *v : 0;
        if (srcset && !q) {
            buf_addc(&out, *s);
            s++;
            continue;
        }
        e = q ? strchr(v + 1, q) : v;
        if (!q) {
            while (*e && !is_space((unsigned char)*e) && *e != '"' &&
                *e != '\'' && *e != '>')
            {
                e++;
            }
        }
        if (!e || (!q && e == v)) {
            buf_addc(&out, *s);
            s++;
            continue;
        }
        val = q ? xstrndup(v + 1, e - v - 1) : xstrndup(v, e - v);
        if (srcset) {
            struct strlist items;
            int i, changed;

            sl_init(&items);
            sl_split(&items, val, ',');
            changed = 0;
            buf_addn(&out, s, v - s);
            buf_addc(&out, q);
            for (i = 0; i < items.n; i++) {
                char *sp, *rest;

                sp = items.v[i];
                rest = sp + strcspn(sp, " \t\n");
                if (i)
                    buf_add(&out, ", ");
                {
                    char *first;

                    first = xstrndup(sp, rest - sp);
                    nv = resolve_safe(w, pg, first);
                    free(first);
                }
                if (nv) {
                    add_value(&out, nv, pg->is_html);
                    free(nv);
                    changed = 1;
                } else {
                    buf_addn(&out, sp, rest - sp);
                }
                while (is_space((unsigned char)*rest))
                    rest++;
                if (*rest) {
                    buf_addc(&out, ' ');
                    buf_add(&out, rest);
                }
            }
            (void)changed;
            buf_addc(&out, q);
            sl_free(&items);
            s = e + 1;
        } else {
            nv = resolve_safe(w, pg, val);
            if (nv) {
                buf_addn(&out, s, v - s);
                buf_addc(&out, q ? q : '"');
                add_value(&out, nv, pg->is_html);
                buf_addc(&out, q ? q : '"');
                free(nv);
            } else {
                buf_addn(&out, s, (q ? e + 1 : e) - s);
            }
            s = q ? e + 1 : e;
        }
        free(val);
    }
    return out.s;
}

/* url(...) in CSS and in style attributes; @import "..." in CSS */
static char *pass_css(struct rewriter *w, struct page *pg,
    const char *text, int imports)
{
    struct buf out;
    const char *s, *v, *e, *after;
    char *val, *nv;
    int q, n;

    buf_init(&out);
    s = text;
    while (*s) {
        n = 0;
        if (str_ieq_n(s, "url(", 4)) {
            n = 4;
        } else if (imports && str_ieq_n(s, "@import", 7) &&
            is_space((unsigned char)s[7]))
        {
            n = 7;
        }
        if (!n) {
            buf_addc(&out, *s);
            s++;
            continue;
        }
        v = s + n;
        while (is_space((unsigned char)*v))
            v++;
        q = *v == '"' || *v == '\'' ? *v : 0;
        if (n == 7 && !q) {
            buf_addn(&out, s, n);
            s += n;
            continue;
        }
        if (q) {
            e = strchr(v + 1, q);
        } else {
            e = v;
            while (*e && *e != ')' && !is_space((unsigned char)*e) &&
                *e != '"' && *e != '\'')
            {
                e++;
            }
        }
        if (!e || (!q && e == v)) {
            buf_addn(&out, s, n);
            s += n;
            continue;
        }
        after = q ? e + 1 : e;
        if (n == 4) {
            while (is_space((unsigned char)*after))
                after++;
            if (*after != ')') {
                buf_addn(&out, s, n);
                s += n;
                continue;
            }
        }
        val = q ? xstrndup(v + 1, e - v - 1) : xstrndup(v, e - v);
        nv = resolve_safe(w, pg, val);
        if (nv) {
            buf_addn(&out, s, v - s);
            buf_addc(&out, q ? q : '"');
            add_value(&out, nv, pg->is_html);
            buf_addc(&out, q ? q : '"');
            free(nv);
            s = q ? e + 1 : e;
        } else {
            buf_addn(&out, s, (q ? e + 1 : e) - s);
            s = q ? e + 1 : e;
        }
        free(val);
    }
    return out.s;
}

/* "http://.../movie.mp4" anywhere in quotes (scripts): local copy */
static char *pass_big(struct rewriter *w, struct page *pg,
    const char *text)
{
    struct buf out;
    const char *s, *e;
    char *val, *url, *path, *p, *link, *d;
    int q, ok;

    buf_init(&out);
    s = text;
    while (*s) {
        q = *s;
        if ((q != '"' && q != '\'') || !(str_ieq_n(s + 1, "http:", 5) ||
            str_ieq_n(s + 1, "https:", 6)))
        {
            buf_addc(&out, *s);
            s++;
            continue;
        }
        e = s + 1;
        while (*e && *e != q && *e != '"' && *e != '\'' && *e != '<' &&
            *e != '>' && !is_space((unsigned char)*e))
        {
            e++;
        }
        if (*e != q) {
            buf_addc(&out, *s);
            s++;
            continue;
        }
        val = xstrndup(s + 1, e - s - 1);
        /* JSON-style "\/" in scripts */
        url = xstrdup(val);
        for (p = url, d = url; *p; p++) {
            if (p[0] == '\\' && p[1] == '/')
                continue;
            *d = *p;
            d++;
        }
        *d = 0;
        if (strchr(url, '#'))
            *strchr(url, '#') = 0;
        path = url_path(url);
        ok = has_ext(path, &big_exts);
        p = ok ? big_get(w->big, url) : 0;
        if (p) {
            link = make_link(p, pg->dir);
            buf_addc(&out, q);
            buf_add(&out, link);
            buf_addc(&out, q);
            free(link);
            free(p);
        } else {
            buf_addn(&out, s, e + 1 - s);
        }
        free(path);
        free(url);
        free(val);
        s = e + 1;
    }
    return out.s;
}

static const char *attr_names[] = { "href", "src", "poster", "data",
    "action", "background", "longdesc", "cite", "formaction", "icon",
    "manifest", "usemap", "data-src", "data-href", "data-poster",
    "data-background", "data-url", 0 };
static const char *srcset_names[] = { "srcset", "imagesrcset",
    "data-srcset", 0 };

static char *convert(struct rewriter *w, const char *text,
    const char *page)
{
    struct page pg;
    char *dir, *url, *a, *b;

    dir = xstrndup(page, strrchr(page, '/') - page);
    url = page_url_for(w, page);
    pg.dir = dir;
    pg.url = url;
    pg.is_html = ends_ci(page, html_exts);
    if (pg.is_html) {
        a = pass_attrs(w, &pg, text, srcset_names, 1);
        b = pass_attrs(w, &pg, a, attr_names, 0);
        free(a);
        a = pass_css(w, &pg, b, 0);
        free(b);
        b = pass_big(w, &pg, a);
        free(a);
    } else {
        b = pass_css(w, &pg, text, 1);
    }
    free(url);
    free(dir);
    return b;
}

static int rewrite_pages(const char *d, struct rewriter *w,
    struct table *referenced, struct table *pre)
{
    struct strlist files;
    char sha[65];
    char *dir, *r, *data, *out;
    long long mtime;
    int i, len, changed;

    sl_init(&files);
    dir = domain_dir(d);
    list_files(dir, &files);
    free(dir);
    changed = 0;
    for (i = 0; i < files.n; i++) {
        r = rel_out(files.v[i]);
        if (!is_page(r) || is_backup(r, pre) || is_link(files.v[i])) {
            free(r);
            continue;
        }
        free(r);
        data = read_file(files.v[i], &len);
        if (!data)
            continue;
        if ((int)strlen(data) != len) {
            free(data);     /* contains zero bytes: not a text page */
            continue;
        }
        out = convert(w, data, files.v[i]);
        if (strcmp(out, data)) {
            sha_data(data, len, sha);
            if (tab_get(referenced, sha))
                store_object_data(data, len, sha);
            mtime = file_mtime(files.v[i]);
            write_file(files.v[i], out, strlen(out));
            set_mtime(files.v[i], mtime);   /* wget's update check */
            changed++;
        }
        free(out);
        free(data);
    }
    sl_free(&files);
    return changed;
}

/* ------------------------------------------------------------ versions */

/* site path -> "sha size" for every file of the current version */
static void current_tree(const char *d, struct table *cache,
    struct table *pre, struct table *tree, struct table *shas)
{
    struct strlist files;
    char val[128];
    const char *h;
    char *dir, *r, *inside;
    int i;

    sl_init(&files);
    dir = domain_dir(d);
    list_files(dir, &files);
    for (i = 0; i < files.n; i++) {
        r = rel_out(files.v[i]);
        if (!is_temp(r) && !is_backup(r, pre) && !is_link(files.v[i])) {
            h = hash_of(cache, files.v[i]);
            sprintf(val, "%s %lld", h, file_size(files.v[i]));
            inside = rel_path(files.v[i], dir);
            tab_set(tree, inside, val);
            tab_set(shas, h, "1");
            free(inside);
        }
        free(r);
    }
    free(dir);
    sl_free(&files);
}

static int same_table(struct table *a, struct table *b)
{
    struct strlist keys;
    const char *v;
    int i, same;

    if (a->count != b->count)
        return 0;
    sl_init(&keys);
    tab_keys(a, &keys);
    same = 1;
    for (i = 0; i < keys.n && same; i++) {
        v = tab_get(b, keys.v[i]);
        same = v && !strcmp(v, tab_get(a, keys.v[i]));
    }
    sl_free(&keys);
    return same;
}

/* save the tree as a new version if it differs from the last one;
 * counts[] = new, changed, removed; 0 if nothing changed */
static int record_version(const char *d, struct table *tree, int partial,
    int *counts)
{
    struct strlist ids, keys;
    struct table last, info;
    struct buf changes;
    const char *old;
    char id[32];
    char *dir, *p, *name;
    int i;

    sl_init(&ids);
    versions_of(d, &ids);
    tab_init(&last);
    if (ids.n)
        version_files(d, ids.v[ids.n - 1], &last);
    sl_free(&ids);
    if (same_table(tree, &last)) {
        tab_free(&last);
        return 0;
    }
    counts[0] = 0;
    counts[1] = 0;
    counts[2] = 0;
    buf_init(&changes);
    sl_init(&keys);
    tab_keys(tree, &keys);
    for (i = 0; i < keys.n; i++) {
        old = tab_get(&last, keys.v[i]);
        if (!old) {
            buf_add(&changes, "+\t");
            counts[0]++;
        } else if (strncmp(old, tab_get(tree, keys.v[i]), 64)) {
            buf_add(&changes, "~\t");
            counts[1]++;
        } else {
            continue;
        }
        buf_add(&changes, keys.v[i]);
        buf_addc(&changes, '\n');
    }
    sl_free(&keys);
    sl_init(&keys);
    tab_keys(&last, &keys);
    for (i = 0; i < keys.n; i++) {
        if (!tab_get(tree, keys.v[i])) {
            buf_add(&changes, "-\t");
            buf_add(&changes, keys.v[i]);
            buf_addc(&changes, '\n');
            counts[2]++;
        }
    }
    sl_free(&keys);
    utc_id(now(), id);
    dir = path2(hist_dir, d);
    mkdirs(dir);
    name = str2(id, ".changes");
    p = path2(dir, name);
    write_file(p, changes.s, changes.len);
    free(p);
    free(name);
    tab_init(&info);
    tab_set(&info, "partial", partial ? "1" : "0");
    name = str2(id, ".info");
    p = path2(dir, name);
    tab_save(&info, p);
    free(p);
    free(name);
    tab_free(&info);
    /* the .files list last: it is what makes the version exist */
    name = str2(id, ".files");
    p = path2(dir, name);
    tab_save(tree, p);
    free(p);
    free(name);
    free(dir);
    buf_free(&changes);
    tab_free(&last);
    return 1;
}

/* ------------------------------------------------------------ finalize */

int finalize(const char *d, const char *rc, int fetch)
{
    struct dstate st;
    struct table pre, index, cache, referenced, tree, shas;
    struct urlmap map;
    struct big big;
    struct rewriter w;
    struct strlist ids;
    char line[256], total[64], size[32], summary[200];
    char *p, *dir;
    struct strlist keys;
    long long bytes;
    int dups, pages, round, before, ok, counts[3], i;

    state_load(d, &st);
    if (strcmp(st.phase, "crawl") && strcmp(st.phase, "finalize"))
        return 0;
    say("--- ", d, "");
    tab_init(&pre);
    pre_load(d, &pre);
    map_load(d, &map);
    if (!strcmp(st.phase, "crawl")) {
        status(d, "checking for unfinished files", 0);
        repair(d, &st, &pre, &map);
        strcpy(st.phase, "finalize");
        state_save(d, &st);
    }
    update_urlmap(d, &map, 0, 0);
    map_save(d, &map);
    referenced_shas(d, &referenced);
    cache_load(d, &cache);
    index_load(d, &index);
    status(d, "removing duplicates", 0);
    dups = dedupe(d, &index, &cache, &pre);

    big.d = d;
    big.index = &index;
    big.referenced = &referenced;
    big.cache = &cache;
    big.map = &map;
    tab_init(&big.seen);
    big.fetch = fetch;
    big.recheck = !strcmp(st.mode, "update") && !strcmp(rc, "0");
    big.new_files = 0;
    big.dups = 0;
    big.updated = 0;
    if (big.recheck && fetch) {
        status(d, "checking big files for changes", 0);
        recheck_all(&big);
    }

    w.d = d;
    w.map = &map;
    w.index = &index;
    w.big = &big;
    p = state_file(d, ".base");
    w.base = read_file(p, 0);
    free(p);
    if (w.base && w.base[0]) {
        p = url_origin(w.base);
        free(w.base);
        w.base = p;
    } else {
        free(w.base);
        w.base = str2("https://", d);
    }
    w.host = url_host(w.base);
    w.bad_links = 0;
    pages = 0;
    for (round = 0; round < 4; round++) {
        /* new CSS files can bring fonts and images of their own */
        before = big.new_files;
        status(d, "fixing links", 0);
        pages += rewrite_pages(d, &w, &referenced, &pre);
        if (big.new_files == before)
            break;
    }
    map_save(d, &map);

    status(d, "saving version", 0);
    tab_init(&tree);
    tab_init(&shas);
    current_tree(d, &cache, &pre, &tree, &shas);
    process_backups(d, &pre, &referenced, &shas);
    cache_save(d, &cache);
    ok = !strcmp(rc, "0");
    if (strcmp(rc, "skip"))
        st.complete = st.complete || ok;
    sprintf(line, "  %d duplicate files removed, %d big files downloaded "
        "(%d updated), %d big-file duplicates skipped, %d pages relinked",
        dups, big.new_files, big.updated, big.dups, pages);
    say(line, 0, 0);
    if (w.bad_links) {
        sprintf(line, "  %d malformed link(s) on the site were left "
            "unchanged", w.bad_links);
        say(line, 0, 0);
    }
    bytes = 0;
    sl_init(&keys);
    tab_keys(&tree, &keys);
    for (i = 0; i < keys.n; i++)
        bytes += parse_ll(tab_get(&tree, keys.v[i]) + 65);
    human(bytes, size);
    sprintf(total, "%d files, %s in archive", keys.n, size);
    sl_free(&keys);
    if (st.complete) {
        if (record_version(d, &tree, !ok, counts)) {
            sl_init(&ids);
            versions_of(d, &ids);
            sprintf(summary, "version %d saved: +%d new, ~%d changed, "
                "-%d removed", ids.n, counts[0], counts[1], counts[2]);
            sl_free(&ids);
            say("  ", summary, ok ? "" : "  [run was interrupted]");
        } else {
            strcpy(summary, "no changes since the last version");
            say("  ", summary, "");
        }
    } else {
        strcpy(summary, "links fixed for what is downloaded - run again "
            "to finish");
        say("  not finished yet - run the same command again to continue",
            0, 0);
    }
    p = str3(summary, "  |  ", total);
    status(d, p, 0);
    free(p);
    dir = domain_dir(d);
    remove_empty_dirs(dir);
    free(dir);
    strcpy(st.phase, "idle");
    state_save(d, &st);
    p = state_file(d, ".pre");
    unlink(p);
    free(p);
    write_pages();

    tab_free(&tree);
    tab_free(&shas);
    tab_free(&big.seen);
    tab_free(&index);
    tab_free(&cache);
    tab_free(&referenced);
    tab_free(&pre);
    map_free(&map);
    free(w.base);
    free(w.host);
    return 0;
}
