/* crypto.c: Argon2id, XChaCha20, Poly1305 and BLAKE2b for seal.
 *
 * Taken from Monocypher 4.0.2 (https://monocypher.org, the untouched
 * file is archive/monocypher/monocypher-4.0.2.tar.gz) and rewritten in
 * C89 for this tree: only the parts seal needs, Argon2 with one lane
 * only, no size_t or stdint.h, no loop macros. The algorithms and their
 * results are Monocypher's; seal/test checks them against libsodium
 * and the Argon2 reference.
 *
 * Monocypher is by Loup Vaillant, Michael Savage and Fabio Scotoni,
 * dedicated to the public domain (CC0 1.0), with a 2-clause BSD licence
 * as a fallback:
 *
 * Copyright (c) 2017-2023, Loup Vaillant
 * Copyright (c) 2017-2019, Michael Savage
 * Copyright (c) 2017-2023, Fabio Scotoni
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
 * WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE. */
#include <limits.h>

#include "crypto.h"

/* the build stops here when unsigned int is not 32 bits */
typedef char u32_is_32_bits[UINT_MAX == 0xffffffffUL ? 1 : -1];

static const u8 zero[128];

/* ------------------------------------------------------------ utils */

/* how many bytes x lacks to be a multiple of pow_2 */
static long gap(long x, long pow_2)
{
    return (pow_2 - (x & (pow_2 - 1))) & (pow_2 - 1);
}

static u32 load32_le(const u8 *s)
{
    return (u32)s[0] | (u32)s[1] << 8 | (u32)s[2] << 16 |
        (u32)s[3] << 24;
}

static u64 load64_le(const u8 *s)
{
    return (u64)load32_le(s) | (u64)load32_le(s + 4) << 32;
}

static void store32_le(u8 *out, u32 in)
{
    out[0] = in & 0xff;
    out[1] = (in >> 8) & 0xff;
    out[2] = (in >> 16) & 0xff;
    out[3] = (in >> 24) & 0xff;
}

static void store64_le(u8 *out, u64 in)
{
    store32_le(out, (u32)in);
    store32_le(out + 4, (u32)(in >> 32));
}

static void load32_le_buf(u32 *dst, const u8 *src, long n)
{
    long i;

    for (i = 0; i < n; i++)
        dst[i] = load32_le(src + i * 4);
}

static void load64_le_buf(u64 *dst, const u8 *src, long n)
{
    long i;

    for (i = 0; i < n; i++)
        dst[i] = load64_le(src + i * 8);
}

static void store32_le_buf(u8 *dst, const u32 *src, long n)
{
    long i;

    for (i = 0; i < n; i++)
        store32_le(dst + i * 4, src[i]);
}

static void store64_le_buf(u8 *dst, const u64 *src, long n)
{
    long i;

    for (i = 0; i < n; i++)
        store64_le(dst + i * 8, src[i]);
}

static u64 rotr64(u64 x, int n)
{
    return (x >> n) ^ (x << (64 - n));
}

static u32 rotl32(u32 x, int n)
{
    return (x << n) ^ (x >> (32 - n));
}

/* constant time: 0 when a and b are the same 16 bytes, -1 if not */
int crypto_verify16(const u8 *a, const u8 *b)
{
    u64 diff, half;

    diff = (load64_le(a) ^ load64_le(b)) |
        (load64_le(a + 8) ^ load64_le(b + 8));
    half = (diff >> 32) | (u32)diff;
    return (int)(1 & ((half - 1) >> 32)) - 1;
}

void crypto_wipe(void *secret, long size)
{
    volatile u8 *v;
    long i;

    v = (volatile u8 *)secret;
    for (i = 0; i < size; i++)
        v[i] = 0;
}

/* --------------------------------------------------------- ChaCha20 */

#define QUARTERROUND(a, b, c, d) \
    a += b; d = rotl32(d ^ a, 16); \
    c += d; b = rotl32(b ^ c, 12); \
    a += b; d = rotl32(d ^ a, 8); \
    c += d; b = rotl32(b ^ c, 7)

