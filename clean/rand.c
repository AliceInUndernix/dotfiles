/* random: makes each clean file differ from every other copy, so its
 * hash can't link the copies or tie one back to you. Only what the
 * format already allows to vary changes, so the content stays the
 * same: how many fill bytes a JPEG has before its markers, where PNG
 * and GIF split their picture data, the binary bytes on a PDF's second
 * line, the content of a padding block (MP4, HEIF, Matroska, FLAC,
 * WAV, Ogg, WebP), the "private" bits of MP3 frames. There is no text
 * or name of ours in it. */
#define _XOPEN_SOURCE 500
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "clean.h"

static unsigned char pool[4096];
static int pool_left;

static int rand_byte(void)
{
    int fd;
    long r;

    if (pool_left == 0) {
        fd = open("/dev/urandom", O_RDONLY);
        r = -1;
        if (fd >= 0) {
            r = read(fd, pool, sizeof pool);
            close(fd);
        }
        if (r != (long)sizeof pool) {
            fprintf(stderr, "clean: can't read /dev/urandom\n");
            exit(1);
        }
        pool_left = sizeof pool;
    }
    pool_left--;
    return pool[pool_left];
}

/* rnd: 0 .. n-1 (n at most 65536) */
static long rnd(long n)
{
    long v;
    int a, b;

    a = rand_byte();
    b = rand_byte();
    v = (long)a << 8 | b;
    return v % n;
}

void rand_bytes(unsigned char *p, long n)
{
    long i;

    for (i = 0; i < n; i++)
        p[i] = (unsigned char)rand_byte();
}

/* ------------------------------------------------------------ in memory */

/* JPEG: 0 to 31 fill bytes (0xFF) before each marker up to the picture
 * data; decoders skip them */
static int rand_jpeg(struct buf *in, struct buf *out)
{
    long i, len, k, n;

    buf_put(out, in->p, 2);
    i = 2;
    while (i + 4 <= in->n && in->p[i] == 0xff) {
        n = rnd(32);
        for (k = 0; k < n; k++)
            buf_byte(out, 0xff);
        len = get16be(in->p + i + 2);
        if (in->p[i + 1] == 0xda || len < 2 || i + 2 + len > in->n)
            break;
        buf_put(out, in->p + i, 2 + len);
        i += 2 + len;
    }
    buf_put(out, in->p + i, in->n - i);
    return 0;
}

static void png_chunk(struct buf *out, const unsigned char *d, long n)
{
    unsigned char h[8];

    put32be(h, n);
    memcpy(h + 4, "IDAT", 4);
    buf_put(out, h, 8);
    buf_put(out, d, n);
    put32be(h, (long)crc32_png(d, n, crc32_png(h + 4, 4, 0)));
    buf_put(out, h, 4);
}

/* PNG: the first IDAT chunk split in four at random places */
static int rand_png(struct buf *in, struct buf *out)
{
    long i, len, cut[5], k, j, t;

    i = 8;
    while (i + 12 <= in->n) {
        len = get32be(in->p + i);
        if (len < 0 || i + 12 + len > in->n)
            return -1;
        if (is_word(in->p + i + 4, "IDAT"))
            break;
        i += 12 + len;
    }
    if (i + 12 > in->n)
        return -1;
    buf_put(out, in->p, i);
    cut[0] = 0;
    cut[4] = len;
    for (k = 1; k < 4; k++)
        cut[k] = len > 0 ? rnd(len + 1 > 65536 ? 65536 : len + 1) : 0;
    for (k = 1; k < 4; k++) {
        for (j = k + 1; j < 4; j++) {
            if (cut[j] < cut[k]) {
                t = cut[k];
                cut[k] = cut[j];
                cut[j] = t;
            }
        }
    }
    for (k = 0; k < 4; k++)
        png_chunk(out, in->p + i + 8 + cut[k], cut[k + 1] - cut[k]);
    i += 12 + len;
    buf_put(out, in->p + i, in->n - i);
    return 0;
}

