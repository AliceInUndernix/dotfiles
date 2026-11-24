#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "kspkan.h"

void die(const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "kspkan: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

void *xalloc(long n)
{
    void *p;

    if (n < 1) {
        n = 1;
    }
    p = malloc((unsigned long)n);
    if (p == NULL) {
        die("out of memory");
    }
    return p;
}

char *xstrndup(const char *s, long n)
{
    char *r;

    r = (char *)xalloc(n + 1);
    memcpy(r, s, (unsigned long)n);
    r[n] = 0;
    return r;
}

char *xstrdup(const char *s)
{
    return xstrndup(s, (long)strlen(s));
}

int is_digit(int c)
{
    return c >= '0' && c <= '9';
}

int is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

void strs_add(struct strs *a, char *s)
{
    char **nv;
    int i, ncap;

    if (a->n == a->cap) {
        ncap = a->cap ? a->cap * 2 : 8;
        nv = (char **)xalloc((long)ncap * (long)sizeof(char *));
        for (i = 0; i < a->n; i++) {
            nv[i] = a->v[i];
        }
        free(a->v);
        a->v = nv;
        a->cap = ncap;
    }
    a->v[a->n] = s;
    a->n++;
}

void strs_free(struct strs *a)
{
    int i;

    for (i = 0; i < a->n; i++) {
        free(a->v[i]);
    }
    free(a->v);
    a->v = NULL;
    a->n = 0;
    a->cap = 0;
}

void buf_add(struct buf *b, const char *s, long n)
{
    char *np;
    long ncap;

    if (b->n + n + 1 > b->cap) {
        ncap = b->cap ? b->cap : 256;
        while (ncap < b->n + n + 1) {
            ncap = ncap * 2;
        }
        np = (char *)xalloc(ncap);
        if (b->n > 0) {
            memcpy(np, b->p, (unsigned long)b->n);
        }
        free(b->p);
        b->p = np;
        b->cap = ncap;
    }
    memcpy(b->p + b->n, s, (unsigned long)n);
    b->n += n;
    b->p[b->n] = 0;
}

void buf_adds(struct buf *b, const char *s)
{
    buf_add(b, s, (long)strlen(s));
}

void buf_free(struct buf *b)
{
    free(b->p);
    b->p = NULL;
    b->n = 0;
    b->cap = 0;
}

char *read_file(const char *path, long *len, long max)
{
    struct stat st;
    char *p;
    long got;
    long r;
    int fd, rc;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return NULL;
    }
    rc = fstat(fd, &st);
    if (rc < 0 || (long)st.st_size > max) {
        close(fd);
        return NULL;
    }
    p = (char *)xalloc((long)st.st_size + 1);
    got = 0;
    r = (long)read(fd, p, (unsigned long)st.st_size);
    while (r > 0) {
        got += r;
        if (got >= (long)st.st_size) {
            break;
        }
        r = (long)read(fd, p + got, (unsigned long)((long)st.st_size - got));
    }
    close(fd);
    if (r < 0) {
        free(p);
        return NULL;
    }
    p[got] = 0;
    if (len != NULL) {
        *len = got;
    }
    return p;
}

int write_file(const char *path, const char *data, long len)
{
    char *tmp;
    long done, r;
    int fd, rc;

    tmp = (char *)xalloc((long)strlen(path) + 5);
    strcpy(tmp, path);
    strcat(tmp, ".tmp");
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        free(tmp);
        return -1;
    }
    done = 0;
    while (done < len) {
        r = (long)write(fd, data + done, (unsigned long)(len - done));
        if (r <= 0) {
            close(fd);
            unlink(tmp);
            free(tmp);
            return -1;
        }
        done += r;
    }
    rc = close(fd);
    if (rc < 0) {
        unlink(tmp);
        free(tmp);
        return -1;
    }
    rc = rename(tmp, path);
    if (rc < 0) {
        unlink(tmp);
        free(tmp);
        return -1;
    }
    free(tmp);
    return 0;
}

int make_dirs(const char *path)
{
    char *p;
    int i, rc;

    p = xstrdup(path);
    for (i = 1; p[i]; i++) {
        if (p[i] == '/') {
            p[i] = 0;
            rc = mkdir(p, 0755);
            if (rc < 0 && errno != EEXIST) {
                free(p);
                return -1;
            }
            p[i] = '/';
        }
    }
    rc = mkdir(p, 0755);
    free(p);
    if (rc < 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

int safe_make_dirs(const char *root, const char *rel)
{
    struct stat st;
    char *p;
    int i, rc, last;

    p = (char *)xalloc((long)(strlen(root) + strlen(rel)) + 2);
    strcpy(p, root);
    strcat(p, "/");
    last = (int)strlen(p);
    strcat(p, rel);
    for (i = last; ; i++) {
        if (p[i] == '/' || p[i] == 0) {
            last = p[i];
            p[i] = 0;
            rc = lstat(p, &st);
            if (rc < 0) {
                rc = mkdir(p, 0755);
                if (rc < 0) {
                    free(p);
                    return -1;
                }
            } else if (!S_ISDIR(st.st_mode)) {
                free(p);
                return -1;
            }
            if (last == 0) {
                break;
            }
            p[i] = '/';
        }
    }
    free(p);
    return 0;
}

int path_is_safe(const char *rel)
{
    const char *s;
    int clen;

    if (rel[0] == 0 || rel[0] == '/') {
        return 0;
    }
    s = rel;
    clen = 0;
    for (;; s++) {
        if (*s == '\\' || (*s != 0 && (unsigned char)*s < 32)) {
            return 0;
        }
        if (*s == '/' || *s == 0) {
            if (clen == 0) {
                return 0;
            }
            if (clen == 1 && s[-1] == '.') {
                return 0;
            }
            if (clen == 2 && s[-1] == '.' && s[-2] == '.') {
                return 0;
            }
            clen = 0;
            if (*s == 0) {
                break;
            }
        } else {
            clen++;
        }
    }
    return 1;
}
