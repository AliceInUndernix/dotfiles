/* archive: tar and gzip. A tar file names each file's owner (user and
 * group, by name and by number) and its times; pax headers can add
 * more times and extended attributes (where a file was downloaded
 * from, say). A gzip file names the file it packed, its time and the
 * system. tar is changed in place; gzip is rewritten in memory, and the
 * tar inside a .tar.gz is cleaned too. */
#include <stdlib.h>
#include <string.h>

#include "clean.h"

#define FIXED_TIME 946684800L           /* 2000-01-01 00:00:00 UTC */
#define MAX_TAR (512L * 1024 * 1024)    /* a .tar.gz unpacked */

static int tar_changes;     /* what tar_walk changed */

/* ------------------------------------------------------------ tar */

static long long octal(const unsigned char *p, int n)
{
    long long v;
    int i;

    v = 0;
    i = 0;
    while (i < n && p[i] == ' ')
        i++;
    for (; i < n && p[i] >= '0' && p[i] <= '7'; i++)
        v = v * 8 + (p[i] - '0');
    return v;
}

static void put_octal(unsigned char *p, int n, long long v)
{
    int i;

    p[n - 1] = 0;
    for (i = n - 2; i >= 0; i--) {
        p[i] = (unsigned char)('0' + (v & 7));
        v >>= 3;
    }
}

static int is_tar_header(const unsigned char *h)
{
    long long sum, want;
    int i;

    if (memcmp(h + 257, "ustar", 5))
        return 0;
    sum = 0;
    for (i = 0; i < 512; i++)
        sum += i >= 148 && i < 156 ? ' ' : h[i];
    want = octal(h + 148, 8);
    return sum == want;
}

int is_tar(const unsigned char *h, long n)
{
    return n >= 512 && is_tar_header(h);
}

/* tar_header: one header block, cleaned; 1 when it changed */
static int tar_header(unsigned char *h)
{
    static unsigned char zero[32];
    long long sum;
    int i, changed;

    changed = 0;
    if (octal(h + 136, 12) != FIXED_TIME) {
        note("file times");
        put_octal(h + 136, 12, FIXED_TIME);
        changed = 1;
    }
    if (octal(h + 108, 8) || octal(h + 116, 8) ||
        memcmp(h + 265, zero, 32) || memcmp(h + 297, zero, 32))
    {
        note("owners (user and group names and IDs)");
        put_octal(h + 108, 8, 0);
        put_octal(h + 116, 8, 0);
        memset(h + 265, 0, 64);
        changed = 1;
    }
    /* old GNU tar: access and change times where ustar has its prefix */
    if (!memcmp(h + 257, "ustar  ", 8) && memcmp(h + 345, zero, 24)) {
        note("file times");
        memset(h + 345, 0, 24);
        changed = 1;
    }
    if (!changed)
        return 0;
    sum = 0;
    memset(h + 148, ' ', 8);
    for (i = 0; i < 512; i++)
        sum += h[i];
    put_octal(h + 148, 7, sum);
    h[155] = ' ';
    return 1;
}

static int key_is(const unsigned char *k, long n, const char *w)
{
    long len;

    len = strlen(w);
    if (len > 0 && w[len - 1] == '*')
        return n >= len - 1 && !memcmp(k, w, len - 1);
    return n == len && !memcmp(k, w, n);
}

/* pax: the records of a pax header ("LEN key=value\n"). The ones that
 * go become comments of the same length, which readers skip; uid and
 * gid become zeros. 1 when something changed. */
static int pax(unsigned char *p, long n)
{
    long i, len, k, eq, j;
    int changed;
    const char *what;

    changed = 0;
    i = 0;
    while (i < n) {
        len = 0;
        k = i;
        while (k < n && p[k] >= '0' && p[k] <= '9') {
            len = len * 10 + (p[k] - '0');
            k++;
        }
        if (len <= 0 || i + len > n || k >= n || p[k] != ' ')
            break;
        k++;
        eq = k;
        while (eq < i + len && p[eq] != '=')
            eq++;
        what = 0;
        if (key_is(p + k, eq - k, "mtime") ||
            key_is(p + k, eq - k, "atime") ||
            key_is(p + k, eq - k, "ctime") ||
            key_is(p + k, eq - k, "LIBARCHIVE.creationtime"))
        {
            what = "file times";
        } else if (key_is(p + k, eq - k, "uname") ||
            key_is(p + k, eq - k, "gname"))
        {
            what = "owners (user and group names and IDs)";
        } else if (key_is(p + k, eq - k, "SCHILY.xattr.*") ||
            key_is(p + k, eq - k, "LIBARCHIVE.xattr.*") ||
            key_is(p + k, eq - k, "SCHILY.acl.*"))
        {
            what = "extended attributes (where it was downloaded from)";
        }
        if (key_is(p + k, eq - k, "uid") || key_is(p + k, eq - k, "gid")) {
            for (j = eq + 1; j < i + len - 1; j++) {
                if (p[j] != '0') {
                    p[j] = '0';
                    changed = 1;
                    what = "owners (user and group names and IDs)";
                }
            }
            if (what)
                note(what);
        } else if (what && i + len - 1 - k >= 8) {
            note(what);
            memcpy(p + k, "comment=", 8);
            memset(p + k + 8, ' ', i + len - 1 - k - 8);
            changed = 1;
        }
        i += len;
    }
    return changed;
}

/* tar_walk: the headers of a tar, from the file (fd) or from memory
 * (m); end: where the end blocks start */