static void chacha20_rounds(u32 *out, const u32 *in)
{
    u32 t0, t1, t2, t3, t4, t5, t6, t7;
    u32 t8, t9, t10, t11, t12, t13, t14, t15;
    int i;

    t0 = in[0]; t1 = in[1]; t2 = in[2]; t3 = in[3];
    t4 = in[4]; t5 = in[5]; t6 = in[6]; t7 = in[7];
    t8 = in[8]; t9 = in[9]; t10 = in[10]; t11 = in[11];
    t12 = in[12]; t13 = in[13]; t14 = in[14]; t15 = in[15];
    for (i = 0; i < 10; i++) {
        QUARTERROUND(t0, t4, t8, t12);
        QUARTERROUND(t1, t5, t9, t13);
        QUARTERROUND(t2, t6, t10, t14);
        QUARTERROUND(t3, t7, t11, t15);
        QUARTERROUND(t0, t5, t10, t15);
        QUARTERROUND(t1, t6, t11, t12);
        QUARTERROUND(t2, t7, t8, t13);
        QUARTERROUND(t3, t4, t9, t14);
    }
    out[0] = t0; out[1] = t1; out[2] = t2; out[3] = t3;
    out[4] = t4; out[5] = t5; out[6] = t6; out[7] = t7;
    out[8] = t8; out[9] = t9; out[10] = t10; out[11] = t11;
    out[12] = t12; out[13] = t13; out[14] = t14; out[15] = t15;
}

static const u8 chacha20_constant[17] = "expand 32-byte k";

/* HChaCha20: out (32 bytes) from key (32) and in (16) */
void chacha20_h(u8 *out, const u8 *key, const u8 *in)
{
    u32 block[16];

    load32_le_buf(block, chacha20_constant, 4);
    load32_le_buf(block + 4, key, 8);
    load32_le_buf(block + 12, in, 4);
    chacha20_rounds(block, block);
    store32_le_buf(out, block, 4);
    store32_le_buf(out + 16, block + 12, 4);
    crypto_wipe(block, sizeof block);
}

/* ChaCha20 with an 8-byte nonce and a 64-bit block counter: out is in
 * XORed with the key stream (in 0: the key stream itself). Returns the
 * counter after the last block. */
u64 chacha20_djb(u8 *out, const u8 *in, long n, const u8 *key,
    const u8 *nonce, u64 ctr)
{
    u32 input[16], pool[16];
    u8 tmp[64];
    long blocks, i, j;
    u32 p;

    load32_le_buf(input, chacha20_constant, 4);
    load32_le_buf(input + 4, key, 8);
    load32_le_buf(input + 14, nonce, 2);
    input[12] = (u32)ctr;
    input[13] = (u32)(ctr >> 32);

    blocks = n >> 6;
    for (i = 0; i < blocks; i++) {
        chacha20_rounds(pool, input);
        if (in != 0) {
            for (j = 0; j < 16; j++) {
                p = pool[j] + input[j];
                store32_le(out, p ^ load32_le(in));
                out += 4;
                in += 4;
            }
        } else {
            for (j = 0; j < 16; j++) {
                p = pool[j] + input[j];
                store32_le(out, p);
                out += 4;
            }
        }
        input[12]++;
        if (input[12] == 0)
            input[13]++;
    }
    n &= 63;

    if (n > 0) {
        if (in == 0)
            in = zero;
        chacha20_rounds(pool, input);
        for (i = 0; i < 16; i++)
            store32_le(tmp + i * 4, pool[i] + input[i]);
        for (i = 0; i < n; i++)
            out[i] = tmp[i] ^ in[i];
        crypto_wipe(tmp, sizeof tmp);
    }
    ctr = input[12] + ((u64)input[13] << 32) + (n > 0);
    crypto_wipe(pool, sizeof pool);
    crypto_wipe(input, sizeof input);
    return ctr;
}

/* --------------------------------------------------------- Poly1305 */

