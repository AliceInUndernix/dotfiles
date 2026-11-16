/* inflate: undoes zlib's deflate (RFC 1950/1951 describe it), for the
 * packed objects inside PDF files. Written from the description, in
 * the way of Mark Adler's puff.c: slow and small. Stops at max bytes,
 * so a zip bomb can't fill the memory. */
#include <string.h>

#include "clean.h"

#define MAX_BITS 15

struct inf {
    const unsigned char *in;
    long n;
    long pos;
    long bitbuf;
    int bitcnt;
    struct buf *out;
    long max;
    int bad;
};

struct huff {
    short count[MAX_BITS + 1];
    short symbol[320];
};

static int bits(struct inf *s, int need)
{
    long v;

    v = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->pos >= s->n) {
            s->bad = 1;
            return 0;
        }
        v |= (long)s->in[s->pos] << s->bitcnt;
        s->pos++;
        s->bitcnt += 8;
    }
    s->bitbuf = v >> need;
    s->bitcnt -= need;
    return (int)(v & ((1L << need) - 1));
}

static int emit(struct inf *s, int c)
{
    if (s->out->n >= s->max) {
        s->bad = 1;
        return -1;
    }
    buf_byte(s->out, c);
    return 0;
}

static int stored(struct inf *s)
{
    long len;

    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->pos + 4 > s->n)
        return -1;
    len = get16le(s->in + s->pos);
    if ((get16le(s->in + s->pos + 2) ^ 0xffff) != len)
        return -1;
    s->pos += 4;
    if (s->pos + len > s->n || s->out->n + len > s->max)
        return -1;
    buf_put(s->out, s->in + s->pos, len);
    s->pos += len;
    return 0;
}

static int decode(struct inf *s, struct huff *h)
{
    int code, first, count, index, len;

    code = 0;
    first = 0;
    index = 0;
    for (len = 1; len <= MAX_BITS; len++) {
        code |= bits(s, 1);
        if (s->bad)
            return -1;
        count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

/* build: a decoding table from code lengths; -1 when they are wrong */
static int build(struct huff *h, const short *length, int n)
{
    short offs[MAX_BITS + 1];
    int sym, len, left;

    for (len = 0; len <= MAX_BITS; len++)
        h->count[len] = 0;
    for (sym = 0; sym < n; sym++)
        h->count[length[sym]]++;
    if (h->count[0] == n)
        return 0;
    left = 1;
    for (len = 1; len <= MAX_BITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
            return -1;
    }
    offs[1] = 0;
    for (len = 1; len < MAX_BITS; len++)
        offs[len + 1] = offs[len] + h->count[len];
    for (sym = 0; sym < n; sym++) {
        if (length[sym] != 0) {
            h->symbol[offs[length[sym]]] = (short)sym;
            offs[length[sym]]++;
        }
    }
    return left;
}

static const short len_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const short len_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const short dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};
static const short dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static int codes(struct inf *s, struct huff *lencode, struct huff *distcode)
{
    int rc;
    int sym, i;
    long len, dist, from;

    for (;;) {
        sym = decode(s, lencode);
        if (sym < 0)
            return -1;
        if (sym < 256) {
            rc = emit(s, sym);
            if (rc < 0)
                return -1;
            continue;
        }
        if (sym == 256)
            return 0;
        sym -= 257;
        if (sym >= 29)
            return -1;
        len = len_base[sym] + bits(s, len_extra[sym]);
        sym = decode(s, distcode);
        if (sym < 0 || sym >= 30)
            return -1;
        dist = dist_base[sym] + bits(s, dist_extra[sym]);
        if (s->bad || dist > s->out->n)
            return -1;
        from = s->out->n - dist;
        for (i = 0; i < len; i++) {
            rc = emit(s, s->out->p[from + i]);
            if (rc < 0)
                return -1;
        }
    }
}

static int fixed(struct inf *s)
{
    static struct huff lencode, distcode;
    static int ready;
    short length[288];
    int sym;

    if (!ready) {
        for (sym = 0; sym < 144; sym++)
            length[sym] = 8;
        for (; sym < 256; sym++)
            length[sym] = 9;
        for (; sym < 280; sym++)
            length[sym] = 7;
        for (; sym < 288; sym++)
            length[sym] = 8;
        build(&lencode, length, 288);
        for (sym = 0; sym < 30; sym++)
            length[sym] = 5;
        build(&distcode, length, 30);
        ready = 1;
    }
    return codes(s, &lencode, &distcode);
}

static const short order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

static int dynamic(struct inf *s)
{
    int rc;
    struct huff lencode, distcode;
    short length[320];
    int nlen, ndist, ncode, index, sym, len, rep, err;

    nlen = bits(s, 5) + 257;
    ndist = bits(s, 5) + 1;
    ncode = bits(s, 4) + 4;
    if (s->bad || nlen > 286 || ndist > 30)
        return -1;
    for (index = 0; index < ncode; index++)
        length[order[index]] = (short)bits(s, 3);
    for (; index < 19; index++)
        length[order[index]] = 0;
    rc = build(&lencode, length, 19);
    if (rc != 0)
        return -1;
    index = 0;
    while (index < nlen + ndist) {
        sym = decode(s, &lencode);
        if (sym < 0)
            return -1;
        if (sym < 16) {
            length[index] = (short)sym;
            index++;
            continue;
        }
        len = 0;
        if (sym == 16) {
            if (index == 0)
                return -1;
            len = length[index - 1];
            rep = 3 + bits(s, 2);
        } else if (sym == 17) {
            rep = 3 + bits(s, 3);
        } else {
            rep = 11 + bits(s, 7);
        }
        if (s->bad || index + rep > nlen + ndist)
            return -1;
        while (rep > 0) {
            length[index] = (short)len;
            index++;
            rep--;
        }
    }
    if (length[256] == 0)
        return -1;
    err = build(&lencode, length, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1))
        return -1;
    err = build(&distcode, length + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1))
        return -1;
    return codes(s, &lencode, &distcode);
}

int inflate_zlib(const unsigned char *in, long n, struct buf *out, long max)
{
    struct inf s;
    int last, type, r;

    if (n < 2 || (in[0] & 0x0f) != 8 || (get16be(in) % 31) != 0 ||
        (in[1] & 0x20))
    {
        return -1;
    }
    memset(&s, 0, sizeof s);
    s.in = in;
    s.n = n;
    s.pos = 2;
    s.out = out;
    s.max = max;
    do {
        last = bits(&s, 1);
        type = bits(&s, 2);
        if (s.bad)
            return -1;
        if (type == 0)
            r = stored(&s);
        else if (type == 1)
            r = fixed(&s);
        else if (type == 2)
            r = dynamic(&s);
        else
            r = -1;
        if (r < 0 || s.bad)
            return -1;
    } while (!last);
    return 0;
}
