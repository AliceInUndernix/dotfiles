/* pictures: JPEG, PNG, GIF and WebP, rewritten without what is not the
 * picture itself. Nothing is decoded, so the picture stays exactly the
 * same. */
#include <string.h>

#include "clean.h"

/* ------------------------------------------------------------ EXIF */

struct tiff {
    const unsigned char *p;
    long n;
    int le;
};

static long t16(struct tiff *t, long off)
{
    if (off < 0 || off + 2 > t->n)
        return -1;
    return t->le ? get16le(t->p + off) : get16be(t->p + off);
}

static long t32(struct tiff *t, long off)
{
    if (off < 0 || off + 4 > t->n)
        return -1;
    return t->le ? get32le(t->p + off) : get32be(t->p + off);
}

static void add_word(char *d, const char *w)
{
    int n;

    if (strstr(d, w))
        return;
    n = strlen(d);
    if (n + strlen(w) + 3 > 100)
        return;
    if (n)
        strcpy(d + n, ", ");
    strcat(d, w);
}

/* look_ifd: what an IFD holds, into d; sub IFDs one level down */
static void look_ifd(struct tiff *t, long off, char *d, int *orient,
    int depth)
{
    long count, i, e, tag;

    count = t16(t, off);
    if (count < 0 || count > 1000)
        return;
    for (i = 0; i < count; i++) {
        e = off + 2 + i * 12;
        tag = t16(t, e);
        if (tag < 0)
            return;
        switch (tag) {
        case 0x010f: case 0x0110: case 0xa433: case 0xa434:
            add_word(d, "camera");
            break;
        case 0x0132: case 0x9003: case 0x9004:
            add_word(d, "date");
            break;
        case 0x0131:
            add_word(d, "program");
            break;
        case 0x013b: case 0x8298: case 0x9c9d: case 0xa430:
            add_word(d, "author");
            break;
        case 0x010e: case 0x9286: case 0x9c9b: case 0x9c9c:
            add_word(d, "description");
            break;
        case 0xa431: case 0xa420:
            add_word(d, "serial number");
            break;
        case 0x927c:
            add_word(d, "maker notes");
            break;
        case 0x8825:
            add_word(d, "GPS position");
            break;
        case 0x0112:
            if (orient && depth == 0)
                *orient = (int)t16(t, e + 8);
            break;
        case 0x8769:
            if (depth == 0)
                look_ifd(t, t32(t, e + 8), d, 0, 1);
            break;
        }
    }
}

/* exif_note: notes what an EXIF block (from its TIFF header) holds */
void exif_note(const unsigned char *p, long n, int *orient)
{
    struct tiff t;
    char d[104];
    long ifd0, next, count;

    d[0] = 0;
    t.p = p;
    t.n = n;
    t.le = n >= 2 && p[0] == 'I';
    ifd0 = t32(&t, 4);
    if (ifd0 > 0) {
        look_ifd(&t, ifd0, d, orient, 0);
        count = t16(&t, ifd0);
        next = count < 0 ? 0 : t32(&t, ifd0 + 2 + count * 12);
        if (next > 0)
            note("thumbnail");
    }
    note2("EXIF", d);
}

/* orient_tiff: 26 bytes of EXIF that hold only the orientation, so a
 * turned photo stays turned */
static void orient_tiff(unsigned char *t, int orient)
{
    static const unsigned char tiff[26] = {
        'M', 'M', 0, 0x2a, 0, 0, 0, 8,
        0, 1, 0x01, 0x12, 0, 3, 0, 0, 0, 1, 0, 1, 0, 0,
        0, 0, 0, 0
    };

    memcpy(t, tiff, sizeof tiff);
    t[19] = (unsigned char)orient;
}

/* ------------------------------------------------------------ JPEG */

/* jpeg_keep: 1 when a segment is needed for the picture; else notes
 * what it was */
static int jpeg_keep(int m, const unsigned char *s, long n, int *orient)
{
    if (m == 0xe0) {
        if (n >= 5 && !memcmp(s, "JFIF", 5))
            return 1;
        if (n >= 5 && !memcmp(s, "JFXX", 5))
            note("thumbnail");
        else
            note("program data");
        return 0;
    }
    if (m == 0xe1) {
        if (n >= 6 && !memcmp(s, "Exif\0", 6))
            exif_note(s + 6, n - 6, orient);
        else if (n >= 28 && !memcmp(s, "http://ns.adobe.com/x", 21))
            note("XMP");
        else
            note("program data");
        return 0;
    }
    if (m == 0xe2) {
        if (n >= 12 && !memcmp(s, "ICC_PROFILE", 12))
            return 1;
        if (n >= 4 && !memcmp(s, "MPF", 4))
            note("more pictures (MPF)");
        else
            note("program data");
        return 0;
    }
    if (m == 0xee && n >= 5 && !memcmp(s, "Adobe", 5))
        return 1;
    if (m == 0xed) {
        note("IPTC/Photoshop");
        return 0;
    }
    if (m == 0xfe) {
        note("comment");
        return 0;
    }
    if (m >= 0xe0 && m <= 0xef) {
        note("program data");
        return 0;
    }
    return 1;
}

