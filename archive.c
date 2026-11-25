#include <stdlib.h>
#include <string.h>
#include "kspkan.h"

static unsigned int rd16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int rd32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
        ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

void archive_close(struct archive *a)
{
    int i;

    for (i = 0; i < a->n; i++) {
        free(a->e[i].name);
    }
    free(a->e);
    a->e = NULL;
    a->n = 0;
}

/* 0 on success, -1 if not a usable zip (zip64 is not supported) */
int zip_open(struct archive *a, const unsigned char *data, long len)
{
    long pos, cd, cdsize, p, lim;
    int i, j, count, nlen, xlen, clen, flags;
    struct aent *e;

    a->data = data;
    a->len = len;
    a->e = NULL;
    a->n = 0;
    a->iszip = 1;
    if (len < 22) {
        return -1;
    }
    lim = len - 22 - 65535;
    if (lim < 0) {
        lim = 0;
    }
    for (pos = len - 22; pos >= lim; pos--) {
        if (rd32(data + pos) == 0x06054b50UL) {
            break;
        }
    }
    if (pos < lim) {
        return -1;
    }
    count = (int)rd16(data + pos + 10);
    cdsize = (long)rd32(data + pos + 12);
    cd = (long)rd32(data + pos + 16);
    if (count == 0xffff || cdsize == 0xffffffffL || cd == 0xffffffffL) {
        return -1;
    }
    if (cd < 0 || cd + cdsize > len) {
        return -1;
    }
    a->e = (struct aent *)xalloc((long)(count + 1) * (long)sizeof(*e));
    p = cd;
    for (i = 0; i < count; i++) {
        if (p + 46 > len || rd32(data + p) != 0x02014b50UL) {
            archive_close(a);
            return -1;
        }
        e = &a->e[i];
        flags = (int)rd16(data + p + 8);
        e->method = (int)rd16(data + p + 10);
        e->crc = rd32(data + p + 16);
        e->csize = (long)rd32(data + p + 20);
        e->usize = (long)rd32(data + p + 24);
        nlen = (int)rd16(data + p + 28);
        xlen = (int)rd16(data + p + 30);
        clen = (int)rd16(data + p + 32);
        e->off = (long)rd32(data + p + 42);
        if (p + 46 + nlen + xlen + clen > len || (flags & 1) ||
            e->csize == 0xffffffffL || e->usize == 0xffffffffL ||
            e->off == 0xffffffffL)
        {
            a->n = i;
            archive_close(a);
            return -1;
        }
        e->name = xstrndup((const char *)data + p + 46, nlen);
        /* zips made on Windows may use \ between names, as CKAN
           allows; the extracted paths are still checked for .. */
        for (j = 0; j < nlen; j++) {
            if (e->name[j] == '\\') {
                e->name[j] = '/';
            }
        }
        e->isdir = (nlen > 0 && e->name[nlen - 1] == '/');
        p += 46 + nlen + xlen + clen;
        a->n = i + 1;
    }
    return 0;
}

static long tar_octal(const unsigned char *p, int n)
{
    long v;
    int i;

    v = 0;
    for (i = 0; i < n && p[i] == ' '; i++) {
        continue;
    }
    for (; i < n && p[i] >= '0' && p[i] <= '7'; i++) {
        v = v * 8 + (p[i] - '0');
    }
    return v;
}

static int tar_blank(const unsigned char *p)
{
    int i;

    for (i = 0; i < 512; i++) {
        if (p[i] != 0) {
            return 0;
        }
    }
    return 1;
}

/* a "path=" record from a pax header, or NULL */
static char *tar_pax_path(const unsigned char *p, long n)
{
    long i, j, recend;

    i = 0;
    while (i < n) {
        recend = i;
        j = 0;
        while (recend < n && is_digit(p[recend])) {
            j = j * 10 + (p[recend] - '0');
            recend++;
        }
        if (j <= 0 || i + j > n) {
            return NULL;
        }
        if (recend + 6 <= i + j && memcmp(p + recend, " path=", 6) == 0) {
            return xstrndup((const char *)p + recend + 6,
                i + j - (recend + 6) - 1);
        }
        i += j;
    }
    return NULL;
}

