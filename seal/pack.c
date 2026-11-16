/* pack.c: the archive inside a sealed file, and its encryption.
 *
 * A sealed file is a 16-byte random salt, then chunks: each is up to
 * 64 KiB of the archive, encrypted with XChaCha20-Poly1305 (16 bytes of
 * mac after it). The key is Argon2id(password, salt). Chunk n's nonce is
 * n (8 bytes, little endian), then a byte that is 1 only in the last
 * chunk, then zeros. Every chunk but the last is full, and the last is
 * not, so nothing can be cut off, added, swapped or changed unnoticed.
 * No header, no magic: the whole file looks like random bytes.
 *
 * The archive is flat: records one after another, a directory before
 * what is in it, numbers little endian:
 *
 *     type (1 byte: d f l e)  mode (2)  path length (2)  path
 *     f: then size (8) and the content
 *     l: then target length (2) and the target
 *     e: the end (type only)
 *
 * Paths start with the top directory's name ("baba", "baba/notes").
 * Kept: names, contents, rwx bits (0777), symbolic links. Not kept:
 * owners, groups, times, setuid bits, hard links (each name gets its
 * own copy). */
#define _XOPEN_SOURCE 700
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "crypto.h"
#include "seal.h"

#define CIPHER_CHUNK (CHUNK_SIZE + 16)

/* ------------------------------------------------- encrypted stream */

static int w_fd;
static u64 w_no;
static long w_n;

static int r_fd;
static u64 r_no;
static long r_n, r_pos;
static int r_final;
static long r_bad;

static void make_nonce(u8 *nonce, u64 no, int final)
{
    int i;

    for (i = 0; i < 24; i++)
        nonce[i] = 0;
    for (i = 0; i < 8; i++)
        nonce[i] = (u8)(no >> (8 * i));
    nonce[8] = (u8)final;
}

static int w_flush(int final)
{
    u8 nonce[24];
    int r;

    make_nonce(nonce, w_no, final);
    aead_lock(sec->out, sec->out + w_n, sec->key, nonce, sec->buf, w_n);
    r = write_all(w_fd, sec->out, w_n + 16);
    if (r < 0) {
        warn("can't write the sealed file: %s", stop_now ?
            "interrupted" : strerror(errno), 0);
        return -1;
    }
    w_no++;
    w_n = 0;
    return 0;
}

static int put(const void *p, long n)
{
    const u8 *s;
    long k;
    int r;

    s = (const u8 *)p;
    while (n > 0) {
        k = CHUNK_SIZE - w_n;
        if (k > n)
            k = n;
        memcpy(sec->buf + w_n, s, k);
        w_n += k;
        s += k;
        n -= k;
        /* a full chunk goes at once: so the last one is never full */
        if (w_n == CHUNK_SIZE) {
            r = w_flush(0);
            if (r < 0)
                return -1;
        }
    }
    return 0;
}

static void r_start(int fd)
{
    r_fd = fd;
    r_no = 0;
    r_n = 0;
    r_pos = 0;
    r_final = 0;
    r_bad = 0;
}

static int r_fill(void)
{
    u8 nonce[24];
    char at[40];
    long got, n;
    int final, bad;

    if (r_final) {
        warn("%s", "the archive in it ends too early (damaged?)", 0);
        return -1;
    }
    if (stop_now) {
        warn("%s", "interrupted", 0);
        return -1;
    }
    got = read_full(r_fd, sec->out, CIPHER_CHUNK);
    if (got < 0) {
        warn("can't read it: %s", strerror(errno), 0);
        return -1;
    }
    if (got < 16) {
        warn("%s", r_no == 0 ? "too short to be a sealed file" :
            "it is cut short", 0);
        return -1;
    }
    final = got < CIPHER_CHUNK;
    n = got - 16;
    make_nonce(nonce, r_no, final);
    bad = aead_unlock(sec->buf, sec->out + n, sec->key, nonce, sec->out,
        n, opt_force);
    if (bad) {
        r_bad++;
        if (r_no == 0) {
            warn("%s", "wrong password, or it is not a sealed file "
                "(or its start was changed)", 0);
        } else {
            sprintf(at, "%lld", (long long)(SALT_SIZE + r_no *
                CIPHER_CHUNK));
            warn("changed or damaged near byte %s%s", at,
                opt_force ? " (-f: going on anyway)" : "");
        }
        if (!opt_force)
            return -1;
    }
    r_no++;
    r_n = n;
    r_pos = 0;
    r_final = final;
    return 0;
}