/* GIF: the first picture's data in sub-blocks of random sizes */
static int rand_gif(struct buf *in, struct buf *out)
{
    struct buf d;
    long i, ct, k, s;
    const unsigned char *p;

    p = in->p;
    i = 13;
    if (p[10] & 0x80)
        i += 3L << ((p[10] & 7) + 1);
    while (i < in->n && p[i] == 0x21) {
        i += 2;
        while (i < in->n && p[i])
            i += 1 + p[i];
        i++;
    }
    if (i + 11 > in->n || p[i] != 0x2c)
        return -1;
    ct = p[i + 9] & 0x80 ? 3L << ((p[i + 9] & 7) + 1) : 0;
    i += 10 + ct + 1;
    buf_put(out, p, i);
    memset(&d, 0, sizeof d);
    while (i < in->n && p[i]) {
        if (i + 1 + p[i] > in->n) {
            buf_free(&d);
            return -1;
        }
        buf_put(&d, p + i + 1, p[i]);
        i += 1 + p[i];
    }
    k = 0;
    while (k < d.n) {
        s = 1 + rnd(d.n - k < 255 ? d.n - k : 255);
        if (s > d.n - k)
            s = d.n - k;
        buf_byte(out, (int)s);
        buf_put(out, d.p + k, s);
        k += s;
    }
    buf_free(&d);
    buf_put(out, p + i, in->n - i);
    return 0;
}

/* PDF: the four bytes of the binary comment on the second line */
static int rand_pdf(struct buf *in, struct buf *out)
{
    long i, k;

    buf_put(out, in->p, in->n);
    for (i = 0; i < in->n && i < 16 && in->p[i] != 10; i++)
        ;
    if (i + 6 > in->n || in->p[i + 1] != '%')
        return -1;
    for (k = 0; k < 4; k++)
        out->p[i + 2 + k] = (unsigned char)(128 | rand_byte());
    return 0;
}

/* WebP: a chunk of 32 random bytes at the end, under a random name of
 * small letters (readers skip chunks they don't know; theirs are
 * capitals). A simple WebP gets the extended header that allows it. */
static int rand_webp(struct buf *in, struct buf *out)
{
    unsigned char h[18], c[40];
    const unsigned char *d;
    long i, size, total, last, w, ht;
    int k, alpha;

    i = 12;
    last = -1;
    d = 0;
    alpha = 0;
    w = 0;
    ht = 0;
    while (i + 8 <= in->n) {
        size = get32le(in->p + i + 4);
        if (size < 0 || size > in->n - i - 8)
            return -1;
        total = 8 + size + (size & 1);
        if (total > in->n - i)
            return -1;
        if (i == 12)
            d = in->p + i;
        last = i;
        i += total;
    }
    if (!d || i != in->n)
        return -1;
    if (is_word(d, "VP8 ") && get32le(d + 4) >= 10 && d[11] == 0x9d &&
        d[12] == 0x01 && d[13] == 0x2a)
    {
        w = get16le(d + 14) & 0x3fff;
        ht = get16le(d + 16) & 0x3fff;
    } else if (is_word(d, "VP8L") && get32le(d + 4) >= 5 && d[8] == 0x2f) {
        w = (get16le(d + 9) & 0x3fff) + 1;
        ht = ((get32le(d + 9) >> 14) & 0x3fff) + 1;
        alpha = (d[12] >> 4) & 1;
    } else if (!is_word(d, "VP8X")) {
        return -1;
    }
    buf_put(out, in->p, 12);
    if (!is_word(d, "VP8X")) {
        if (w < 1 || ht < 1)
            return -1;
        memset(h, 0, sizeof h);
        memcpy(h, "VP8X", 4);
        put32le(h + 4, 10);
        h[8] = alpha ? 0x10 : 0;
        w--;
        ht--;
        for (k = 0; k < 3; k++) {
            h[12 + k] = (unsigned char)(w >> (8 * k) & 0xff);
            h[15 + k] = (unsigned char)(ht >> (8 * k) & 0xff);
        }
        buf_put(out, h, 18);
    }
    if (last > 12 && get32le(in->p + last + 4) == 32 &&
        in->p[last] >= 'a' && in->p[last] <= 'z')
    {
        buf_put(out, in->p + 12, last - 12);   /* ours, from before */
    } else {
        buf_put(out, in->p + 12, in->n - 12);
    }
    for (k = 0; k < 4; k++)
        c[k] = (unsigned char)('a' + rnd(26));
    put32le(c + 4, 32);
    rand_bytes(c + 8, 32);
    buf_put(out, c, 40);
    put32le(out->p + 4, out->n - 8);
    return 0;
}

/* SVG: 32 random spaces, tabs and line ends after the drawing, where
 * XML allows any; what was there before goes first */
