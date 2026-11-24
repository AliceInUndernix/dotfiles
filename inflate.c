#include <stdlib.h>
#include <string.h>
#include "kspkan.h"

struct inf {
    const unsigned char *in;
    long inlen;
    long incnt;
    unsigned char *out;
    long outmax;
    long outcnt;
    long bitbuf;
    int bitcnt;
    int err;
};

struct huff {
    short count[16];
    short symbol[288];
};

static const short lens[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const short lext[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const short dists[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};
static const short dext[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static int inf_bits(struct inf *s, int need)
{
    long val;
    long b;

    val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt >= s->inlen) {
            s->err = 1;
            return 0;
        }
        b = s->in[s->incnt];
        s->incnt++;
        val |= b << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1L << need) - 1));
}

static int inf_decode(struct inf *s, const struct huff *h)
{
    int len, code, first, index, count;

    code = 0;
    first = 0;
    index = 0;
    for (len = 1; len <= 15; len++) {
        code |= inf_bits(s, 1);
        if (s->err) {
            return -1;
        }
        count = h->count[len];
        if (code - count < first) {
            return h->symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static int inf_construct(struct huff *h, const short *length, int n)
{
    short offs[16];
    int symbol, len, left;

    for (len = 0; len <= 15; len++) {
        h->count[len] = 0;
    }
    for (symbol = 0; symbol < n; symbol++) {
        h->count[length[symbol]]++;
    }
    if (h->count[0] == n) {
        return 0;
    }
    left = 1;
    for (len = 1; len <= 15; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) {
            return left;
        }
    }
    offs[1] = 0;
    for (len = 1; len < 15; len++) {
        offs[len + 1] = (short)(offs[len] + h->count[len]);
    }
    for (symbol = 0; symbol < n; symbol++) {
        if (length[symbol] != 0) {
            h->symbol[offs[length[symbol]]] = (short)symbol;
            offs[length[symbol]]++;
        }
    }
    return left;
}

static int inf_codes(struct inf *s, const struct huff *lc,
    const struct huff *dc)
{
    int sym, len, dist, extra;

    for (;;) {
        sym = inf_decode(s, lc);
        if (sym < 0) {
            return -1;
        }
        if (sym < 256) {
            if (s->outcnt >= s->outmax) {
                return -2;
            }
            s->out[s->outcnt] = (unsigned char)sym;
            s->outcnt++;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29) {
                return -1;
            }
            extra = inf_bits(s, lext[sym]);
            len = lens[sym] + extra;
            sym = inf_decode(s, dc);
            if (sym < 0 || sym >= 30) {
                return -1;
            }
            extra = inf_bits(s, dext[sym]);
            dist = dists[sym] + extra;
            if (s->err || dist > s->outcnt) {
                return -1;
            }
            if (s->outcnt + len > s->outmax) {
                return -2;
            }
            while (len > 0) {
                s->out[s->outcnt] = s->out[s->outcnt - dist];
                s->outcnt++;
                len--;
            }
        }
    }
}

static int inf_stored(struct inf *s)
{
    long len, nlen;

    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->incnt + 4 > s->inlen) {
        return -1;
    }
    len = s->in[s->incnt] | (s->in[s->incnt + 1] << 8);
    nlen = s->in[s->incnt + 2] | (s->in[s->incnt + 3] << 8);
    s->incnt += 4;
    if (len != (~nlen & 0xffff)) {
        return -1;
    }
    if (s->incnt + len > s->inlen) {
        return -1;
    }
    if (s->outcnt + len > s->outmax) {
        return -2;
    }
    memcpy(s->out + s->outcnt, s->in + s->incnt, (unsigned long)len);
    s->incnt += len;
    s->outcnt += len;
    return 0;
}

static int inf_fixed(struct inf *s)
{
    struct huff lc, dc;
    short lengths[288];
    int i;

    for (i = 0; i < 144; i++) {
        lengths[i] = 8;
    }
    for (; i < 256; i++) {
        lengths[i] = 9;
    }
    for (; i < 280; i++) {
        lengths[i] = 7;
    }
    for (; i < 288; i++) {
        lengths[i] = 8;
    }
    inf_construct(&lc, lengths, 288);
    for (i = 0; i < 30; i++) {
        lengths[i] = 5;
    }
    inf_construct(&dc, lengths, 30);
    return inf_codes(s, &lc, &dc);
}