/* h = (h + c) * r, for each 16-byte block; end: the 2^128 bit */
static void poly_blocks(struct poly1305 *ctx, const u8 *in, long blocks,
    u32 end)
{
    u32 r0, r1, r2, r3, rr0, rr1, rr2, rr3, rr4;
    u32 h0, h1, h2, h3, h4, s4, x4, u4, u5;
    u64 s0, s1, s2, s3, x0, x1, x2, x3, u0, u1, u2, u3;
    long i;

    r0 = ctx->r[0];
    r1 = ctx->r[1];
    r2 = ctx->r[2];
    r3 = ctx->r[3];
    rr0 = (r0 >> 2) * 5;
    rr1 = (r1 >> 2) + r1;
    rr2 = (r2 >> 2) + r2;
    rr3 = (r3 >> 2) + r3;
    rr4 = r0 & 3;
    h0 = ctx->h[0];
    h1 = ctx->h[1];
    h2 = ctx->h[2];
    h3 = ctx->h[3];
    h4 = ctx->h[4];

    for (i = 0; i < blocks; i++) {
        s0 = (u64)h0 + load32_le(in);
        s1 = (u64)h1 + load32_le(in + 4);
        s2 = (u64)h2 + load32_le(in + 8);
        s3 = (u64)h3 + load32_le(in + 12);
        s4 = h4 + end;
        in += 16;

        x0 = s0 * r0 + s1 * rr3 + s2 * rr2 + s3 * rr1 + s4 * rr0;
        x1 = s0 * r1 + s1 * r0 + s2 * rr3 + s3 * rr2 + s4 * rr1;
        x2 = s0 * r2 + s1 * r1 + s2 * r0 + s3 * rr3 + s4 * rr2;
        x3 = s0 * r3 + s1 * r2 + s2 * r1 + s3 * r0 + s4 * rr3;
        x4 = s4 * rr4;

        u5 = x4 + (u32)(x3 >> 32);
        u0 = (u5 >> 2) * 5 + (x0 & 0xffffffff);
        u1 = (u0 >> 32) + (x1 & 0xffffffff) + (x0 >> 32);
        u2 = (u1 >> 32) + (x2 & 0xffffffff) + (x1 >> 32);
        u3 = (u2 >> 32) + (x3 & 0xffffffff) + (x2 >> 32);
        u4 = (u32)(u3 >> 32) + (u5 & 3);

        h0 = (u32)(u0 & 0xffffffff);
        h1 = (u32)(u1 & 0xffffffff);
        h2 = (u32)(u2 & 0xffffffff);
        h3 = (u32)(u3 & 0xffffffff);
        h4 = u4;
    }
    ctx->h[0] = h0;
    ctx->h[1] = h1;
    ctx->h[2] = h2;
    ctx->h[3] = h3;
    ctx->h[4] = h4;
}

void poly1305_init(struct poly1305 *ctx, const u8 *key)
{
    int i;

    for (i = 0; i < 5; i++)
        ctx->h[i] = 0;
    ctx->c_idx = 0;
    load32_le_buf(ctx->r, key, 4);
    load32_le_buf(ctx->pad, key + 16, 4);
    ctx->r[0] &= 0x0fffffff;
    for (i = 1; i < 4; i++)
        ctx->r[i] &= 0x0ffffffc;
}

void poly1305_update(struct poly1305 *ctx, const u8 *msg, long n)
{
    long aligned, blocks, i;

    if (n == 0)
        return;

    aligned = gap(ctx->c_idx, 16);
    if (aligned > n)
        aligned = n;
    for (i = 0; i < aligned; i++) {
        ctx->c[ctx->c_idx] = msg[i];
        ctx->c_idx++;
    }
    msg += aligned;
    n -= aligned;

    if (ctx->c_idx == 16) {
        poly_blocks(ctx, ctx->c, 1, 1);
        ctx->c_idx = 0;
    }

    blocks = n >> 4;
    poly_blocks(ctx, msg, blocks, 1);
    msg += blocks << 4;
    n &= 15;

    for (i = 0; i < n; i++) {
        ctx->c[ctx->c_idx] = msg[i];
        ctx->c_idx++;
    }
}