static int rand_svg(struct buf *in, struct buf *out)
{
    static const char white[3] = { ' ', '\t', '\n' };
    long n, k;

    n = in->n;
    while (n > 0 && (in->p[n - 1] == ' ' || in->p[n - 1] == '\t' ||
        in->p[n - 1] == '\n' || in->p[n - 1] == '\r'))
    {
        n--;
    }
    if (n == 0)
        return -1;
    buf_put(out, in->p, n);
    buf_byte(out, '\n');
    for (k = 0; k < 32; k++)
        buf_byte(out, white[rnd(3)]);
    buf_byte(out, '\n');
    return 0;
}

/* BMP: 32 random bytes after the picture (the old ones go first) */
static int rand_bmp(struct buf *in, struct buf *out)
{
    unsigned char b[32];
    long end;

    end = bmp_end(in->p, in->n);
    if (end < 0)
        return -1;
    buf_put(out, in->p, end);
    rand_bytes(b, 32);
    buf_put(out, b, 32);
    put32le(out->p + 2, out->n);
    return 0;
}

int rand_mem(int kind, struct buf *b)
{
    struct buf out;
    int r;

    memset(&out, 0, sizeof out);
    switch (kind) {
    case k_jpeg: r = rand_jpeg(b, &out); break;
    case k_png: r = rand_png(b, &out); break;
    case k_gif: r = rand_gif(b, &out); break;
    case k_pdf: r = rand_pdf(b, &out); break;
    case k_webp: r = rand_webp(b, &out); break;
    case k_svg: r = rand_svg(b, &out); break;
    case k_bmp: r = rand_bmp(b, &out); break;
    case k_zip: r = rand_zip(b->p, b->n, &out); break;
    default: r = -1; break;
    }
    if (r < 0) {
        buf_free(&out);
        return 0;
    }
    buf_free(b);
    *b = out;
    randomized = 1;
    return 1;
}

/* ------------------------------------------------------------ in place */

/* last_box: the start of the last top-level box of an MP4 or HEIF file */
static long long last_box(int fd, long long size, long long *bsize)
{
    unsigned char h[16];
    long long off, s, last;
    int r;

    off = 0;
    last = -1;
    *bsize = 0;
    while (off + 8 <= size) {
        r = at_read(fd, off, h, 16 <= size - off ? 16 : 8);
        if (r < 0)
            return -1;
        s = (long long)get32be(h) & 0xffffffffLL;
        if (s == 1 && size - off >= 16)
            s = ((long long)get32be(h + 8) & 0xffffffffLL) << 32 |
                ((long long)get32be(h + 12) & 0xffffffffLL);
        else if (s == 0)
            s = size - off;
        if (s < 8 || s > size - off)
            return -1;
        last = off;
        *bsize = s;
        off += s;
    }
    return off == size ? last : -1;
}

/* MP4, HEIF: a free box of 32 random bytes at the end */
static int rand_box(int fd, long long size)
{
    unsigned char b[40], h[8];
    long long last, bsize;
    int r;

    last = last_box(fd, size, &bsize);
    if (last < 0)
        return 0;
    rand_bytes(b + 8, 32);
    r = -1;
    if (bsize == 40)
        r = at_read(fd, last, h, 8);
    if (r == 0 && is_word(h + 4, "free")) {
        r = at_write(fd, last + 8, b + 8, 32);
    } else {
        put32be(b, 40);
        memcpy(b + 4, "free", 4);
        r = at_write(fd, size, b, 40);
    }
    return r < 0 ? -1 : 1;
}

/* Matroska: a Void element of 34 bytes at the end */
static int rand_void(int fd, long long size)
{
    unsigned char b[34];
    long long last;
    int r;

    last = mkv_last(fd, size);
    if (last < 0)
        return 0;
    r = -1;
    if (last == size - 34)
        r = at_read(fd, last, b, 2);
    rand_bytes(b + 2, 32);
    if (r == 0 && b[0] == 0xec && b[1] == 0xa0) {
        r = at_write(fd, size - 32, b + 2, 32);
    } else {
        b[0] = 0xec;
        b[1] = 0xa0;
        r = at_write(fd, size, b, 34);
    }
    return r < 0 ? -1 : 1;
}

/* FLAC: random bytes at the start of the first padding block */
static int rand_flac(int fd, long long size)
{
    unsigned char h[10], b[32];
    long long off, len;
    int r;

    off = 0;
    r = at_read(fd, 0, h, 10);
    if (r < 0)
        return 0;
    if (is_word(h, "ID3"))
        off = 10 + ((long long)(h[6] & 127) << 21 | (h[7] & 127) << 14 |
            (h[8] & 127) << 7 | (h[9] & 127)) + (h[5] & 0x10 ? 10 : 0);
    off += 4;
    for (;;) {
        if (off + 4 > size)
            return 0;
        r = at_read(fd, off, h, 4);
        if (r < 0)
            return 0;
        len = (long long)h[1] << 16 | h[2] << 8 | h[3];
        if ((h[0] & 0x7f) == 1 && len >= 32) {
            rand_bytes(b, 32);
            r = at_write(fd, off + 4, b, 32);
            return r < 0 ? -1 : 1;
        }
        if (h[0] & 0x80)
            return 0;
        off += 4 + len;
    }
}