static int get(void *p, long n)
{
    u8 *d;
    long k;
    int r;

    d = (u8 *)p;
    while (n > 0) {
        if (r_pos == r_n) {
            r = r_fill();
            if (r < 0)
                return -1;
            continue;
        }
        k = r_n - r_pos;
        if (k > n)
            k = n;
        memcpy(d, sec->buf + r_pos, k);
        r_pos += k;
        d += k;
        n -= k;
    }
    return 0;
}

/* after the end record: nothing may follow */
static int expect_end(void)
{
    int r;

    if (r_pos == r_n && !r_final) {
        r = r_fill();
        if (r < 0)
            return -1;
    }
    if (r_pos != r_n || !r_final) {
        warn("%s", "there is more after the end of the archive", 0);
        return -1;
    }
    return 0;
}

static int put_num(unsigned long long v, int size)
{
    u8 b[8];
    int i;

    for (i = 0; i < size; i++)
        b[i] = (u8)(v >> (8 * i));
    return put(b, size);
}

static int get_num(unsigned long long *v, int size)
{
    u8 b[8];
    int i, r;

    r = get(b, size);
    if (r < 0)
        return -1;
    *v = 0;
    for (i = 0; i < size; i++)
        *v |= (unsigned long long)b[i] << (8 * i);
    return 0;
}

/* ---------------------------------------------------------- records */

struct rec {
    int type;
    int mode;
    char path[PATH_SIZE];
    long plen;
    long long size;
    char target[PATH_SIZE];
};

static struct rec rec;
static char root_name[256];
static long root_len;

static int put_head(int type, int mode, const char *path, long plen)
{
    u8 t;
    int r;

    t = (u8)type;
    r = put(&t, 1);
    if (r == 0)
        r = put_num(mode, 2);
    if (r == 0)
        r = put_num(plen, 2);
    if (r == 0)
        r = put(path, plen);
    return r;
}

/* a path we accept: relative, under the root name, no "." or ".."
 * parts, no empty parts, no NUL */
static int valid_path(const char *p, long n)
{
    long i, start;

    if (n < 1 || memchr(p, 0, n))
        return 0;
    start = 0;
    for (i = 0; i <= n; i++) {
        if (i < n && p[i] != '/')
            continue;
        if (i == start)
            return 0;
        if (i - start == 1 && p[start] == '.')
            return 0;
        if (i - start == 2 && p[start] == '.' && p[start + 1] == '.')
            return 0;
        if (start == 0 && root_len > 0 &&
            (i != root_len || memcmp(p, root_name, root_len)))
        {
            return 0;
        }
        start = i + 1;
    }
    return 1;
}

/* the next record into rec; its type, or -1 */
static int get_rec(void)
{
    unsigned long long v;
    u8 t;
    int r;

    r = get(&t, 1);
    if (r < 0)
        return -1;
    rec.type = t;
    if (t == 'e')
        return 'e';
    if (t != 'd' && t != 'f' && t != 'l')
        goto broken;
    r = get_num(&v, 2);
    if (r < 0)
        return -1;
    if (v > 0777)
        goto broken;
    rec.mode = (int)v;
    r = get_num(&v, 2);
    if (r < 0)
        return -1;
    if (v < 1 || v >= PATH_SIZE)
        goto broken;
    rec.plen = (long)v;
    r = get(rec.path, rec.plen);
    if (r < 0)
        return -1;
    rec.path[rec.plen] = 0;
    if (!valid_path(rec.path, rec.plen))
        goto broken;
    if (t == 'f') {
        r = get_num(&v, 8);
        if (r < 0)
            return -1;
        if (v >> 62)
            goto broken;
        rec.size = (long long)v;
    } else if (t == 'l') {
        r = get_num(&v, 2);
        if (r < 0)
            return -1;
        if (v < 1 || v >= PATH_SIZE)
            goto broken;
        r = get(rec.target, (long)v);
        if (r < 0)
            return -1;
        rec.target[v] = 0;
        if (memchr(rec.target, 0, (long)v))
            goto broken;
    }
    return t;
broken:
    warn("%s", "the archive in it is broken", 0);
    return -1;
}