void poly1305_final(struct poly1305 *ctx, u8 *mac)
{
    u64 c;
    long i;

    if (ctx->c_idx != 0) {
        for (i = ctx->c_idx; i < 16; i++)
            ctx->c[i] = 0;
        ctx->c[ctx->c_idx] = 1;
        poly_blocks(ctx, ctx->c, 1, 0);
    }

    /* subtract 2^130-5 when h is at least that */
    c = 5;
    for (i = 0; i < 4; i++) {
        c += ctx->h[i];
        c >>= 32;
    }
    c += ctx->h[4];
    c = (c >> 2) * 5;
    for (i = 0; i < 4; i++) {
        c += (u64)ctx->h[i] + ctx->pad[i];
        store32_le(mac + i * 4, (u32)c);
        c = c >> 32;
    }
    crypto_wipe(ctx, sizeof *ctx);
}

/* ---------------------------------------------------------- BLAKE2b */

static const u64 blake2b_iv[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
    0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

static const u8 sigma[12][16] = {
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 },
    { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 },
    { 11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4 },
    {  7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8 },
    {  9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13 },
    {  2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9 },
    { 12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11 },
    { 13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10 },
    {  6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5 },
    { 10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13,  0 },
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 },
    { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 }
};

#define BLAKE2_G(a, b, c, d, x, y) \
    a += b + x; d = rotr64(d ^ a, 32); \
    c += d; b = rotr64(b ^ c, 24); \
    a += b + y; d = rotr64(d ^ a, 16); \
    c += d; b = rotr64(b ^ c, 63)

static void blake2b_compress(struct blake2b *ctx, int is_last)
{
    u64 v[16];
    u64 *m;
    const u8 *s;
    int i;

    ctx->offset[0] += (u64)ctx->idx;
    if (ctx->offset[0] < (u64)ctx->idx)
        ctx->offset[1]++;

    for (i = 0; i < 8; i++) {
        v[i] = ctx->hash[i];
        v[i + 8] = blake2b_iv[i];
    }
    v[12] ^= ctx->offset[0];
    v[13] ^= ctx->offset[1];
    if (is_last)
        v[14] = ~v[14];

    m = ctx->input;
    for (i = 0; i < 12; i++) {
        s = sigma[i];
        BLAKE2_G(v[0], v[4], v[8], v[12], m[s[0]], m[s[1]]);
        BLAKE2_G(v[1], v[5], v[9], v[13], m[s[2]], m[s[3]]);
        BLAKE2_G(v[2], v[6], v[10], v[14], m[s[4]], m[s[5]]);
        BLAKE2_G(v[3], v[7], v[11], v[15], m[s[6]], m[s[7]]);
        BLAKE2_G(v[0], v[5], v[10], v[15], m[s[8]], m[s[9]]);
        BLAKE2_G(v[1], v[6], v[11], v[12], m[s[10]], m[s[11]]);
        BLAKE2_G(v[2], v[7], v[8], v[13], m[s[12]], m[s[13]]);
        BLAKE2_G(v[3], v[4], v[9], v[14], m[s[14]], m[s[15]]);
    }

    for (i = 0; i < 8; i++)
        ctx->hash[i] ^= v[i] ^ v[i + 8];
}

void blake2b_init(struct blake2b *ctx, long hash_size)
{
    int i;

    for (i = 0; i < 8; i++)
        ctx->hash[i] = blake2b_iv[i];
    ctx->hash[0] ^= 0x01010000 ^ (u64)hash_size;
    ctx->offset[0] = 0;
    ctx->offset[1] = 0;
    ctx->hash_size = hash_size;
    ctx->idx = 0;
    for (i = 0; i < 16; i++)
        ctx->input[i] = 0;
}

/* Byte by byte into the 128-byte block; a full block is compressed
 * only when more input comes, since the last block is special. */
