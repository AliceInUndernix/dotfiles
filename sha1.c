#include <string.h>
#include "kspkan.h"

#define SHA_ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void sha1_block(struct sha1 *s, const unsigned char *p)
{
    unsigned int w[80];
    unsigned int a, b, c, d, e, f, k, t;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((unsigned int)p[4 * i] << 24) |
            ((unsigned int)p[4 * i + 1] << 16) |
            ((unsigned int)p[4 * i + 2] << 8) |
            (unsigned int)p[4 * i + 3];
    }
    for (i = 16; i < 80; i++) {
        w[i] = SHA_ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    a = s->h[0];
    b = s->h[1];
    c = s->h[2];
    d = s->h[3];
    e = s->h[4];
    for (i = 0; i < 80; i++) {
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5a827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ed9eba1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdc;
        } else {
            f = b ^ c ^ d;
            k = 0xca62c1d6;
        }
        t = SHA_ROL(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = SHA_ROL(b, 30);
        b = a;
        a = t;
    }
    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
}

void sha1_init(struct sha1 *s)
{
    s->h[0] = 0x67452301;
    s->h[1] = 0xefcdab89;
    s->h[2] = 0x98badcfe;
    s->h[3] = 0x10325476;
    s->h[4] = 0xc3d2e1f0;
    s->n = 0;
    s->total = 0;
}

void sha1_add(struct sha1 *s, const unsigned char *p, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        s->buf[s->n] = p[i];
        s->n++;
        if (s->n == 64) {
            sha1_block(s, s->buf);
            s->n = 0;
        }
    }
    s->total += len;
}

void sha1_done(struct sha1 *s, unsigned char *out)
{
    unsigned char pad[72];
    long long bits;
    int padlen;
    int i;

    bits = s->total * 8;
    padlen = 56 - s->n;
    if (padlen <= 0) {
        padlen += 64;
    }
    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    for (i = 0; i < 8; i++) {
        pad[padlen + i] = (unsigned char)(bits >> (56 - 8 * i));
    }
    sha1_add(s, pad, padlen + 8);
    for (i = 0; i < 20; i++) {
        out[i] = (unsigned char)(s->h[i / 4] >> (24 - 8 * (i % 4)));
    }
}

void sha1_hex(const unsigned char *d, char *hex)
{
    static const char digits[] = "0123456789abcdef";
    int i;

    for (i = 0; i < 20; i++) {
        hex[2 * i] = digits[d[i] >> 4];
        hex[2 * i + 1] = digits[d[i] & 15];
    }
    hex[40] = 0;
}