/* ------------------------------------------------- the list of names */

struct entry {
    long off;           /* the path, in pool */
    int type;
    int mode;
    long long size;
    long long mtime;
};

static struct entry *ents;
static long n_ents, cap_ents;
static char *pool;
static long pool_used, pool_cap;

static void *grow(void *old, long used, long size)
{
    void *p;

    p = malloc(size);
    if (!p) {
        warn("%s", "out of memory", 0);
        exit(1);
    }
    if (old) {
        memcpy(p, old, used);
        free(old);
    }
    return p;
}

static void add_entry(const char *path, int type, int mode,
    long long size, long long mtime)
{
    long n;

    n = strlen(path) + 1;
    if (pool_used + n > pool_cap) {
        pool_cap = pool_cap * 2 + n + 65536;
        pool = grow(pool, pool_used, pool_cap);
    }
    if (n_ents == cap_ents) {
        cap_ents = cap_ents * 2 + 1024;
        ents = grow(ents, n_ents * (long)sizeof *ents,
            cap_ents * (long)sizeof *ents);
    }
    memcpy(pool + pool_used, path, n);
    ents[n_ents].off = pool_used;
    ents[n_ents].type = type;
    ents[n_ents].mode = mode;
    ents[n_ents].size = size;
    ents[n_ents].mtime = mtime;
    pool_used += n;
    n_ents++;
}

/* path on disk of an archive path: root (the directory as given, or
 * where it is unpacked) plus the path after the root name */
static int disk_path(char *dst, const char *root, const char *apath)
{
    return join(dst, root, apath + root_len);
}

/* ------------------------------------------------------------ pack */

static char rel[PATH_SIZE];
static long rel_len;
static const char *p_disk;
static long p_dev, p_ino;
static char dpath[PATH_SIZE], apath[PATH_SIZE];

static int pack_one(struct counts *c);

static int pack_dir(struct counts *c)
{
    DIR *d;
    struct dirent *e;
    long save, n;
    int r;

    d = opendir(dpath);
    if (!d) {
        warn("can't read %s: %s", dpath, strerror(errno));
        return -1;
    }
    save = rel_len;
    r = 0;
    while ((e = readdir(d)) != 0) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        n = strlen(e->d_name);
        if (save + 1 + n >= PATH_SIZE) {
            warn("%s/%s: the path is too long", dpath, e->d_name);
            r = -1;
            break;
        }
        rel[save] = '/';
        memcpy(rel + save + 1, e->d_name, n + 1);
        rel_len = save + 1 + n;
        r = pack_one(c);
        rel_len = save;
        rel[save] = 0;
        if (r < 0)
            break;
    }
    closedir(d);
    return r;
}

static int pack_file(struct stat *st, long plen, struct counts *c)
{
    struct stat st2;
    long long left;
    long n, k;
    int fd, r;

    fd = open(dpath, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) {
        warn("can't read %s: %s", dpath, strerror(errno));
        return -1;
    }
    r = fstat(fd, &st2);
    if (r < 0 || st2.st_ino != st->st_ino || !S_ISREG(st2.st_mode)) {
        warn("%s: %s", dpath, "it changed while sealing");
        close(fd);
        return -1;
    }
    left = st2.st_size;
    r = put_head('f', st2.st_mode & 0777, apath, plen);
    if (r == 0)
        r = put_num((unsigned long long)left, 8);
    while (r == 0 && left > 0) {
        k = left < CHUNK_SIZE ? (long)left : CHUNK_SIZE;
        n = read(fd, sec->cmp, k);
        if (n < 0 && errno == EINTR && !stop_now)
            continue;
        if (n <= 0) {
            warn("%s: %s", dpath, n < 0 ? strerror(errno) :
                "it got shorter while sealing");
            r = -1;
            break;
        }
        r = put(sec->cmp, n);
        left -= n;
    }
    close(fd);
    c->files++;
    c->bytes += st2.st_size;
    return r;
}