void blake2b_update(struct blake2b *ctx, const u8 *msg, long n)
{
    long i, word, byte;

    for (i = 0; i < n; i++) {
        if (ctx->idx == 128) {
            blake2b_compress(ctx, 0);
            ctx->idx = 0;
        }
        if (ctx->idx == 0) {
            if (n - i >= 128) {
                /* a whole block at once: much faster */
                load64_le_buf(ctx->input, msg + i, 16);
                ctx->idx = 128;
                i += 127;
                continue;
            }
            for (word = 0; word < 16; word++)
                ctx->input[word] = 0;
        }
        word = ctx->idx >> 3;
        byte = ctx->idx & 7;
        ctx->input[word] |= (u64)msg[i] << (byte << 3);
        ctx->idx++;
    }
}

void blake2b_final(struct blake2b *ctx, u8 *hash)
{
    long size, words, i;

    blake2b_compress(ctx, 1);
    size = ctx->hash_size < 64 ? ctx->hash_size : 64;
    words = size >> 3;
    store64_le_buf(hash, ctx->hash, words);
    for (i = words << 3; i < size; i++)
        hash[i] = (u8)((ctx->hash[i >> 3] >> (8 * (i & 7))) & 0xff);
    crypto_wipe(ctx, sizeof *ctx);
}

void blake2b(u8 *hash, long hash_size, const u8 *msg, long n)
{
    struct blake2b ctx;

    blake2b_init(&ctx, hash_size);
    blake2b_update(&ctx, msg, n);
    blake2b_final(&ctx, hash);
}

/* ----------------------------------------------------------- Argon2 */

struct blk {
    u64 a[128];
};

static void blake_update_32(struct blake2b *ctx, u32 input)
{
    u8 buf[4];

    store32_le(buf, input);
    blake2b_update(ctx, buf, 4);
    crypto_wipe(buf, sizeof buf);
}

static void blake_update_32_buf(struct blake2b *ctx, const u8 *buf,
    u32 size)
{
    blake_update_32(ctx, size);
    blake2b_update(ctx, buf, size);
}

static void copy_block(struct blk *dst, const struct blk *in)
{
    int i;

    for (i = 0; i < 128; i++)
        dst->a[i] = in->a[i];
}

static void xor_block(struct blk *dst, const struct blk *in)
{
    int i;

    for (i = 0; i < 128; i++)
        dst->a[i] ^= in->a[i];
}

/* H': BLAKE2b with any digest size */
static void extended_hash(u8 *digest, u32 digest_size, const u8 *input,
    u32 input_size)
{
    struct blake2b ctx;
    u32 r, i, in, out;

    blake2b_init(&ctx, digest_size < 64 ? digest_size : 64);
    blake_update_32(&ctx, digest_size);
    blake2b_update(&ctx, input, input_size);
    blake2b_final(&ctx, digest);

    if (digest_size > 64) {
        r = (u32)(((u64)digest_size + 31) >> 5) - 2;
        i = 1;
        in = 0;
        out = 32;
        while (i < r) {
            /* input and output overlap: on purpose */
            blake2b(digest + out, 64, digest + in, 64);
            i += 1;
            in += 32;
            out += 32;
        }
        blake2b(digest + out, digest_size - 32 * r, digest + in, 64);
    }
}

#define ARGON_LSB(x) ((u64)(u32)(x))
#define ARGON_G(a, b, c, d) \
    a += b + ((ARGON_LSB(a) * ARGON_LSB(b)) << 1); \
    d ^= a; d = rotr64(d, 32); \
    c += d + ((ARGON_LSB(c) * ARGON_LSB(d)) << 1); \
    b ^= c; b = rotr64(b, 24); \
    a += b + ((ARGON_LSB(a) * ARGON_LSB(b)) << 1); \
    d ^= a; d = rotr64(d, 16); \
    c += d + ((ARGON_LSB(c) * ARGON_LSB(d)) << 1); \
    b ^= c; b = rotr64(b, 63)
