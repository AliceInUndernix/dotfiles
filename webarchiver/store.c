/* the archive's own data: flat text files, one record per line */
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "webarchiver.h"

/* ------------------------------------------------------------ paths */

char *domain_dir(const char *d)
{
    return path2(out_dir, d);
}

char *state_file(const char *d, const char *ext)
{
    char *name, *p;

    name = str2(d, ext);
    p = path2(state_dir, name);
    free(name);
    return p;
}

/* path relative to the archive directory, and back */
char *rel_out(const char *path)
{
    int n;

    n = strlen(out_dir);
    if (!strncmp(path, out_dir, n) && path[n] == '/')
        return xstrdup(path + n + 1);
    return xstrdup(path);
}

char *abs_out(const char *rel)
{
    return path2(out_dir, rel);
}

/* ------------------------------------------------------------ state */

void state_load(const char *d, struct dstate *st)
{
    struct table t;
    const char *v;
    char *p;

    st->complete = 0;
    strcpy(st->phase, "idle");
    strcpy(st->mode, "new");
    st->log_offset = 0;
    tab_init(&t);
    p = state_file(d, ".state");
    tab_load(&t, p);
    free(p);
    v = tab_get(&t, "complete");
    if (v)
        st->complete = v[0] == '1';
    v = tab_get(&t, "phase");
    if (v && strlen(v) < sizeof(st->phase))
        strcpy(st->phase, v);
    v = tab_get(&t, "mode");
    if (v && strlen(v) < sizeof(st->mode))
        strcpy(st->mode, v);
    v = tab_get(&t, "log_offset");
    if (v)
        st->log_offset = parse_ll(v);
    tab_free(&t);
}

void state_save(const char *d, struct dstate *st)
{
    struct table t;
    char num[32];
    char *p;

    tab_init(&t);
    tab_set(&t, "complete", st->complete ? "1" : "0");
    tab_set(&t, "phase", st->phase);
    tab_set(&t, "mode", st->mode);
    sprintf(num, "%lld", st->log_offset);
    tab_set(&t, "log_offset", num);
    p = state_file(d, ".state");
    tab_save(&t, p);
    free(p);
    tab_free(&t);
}

/* one line for the progress table */
void status(const char *d, const char *a, const char *b)
{
    char *p, *msg;

    msg = str2(a, b ? b : "");
    p = state_file(d, ".status");
    write_file(p, msg, strlen(msg));
    free(p);
    free(msg);
}

/* ------------------------------------------------------------ url map */

void map_set(struct urlmap *m, const char *url, const char *rel, int keep)
{
    tab_set(&m->u, url, rel);
    if (!keep || !tab_get(&m->p, rel))
        tab_set(&m->p, rel, url);
}

void map_load(const char *d, struct urlmap *m)
{
    struct table t;
    struct strlist keys;
    char *p;
    int i;

    tab_init(&m->u);
    tab_init(&m->p);
    tab_init(&t);
    p = state_file(d, ".urlmap");
    tab_load(&t, p);
    free(p);
    sl_init(&keys);
    tab_keys(&t, &keys);
    for (i = 0; i < keys.n; i++)
        map_set(m, keys.v[i], tab_get(&t, keys.v[i]), 0);
    sl_free(&keys);
    tab_free(&t);
}

void map_save(const char *d, struct urlmap *m)
{
    char *p;

    p = state_file(d, ".urlmap");
    tab_save(&m->u, p);
    free(p);
}

void map_free(struct urlmap *m)
{
    tab_free(&m->u);
    tab_free(&m->p);
}

/* closing quote of the file name in a wget log line: " ' or the
 * typographic quotes some translations use */
static int quote_len_end(const char *s, int n)
{
    if (n >= 1 && (s[n - 1] == '"' || s[n - 1] == '\''))
        return 1;
    if (n >= 2 && (unsigned char)s[n - 2] == 0xc2 &&
        (unsigned char)s[n - 1] == 0xbb)
    {
        return 2;
    }
    if (n >= 3 && (unsigned char)s[n - 3] == 0xe2 &&
        (unsigned char)s[n - 2] == 0x80 &&
        ((unsigned char)s[n - 1] == 0x9d || (unsigned char)s[n - 1] == 0x99))
    {
        return 3;
    }
    return 0;
}

static int quote_len_start(const char *s)
{
    const unsigned char *u;

    u = (const unsigned char *)s;
    if (u[0] == '"' || u[0] == '\'')
        return 1;
    if (u[0] == 0xc2 && u[1] == 0xab)
        return 2;
    if (u[0] == 0xe2 && u[1] == 0x80 && (u[2] == 0x9c || u[2] == 0x98))
        return 3;
    return 0;
}