static int pack_one(struct counts *c)
{
    struct stat st;
    char target[PATH_SIZE];
    long plen, n;
    int r;

    if (stop_now) {
        warn("%s", "interrupted", 0);
        return -1;
    }
    r = join(dpath, p_disk, rel);
    if (r == 0)
        r = join(apath, root_name, rel);
    if (r < 0 || strlen(apath) >= 65536) {
        warn("%s%s: the path is too long", p_disk, rel);
        return -1;
    }
    plen = strlen(apath);
    r = lstat(dpath, &st);
    if (r < 0) {
        warn("%s: %s", dpath, strerror(errno));
        return -1;
    }
    if ((long)st.st_dev == p_dev && (long)st.st_ino == p_ino) {
        warn("%s: %s", dpath, "the sealed file can't be inside the "
            "directory: put it somewhere else with -o");
        return -1;
    }
    if (S_ISDIR(st.st_mode)) {
        c->dirs++;
        r = put_head('d', st.st_mode & 0777, apath, plen);
        if (r < 0)
            return -1;
        return pack_dir(c);
    }
    if (S_ISLNK(st.st_mode)) {
        n = readlink(dpath, target, sizeof target);
        if (n < 0 || n >= (long)sizeof target) {
            warn("%s: %s", dpath, n < 0 ? strerror(errno) :
                "the link is too long");
            return -1;
        }
        c->links++;
        r = put_head('l', 0, apath, plen);
        if (r == 0)
            r = put_num(n, 2);
        if (r == 0)
            r = put(target, n);
        return r;
    }
    if (S_ISREG(st.st_mode))
        return pack_file(&st, plen, c);
    warn("%s: %s", dpath, "a device, pipe or socket: seal can't keep "
        "it, so nothing was sealed");
    return -1;
}

int pack(int fd, const char *disk, const char *name, long skip_dev,
    long skip_ino, struct counts *c)
{
    u8 t;
    int r;

    w_fd = fd;
    w_no = 0;
    w_n = 0;
    p_disk = disk;
    p_dev = skip_dev;
    p_ino = skip_ino;
    strcpy(root_name, name);
    root_len = strlen(name);
    rel[0] = 0;
    rel_len = 0;
    r = pack_one(c);
    if (r < 0)
        return -1;
    t = 'e';
    r = put(&t, 1);
    if (r < 0)
        return -1;
    return w_flush(1);
}

/* ---------------------------------------------------------- verify */

static int verify_file(struct stat *st)
{
    long long left;
    long k, n;
    int fd, r;

    if (!S_ISREG(st->st_mode) || st->st_size != rec.size)
        return -2;
    fd = open(dpath, O_RDONLY | O_NOFOLLOW);
    if (fd < 0)
        return -2;
    left = rec.size;
    r = 0;
    while (left > 0) {
        k = left < CHUNK_SIZE ? (long)left : CHUNK_SIZE;
        r = get(sec->cmp, k);
        if (r < 0)
            break;
        n = read_full(fd, sec->disk, k);
        if (n != k || memcmp(sec->cmp, sec->disk, k)) {
            r = -2;
            break;
        }
        left -= k;
    }
    close(fd);
    return r;
}

int pack_verify(int fd, const char *disk, struct counts *c)
{
    struct stat st;
    char target[PATH_SIZE];
    long n, seen;
    int t, r;

    r = (int)lseek(fd, SALT_SIZE, SEEK_SET);
    if (r < 0) {
        warn("can't read the sealed file back: %s", strerror(errno), 0);
        return -1;
    }
    r_start(fd);
    seen = 0;
    for (;;) {
        t = get_rec();
        if (t < 0)
            return -1;
        if (t == 'e')
            break;
        r = disk_path(dpath, disk, rec.path);
        if (r < 0)
            goto differs;
        r = lstat(dpath, &st);
        if (r < 0)
            goto differs;
        if (t == 'd') {
            if (!S_ISDIR(st.st_mode) || (int)(st.st_mode & 0777) != rec.mode)
                goto differs;
        } else if (t == 'l') {
            if (!S_ISLNK(st.st_mode))
                goto differs;
            n = readlink(dpath, target, sizeof target - 1);
            if (n < 0 || strlen(rec.target) != (unsigned long)n ||
                memcmp(target, rec.target, n))
            {
                goto differs;
            }
        } else {
            if ((int)(st.st_mode & 0777) != rec.mode)
                goto differs;
            r = verify_file(&st);
            if (r == -1)
                return -1;
            if (r < 0)
                goto differs;
        }
        add_entry(rec.path, t, st.st_mode & 07777, st.st_size,
            (long long)st.st_mtime);
        seen++;
    }
    r = expect_end();
    if (r < 0)
        return -1;
    if (seen != c->dirs + c->files + c->links) {
        warn("%s", "the sealed file doesn't hold everything", 0);
        return -1;
    }
    return 0;
differs:
    warn("%s: %s", dpath, "it changed while sealing");
    return -1;
}