#define ARGON_ROUND(v0, v1, v2, v3, v4, v5, v6, v7, \
    v8, v9, v10, v11, v12, v13, v14, v15) \
    ARGON_G(v0, v4, v8, v12); ARGON_G(v1, v5, v9, v13); \
    ARGON_G(v2, v6, v10, v14); ARGON_G(v3, v7, v11, v15); \
    ARGON_G(v0, v5, v10, v15); ARGON_G(v1, v6, v11, v12); \
    ARGON_G(v2, v7, v8, v13); ARGON_G(v3, v4, v9, v14)

/* the compression function's core: Z from R, in place */
static void g_rounds(struct blk *b)
{
    u64 *a;
    int i;

    a = b->a;
    for (i = 0; i < 128; i += 16) {
        ARGON_ROUND(a[i], a[i + 1], a[i + 2], a[i + 3],
            a[i + 4], a[i + 5], a[i + 6], a[i + 7],
            a[i + 8], a[i + 9], a[i + 10], a[i + 11],
            a[i + 12], a[i + 13], a[i + 14], a[i + 15]);
    }
    for (i = 0; i < 16; i += 2) {
        ARGON_ROUND(a[i], a[i + 1], a[i + 16], a[i + 17],
            a[i + 32], a[i + 33], a[i + 48], a[i + 49],
            a[i + 64], a[i + 65], a[i + 80], a[i + 81],
            a[i + 96], a[i + 97], a[i + 112], a[i + 113]);
    }
}

#define ARGON2_ID 2

void argon2id(u8 *hash, u32 hash_size, void *work, u32 nb_blocks,
    u32 nb_passes, const u8 *pass, u32 pass_size, const u8 *salt,
    u32 salt_size)
{
    struct blk *blocks, *current, *previous, *reference;
    struct blk tmp, index_block;
    struct blake2b ctx;
    u8 initial_hash[72], hash_area[1024];
    u32 segment_size, lane_size, pass_no, slice, pass_offset;
    u32 slice_offset, index_ctr, block, next_slice, window_start;
    u32 nb_segments, window_size;
    u64 index_seed, j1, x, y, z, ref;
    volatile u64 *p;
    int constant_time;
    long i;

    segment_size = nb_blocks / 4;
    lane_size = segment_size * 4;
    blocks = (struct blk *)work;

    blake2b_init(&ctx, 64);
    blake_update_32(&ctx, 1);              /* lanes */
    blake_update_32(&ctx, hash_size);
    blake_update_32(&ctx, nb_blocks);
    blake_update_32(&ctx, nb_passes);
    blake_update_32(&ctx, 0x13);           /* version */
    blake_update_32(&ctx, ARGON2_ID);
    blake_update_32_buf(&ctx, pass, pass_size);
    blake_update_32_buf(&ctx, salt, salt_size);
    blake_update_32_buf(&ctx, zero, 0);    /* no key */
    blake_update_32_buf(&ctx, zero, 0);    /* no extra data */
    blake2b_final(&ctx, initial_hash);

    for (i = 0; i < 2; i++) {
        store32_le(initial_hash + 64, (u32)i);
        store32_le(initial_hash + 68, 0);
        extended_hash(hash_area, 1024, initial_hash, 72);
        load64_le_buf(blocks[i].a, hash_area, 128);
    }
    crypto_wipe(initial_hash, sizeof initial_hash);
    crypto_wipe(hash_area, sizeof hash_area);

    /* Argon2id: data independent indexing for the first half of the
     * first pass, then data dependent */
    constant_time = 1;
    for (pass_no = 0; pass_no < nb_passes; pass_no++) {
        for (slice = 0; slice < 4; slice++) {
            pass_offset = pass_no == 0 && slice == 0 ? 2 : 0;
            slice_offset = slice * segment_size;
            if (slice == 2)
                constant_time = 0;

            index_ctr = 1;
            for (block = pass_offset; block < segment_size; block++) {
                current = blocks + slice_offset + block;
                if (block == 0 && slice_offset == 0)
                    previous = blocks + lane_size - 1;
                else
                    previous = blocks + slice_offset + block - 1;

                if (constant_time) {
                    if (block == pass_offset || block % 128 == 0) {
                        for (i = 0; i < 128; i++)
                            index_block.a[i] = 0;
                        index_block.a[0] = pass_no;
                        index_block.a[1] = 0;
                        index_block.a[2] = slice;
                        index_block.a[3] = lane_size;
                        index_block.a[4] = nb_passes;
                        index_block.a[5] = ARGON2_ID;
                        index_block.a[6] = index_ctr;
                        index_ctr++;
                        copy_block(&tmp, &index_block);
                        g_rounds(&index_block);
                        xor_block(&index_block, &tmp);
                        copy_block(&tmp, &index_block);
                        g_rounds(&index_block);
                        xor_block(&index_block, &tmp);
                    }
                    index_seed = index_block.a[block % 128];
                } else {
                    index_seed = previous->a[0];
                }

                next_slice = ((slice + 1) % 4) * segment_size;
                window_start = pass_no == 0 ? 0 : next_slice;
                nb_segments = pass_no == 0 ? slice : 3;
                window_size = nb_segments * segment_size + block - 1;

                j1 = index_seed & 0xffffffff;
                x = (j1 * j1) >> 32;
                y = ((u64)window_size * x) >> 32;
                z = (window_size - 1) - y;
                ref = (window_start + z) % lane_size;
                reference = blocks + (u32)ref;

                copy_block(&tmp, previous);
                xor_block(&tmp, reference);
                if (pass_no == 0)
                    copy_block(current, &tmp);
                else
                    xor_block(current, &tmp);
                g_rounds(&tmp);
                xor_block(current, &tmp);
            }
        }
    }

    crypto_wipe(&tmp, sizeof tmp);
    crypto_wipe(&index_block, sizeof index_block);
    store64_le_buf(hash_area, blocks[lane_size - 1].a, 128);
    p = (volatile u64 *)work;
    for (i = 0; i < 128L * nb_blocks; i++)
        p[i] = 0;
    extended_hash(hash, hash_size, hash_area, 1024);
    crypto_wipe(hash_area, sizeof hash_area);
}