/* WAV, AVI: a JUNK chunk of random bytes at the end (one RIFF only) */
static int rand_riff(int fd, long long size)
{
    unsigned char h[8], b[40];
    long long off, len, last, llen, riff;
    int r;

    r = at_read(fd, 4, h, 4);
    if (r < 0)
        return 0;
    riff = (long long)get32le(h) & 0xffffffffLL;
    if (8 + riff != size)
        return 0;
    off = 12;
    last = -1;
    llen = 0;
    while (off + 8 <= size) {
        r = at_read(fd, off, h, 8);
        if (r < 0)
            return 0;
        len = (long long)get32le(h + 4) & 0xffffffffLL;
        last = off;
        llen = len;
        off += 8 + len + (len & 1);
    }
    if (off != size)
        return 0;
    rand_bytes(b + 8, 32);
    r = -1;
    if (llen == 32)
        r = at_read(fd, last, h, 4);
    if (r == 0 && is_word(h, "JUNK")) {
        r = at_write(fd, last + 8, b + 8, 32);
        return r < 0 ? -1 : 1;
    }
    memcpy(b, "JUNK", 4);
    put32le(b + 4, 32);
    r = at_write(fd, size, b, 40);
    if (r < 0)
        return -1;
    put32le(h, (long)(riff + 40));
    r = at_write(fd, 4, h, 4);
    return r < 0 ? -1 : 1;
}

/* MP3: the "private" bit of each frame header, which decoders don't
 * read, set at random in the first frames. Not the first frame (it
 * may be the encoder's Info frame, which has a checksum over its
 * header), nor frames whose header a CRC covers. */
static int rand_mp3(int fd, long long size)
{
    unsigned char h[10];
    long long off;
    long len;
    int n, done, r, adts, mask;

    off = 0;
    r = at_read(fd, 0, h, 10);
    if (r < 0)
        return 0;
    if (is_word(h, "ID3"))
        off = 10 + ((long long)(h[6] & 127) << 21 | (h[7] & 127) << 14 |
            (h[8] & 127) << 7 | (h[9] & 127)) + (h[5] & 0x10 ? 10 : 0);
    done = 0;
    for (n = 0; n < 1024 && off + 4 <= size; n++) {
        r = at_read(fd, off, h, 4);
        if (r < 0)
            return 0;
        adts = h[0] == 0xff && (h[1] & 0xf6) == 0xf0;
        if (adts) {
            r = at_read(fd, off, h, 6);
            if (r < 0)
                return 0;
            len = (long)(h[3] & 3) << 11 | (long)h[4] << 3 | h[5] >> 5;
        } else {
            len = mp3_frame(h);
        }
        if (len < 7 || off + len > size)
            break;
        /* AAC (ADTS) has a private bit too, 0x02; a CRC covers it when
         * the "no CRC" bit is not set */
        if (n > 0 && (h[1] & 1)) {
            mask = adts ? 2 : 1;
            h[2] = (unsigned char)((h[2] & ~mask) | (rand_byte() & mask));
            r = at_write(fd, off + 2, h + 2, 1);
            if (r < 0)
                return -1;
            done++;
        }
        off += len;
    }
    return done >= 16 ? 1 : 0;   /* fewer: too likely the same */
}

/* rand_place: 1 done, 0 no room in this file, -1 error */
int rand_place(int kind, int fd, long long size)
{
    int r;

    switch (kind) {
    case k_mp4: case k_heif: r = rand_box(fd, size); break;
    case k_mkv: r = rand_void(fd, size); break;
    case k_flac: r = rand_flac(fd, size); break;
    case k_riff: r = rand_riff(fd, size); break;
    case k_ogg: r = rand_ogg(fd, size); break;
    case k_mp3: r = rand_mp3(fd, size); break;
    case k_aiff: r = rand_aiff(fd, size); break;
    case k_asf: r = rand_asf(fd, size); break;
    case k_tiff: r = rand_tiff(fd, size); break;
    default: r = 0; break;
    }
    if (r == 1)
        randomized = 1;
    return r;
}