int clean_jpeg(const unsigned char *in, long n, struct buf *out)
{
    long i, len, start;
    int m, orient, orient_done;
    unsigned char block[36];

    orient = 1;
    orient_done = 0;
    buf_put(out, in, 2);
    i = 2;
    for (;;) {
        if (i >= n)
            break;
        if (in[i] != 0xff) {
            why = "a broken JPEG";
            return -1;
        }
        while (i < n && in[i] == 0xff)
            i++;
        if (i >= n)
            break;
        m = in[i];
        i++;
        if (m == 0xd9) {
            buf_byte(out, 0xff);
            buf_byte(out, 0xd9);
            if (i < n)
                note("hidden data after the picture");
            break;
        }
        if (m == 0x01 || (m >= 0xd0 && m <= 0xd7)) {
            buf_byte(out, 0xff);
            buf_byte(out, m);
            continue;
        }
        if (i + 2 > n) {
            why = "a broken JPEG";
            return -1;
        }
        len = get16be(in + i);
        if (len < 2 || i + len > n) {
            why = "a broken JPEG";
            return -1;
        }
        if (!jpeg_keep(m, in + i + 2, len - 2, &orient)) {
            i += len;
            continue;
        }
        if (m != 0xe0 && !orient_done) {
            orient_done = 1;
            if (orient > 1 && orient <= 8) {
                memcpy(block, "\377\341\000\042Exif\000\000", 10);
                orient_tiff(block + 10, orient);
                buf_put(out, block, sizeof block);
            }
        }
        buf_byte(out, 0xff);
        buf_byte(out, m);
        buf_put(out, in + i, len);
        i += len;
        if (m != 0xda)
            continue;
        /* the picture data: up to a marker that is not RSTn or FF00 */
        start = i;
        for (;;) {
            if (i + 1 >= n) {
                i = n;
                break;
            }
            if (in[i] == 0xff && in[i + 1] != 0 &&
                !(in[i + 1] >= 0xd0 && in[i + 1] <= 0xd7))
            {
                break;
            }
            i++;
        }
        buf_put(out, in + start, i - start);
    }
    return 0;
}

/* ------------------------------------------------------------ PNG */

static const char *png_keep[] = {
    "tRNS", "gAMA", "cHRM", "sRGB", "iCCP", "sBIT", "bKGD", "hIST",
    "acTL", "fcTL", "fdAT", "cICP", "mDCv", "cLLi", 0
};

/* text_word: a text chunk's keyword, printable, into d */
static void text_word(const unsigned char *s, long n, char *d)
{
    long i;

    for (i = 0; i < n && i < 40 && s[i]; i++)
        d[i] = s[i] >= 32 && s[i] < 127 ? (char)s[i] : '?';
    d[i] = 0;
}

int clean_png(const unsigned char *in, long n, struct buf *out)
{
    long i, len;
    const unsigned char *t;
    unsigned char ex[38];
    struct buf d;
    char w[44];
    int k, keep, orient;

    buf_put(out, in, 8);
    i = 8;
    for (;;) {
        if (i + 12 > n) {
            why = "a broken PNG (no end)";
            return -1;
        }
        len = get32be(in + i);
        if (len < 0 || len > n - i - 12) {
            why = "a broken PNG";
            return -1;
        }
        t = in + i + 4;
        if (is_word(t, "IEND")) {
            buf_put(out, in + i, 12 + len);
            if (i + 12 + len < n)
                note("hidden data after the picture");
            break;
        }
        if (is_word(t, "IDAT")) {
            /* the picture data as one chunk, however it was split */
            memset(&d, 0, sizeof d);
            while (i + 12 <= n && is_word(in + i + 4, "IDAT")) {
                len = get32be(in + i);
                if (len < 0 || len > n - i - 12) {
                    buf_free(&d);
                    why = "a broken PNG";
                    return -1;
                }
                buf_put(&d, in + i + 8, len);
                i += 12 + len;
            }
            put32be(ex, d.n);
            memcpy(ex + 4, "IDAT", 4);
            buf_put(out, ex, 8);
            buf_put(out, d.p, d.n);
            put32be(ex, (long)crc32_png(d.p, d.n,
                crc32_png((const unsigned char *)"IDAT", 4, 0)));
            buf_put(out, ex, 4);
            buf_free(&d);
            continue;
        }
        keep = !(t[0] & 0x20);  /* critical chunks: always */
        for (k = 0; png_keep[k] && !keep; k++) {
            if (is_word(t, png_keep[k]))
                keep = 1;
        }
        if (keep) {
            buf_put(out, in + i, 12 + len);
        } else if (is_word(t, "tEXt") || is_word(t, "zTXt") ||
            is_word(t, "iTXt"))
        {
            text_word(t + 4, len, w);
            note2("text", w);
        } else if (is_word(t, "tIME")) {
            note("date");
        } else if (is_word(t, "eXIf")) {
            orient = 1;
            exif_note(t + 4, len, &orient);
            if (orient > 1 && orient <= 8) {
                put32be(ex, 26);
                memcpy(ex + 4, "eXIf", 4);
                orient_tiff(ex + 8, orient);
                put32be(ex + 34, (long)crc32_png(ex + 4, 30, 0));
                buf_put(out, ex, 38);
            }
        } else if (is_word(t, "pHYs")) {
            note("pixel size");
        } else {
            note("program data");
        }
        i += 12 + len;
    }
    return 0;
}