/* ------------------------------------------------------------- AEAD */

static void lock_auth(u8 *mac, const u8 *auth_key, const u8 *cipher,
    long n)
{
    struct poly1305 ctx;
    u8 sizes[16];

    store64_le(sizes, 0);
    store64_le(sizes + 8, (u64)n);
    poly1305_init(&ctx, auth_key);
    poly1305_update(&ctx, cipher, n);
    poly1305_update(&ctx, zero, gap(n, 16));
    poly1305_update(&ctx, sizes, 16);
    poly1305_final(&ctx, mac);
}

void aead_lock(u8 *cipher, u8 *mac, const u8 *key, const u8 *nonce,
    const u8 *plain, long n)
{
    u8 sub_key[32], auth_key[64];

    chacha20_h(sub_key, key, nonce);
    chacha20_djb(auth_key, 0, 64, sub_key, nonce + 16, 0);
    chacha20_djb(cipher, plain, n, sub_key, nonce + 16, 1);
    lock_auth(mac, auth_key, cipher, n);
    crypto_wipe(sub_key, sizeof sub_key);
    crypto_wipe(auth_key, sizeof auth_key);
}

int aead_unlock(u8 *plain, const u8 *mac, const u8 *key, const u8 *nonce,
    const u8 *cipher, long n, int force)
{
    u8 sub_key[32], auth_key[64], real_mac[16];
    int mismatch;

    chacha20_h(sub_key, key, nonce);
    chacha20_djb(auth_key, 0, 64, sub_key, nonce + 16, 0);
    lock_auth(real_mac, auth_key, cipher, n);
    mismatch = crypto_verify16(mac, real_mac);
    if (!mismatch || force)
        chacha20_djb(plain, cipher, n, sub_key, nonce + 16, 1);
    crypto_wipe(sub_key, sizeof sub_key);
    crypto_wipe(auth_key, sizeof auth_key);
    crypto_wipe(real_mac, sizeof real_mac);
    return mismatch;
}
