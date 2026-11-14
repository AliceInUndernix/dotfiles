/* SHA-256 (FIPS 180-2 description), portable to 32-bit machines */
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "webarchiver.h"

#define MASK32 0xffffffffUL
#define ROTR32(x, n) ((((x) >> (n)) | ((x) << (32 - (n)))) & MASK32)

static const unsigned long sha_k[64] = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL,
    0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL,
    0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
    0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
    0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL,
    0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL,
    0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL,
    0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL,
    0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
    0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
};

void sha_init(struct sha256 *c)
{
    c->h[0] = 0x6a09e667UL;
    c->h[1] = 0xbb67ae85UL;
    c->h[2] = 0x3c6ef372UL;
    c->h[3] = 0xa54ff53aUL;
    c->h[4] = 0x510e527fUL;
    c->h[5] = 0x9b05688cUL;
    c->h[6] = 0x1f83d9abUL;
    c->h[7] = 0x5be0cd19UL;
    c->used = 0;
    c->total = 0;
}

static void sha_block(struct sha256 *c, const unsigned char *p)
{
    unsigned long w[64], a, b, cc, d, e, f, g, h, t1, t2, s0, s1;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((unsigned long)p[4 * i] << 24) |
            ((unsigned long)p[4 * i + 1] << 16) |
            ((unsigned long)p[4 * i + 2] << 8) | p[4 * i + 3];
    }
    for (i = 16; i < 64; i++) {
        s0 = ROTR32(w[i - 15], 7) ^ ROTR32(w[i - 15], 18) ^
            (w[i - 15] >> 3);
        s1 = ROTR32(w[i - 2], 17) ^ ROTR32(w[i - 2], 19) ^
            (w[i - 2] >> 10);
        w[i] = (w[i - 16] + s0 + w[i - 7] + s1) & MASK32;
    }
    a = c->h[0];
    b = c->h[1];
    cc = c->h[2];
    d = c->h[3];
    e = c->h[4];
    f = c->h[5];
    g = c->h[6];
    h = c->h[7];
    for (i = 0; i < 64; i++) {
        s1 = ROTR32(e, 6) ^ ROTR32(e, 11) ^ ROTR32(e, 25);
        t1 = (h + s1 + ((e & f) ^ (~e & g)) + sha_k[i] + w[i]) & MASK32;
        s0 = ROTR32(a, 2) ^ ROTR32(a, 13) ^ ROTR32(a, 22);
        t2 = (s0 + ((a & b) ^ (a & cc) ^ (b & cc))) & MASK32;
        h = g;
        g = f;
        f = e;
        e = (d + t1) & MASK32;
        d = cc;
        cc = b;
        b = a;
        a = (t1 + t2) & MASK32;
    }
    c->h[0] = (c->h[0] + a) & MASK32;
    c->h[1] = (c->h[1] + b) & MASK32;
    c->h[2] = (c->h[2] + cc) & MASK32;
    c->h[3] = (c->h[3] + d) & MASK32;
    c->h[4] = (c->h[4] + e) & MASK32;
    c->h[5] = (c->h[5] + f) & MASK32;
    c->h[6] = (c->h[6] + g) & MASK32;
    c->h[7] = (c->h[7] + h) & MASK32;
}

void sha_update(struct sha256 *c, const unsigned char *p, int n)
{
    int take;

    c->total += n;
    while (n > 0) {
        take = 64 - c->used;
        if (take > n)
            take = n;
        memcpy(c->block + c->used, p, take);
        c->used += take;
        p += take;
        n -= take;
        if (c->used == 64) {
            sha_block(c, c->block);
            c->used = 0;
        }
    }
}

void sha_final(struct sha256 *c, unsigned char *digest)
{
    unsigned long long bits;
    unsigned char pad;
    unsigned char len[8];
    int i;

    bits = c->total * 8;
    for (i = 0; i < 8; i++)
        len[7 - i] = (unsigned char)(bits >> (8 * i));
    pad = 0x80;
    sha_update(c, &pad, 1);
    pad = 0;
    while (c->used != 56)
        sha_update(c, &pad, 1);
    sha_update(c, len, 8);
    for (i = 0; i < 8; i++) {
        digest[4 * i] = (unsigned char)(c->h[i] >> 24);
        digest[4 * i + 1] = (unsigned char)(c->h[i] >> 16);
        digest[4 * i + 2] = (unsigned char)(c->h[i] >> 8);
        digest[4 * i + 3] = (unsigned char)c->h[i];
    }
}

void sha_hex(const unsigned char *digest, char *hex)
{
    static const char digits[] = "0123456789abcdef";
    int i;

    for (i = 0; i < 32; i++) {
        hex[2 * i] = digits[digest[i] >> 4];
        hex[2 * i + 1] = digits[digest[i] & 15];
    }
    hex[64] = 0;
}

void sha_data(const char *data, int len, char *hex)
{
    struct sha256 c;
    unsigned char digest[32];

    sha_init(&c);
    sha_update(&c, (const unsigned char *)data, len);
    sha_final(&c, digest);
    sha_hex(digest, hex);
}

/* hex digest of a file; 0 if it cannot be read */
int sha_file(const char *path, char *hex)
{
    struct sha256 c;
    unsigned char digest[32];
    unsigned char tmp[65536];
    int fd, n;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    sha_init(&c);
    while ((n = read(fd, tmp, sizeof(tmp))) > 0)
        sha_update(&c, tmp, n);
    close(fd);
    sha_final(&c, digest);
    sha_hex(digest, hex);
    return 1;
}
