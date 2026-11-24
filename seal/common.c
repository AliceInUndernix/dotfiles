/* common.c: small helpers that seal and diary (../diary) share: errors,
 * whole reads and writes, paths, random bytes, and whether a directory
 * is in RAM. Each program sets prog to its own name. */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <unistd.h>

#include "seal.h"

#define TMPFS_MAGIC_NO 0x01021994L
#define RAMFS_MAGIC_NO 0x858458f6L

void warn(const char *fmt, const char *a, const char *b)
{
    fprintf(stderr, "%s: ", prog);
    fprintf(stderr, fmt, a, b);
    fputc('\n', stderr);
}

int write_all(int fd, const void *p, long n)
{
    const char *s;
    long k;

    s = (const char *)p;
    while (n > 0) {
        k = write(fd, s, n);
        if (k < 0 && errno == EINTR && !stop_now)
            continue;
        if (k <= 0)
            return -1;
        s += k;
        n -= k;
    }
    return 0;
}

/* read n bytes, fewer only at the end of the file; -1 on error */
long read_full(int fd, void *p, long n)
{
    char *d;
    long got, k;

    d = (char *)p;
    got = 0;
    while (got < n) {
        k = read(fd, d + got, n - got);
        if (k < 0 && errno == EINTR && !stop_now)
            continue;
        if (k < 0)
            return -1;
        if (k == 0)
            break;
        got += k;
    }
    return got;
}

/* dst = a b; dst may be a. -1 when too long. */
int join(char *dst, const char *a, const char *b)
{
    long n, m;

    n = strlen(a);
    m = strlen(b);
    if (n + m >= PATH_SIZE)
        return -1;
    memmove(dst, a, n);
    memmove(dst + n, b, m + 1);
    return 0;
}

void rand_bytes(unsigned char *p, long n)
{
    long got;
    int fd;

    got = -1;
    fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        got = read_full(fd, p, n);
        close(fd);
    }
    if (got != n) {
        warn("%s", "can't read /dev/urandom", 0);
        exit(1);
    }
}

void rand_name(char *s)
{
    static const char hex[] = "0123456789abcdef";
    unsigned char b[6];
    int i;

    rand_bytes(b, sizeof b);
    for (i = 0; i < 6; i++) {
        s[2 * i] = hex[b[i] >> 4];
        s[2 * i + 1] = hex[b[i] & 15];
    }
    s[12] = 0;
}

/* split path into its directory and its last part */
int split(const char *path, char *dir, char *base)
{
    const char *slash;
    long n;

    slash = strrchr(path, '/');
    if (!slash) {
        strcpy(dir, ".");
        return join(base, path, "");
    }
    n = slash - path;
    if (n == 0)
        n = 1;
    memcpy(dir, path, n);
    dir[n] = 0;
    return join(base, slash + 1, "");
}

int same_file(const char *a, const char *b)
{
    struct stat sa, sb;
    int r;

    r = stat(a, &sa);
    if (r == 0)
        r = stat(b, &sb);
    if (r < 0)
        return 0;
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

void sync_dir(const char *dir)
{
    int fd;

    fd = open(dir, O_RDONLY);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
}
int is_ram(const char *path)
{
    struct statfs sf;
    int r;

    r = statfs(path, &sf);
    if (r < 0)
        return 0;
    return (long)sf.f_type == TMPFS_MAGIC_NO ||
        (long)sf.f_type == RAMFS_MAGIC_NO;
}