int pack_remove(const char *disk)
{
    struct stat st;
    struct entry *e;
    long i, bad;
    int r;

    bad = 0;
    /* directories without write permission: let them be emptied */
    for (i = 0; i < n_ents; i++) {
        e = ents + i;
        if (e->type == 'd' && (e->mode & 0700) != 0700) {
            r = disk_path(dpath, disk, pool + e->off);
            if (r == 0)
                chmod(dpath, (e->mode & 07777) | 0700);
        }
    }
    for (i = n_ents - 1; i >= 0; i--) {
        e = ents + i;
        r = disk_path(dpath, disk, pool + e->off);
        if (r == 0)
            r = lstat(dpath, &st);
        if (r < 0)
            continue;
        if (e->type == 'f' && (!S_ISREG(st.st_mode) ||
            st.st_size != e->size || (long long)st.st_mtime != e->mtime))
        {
            warn("%s: %s", dpath, "changed after it was sealed: left "
                "as it is");
            bad++;
            continue;
        }
        if (e->type == 'd')
            r = rmdir(dpath);
        else
            r = unlink(dpath);
        if (r < 0) {
            warn("can't remove %s: %s", dpath, strerror(errno));
            bad++;
        }
    }
    return bad ? -1 : 0;
}

/* ---------------------------------------------------------- unpack */

static char upath[PATH_SIZE];
static char last_parent[PATH_SIZE];

/* every directory above apath (under tmp) must be a real directory,
 * one we made: never follow a link out of the tree */
static int check_parent(const char *tmp, const char *p)
{
    struct stat st;
    char part[PATH_SIZE], where[PATH_SIZE];
    const char *slash;
    long n, i;
    int r;

    slash = strrchr(p, '/');
    if (!slash)
        return -1;
    n = slash - p;
    if ((long)strlen(last_parent) == n && !memcmp(last_parent, p, n))
        return 0;
    for (i = 1; i <= n; i++) {
        if (i < n && p[i] != '/')
            continue;
        memcpy(part, p, i);
        part[i] = 0;
        r = join(where, tmp, "/");
        if (r == 0)
            r = join(where, where, part);
        if (r == 0)
            r = lstat(where, &st);
        if (r < 0 || !S_ISDIR(st.st_mode))
            return -1;
    }
    memcpy(last_parent, p, n);
    last_parent[n] = 0;
    return 0;
}

static int skip_data(long long n)
{
    long k;
    int r;

    while (n > 0) {
        k = n < CHUNK_SIZE ? (long)n : CHUNK_SIZE;
        r = get(sec->cmp, k);
        if (r < 0)
            return -1;
        n -= k;
    }
    return 0;
}

static int write_data(int fd, long long n)
{
    long k;
    int r;

    while (n > 0) {
        k = n < CHUNK_SIZE ? (long)n : CHUNK_SIZE;
        r = get(sec->cmp, k);
        if (r < 0)
            return -1;
        r = write_all(fd, sec->cmp, k);
        if (r < 0) {
            warn("can't write %s: %s", upath, strerror(errno));
            return -1;
        }
        n -= k;
    }
    return 0;
}