/* "... URL:<url> [n/n] -> "<file>" [1]" -> url and file */
int parse_log_line(const char *line, char **url, char **path)
{
    const char *u, *e, *arrow, *last, *p;
    int qs, qe;

    u = strstr(line, "URL:");
    if (!u)
        return 0;
    u += 4;
    while (*u == ' ')
        u++;
    e = u;
    while (*e && *e != ' ')
        e++;
    if (e[0] != ' ' || e[1] != '[')
        return 0;
    arrow = strstr(e, "] -> ");
    if (!arrow)
        return 0;
    p = arrow + 5;
    last = 0;
    for (e = p; (e = strstr(e, " [")) != 0; e++)
        last = e;
    if (!last)
        return 0;
    qs = quote_len_start(p);
    qe = quote_len_end(p, last - p);
    if (!qs || !qe || last - p <= qs + qe)
        return 0;
    if (url)
        *url = xstrndup(u, strcspn(u, " "));
    if (path)
        *path = xstrndup(p + qs, last - p - qs - qe);
    return 1;
}

/* read the domain's wget log: url <-> file; files finished after
 * byte offset since go into done (if given) */
void update_urlmap(const char *d, struct urlmap *m, long long since,
    struct table *done)
{
    char *log, *data, *line, *next, *url, *path, *r;
    long long pos;
    int len, n;

    r = path2(logs_dir, d);
    log = str2(r, ".log");
    free(r);
    data = read_file(log, &len);
    free(log);
    if (!data)
        return;
    pos = 0;
    for (line = data; *line; line = next) {
        next = strchr(line, '\n');
        n = next ? next - line + 1 : (int)strlen(line);
        if (next)
            *next = 0;
        next = line + n;
        if (parse_log_line(line, &url, &path)) {
            if (path[0] == '/' && starts_with(path, out_dir) &&
                path[strlen(out_dir)] == '/')
            {
                r = rel_out(path);
                map_set(m, url, r, 0);
                if (done && pos >= since)
                    tab_set(done, r, "1");
                free(r);
            }
            free(url);
            free(path);
        }
        pos += n;
    }
    free(data);
}

int log_has_downloads(const char *d, long long since)
{
    char *log, *p, *data;
    int len, found;

    log = path2(logs_dir, d);
    p = str2(log, ".log");
    data = read_file(p, &len);
    free(log);
    free(p);
    if (!data)
        return 0;
    found = since < len && strstr(data + since, "URL:") &&
        strstr(data + since, " -> ");
    free(data);
    return found;
}

/* ------------------------------------------------------------ indexes */

void index_load(const char *d, struct table *t)
{
    char *p;

    tab_init(t);
    p = state_file(d, ".index");
    tab_load(t, p);
    free(p);
}

void index_save(const char *d, struct table *t)
{
    char *p;

    p = state_file(d, ".index");
    tab_save(t, p);
    free(p);
}

/* the cross-domain index is shared by processes: mkdir is the lock */
static char *global_lock(void)
{
    char *lock;
    int tries;

    lock = path2(state_dir, "global.lock");
    for (tries = 0; tries < 300; tries++) {
        if (mkdir(lock, 0755) == 0)
            return lock;
        sleep_ms(50);
    }
    return lock;    /* a stale lock after a crash: go on */
}

static void global_unlock(char *lock)
{
    rmdir(lock);
    free(lock);
}

const char *global_get(const char *key)
{
    static struct table t;
    static int loaded;
    char *p;

    if (loaded)
        tab_free(&t);
    tab_init(&t);
    loaded = 1;
    p = path2(state_dir, "global.index");
    tab_load(&t, p);
    free(p);
    return tab_get(&t, key);
}

void global_set(struct strlist *keys, const char *rel)
{
    struct table t;
    char *lock, *p;
    int i;

    lock = global_lock();
    tab_init(&t);
    p = path2(state_dir, "global.index");
    tab_load(&t, p);
    for (i = 0; i < keys->n; i++)
        tab_set(&t, keys->v[i], rel);
    tab_save(&t, p);
    free(p);
    tab_free(&t);
    global_unlock(lock);
}

/* ------------------------------------------------------------ hashes */

void cache_load(const char *d, struct table *cache)
{
    char *p;

    tab_init(cache);
    p = state_file(d, ".hashes");
    tab_load(cache, p);
    free(p);
}

void cache_save(const char *d, struct table *cache)
{
    struct strlist keys;
    char *p;
    int i;

    sl_init(&keys);
    tab_keys(cache, &keys);
    for (i = 0; i < keys.n; i++) {
        p = abs_out(keys.v[i]);
        if (!is_file(p))
            tab_del(cache, keys.v[i]);
        free(p);
    }
    sl_free(&keys);
    p = state_file(d, ".hashes");
    tab_save(cache, p);
    free(p);
}

/* SHA-256 of a file, remembered by size and time ("sha size mtime") */
const char *hash_of(struct table *cache, const char *path)
{
    static char hex[65];
    char val[128];
    const char *e;
    char *r;
    long long size, mtime;

    size = file_size(path);
    mtime = file_mtime(path);
    r = rel_out(path);
    e = tab_get(cache, r);
    sprintf(val, " %lld %lld", size, mtime);
    if (e && strlen(e) > 64 && !strcmp(e + 64, val)) {
        memcpy(hex, e, 64);
        hex[64] = 0;
        free(r);
        return hex;
    }
    if (!sha_file(path, hex))
        hex[0] = 0;
    sprintf(val, "%s %lld %lld", hex, size, mtime);
    tab_set(cache, r, val);
    free(r);
    return hex;
}

