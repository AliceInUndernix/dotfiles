/* vectors: checks crypto.c against results of libsodium (XChaCha20-
 * Poly1305), Python's hashlib (BLAKE2b) and the Argon2 reference
 * (argon2-cffi), made once and kept here. The inputs are
 * made from a counter, so nothing big is stored. make test runs it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crypto.h"

static int fails;

/* deterministic input: byte i of stream s */
static void fill(u8 *p, long n, int s)
{
    long i;
    unsigned long x;

    x = 2463534242UL + (unsigned long)s * 7919;
    for (i = 0; i < n; i++) {
        x ^= (x << 13) & 0xffffffffUL;
        x ^= x >> 17;
        x ^= (x << 5) & 0xffffffffUL;
        p[i] = (u8)(x & 0xff);
    }
}

static void hex(char *out, const u8 *p, long n)
{
    static const char digits[] = "0123456789abcdef";
    long i;

    for (i = 0; i < n; i++) {
        out[2 * i] = digits[p[i] >> 4];
        out[2 * i + 1] = digits[p[i] & 15];
    }
    out[2 * n] = 0;
}

static void expect(const char *what, const u8 *got, long n,
    const char *want)
{
    static char h[4200];

    hex(h, got, n);
    if (strcmp(h, want)) {
        printf("FAIL %s\n  got  %s\n  want %s\n", what, h, want);
        fails++;
    } else {
        printf("ok   %s\n", what);
    }
}

struct b2v { long n; long out; int s; const char *want; };
static const struct b2v b2v[] = {
    { 0, 64, 1,
    "786a02f742015903c6c6fd852552d272912f4740e15847618a86e217f71f5419"
    "d25e1031afee585313896444934eb04b903a685b1448b755d56f701afe9be2ce" },
    { 3, 64, 2,
    "e106ff5a3ee5247a2718156cb21015b634d343b9ad6cc86f6859b557058758d8"
    "62385f45f60ee3f614fb366609bd138092f2a080575b93f0dbe5c5003754a06c" },
    { 128, 64, 3,
    "f0a7a998294f4df380afeba37bfb65eecd7c2f11b34d450108a236ed4a261693"
    "fd632e3fec6fc46ce118753029565f1b6e2dbf2d8e46fc64ec8f62137bef76f0" },
    { 129, 32, 4,
    "9837928224e07cf5f24f2667b97869e1e5f2916e88b52a78c1b502a14a6fe38d" },
    { 1000, 64, 5,
    "23fd803d05bf59f0c9add2a7fb4d637dfd8fe0721e3a17cb0634eeaa44474d49"
    "525b1e043f44319b5e0c6f3507e533535905b990256d8335047571b07359685e" },
    { 256, 7, 6,
    "ba699b2f716edf" },
};

struct aev { long n; int s; const char *want; };
static const struct aev aev[] = {
    { 0, 7,
    "0e5751c026e543b2e8ab2eb06099daa1d1e5df47778f7787faab45cdf12fe3a8"
    "90f50061bfa49d582bae9c36a7e33188" },
    { 1, 8,
    "23c746ecc949815d3a1f142cf32a29b804d89274e792fcd566c659b7e4e5f3bd"
    "9011349f57ea1bdfc2a7074e369e7f49" },
    { 16, 9,
    "a08267438546cadc8f1e5323a0d7923d27fcae4537983449918d1c3af10f683b"
    "55ac976bdc1900451e438d84ee01f736" },
    { 63, 10,
    "74ddc6a91c5f66b4fafacb2f27ad81ade4dffcd307004b6ec50f72e069b946f6"
    "a645c560dcf7060e94b4c23a72f173b7" },
    { 64, 11,
    "df1fdf3e17fd2f4115f1d27c01e74d1779edc2b667b6afb4b2954ad82dd28594"
    "c1b18a762b929c62476ac441b1fca065" },
    { 65, 12,
    "c23ae85b5d06d0899f59024627f7bfc35e8a55f3abd430d147b321246a00422d"
    "1d5ee5f793f169d2ce924d09906021a2" },
    { 1000, 13,
    "047e884e84b1eb853eb13bae3d30decd459e99e571e74b3f603ea1dfd60ecaa0"
    "791d6d684bb1503881f519d66611ecda" },
    { 65536, 14,
    "651917e7805d37804ef4e488a54180979c90bd42809c38706b4d2ca91f7c8d99"
    "9cc21264c8c78e980e14bb69ceac9771" },
};