static int unpack_one(const char *tmp, int t, struct counts *c)
{
    int fd, r, k;

    r = join(upath, tmp, "/");
    if (r == 0)
        r = join(upath, upath, rec.path);
    if (r < 0) {
        warn("%s: the path is too long", rec.path, 0);
        return -1;
    }
    r = check_parent(tmp, rec.path);
    if (r < 0) {
        warn("%s: %s", rec.path, "it isn't inside a directory of the "
            "archive (a broken or hostile file)");
        return -1;
    }
    if (t == 'd') {
        r = mkdir(upath, 0700);
        if (r < 0) {
            warn("can't make %s: %s", upath, strerror(errno));
            return -1;
        }
        add_entry(rec.path, 'd', rec.mode, 0, 0);
        c->dirs++;
    } else if (t == 'l') {
        r = symlink(rec.target, upath);
        if (r < 0) {
            warn("can't make %s: %s", upath, strerror(errno));
            return -1;
        }
        c->links++;
    } else {
        fd = open(upath, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (fd < 0) {
            warn("can't make %s: %s", upath, strerror(errno));
            return -1;
        }
        r = write_data(fd, rec.size);
        if (r == 0)
            r = fchmod(fd, rec.mode);
        if (r == 0)
            r = fsync(fd);
        k = close(fd);
        if (k < 0 && r == 0) {
            warn("can't write %s: %s", upath, strerror(errno));
            r = -1;
        }
        if (r < 0)
            return -1;
        c->files++;
        c->bytes += rec.size;
    }
    return 0;
}

int unpack_open(int fd, char *name)
{
    int t;

    r_start(fd);
    root_len = 0;
    root_name[0] = 0;
    n_ents = 0;
    pool_used = 0;
    last_parent[0] = 0;
    t = get_rec();
    if (t < 0)
        return -1;
    if (t != 'd' || strchr(rec.path, '/') || rec.plen >= 256) {
        warn("%s", "the archive in it is broken", 0);
        return -1;
    }
    strcpy(root_name, rec.path);
    root_len = rec.plen;
    strcpy(name, root_name);
    return 0;
}

static void list_rec(int t)
{
    if (t == 'd')
        printf("%s/\n", rec.path);
    else if (t == 'l')
        printf("%s -> %s\n", rec.path, rec.target);
    else
        printf("%s\n", rec.path);
}

int unpack_rest(const char *tmp, int list, struct counts *c)
{
    struct entry *e;
    long i;
    int t, r;

    if (list) {
        list_rec('d');
        c->dirs++;
    } else {
        r = join(upath, tmp, "/");
        if (r == 0)
            r = join(upath, upath, rec.path);
        if (r == 0)
            r = mkdir(upath, 0700);
        if (r < 0) {
            warn("can't make %s: %s", upath, strerror(errno));
            return -1;
        }
        add_entry(rec.path, 'd', rec.mode, 0, 0);
        c->dirs++;
    }
    for (;;) {
        t = get_rec();
        if (t < 0)
            goto failed;
        if (t == 'e')
            break;
        if (list) {
            list_rec(t);
            if (t == 'd')
                c->dirs++;
            else if (t == 'l')
                c->links++;
            else
                c->files++;
            if (t == 'f') {
                c->bytes += rec.size;
                r = skip_data(rec.size);
                if (r < 0)
                    goto failed;
            }
            continue;
        }
        r = unpack_one(tmp, t, c);
        if (r < 0)
            goto failed;
    }
    r = expect_end();
    if (r < 0)
        goto failed;
    r = r_bad ? 1 : 0;
    goto modes;
failed:
    if (!opt_force)
        return -1;
    r = 1;
modes:
    /* the directories' own modes last: one without write permission
     * could not have been filled */
    for (i = n_ents - 1; i >= 0; i--) {
        e = ents + i;
        t = join(upath, tmp, "/");
        if (t == 0)
            t = join(upath, upath, pool + e->off);
        if (t == 0)
            chmod(upath, e->mode);
    }
    return r;
}

/* ---------------------------------------------------------- rm -r */

static char rm_path[PATH_SIZE];

static int rm_rec(long len)
{
    struct stat st;
    DIR *d;
    struct dirent *e;
    long n;
    int r, bad;

    r = lstat(rm_path, &st);
    if (r < 0)
        return errno == ENOENT ? 0 : -1;
    if (!S_ISDIR(st.st_mode))
        return unlink(rm_path);
    chmod(rm_path, 0700);
    d = opendir(rm_path);
    if (!d)
        return -1;
    bad = 0;
    while ((e = readdir(d)) != 0) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        n = strlen(e->d_name);
        if (len + 1 + n >= PATH_SIZE) {
            bad = 1;
            continue;
        }
        rm_path[len] = '/';
        memcpy(rm_path + len + 1, e->d_name, n + 1);
        r = rm_rec(len + 1 + n);
        rm_path[len] = 0;
        if (r < 0)
            bad = 1;
    }
    closedir(d);
    r = rmdir(rm_path);
    return bad || r < 0 ? -1 : 0;
}

int rm_tree(const char *path)
{
    long n;

    n = strlen(path);
    if (n >= PATH_SIZE)
        return -1;
    memcpy(rm_path, path, n + 1);
    return rm_rec(n);
}