/* ------------------------------------------------------------ GIF */

/* blocks_end: after the sub-blocks starting at i; -1 when cut short */
static long blocks_end(const unsigned char *in, long n, long i)
{
    while (i < n) {
        if (in[i] == 0)
            return i + 1;
        i += 1 + in[i];
    }
    return -1;
}

int clean_gif(const unsigned char *in, long n, struct buf *out)
{
    long i, end, ct;
    const unsigned char *id;

    if (n < 13) {
        why = "a broken GIF";
        return -1;
    }
    i = 13;
    if (in[10] & 0x80)
        i += 3L << ((in[10] & 7) + 1);
    if (i > n) {
        why = "a broken GIF";
        return -1;
    }
    buf_put(out, in, i);
    for (;;) {
        if (i >= n) {
            why = "a broken GIF (no end)";
            return -1;
        }
        if (in[i] == 0x3b) {
            buf_byte(out, 0x3b);
            if (i + 1 < n)
                note("hidden data after the picture");
            break;
        }
        if (in[i] == 0x2c) {
            if (i + 11 > n) {
                why = "a broken GIF";
                return -1;
            }
            ct = in[i + 9] & 0x80 ? 3L << ((in[i + 9] & 7) + 1) : 0;
            end = blocks_end(in, n, i + 10 + ct + 1);
            if (end < 0) {
                why = "a broken GIF";
                return -1;
            }
            buf_put(out, in + i, end - i);
            i = end;
            continue;
        }
        if (in[i] != 0x21 || i + 2 > n) {
            why = "a broken GIF";
            return -1;
        }
        end = blocks_end(in, n, i + 2);
        if (end < 0) {
            why = "a broken GIF";
            return -1;
        }
        id = in + i + 3;
        if (in[i + 1] == 0xfe) {
            note("comment");
        } else if (in[i + 1] != 0xff) {
            buf_put(out, in + i, end - i);
        } else if (end - i >= 14 && in[i + 2] == 11 &&
            (!memcmp(id, "NETSCAPE2.0", 11) ||
            !memcmp(id, "ANIMEXTS1.0", 11) ||
            !memcmp(id, "ICCRGBG1012", 11)))
        {
            buf_put(out, in + i, end - i);
        } else if (end - i >= 14 && !memcmp(id, "XMP DataXMP", 11)) {
            note("XMP");
        } else {
            note("program data");
        }
        i = end;
    }
    return 0;
}

/* ------------------------------------------------------------ WebP */

int clean_webp(const unsigned char *in, long n, struct buf *out)
{
    long i, size, total, vp8x;
    unsigned char ex[34];
    int orient;

    if (n < 12) {
        why = "a broken WebP";
        return -1;
    }
    buf_put(out, in, 12);
    vp8x = -1;
    orient = 1;
    i = 12;
    while (i + 8 <= n) {
        size = get32le(in + i + 4);
        if (size < 0 || size > n - i - 8) {
            why = "a broken WebP";
            return -1;
        }
        total = 8 + size + (size & 1);
        if (total > n - i)
            total = n - i;
        if (is_word(in + i, "EXIF")) {
            exif_note(in + i + 8, size, &orient);
        } else if (is_word(in + i, "XMP ")) {
            note("XMP");
        } else {
            if (is_word(in + i, "VP8X") && size >= 1)
                vp8x = out->n + 8;
            buf_put(out, in + i, total);
        }
        i += total;
    }
    if (i < n)
        note("hidden data after the picture");
    if (vp8x >= 0)
        out->p[vp8x] &= ~0x0c;  /* no EXIF, no XMP */
    if (vp8x >= 0 && orient > 1 && orient <= 8) {
        memcpy(ex, "EXIF", 4);
        put32le(ex + 4, 26);
        orient_tiff(ex + 8, orient);
        buf_put(out, ex, 34);
        out->p[vp8x] |= 0x08;
    }
    put32le(out->p + 4, out->n - 8);
    return 0;
}