/* ------------------------------------------------------------ versions */

/* version ids of a domain, oldest first */
void versions_of(const char *d, struct strlist *ids)
{
    struct strlist names;
    char *dir;
    int i, n;

    sl_init(&names);
    dir = path2(hist_dir, d);
    list_dir(dir, &names);
    free(dir);
    for (i = 0; i < names.n; i++) {
        n = strlen(names.v[i]);
        if (ends_with(names.v[i], ".files")) {
            names.v[i][n - 6] = 0;
            sl_add(ids, names.v[i]);
        }
    }
    sl_free(&names);
    sl_sort(ids);
}

static char *version_file(const char *d, const char *id, const char *ext)
{
    char *dir, *name, *p;

    dir = path2(hist_dir, d);
    name = str2(id, ext);
    p = path2(dir, name);
    free(dir);
    free(name);
    return p;
}

/* path inside the site -> "sha size" */
void version_files(const char *d, const char *id, struct table *files)
{
    char *p;

    p = version_file(d, id, ".files");
    tab_load(files, p);
    free(p);
}

int version_info(const char *d, const char *id, const char *key)
{
    struct table t;
    const char *v;
    char *p;
    int res;

    tab_init(&t);
    p = version_file(d, id, ".info");
    tab_load(&t, p);
    free(p);
    v = tab_get(&t, key);
    res = v ? (int)parse_ll(v) : 0;
    tab_free(&t);
    return res;
}

/* lines "+<TAB>path", "~<TAB>path", "-<TAB>path" */
void version_changes(const char *d, const char *id, struct strlist *lines)
{
    char *p, *data, *line, *next;
    int len;

    p = version_file(d, id, ".changes");
    data = read_file(p, &len);
    free(p);
    if (!data)
        return;
    for (line = data; *line; line = next) {
        next = strchr(line, '\n');
        if (next) {
            *next = 0;
            next++;
        } else {
            next = line + strlen(line);
        }
        if (line[0] && line[1] == '\t')
            sl_add(lines, line);
    }
    free(data);
}

void referenced_shas(const char *d, struct table *shas)
{
    struct strlist ids, keys;
    struct table files;
    char sha[65];
    int i, j;

    tab_init(shas);
    sl_init(&ids);
    versions_of(d, &ids);
    for (i = 0; i < ids.n; i++) {
        tab_init(&files);
        version_files(d, ids.v[i], &files);
        sl_init(&keys);
        tab_keys(&files, &keys);
        for (j = 0; j < keys.n; j++) {
            memcpy(sha, tab_get(&files, keys.v[j]), 64);
            sha[64] = 0;
            tab_set(shas, sha, "1");
        }
        sl_free(&keys);
        tab_free(&files);
    }
    sl_free(&ids);
}

/* ------------------------------------------------------------ objects */

char *obj_path(const char *sha)
{
    char sub[3];
    char *dir, *p;

    sub[0] = sha[0];
    sub[1] = sha[1];
    sub[2] = 0;
    dir = path2(obj_dir, sub);
    p = path2(dir, sha);
    free(dir);
    return p;
}

/* keep an old copy for history (once; content-addressed) */
void store_object(const char *path, const char *sha, int move)
{
    char tail[32];
    char *dst, *dir, *tmp;

    dst = obj_path(sha);
    if (is_file(dst)) {
        if (move)
            unlink(path);
        free(dst);
        return;
    }
    dir = xstrndup(dst, strrchr(dst, '/') - dst);
    mkdirs(dir);
    free(dir);
    sprintf(tail, ".%ld.tmp", (long)getpid());
    tmp = str2(dst, tail);
    if (move) {
        if (rename(path, tmp) != 0) {
            copy_file(path, tmp);
            unlink(path);
        }
    } else {
        copy_file(path, tmp);
    }
    rename(tmp, dst);
    free(tmp);
    free(dst);
}

void store_object_data(const char *data, int len, const char *sha)
{
    char *dst, *dir;

    dst = obj_path(sha);
    if (!is_file(dst)) {
        dir = xstrndup(dst, strrchr(dst, '/') - dst);
        mkdirs(dir);
        free(dir);
        write_file(dst, data, len);
    }
    free(dst);
}

/* every archived domain (it has a state file and a folder) */
void all_domains(struct strlist *out)
{
    struct strlist names;
    char *d, *dir;
    int i;

    sl_init(&names);
    list_dir(state_dir, &names);
    for (i = 0; i < names.n; i++) {
        if (!ends_with(names.v[i], ".state"))
            continue;
        d = xstrndup(names.v[i], strlen(names.v[i]) - 6);
        dir = domain_dir(d);
        if (is_dir(dir))
            sl_add(out, d);
        free(dir);
        free(d);
    }
    sl_free(&names);
}