int tar_open(struct archive *a, const unsigned char *data, long len)
{
    long pos, size, cap, hold;
    char *longname;
    char name[260];
    int type, plen, nlen, i, rc;
    struct aent *ne;

    a->data = data;
    a->len = len;
    a->n = 0;
    a->iszip = 0;
    cap = 64;
    a->e = (struct aent *)xalloc(cap * (long)sizeof(struct aent));
    longname = NULL;
    pos = 0;
    while (pos + 512 <= len) {
        if (tar_blank(data + pos)) {
            break;
        }
        size = tar_octal(data + pos + 124, 12);
        type = data[pos + 156];
        if (size < 0 || pos + 512 + size > len) {
            rc = -1;
            goto quit;
        }
        if (type == 'L' || type == 'x' || type == 'g') {
            if (type == 'L') {
                free(longname);
                nlen = (int)size;
                if (nlen > 0 && data[pos + 512 + nlen - 1] == 0) {
                    nlen--;
                }
                longname = xstrndup((const char *)data + pos + 512, nlen);
            } else if (type == 'x') {
                free(longname);
                longname = tar_pax_path(data + pos + 512, size);
            }
            pos += 512 + ((size + 511) / 512) * 512;
            continue;
        }
        name[0] = 0;
        plen = 0;
        if (memcmp(data + pos + 257, "ustar", 5) == 0 &&
            data[pos + 345] != 0)
        {
            while (plen < 155 && data[pos + 345 + plen] != 0) {
                plen++;
            }
            memcpy(name, data + pos + 345, (unsigned long)plen);
            name[plen] = '/';
            plen++;
        }
        nlen = 0;
        while (nlen < 100 && data[pos + nlen] != 0) {
            nlen++;
        }
        memcpy(name + plen, data + pos, (unsigned long)nlen);
        name[plen + nlen] = 0;
        if (a->n == cap) {
            hold = cap * 2;
            ne = (struct aent *)xalloc(hold * (long)sizeof(struct aent));
            for (i = 0; i < a->n; i++) {
                ne[i] = a->e[i];
            }
            free(a->e);
            a->e = ne;
            cap = hold;
        }
        ne = &a->e[a->n];
        if (longname != NULL) {
            ne->name = longname;
            longname = NULL;
        } else {
            ne->name = xstrdup(name);
        }
        ne->method = 0;
        ne->crc = 0;
        ne->csize = size;
        ne->usize = size;
        ne->off = pos + 512;
        ne->isdir = (type == '5');
        if (type != '0' && type != 0 && type != '5') {
            free(ne->name);
        } else {
            a->n++;
        }
        pos += 512 + ((size + 511) / 512) * 512;
    }
    rc = 0;
quit:
    free(longname);
    if (rc < 0) {
        archive_close(a);
    }
    return rc;
}

/* contents of entry i, malloc'd with one extra NUL byte; NULL on any
   error, including a size above maxsize or a failed CRC */
unsigned char *archive_read(struct archive *a, int i, long maxsize,
    long *len)
{
    const unsigned char *lh;
    struct aent *e;
    unsigned char *out;
    long start, got;
    int nlen, xlen, rc;

    e = &a->e[i];
    if (e->usize > maxsize || e->off < 0 || e->csize < 0) {
        return NULL;
    }
    if (!a->iszip) {
        out = (unsigned char *)xalloc(e->usize + 1);
        memcpy(out, a->data + e->off, (unsigned long)e->usize);
        out[e->usize] = 0;
        *len = e->usize;
        return out;
    }
    if (e->off + 30 > a->len) {
        return NULL;
    }
    lh = a->data + e->off;
    if (rd32(lh) != 0x04034b50UL) {
        return NULL;
    }
    nlen = (int)rd16(lh + 26);
    xlen = (int)rd16(lh + 28);
    start = e->off + 30 + nlen + xlen;
    if (start + e->csize > a->len) {
        return NULL;
    }
    out = (unsigned char *)xalloc(e->usize + 1);
    if (e->method == 0) {
        if (e->csize != e->usize) {
            free(out);
            return NULL;
        }
        memcpy(out, a->data + start, (unsigned long)e->usize);
        got = e->usize;
    } else if (e->method == 8) {
        rc = inflate_raw(a->data + start, e->csize, out, e->usize, &got);
        if (rc != 0 || got != e->usize) {
            free(out);
            return NULL;
        }
    } else {
        free(out);
        return NULL;
    }
    out[got] = 0;
    if (crc32_update(0, out, got) != e->crc) {
        free(out);
        return NULL;
    }
    *len = got;
    return out;
}
