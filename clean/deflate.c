/* deflate: packs data as raw deflate (RFC 1951) with the fixed codes:
 * matches found through a hash of the next three bytes, the longest of
 * a few tries. Simple and small; for the parts of zip files that clean
 * changed. */
#include <stdlib.h>
#include <string.h>

#include "clean.h"

#define WIN_SIZE 32768
#define HASH_SIZE 32768
#define MAX_TRIES 32
#define MIN_MATCH 3
#define MAX_MATCH 258

struct bits {
    struct buf *out;
    unsigned long acc;
    int n;
};

/* put_bits: n bits of v, lowest first */
static void put_bits(struct bits *b, unsigned long v, int n)
{
    b->acc |= v << b->n;
    b->n += n;
    while (b->n >= 8) {
        buf_byte(b->out, (int)(b->acc & 0xff));
        b->acc >>= 8;
        b->n -= 8;
    }
}

/* put_code: a Huffman code, highest bit first */
static void put_code(struct bits *b, unsigned long code, int len)
{
    unsigned long r;
    int i;

    r = 0;
    for (i = 0; i < len; i++) {
        r = r << 1 | (code & 1);
        code >>= 1;
    }
    put_bits(b, r, len);
}

static void put_lit(struct bits *b, int c)
{
    if (c < 144)
        put_code(b, 0x30 + c, 8);
    else if (c < 256)
        put_code(b, 0x190 + c - 144, 9);
    else if (c < 280)
        put_code(b, c - 256, 7);
    else
        put_code(b, 0xc0 + c - 280, 8);
}

static const short len_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const short len_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const long dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};
static const short dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static void put_match(struct bits *b, long len, long dist)
{
    int i;

    i = 28;
    while (len_base[i] > len)
        i--;
    put_lit(b, 257 + i);
    put_bits(b, (unsigned long)(len - len_base[i]), len_extra[i]);
    i = 29;
    while (dist_base[i] > dist)
        i--;
    put_code(b, (unsigned long)i, 5);
    put_bits(b, (unsigned long)(dist - dist_base[i]), dist_extra[i]);
}

static long hash3(const unsigned char *p)
{
    return ((long)p[0] << 10 ^ (long)p[1] << 5 ^ p[2]) & (HASH_SIZE - 1);
}

void deflate_raw(const unsigned char *in, long n, struct buf *out)
{
    struct bits b;
    long *head, *prev;
    long i, j, k, best, bdist, cand, h, tries;

    memset(&b, 0, sizeof b);
    b.out = out;
    head = malloc(HASH_SIZE * sizeof *head);
    prev = malloc(WIN_SIZE * sizeof *prev);
    put_bits(&b, 1, 1);         /* the last block */
    put_bits(&b, 1, 2);         /* fixed codes */
    if (head && prev) {
        for (i = 0; i < HASH_SIZE; i++)
            head[i] = -1;
    }
    i = 0;
    while (i < n) {
        best = 0;
        bdist = 0;
        if (head && prev && i + MIN_MATCH <= n) {
            h = hash3(in + i);
            cand = head[h];
            tries = 0;
            while (cand >= 0 && i - cand <= WIN_SIZE - 1 &&
                tries < MAX_TRIES)
            {
                k = 0;
                while (k < MAX_MATCH && i + k < n &&
                    in[cand + k] == in[i + k])
                {
                    k++;
                }
                if (k > best) {
                    best = k;
                    bdist = i - cand;
                }
                cand = prev[cand & (WIN_SIZE - 1)];
                tries++;
            }
        }
        if (best < MIN_MATCH) {
            put_lit(&b, in[i]);
            best = 1;
        } else {
            put_match(&b, best, bdist);
        }
        for (j = 0; j < best && head && prev; j++) {
            if (i + j + MIN_MATCH <= n) {
                h = hash3(in + i + j);
                prev[(i + j) & (WIN_SIZE - 1)] = head[h];
                head[h] = i + j;
            }
        }
        i += best;
    }
    put_lit(&b, 256);
    if (b.n > 0)
        buf_byte(out, (int)(b.acc & 0xff));
    free(head);
    free(prev);
}
