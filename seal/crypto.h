/* crypto.h: the few algorithms seal uses, taken from Monocypher 4.0.2
 * (see crypto.c): Argon2id, XChaCha20-Poly1305 and what they are built
 * of. One fixed set, nothing to choose. */

typedef unsigned char u8;
typedef unsigned int u32;           /* checked to be 32 bits in crypto.c */
typedef unsigned long long u64;

struct blake2b {
    u64 hash[8];
    u64 offset[2];
    u64 input[16];
    long idx;
    long hash_size;
};

struct poly1305 {
    u8 c[16];
    long c_idx;
    u32 r[4];
    u32 pad[4];
    u32 h[5];
};

void crypto_wipe(void *secret, long size);
int crypto_verify16(const u8 *a, const u8 *b);

void blake2b_init(struct blake2b *ctx, long hash_size);
void blake2b_update(struct blake2b *ctx, const u8 *msg, long n);
void blake2b_final(struct blake2b *ctx, u8 *hash);
void blake2b(u8 *hash, long hash_size, const u8 *msg, long n);

void chacha20_h(u8 *out, const u8 *key, const u8 *in);
u64 chacha20_djb(u8 *out, const u8 *in, long n, const u8 *key,
    const u8 *nonce, u64 ctr);

void poly1305_init(struct poly1305 *ctx, const u8 *key);
void poly1305_update(struct poly1305 *ctx, const u8 *msg, long n);
void poly1305_final(struct poly1305 *ctx, u8 *mac);

/* Argon2id, one lane, version 0x13. work: nb_blocks blocks of 1024
 * bytes, 8-byte aligned (malloc's memory is). */
void argon2id(u8 *hash, u32 hash_size, void *work, u32 nb_blocks,
    u32 nb_passes, const u8 *pass, u32 pass_size, const u8 *salt,
    u32 salt_size);

/* XChaCha20-Poly1305 without additional data (libsodium's
 * crypto_aead_xchacha20poly1305_ietf, Monocypher's crypto_aead_lock).
 * unlock: 0 when the mac is right; plain is then written. With force
 * set, plain is written even when it isn't (and -1 returned). */
void aead_lock(u8 *cipher, u8 *mac, const u8 *key, const u8 *nonce,
    const u8 *plain, long n);
int aead_unlock(u8 *plain, const u8 *mac, const u8 *key, const u8 *nonce,
    const u8 *cipher, long n, int force);