static int tar_walk(int fd, unsigned char *m, long long size,
    long long *end)
{
    unsigned char h[512], *x;
    long long off, len;
    int rc, type, k;

    off = 0;
    *end = -1;
    x = 0;
    while (off + 512 <= size) {
        rc = 0;
        if (m)
            memcpy(h, m + off, 512);
        else
            rc = at_read(fd, off, h, 512);
        if (rc < 0)
            return -1;
        if (h[0] == 0) {
            *end = off;
            break;
        }
        if (!is_tar_header(h)) {
            why = "a broken tar file";
            return -1;
        }
        type = h[156];
        len = octal(h + 124, 12);
        if (len < 0 || off + 512 + len > size) {
            why = "a broken tar file";
            return -1;
        }
        rc = tar_header(h);
        tar_changes += rc;
        if (rc && m) {
            memcpy(m + off, h, 512);
        } else if (rc) {
            rc = at_write(fd, off, h, 512);
            if (rc < 0)
                return -1;
        }
        if ((type == 'x' || type == 'g') && len <= 1024L * 1024) {
            if (m) {
                tar_changes += pax(m + off + 512, (long)len);
            } else {
                x = malloc((long)len + 1);
                if (!x)
                    return -1;
                rc = at_read(fd, off + 512, x, (long)len);
                k = 0;
                if (rc == 0)
                    k = pax(x, (long)len);
                if (k)
                    rc = at_write(fd, off + 512, x, (long)len);
                tar_changes += k;
                free(x);
                if (rc < 0)
                    return -1;
            }
        }
        off += 512 + (len + 511) / 512 * 512;
    }
    return 0;
}

int clean_tar(int fd, long long size)
{
    long long end;

    return tar_walk(fd, 0, size, &end);
}

/* tar: 32 random bytes after the two empty blocks that end it, where
 * readers stop */
int rand_tar(int fd, long long size)
{
    unsigned char b[32];
    long long end;
    int rc;

    rc = tar_walk(fd, 0, size, &end);
    if (rc < 0 || end < 0)
        return 0;
    rand_bytes(b, 32);
    rc = at_write(fd, end + 1024, b, 32);
    return rc < 0 ? -1 : 1;
}

/* ------------------------------------------------------------ gzip */

/* gzip: the original file's name, comment, time, system and extra
 * fields go. Each member's packed data stays, unless it is a tar,
 * which is cleaned and packed again. mode 1: a comment of 32 random
 * spaces and tabs instead (gzip shows none of it) */
static int gz_walk(const unsigned char *in, long n, struct buf *out,
    int mode)
{
    static const char white[2] = { ' ', '\t' };
    struct buf data, packed;
    unsigned char h[10], r[32], t[8];
    long i, used, k;
    long long end;
    int flags, rc, members, changed;

    i = 0;
    members = 0;
    while (i < n) {
        if (n - i < 18 || in[i] != 0x1f || in[i + 1] != 0x8b ||
            in[i + 2] != 8)
        {
            if (members == 0)
                goto broken;
            note("hidden data after it");
            break;
        }
        flags = in[i + 3];
        if (mode == 0 && (get32le(in + i + 4) || in[i + 9] != 255))
            note("time and system");
        i += 10;
        if (flags & 4) {
            if (i + 2 > n)
                goto broken;
            if (mode == 0)
                note("extra fields");
            i += 2 + get16le(in + i);
        }
        if (flags & 8) {
            if (mode == 0)
                note("the original file's name");
            while (i < n && in[i])
                i++;
            i++;
        }
        if (flags & 16) {
            for (k = i; mode == 0 && k < n && in[k]; k++) {
                if (in[k] != ' ' && in[k] != '\t') {
                    note("comment");
                    break;
                }
            }
            while (i < n && in[i])
                i++;
            i++;
        }
        if (flags & 2)
            i += 2;
        if (i >= n)
            goto broken;
        memset(&data, 0, sizeof data);
        rc = inflate_used(in + i, n - i, &data, MAX_TAR, &used);
        if (rc < 0 || i + used + 8 > n ||
            (unsigned long)get32le(in + i + used + 4) !=
            ((unsigned long)data.n & 0xffffffffUL))
        {
            buf_free(&data);
            goto broken;
        }
        changed = 0;
        if (mode == 0 && is_tar(data.p, data.n)) {
            tar_changes = 0;
            rc = tar_walk(-1, data.p, data.n, &end);
            if (rc < 0) {
                buf_free(&data);
                return -1;
            }
            changed = tar_changes > 0;
        }
        memcpy(h, "\037\213\010\000\000\000\000\000\000\377", 10);
        if (mode == 1)
            h[3] = 16;
        buf_put(out, h, 10);
        if (mode == 1) {
            rand_bytes(r, 32);
            for (k = 0; k < 32; k++)
                buf_byte(out, white[r[k] & 1]);
            buf_byte(out, 0);
        }
        memset(&packed, 0, sizeof packed);
        if (changed) {
            deflate_raw(data.p, data.n, &packed);
            buf_put(out, packed.p, packed.n);
            put32le(t, (long)crc32_png(data.p, data.n, 0));
            put32le(t + 4, data.n);
            buf_put(out, t, 8);
        } else {
            buf_put(out, in + i, used + 8);
        }
        buf_free(&packed);
        buf_free(&data);
        i += used + 8;
        members++;
    }
    return 0;
broken:
    why = "a broken gzip file";
    return -1;
}

int clean_gz(const unsigned char *in, long n, struct buf *out)
{
    return gz_walk(in, n, out, 0);
}

int rand_gz(const unsigned char *in, long n, struct buf *out)
{
    return gz_walk(in, n, out, 1);
}
