#include <string.h>
#include "kspkan.h"

#define SHA_ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static const unsigned int sha_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
    0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
    0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
    0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
    0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static void sha_block(struct sha256 *s, const unsigned char *p)
{
    unsigned int w[64];
    unsigned int v[8];
    unsigned int t1, t2, x0, x1;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((unsigned int)p[4 * i] << 24) |
            ((unsigned int)p[4 * i + 1] << 16) |
            ((unsigned int)p[4 * i + 2] << 8) |
            (unsigned int)p[4 * i + 3];
    }
    for (i = 16; i < 64; i++) {
        x0 = SHA_ROR(w[i - 15], 7) ^ SHA_ROR(w[i - 15], 18) ^
            (w[i - 15] >> 3);
        x1 = SHA_ROR(w[i - 2], 17) ^ SHA_ROR(w[i - 2], 19) ^
            (w[i - 2] >> 10);
        w[i] = w[i - 16] + x0 + w[i - 7] + x1;
    }
    for (i = 0; i < 8; i++) {
        v[i] = s->h[i];
    }
    for (i = 0; i < 64; i++) {
        x1 = SHA_ROR(v[4], 6) ^ SHA_ROR(v[4], 11) ^ SHA_ROR(v[4], 25);
        t1 = v[7] + x1 + ((v[4] & v[5]) ^ (~v[4] & v[6])) +
            sha_k[i] + w[i];
        x0 = SHA_ROR(v[0], 2) ^ SHA_ROR(v[0], 13) ^ SHA_ROR(v[0], 22);
        t2 = x0 + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + t1;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = t1 + t2;
    }
    for (i = 0; i < 8; i++) {
        s->h[i] += v[i];
    }
}

void sha256_init(struct sha256 *s)
{
    s->h[0] = 0x6a09e667;
    s->h[1] = 0xbb67ae85;
    s->h[2] = 0x3c6ef372;
    s->h[3] = 0xa54ff53a;
    s->h[4] = 0x510e527f;
    s->h[5] = 0x9b05688c;
    s->h[6] = 0x1f83d9ab;
    s->h[7] = 0x5be0cd19;
    s->n = 0;
    s->total = 0;
}

void sha256_add(struct sha256 *s, const unsigned char *p, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        s->buf[s->n] = p[i];
        s->n++;
        if (s->n == 64) {
            sha_block(s, s->buf);
            s->n = 0;
        }
    }
    s->total += len;
}

void sha256_done(struct sha256 *s, unsigned char *out)
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
    sha256_add(s, pad, padlen + 8);
    for (i = 0; i < 32; i++) {
        out[i] = (unsigned char)(s->h[i / 4] >> (24 - 8 * (i % 4)));
    }
}

void sha256_hex(const unsigned char *d, char *hex)
{
    static const char digits[] = "0123456789abcdef";
    int i;

    for (i = 0; i < 32; i++) {
        hex[2 * i] = digits[d[i] >> 4];
        hex[2 * i + 1] = digits[d[i] & 15];
    }
    hex[64] = 0;
}