static int inf_dynamic(struct inf *s)
{
    static const short order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };
    struct huff lc, dc;
    short lengths[320];
    int nlen, ndist, ncode, index, err, sym, len, rep;

    nlen = inf_bits(s, 5) + 257;
    ndist = inf_bits(s, 5) + 1;
    ncode = inf_bits(s, 4) + 4;
    if (s->err || nlen > 286 || ndist > 30) {
        return -1;
    }
    for (index = 0; index < ncode; index++) {
        lengths[order[index]] = (short)inf_bits(s, 3);
    }
    for (; index < 19; index++) {
        lengths[order[index]] = 0;
    }
    err = inf_construct(&lc, lengths, 19);
    if (err != 0 || s->err) {
        return -1;
    }
    index = 0;
    while (index < nlen + ndist) {
        sym = inf_decode(s, &lc);
        if (sym < 0) {
            return -1;
        }
        if (sym < 16) {
            lengths[index] = (short)sym;
            index++;
        } else {
            len = 0;
            if (sym == 16) {
                if (index == 0) {
                    return -1;
                }
                len = lengths[index - 1];
                rep = 3 + inf_bits(s, 2);
            } else if (sym == 17) {
                rep = 3 + inf_bits(s, 3);
            } else {
                rep = 11 + inf_bits(s, 7);
            }
            if (s->err || index + rep > nlen + ndist) {
                return -1;
            }
            while (rep > 0) {
                lengths[index] = (short)len;
                index++;
                rep--;
            }
        }
    }
    if (lengths[256] == 0) {
        return -1;
    }
    err = inf_construct(&lc, lengths, nlen);
    if (err != 0 && (err < 0 || nlen != lc.count[0] + lc.count[1])) {
        return -1;
    }
    err = inf_construct(&dc, lengths + nlen, ndist);
    if (err != 0 && (err < 0 || ndist != dc.count[0] + dc.count[1])) {
        return -1;
    }
    return inf_codes(s, &lc, &dc);
}

/* returns 0 on success, -1 on bad data, -2 if output limit exceeded */
int inflate_raw(const unsigned char *in, long inlen, unsigned char *out,
    long outmax, long *outlen)
{
    struct inf s;
    int last, type, err;

    s.in = in;
    s.inlen = inlen;
    s.incnt = 0;
    s.out = out;
    s.outmax = outmax;
    s.outcnt = 0;
    s.bitbuf = 0;
    s.bitcnt = 0;
    s.err = 0;
    do {
        last = inf_bits(&s, 1);
        type = inf_bits(&s, 2);
        if (s.err) {
            return -1;
        }
        if (type == 0) {
            err = inf_stored(&s);
        } else if (type == 1) {
            err = inf_fixed(&s);
        } else if (type == 2) {
            err = inf_dynamic(&s);
        } else {
            err = -1;
        }
        if (err != 0) {
            return err;
        }
    } while (!last);
    *outlen = s.outcnt;
    return 0;
}

/* result is malloc'd; the size comes from the gzip trailer and is
   checked against maxout and against what really came out */
unsigned char *gunzip_alloc(const unsigned char *in, long inlen,
    long maxout, long *outlen)
{
    unsigned char *out;
    long pos, isize, got;
    int flags, xlen, rc;

    if (inlen < 18 || in[0] != 0x1f || in[1] != 0x8b || in[2] != 8) {
        return NULL;
    }
    flags = in[3];
    pos = 10;
    if (flags & 4) {
        xlen = in[pos] | (in[pos + 1] << 8);
        pos += 2 + xlen;
    }
    if (flags & 8) {
        while (pos < inlen && in[pos] != 0) {
            pos++;
        }
        pos++;
    }
    if (flags & 16) {
        while (pos < inlen && in[pos] != 0) {
            pos++;
        }
        pos++;
    }
    if (flags & 2) {
        pos += 2;
    }
    if (pos >= inlen - 8) {
        return NULL;
    }
    isize = (long)in[inlen - 4] | ((long)in[inlen - 3] << 8) |
        ((long)in[inlen - 2] << 16) | ((long)in[inlen - 1] << 24);
    if (isize > maxout) {
        return NULL;
    }
    out = (unsigned char *)xalloc(isize + 1);
    rc = inflate_raw(in + pos, inlen - pos - 8, out, isize, &got);
    if (rc != 0 || got != isize) {
        free(out);
        return NULL;
    }
    *outlen = got;
    return out;
}