struct arv { unsigned blocks, passes, out; int s; const char *want; };
static const struct arv arv[] = {
    { 8, 1, 32, 10,
    "c517e596733535961a5fc5c1134e4718dba9b80aaaffc13fc1ccdbfc1e3b2304" },
    { 64, 3, 32, 11,
    "50b11aa2b7e2a59c51a49629934ddc90571033d8526330af84155640e83ecf29" },
    { 256, 2, 64, 12,
    "8719dd6dd6d0ab6e2e5f90025dbfb1d3f5455fcd4f8299c5d40f2f1f0388c02f"
    "d8d965c1a9235a10ea67784483cdf8d0ea82e03fa07710be3edde35dba24852e" },
    { 1000, 1, 100, 13,
    "0e298e70d62ccf0bb3fd2d46af0203097d349f1baa2b8d0d16eb53cb92d949b7"
    "46e76b12073e5322c4ca6c0fb1469323c98b965d6088194981caaf34d2a6ae16"
    "1f31d00f6e9102136e237f8d10aa741f0e91197baec3645eaff764259d7e571d"
    "3fed7356" },
    { 32768, 2, 32, 14,
    "f285f23a932f9aa07f5f7396d2b49ae23d4d4fae2ab3e025dba40e7eea88aaee" },
};

#define COUNT(a) (long)(sizeof a / sizeof a[0])

int main(void)
{
    static u8 in[65536], out[65536], back[65536];
    u8 key[32], nonce[24], mac[16], res[48];
    char what[80];
    void *work;
    long i;
    int r;

    for (i = 0; i < COUNT(b2v); i++) {
        fill(in, b2v[i].n, b2v[i].s);
        blake2b(out, b2v[i].out, in, b2v[i].n);
        sprintf(what, "BLAKE2b, %ld bytes", b2v[i].n);
        expect(what, out, b2v[i].out, b2v[i].want);
    }
    for (i = 0; i < COUNT(aev); i++) {
        fill(in, aev[i].n, aev[i].s);
        fill(key, 32, aev[i].s + 100);
        fill(nonce, 24, aev[i].s + 200);
        aead_lock(out, mac, key, nonce, in, aev[i].n);
        blake2b(res, 32, out, aev[i].n);
        memcpy(res + 32, mac, 16);
        sprintf(what, "XChaCha20-Poly1305, %ld bytes", aev[i].n);
        expect(what, res, 48, aev[i].want);
        r = aead_unlock(back, mac, key, nonce, out, aev[i].n, 0);
        if (r != 0 || memcmp(back, in, aev[i].n)) {
            printf("FAIL   and back\n");
            fails++;
        }
        mac[3] ^= 1;
        r = aead_unlock(back, mac, key, nonce, out, aev[i].n, 0);
        if (r == 0) {
            printf("FAIL   a changed mac passes\n");
            fails++;
        }
    }
    for (i = 0; i < COUNT(arv); i++) {
        fill(in, 11, arv[i].s);
        fill(back, 16, arv[i].s + 50);
        work = malloc(arv[i].blocks * 1024L);
        if (!work)
            return 1;
        argon2id(out, arv[i].out, work, arv[i].blocks, arv[i].passes,
            in, 11, back, 16);
        free(work);
        sprintf(what, "Argon2id, %u KiB, %u passes", arv[i].blocks,
            arv[i].passes);
        expect(what, out, arv[i].out, arv[i].want);
    }
    if (fails) {
        printf("%d crypto tests failed\n", fails);
        return 1;
    }
    printf("crypto: all as the references\n");
    return 0;
}
