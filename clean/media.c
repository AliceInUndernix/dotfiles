/* video and audio: MP4/MOV, MKV/WebM, MP3, FLAC, WAV/AVI. These can be
 * big, so they are changed where they are: what is metadata becomes
 * padding of the same size (MP4 "free", Matroska "Void", FLAC padding,
 * RIFF "JUNK") or zeros, and nothing else moves. */
#define _XOPEN_SOURCE 500
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "clean.h"

static void add_word(char *d, int size, const char *w)
{
    int n;

    if (strstr(d, w))
        return;
    n = strlen(d);
    if (n + (int)strlen(w) + 3 > size)
        return;
    if (n)
        strcpy(d + n, ", ");
    strcat(d, w);
}

/* any_set: 1 when any of the n bytes at off is not 0 */
static int any_set(int fd, long long off, long long n)
{
    unsigned char b[4096];
    long k, i;

    while (n > 0) {
        k = n > (long long)sizeof b ? (long)sizeof b : (long)n;
        if (at_read(fd, off, b, k) < 0)
            return 1;
        for (i = 0; i < k; i++) {
            if (b[i])
                return 1;
        }
        off += k;
        n -= k;
    }
    return 0;
}

/* has_text: 1 when the first 64 KB at off hold w */
static int has_text(int fd, long long off, long long n, const char *w)
{
    static unsigned char b[65536];
    long k, i, wl;

    wl = strlen(w);
    k = n > (long long)sizeof b ? (long)sizeof b : (long)n;
    if (k < wl || at_read(fd, off, b, k) < 0)
        return 0;
    for (i = 0; i + wl <= k; i++) {
        if (!memcmp(b + i, w, wl))
            return 1;
    }
    return 0;
}

/* encoder_text: x264, x265 and FFmpeg (Lavc) put their name, version
 * and settings as text into the first picture; it becomes spaces (the
 * video stays valid: the text is free-form) */
static int encoder_text(int fd, long long size)
{
    static unsigned char b[65536];
    static const char *names[] = {
        "x264 - core ", "x265 (build ", "Lavc", "Lavf", 0
    };
    long long off;
    long k, i, j, e, wl;
    int w, found;

    found = 0;
    for (off = 0; off < size && off < 8LL * 1024 * 1024; off += 65000) {
        k = size - off > (long long)sizeof b ? (long)sizeof b
            : (long)(size - off);
        if (at_read(fd, off, b, k) < 0)
            return 0;
        for (i = 0; i + 12 <= k; i++) {
            for (w = 0; names[w]; w++) {
                wl = strlen(names[w]);
                if (i + wl >= k || memcmp(b + i, names[w], wl))
                    continue;
                if (wl == 4 && (b[i + 4] < '0' || b[i + 4] > '9'))
                    continue;
                e = i;
                while (e < k && b[e] >= 32 && b[e] < 127) {
                    /* a short name is only name and version: binary
                     * data may follow it (LAME's tag) */
                    if (wl == 4 && (e - i >= 16 ||
                        !((b[e] >= '0' && b[e] <= '9') || b[e] == '.' ||
                        (b[e] >= 'A' && b[e] <= 'Z') ||
                        (b[e] >= 'a' && b[e] <= 'z'))))
                    {
                        break;
                    }
                    e++;
                }
                if (e == k)
                    continue;   /* it goes on in the next piece */
                note("encoder settings");
                for (j = i; j < e; j++)
                    b[j] = ' ';
                if (at_write(fd, off + i, b + i, e - i) < 0)
                    return -1;
                found++;
            }
        }
        if (found >= 4)
            break;
    }
    return 0;
}

/* ------------------------------------------------------------ MP4 */

static const unsigned char xmp_uuid[16] = {
    0xbe, 0x7a, 0xcf, 0xcb, 0x97, 0xa9, 0x42, 0xe8,
    0x9c, 0x71, 0x99, 0x94, 0x91, 0xe3, 0xaf, 0xac
};

/* udta_words: what a user-data box holds, from its boxes' names */
static void udta_words(int fd, long long off, long long end, char *d)
{
    unsigned char h[8];
    long long size;
    int n;

    n = 0;
    while (off + 8 <= end && n < 200) {
        if (at_read(fd, off, h, 8) < 0)
            return;
        size = (long long)get32be(h) & 0xffffffffLL;
        if (size < 8 || off + size > end)
            return;
        if (is_word(h + 4, "\251xyz") || is_word(h + 4, "loci"))
            add_word(d, 100, "GPS position");
        else if (is_word(h + 4, "\251day"))
            add_word(d, 100, "date");
        else if (is_word(h + 4, "\251mak") || is_word(h + 4, "\251mod"))
            add_word(d, 100, "camera");
        else if (is_word(h + 4, "\251swr") || is_word(h + 4, "\251too"))
            add_word(d, 100, "program");
        else if (is_word(h + 4, "meta"))
            add_word(d, 100, "tags");
        off += size;
        n++;
    }
}

/* mp4_free: a box becomes "free" with only zeros in it */
static int mp4_free(int fd, long long off, long long hdr, long long size)
{
    if (at_write(fd, off + 4, (const unsigned char *)"free", 4) < 0)
        return -1;
    return at_zero(fd, off + hdr, size - hdr);
}

/* mp4_program: the encoder's name in a video sample description */
static int mp4_program(int fd, long long off, long long hdr,
    long long size)
{
    static const char *video[] = {
        "avc1", "avc3", "hvc1", "hev1", "mp4v", "vp09", "av01", "s263",
        "jpeg", "mjpa", "apcn", "apch", "apcs", "apco", "ap4h", 0
    };
    unsigned char h[8];
    long long e, esize, end;
    long count;
    int i;

    end = off + size;
    if (at_read(fd, off + hdr + 4, h, 4) < 0)
        return 0;
    count = get32be(h);
    e = off + hdr + 8;
    while (count > 0 && e + 8 <= end) {
        if (at_read(fd, e, h, 8) < 0)
            return 0;
        esize = (long long)get32be(h) & 0xffffffffLL;
        if (esize < 8 || e + esize > end)
            return 0;
        for (i = 0; video[i]; i++) {
            if (is_word(h + 4, video[i]) && esize >= 82 &&
                any_set(fd, e + 50, 32))
            {
                note("program");
                if (at_zero(fd, e + 50, 32) < 0)
                    return -1;
            }
        }
        e += esize;
        count--;
    }
    return 0;
}

/* mp4_dates: the creation and change dates of mvhd, tkhd, mdhd */
static int mp4_dates(int fd, long long off, long long hdr, long long size)
{
    unsigned char v;
    long long n;

    if (size < hdr + 4 || at_read(fd, off + hdr, &v, 1) < 0)
        return 0;
    n = v == 1 ? 16 : 8;
    if (size < hdr + 4 + n || !any_set(fd, off + hdr + 4, n))
        return 0;
    note("dates");
    return at_zero(fd, off + hdr + 4, n);
}

static int mp4_walk(int fd, long long off, long long end, int depth)
{
    unsigned char h[24];
    long long size, hdr;
    char d[104];
    const unsigned char *t;
    int r;

    while (off + 8 <= end) {
        if (at_read(fd, off, h, 8) < 0) {
            why = "a broken MP4 (cut short)";
            return -1;
        }
        size = (long long)get32be(h) & 0xffffffffLL;
        hdr = 8;
        if (size == 1) {
            if (off + 16 > end || at_read(fd, off + 8, h + 8, 8) < 0) {
                why = "a broken MP4";
                return -1;
            }
            size = ((long long)get32be(h + 8) & 0xffffffffLL) << 32 |
                ((long long)get32be(h + 12) & 0xffffffffLL);
            hdr = 16;
        } else if (size == 0) {
            size = end - off;
        }
        if (size < hdr) {
            why = "a broken MP4";
            return -1;
        }
        if (size > end - off) {
            if (depth > 0) {
                why = "a broken MP4";
                return -1;
            }
            size = end - off;   /* a recording cut short */
        }
        t = h + 4;
        r = 0;
        if (is_word(t, "moov") || is_word(t, "trak") ||
            is_word(t, "mdia") || is_word(t, "minf") ||
            is_word(t, "stbl"))
        {
            r = mp4_walk(fd, off + hdr, off + size, depth + 1);
        } else if (is_word(t, "udta")) {
            d[0] = 0;
            udta_words(fd, off + hdr, off + size, d);
            note2("user data", d);
            r = mp4_free(fd, off, hdr, size);
        } else if (is_word(t, "meta") && depth > 0) {
            d[0] = 0;
            if (has_text(fd, off + hdr, size - hdr, "location"))
                add_word(d, 100, "GPS position");
            note2("tags", d);
            r = mp4_free(fd, off, hdr, size);
        } else if (is_word(t, "uuid") && depth == 0 && size >= hdr + 16) {
            if (at_read(fd, off + hdr, h + 8, 16) == 0 &&
                !memcmp(h + 8, xmp_uuid, 16))
            {
                note("XMP");
                r = mp4_free(fd, off, hdr, size);
            }
        } else if (is_word(t, "mvhd") || is_word(t, "tkhd") ||
            is_word(t, "mdhd"))
        {
            r = mp4_dates(fd, off, hdr, size);
        } else if (is_word(t, "stsd")) {
            r = mp4_program(fd, off, hdr, size);
        }
        if (r < 0)
            return -1;
        off += size;
    }
    return 0;
}

int clean_mp4(int fd, long long size)
{
    if (mp4_walk(fd, 0, size, 0) < 0)
        return -1;
    return encoder_text(fd, size);
}

/* ------------------------------------------------------------ Matroska */

#define ID_EBML 0x1a45dfa3L
#define ID_SEGMENT 0x18538067L
#define ID_SEEKHEAD 0x114d9b74L
#define ID_SEEK 0x4dbbL
#define ID_SEEKID 0x53abL
#define ID_INFO 0x1549a966L
#define ID_TAGS 0x1254c367L
#define ID_ATTACH 0x1941a469L
#define ID_CLUSTER 0x1f43b675L
#define ID_DATEUTC 0x4461L
#define ID_TITLE 0x7ba9L
#define ID_MUXAPP 0x4d80L
#define ID_WRITEAPP 0x5741L

struct elem {
    long id;
    int idlen;
    int szlen;
    long long size;     /* -1: unknown */
};

/* ebml_head: the element header at off */
static int ebml_head(int fd, long long off, long long end, struct elem *e)
{
    unsigned char b[12];
    long k;
    int i, mask;

    k = end - off < 12 ? (long)(end - off) : 12;
    if (k < 2 || at_read(fd, off, b, k) < 0)
        return -1;
    e->idlen = 1;
    mask = 0x80;
    while (e->idlen <= 4 && !(b[0] & mask)) {
        e->idlen++;
        mask >>= 1;
    }
    if (e->idlen > 4 || e->idlen >= k)
        return -1;
    e->id = 0;
    for (i = 0; i < e->idlen; i++)
        e->id = e->id << 8 | b[i];
    e->szlen = 1;
    mask = 0x80;
    while (e->szlen <= 8 && !(b[e->idlen] & mask)) {
        e->szlen++;
        mask >>= 1;
    }
    if (e->szlen > 8 || e->idlen + e->szlen > k)
        return -1;
    e->size = b[e->idlen] & (mask - 1);
    for (i = 1; i < e->szlen; i++)
        e->size = e->size << 8 | b[e->idlen + i];
    if (e->size == (1LL << (7 * e->szlen)) - 1)
        e->size = -1;
    return 0;
}

/* ebml_void: the element becomes a Void element of the same length */
static int ebml_void(int fd, long long off, struct elem *e)
{
    unsigned char h[9];
    long long v;
    int k, i;

    k = e->idlen + e->szlen - 1;
    if (k > 8)
        k = 8;
    v = e->size + (e->idlen + e->szlen - 1 - k);
    h[0] = 0xec;
    v |= 1LL << (7 * k);
    for (i = k; i >= 1; i--) {
        h[i] = (unsigned char)v;
        v >>= 8;
    }
    if (at_write(fd, off, h, 1 + k) < 0)
        return -1;
    return at_zero(fd, off + 1 + k,
        e->size + (e->idlen + e->szlen - 1 - k));
}

/* mkv_children: walk the elements in off..end, doing what each wants */
static int mkv_children(int fd, long long off, long long end, int level)
{
    struct elem e;
    unsigned char id[4];
    long long data;
    int r;

    while (off < end) {
        if (ebml_head(fd, off, end, &e) < 0) {
            why = "a broken Matroska file";
            return -1;
        }
        data = off + e.idlen + e.szlen;
        if (e.size < 0) {
            if (e.id == ID_SEGMENT && level == 0) {
                return mkv_children(fd, data, end, 1);
            }
            return 0;   /* a live stream's cluster: nothing more to see */
        }
        if (e.size > end - data) {
            if (e.id == ID_CLUSTER || level == 0)
                return 0;
            why = "a broken Matroska file";
            return -1;
        }
        r = 0;
        if (level == 0 && e.id == ID_SEGMENT) {
            r = mkv_children(fd, data, data + e.size, 1);
        } else if (level == 1 && e.id == ID_INFO) {
            r = mkv_children(fd, data, data + e.size, 2);
        } else if (level == 1 && e.id == ID_SEEKHEAD) {
            r = mkv_children(fd, data, data + e.size, 3);
        } else if (level == 1 && e.id == ID_TAGS) {
            note("tags");
            r = ebml_void(fd, off, &e);
        } else if (level == 1 && e.id == ID_ATTACH) {
            note("attachments (cover pictures, fonts)");
            r = ebml_void(fd, off, &e);
        } else if (level == 2 && e.id == ID_TITLE) {
            note("title");
            r = ebml_void(fd, off, &e);
        } else if (level == 2 && (e.id == ID_DATEUTC ||
            e.id == ID_MUXAPP || e.id == ID_WRITEAPP))
        {
            if (any_set(fd, data, e.size)) {
                note(e.id == ID_DATEUTC ? "date" : "program");
                r = at_zero(fd, data, e.size);
            }
        } else if (level == 3 && e.id == ID_SEEK) {
            r = mkv_children(fd, data, data + e.size, 4);
            if (r == 1)
                r = ebml_void(fd, off, &e);
        } else if (level == 4 && e.id == ID_SEEKID && e.size == 4 &&
            at_read(fd, data, id, 4) == 0)
        {
            /* a pointer to the tags or attachments goes with them */
            if (get32be(id) == ID_TAGS || get32be(id) == ID_ATTACH)
                return 1;
        }
        if (r < 0)
            return -1;
        off = data + e.size;
    }
    return 0;
}

int clean_mkv(int fd, long long size)
{
    int r;

    r = mkv_children(fd, 0, size, 0);
    if (r < 0)
        return -1;
    return encoder_text(fd, size);
}

/* ------------------------------------------------------------ ID3, MP3 */

/* id3_words: the kinds of ID3v2.3/2.4 frames in the tag */
static void id3_words(int fd, long long off, long long end, int v4,
    char *d)
{
    unsigned char h[10];
    long long size;
    int n;

    n = 0;
    while (off + 10 <= end && n < 300) {
        if (at_read(fd, off, h, 10) < 0 || h[0] == 0)
            return;
        if (v4)
            size = (long long)(h[4] & 127) << 21 | (h[5] & 127) << 14 |
                (h[6] & 127) << 7 | (h[7] & 127);
        else
            size = (long long)get32be(h + 4) & 0xffffffffLL;
        if (is_word(h, "APIC"))
            add_word(d, 100, "pictures");
        else if (is_word(h, "COMM") || is_word(h, "USLT"))
            add_word(d, 100, "comments");
        else if (is_word(h, "TENC") || is_word(h, "TSSE"))
            add_word(d, 100, "program");
        else if (is_word(h, "PRIV") || is_word(h, "GEOB") ||
            is_word(h, "UFID"))
        {
            add_word(d, 100, "private data");
        } else if (h[0] == 'T') {
            add_word(d, 100, "text");
        }
        off += 10 + size;
        n++;
    }
}

/* id3_clean: an ID3v2 tag at off keeps its size, with only padding */
static int id3_clean(int fd, long long off, long long fsize,
    long long *total)
{
    unsigned char h[10];
    long long body;
    char d[104];
    unsigned char flags;

    if (off + 10 > fsize || at_read(fd, off, h, 10) < 0 ||
        !is_word(h, "ID3"))
    {
        *total = 0;
        return 0;
    }
    body = (long long)(h[6] & 127) << 21 | (h[7] & 127) << 14 |
        (h[8] & 127) << 7 | (h[9] & 127);
    *total = 10 + body + (h[5] & 0x10 ? 10 : 0);
    if (off + *total > fsize) {
        why = "a broken ID3 tag";
        return -1;
    }
    if (!any_set(fd, off + 10, body > 16 ? 16 : body) && !(h[5] & 0x40))
        return 0;   /* only padding already */
    d[0] = 0;
    if (h[3] == 3 || h[3] == 4)
        id3_words(fd, off + 10, off + 10 + body, h[3] == 4, d);
    note2("ID3 tags", d);
    flags = h[5] & 0x10;
    if (at_write(fd, off + 5, &flags, 1) < 0)
        return -1;
    if ((h[5] & 0x10) && at_write(fd, off + 10 + body + 5, &flags, 1) < 0)
        return -1;
    return at_zero(fd, off + 10, body);
}

/* mp3_frame: the length of the MPEG audio frame with header h, 0 when
 * it is not one */
long mp3_frame(const unsigned char *h)
{
    static const int rate1[3][16] = {
        { 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384,
            416, 448, 0 },
        { 0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320,
            384, 0 },
        { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256,
            320, 0 }
    };
    static const int rate2[2][16] = {
        { 0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224,
            256, 0 },
        { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160,
            0 }
    };
    static const long freq[3] = { 44100, 48000, 32000 };
    int ver, layer, br, sr, pad;
    long rate, f;

    if (h[0] != 0xff || (h[1] & 0xe0) != 0xe0)
        return 0;
    ver = (h[1] >> 3) & 3;      /* 3 MPEG-1, 2 MPEG-2, 0 MPEG-2.5 */
    layer = 4 - ((h[1] >> 1) & 3);
    br = h[2] >> 4;
    sr = (h[2] >> 2) & 3;
    pad = (h[2] >> 1) & 1;
    if (ver == 1 || layer == 4 || br == 0 || br == 15 || sr == 3)
        return 0;
    if (ver == 3)
        rate = rate1[layer - 1][br];
    else
        rate = rate2[layer == 1 ? 0 : 1][br];
    f = freq[sr];
    if (ver == 2)
        f /= 2;
    else if (ver == 0)
        f /= 4;
    rate *= 1000;
    if (layer == 1)
        return (12 * rate / f + pad) * 4;
    if (layer == 3 && ver != 3)
        return 72 * rate / f + pad;
    return 144 * rate / f + pad;
}

/* lame_crc: the checksum of LAME's tag (CRC-16, reflected) */
static long lame_crc(const unsigned char *p, long n)
{
    long c, i;
    int k;

    c = 0;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++)
            c = c & 1 ? (c >> 1) ^ 0xa001 : c >> 1;
    }
    return c;
}

/* lame_tag: where the encoder's tag in the first frame (its Info or
 * Xing frame) is, -1 when there is none; ok: its checksum was right */
static long long lame_tag(int fd, long long off, long long size,
    unsigned char *f, int *ok)
{
    static const int side[4] = { 13, 21, 36, 0 };
    long len, flags, t;
    int i, r;

    *ok = 0;
    if (off + 4 > size)
        return -1;
    r = at_read(fd, off, f, 4);
    if (r < 0)
        return -1;
    len = mp3_frame(f);
    if (len < 40 || len > 2048 || off + len > size)
        return -1;
    r = at_read(fd, off, f, len);
    if (r < 0)
        return -1;
    for (i = 0; side[i]; i++) {
        if (is_word(f + side[i], "Info") || is_word(f + side[i], "Xing"))
            break;
    }
    if (!side[i])
        return -1;
    flags = get32be(f + side[i] + 4);
    t = side[i] + 8 + (flags & 1 ? 4 : 0) + (flags & 2 ? 4 : 0) +
        (flags & 4 ? 100 : 0) + (flags & 8 ? 4 : 0);
    if (t + 36 > len)
        return -1;
    *ok = lame_crc(f, t + 34) == get16be(f + t + 34);
    return t;
}

int clean_mp3(int fd, long long size)
{
    static unsigned char f[2048];
    unsigned char t[3];
    long long total, tag;
    long c;
    int r, ok;

    r = id3_clean(fd, 0, size, &total);
    if (r < 0)
        return -1;
    if (size - total >= 128 && at_read(fd, size - 128, t, 3) == 0 &&
        is_word(t, "TAG"))
    {
        note("ID3v1 tags");
        if (!dry_run && ftruncate(fd, (off_t)(size - 128)) < 0) {
            why = "could not shorten it";
            return -1;
        }
        size -= 128;
    }
    /* the encoder's name and version in its tag (in the first frame)
     * become "LAME": players read the gap at each end from the tag
     * only when it starts with a name they know. (LAME's name in the
     * other frames stays: those bytes are read as sound.) */
    tag = lame_tag(fd, total, size, f, &ok);
    if (tag < 0 || !memcmp(f + tag, "LAME     ", 9))
        return 0;
    note("encoder settings");
    if (dry_run)
        return 0;
    memcpy(f + tag, "LAME     ", 9);
    if (ok) {
        c = lame_crc(f, (long)tag + 34);
        f[tag + 34] = (unsigned char)(c >> 8);
        f[tag + 35] = (unsigned char)(c & 0xff);
    }
    r = at_write(fd, total + tag, f + tag, 9);
    if (r == 0 && ok)
        r = at_write(fd, total + tag + 34, f + tag + 34, 2);
    return r;
}

/* ------------------------------------------------------------ FLAC */

/* vorbis_words: the names in a Vorbis comment block (NAME=value) */
static void vorbis_words(int fd, long long off, long long len, char *d)
{
    static unsigned char b[65536];
    char w[24];
    long k, i, n, count, c, j, s;

    k = len > (long long)sizeof b ? (long)sizeof b : (long)len;
    if (k < 8 || at_read(fd, off, b, k) < 0)
        return;
    i = 4 + get32le(b);     /* past the encoder's name */
    if (i < 4 || i + 4 > k)
        return;
    if (i > 4)
        add_word(d, 100, "program");
    count = get32le(b + i);
    i += 4;
    for (c = 0; c < count && c < 100 && i + 4 <= k; c++) {
        n = get32le(b + i);
        i += 4;
        if (n < 0 || i + n > k)
            return;
        s = 0;
        for (j = 0; j < n && b[i + j] != '=' && s < 20; j++) {
            if (b[i + j] >= 32 && b[i + j] < 127) {
                w[s] = (char)b[i + j];
                s++;
            }
        }
        w[s] = 0;
        if (s)
            add_word(d, 100, w);
        i += n;
    }
}

int clean_flac(int fd, long long size)
{
    unsigned char h[4];
    long long off, len;
    char d[104];
    int type, r;

    r = id3_clean(fd, 0, size, &off);
    if (r < 0)
        return -1;
    if (at_read(fd, off, h, 4) < 0 || !is_word(h, "fLaC")) {
        why = "a broken FLAC file";
        return -1;
    }
    off += 4;
    for (;;) {
        if (off + 4 > size || at_read(fd, off, h, 4) < 0) {
            why = "a broken FLAC file";
            return -1;
        }
        type = h[0] & 0x7f;
        len = (long long)h[1] << 16 | h[2] << 8 | h[3];
        if (off + 4 + len > size) {
            why = "a broken FLAC file";
            return -1;
        }
        if (type == 2 || type == 4 || type == 6) {
            if (type == 2) {
                note("program data");
            } else if (type == 6) {
                note("pictures");
            } else {
                d[0] = 0;
                vorbis_words(fd, off + 4, len, d);
                note2("tags", d);
            }
            h[0] = (unsigned char)((h[0] & 0x80) | 1);  /* padding */
            if (at_write(fd, off, h, 1) < 0 ||
                at_zero(fd, off + 4, len) < 0)
            {
                return -1;
            }
        }
        if (h[0] & 0x80)
            break;
        off += 4 + len;
    }
    return 0;
}

/* ------------------------------------------------------------ WAV, AVI */

int clean_riff(int fd, long long size)
{
    unsigned char h[12];
    long long off, end, len;
    const char *what;

    if (at_read(fd, 4, h, 4) < 0)
        return -1;
    end = 8 + ((long long)get32le(h) & 0xffffffffLL);
    if (end > size)
        end = size;
    off = 12;
    while (off + 8 <= end) {
        if (at_read(fd, off, h, off + 12 <= end ? 12 : 8) < 0) {
            why = "a broken RIFF file";
            return -1;
        }
        len = (long long)get32le(h + 4) & 0xffffffffLL;
        what = 0;
        if (is_word(h, "LIST") && len >= 4 && is_word(h + 8, "INFO"))
            what = "tags";
        else if (is_word(h, "id3 ") || is_word(h, "ID3 "))
            what = "ID3 tags";
        else if (is_word(h, "bext"))
            what = "recorder data (program, date)";
        else if (is_word(h, "iXML") || is_word(h, "_PMX"))
            what = "XMP";
        if (what) {
            if (off + 8 + len > end) {
                why = "a broken RIFF file";
                return -1;
            }
            note(what);
            if (at_write(fd, off, (const unsigned char *)"JUNK", 4) < 0 ||
                at_zero(fd, off + 8, len) < 0)
            {
                return -1;
            }
        }
        off += 8 + len + (len & 1);
    }
    return encoder_text(fd, size);
}

/* ------------------------------------------------------------ HEIF */

/* HEIF and AVIF pictures (iPhone photos): EXIF and XMP are items, like
 * the picture itself; the "meta" box says where each item's bytes are
 * (iloc). Those bytes become zeros (EXIF) or spaces (XMP); the picture's
 * rotation is not in EXIF here (irot), so nothing turns. */

#define MAX_ITEMS 32

struct item {
    long id;
    int xmp;
};

static long long uval(const unsigned char *p, int n)
{
    long long v;
    int i;

    v = 0;
    for (i = 0; i < n; i++)
        v = v << 8 | p[i];
    return v;
}

/* heif_items: the EXIF and XMP items in iinf (at p, n bytes) */
static int heif_items(const unsigned char *p, long n, struct item *it)
{
    long pos, size, e;
    int count, v;
    const unsigned char *t;

    count = 0;
    if (n < 6)
        return 0;
    pos = p[0] == 0 ? 6 : 8;    /* version, flags, entry count */
    while (pos + 8 <= n) {
        size = get32be(p + pos);
        if (size < 8 || size > n - pos)
            break;
        if (is_word(p + pos + 4, "infe") && size >= 20) {
            v = p[pos + 8];
            e = pos + 12;
            if (v == 2 || v == 3) {
                it[count].id = (long)uval(p + e, v == 2 ? 2 : 4);
                e += (v == 2 ? 2 : 4) + 2;
                t = p + e;
                it[count].xmp = -1;
                if (e + 4 <= pos + size && is_word(t, "Exif")) {
                    it[count].xmp = 0;
                } else if (e + 4 <= pos + size && is_word(t, "mime")) {
                    e += 4;
                    while (e < pos + size && p[e])
                        e++;    /* the item's name */
                    e++;
                    if (pos + size - e >= 19 &&
                        !memcmp(p + e, "application/rdf+xml", 19))
                    {
                        it[count].xmp = 1;
                    }
                }
                if (it[count].xmp >= 0 && count < MAX_ITEMS - 1)
                    count++;
            }
        }
        pos += size;
    }
    return count;
}

/* an EXIF item with nothing in it: where the TIFF header starts (0),
 * the header, and an empty list of tags; strict readers want one */
static const unsigned char empty_exif[18] = {
    0, 0, 0, 0, 'M', 'M', 0, 0x2a, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0
};

/* heif_blank: blanks the bytes of one item, as iloc (at p) says */
static int heif_blank(int fd, long long fsize, const unsigned char *p,
    long n, struct item *it, long long idat)
{
    static unsigned char b[65536];
    int v, osz, lsz, bsz, isz, method;
    long pos, count, i, ext, k, j;
    long long id, base, eoff, elen, off;
    int first;

    if (n < 8)
        return 0;
    v = p[0];
    osz = p[4] >> 4;
    lsz = p[4] & 15;
    bsz = p[5] >> 4;
    isz = v >= 1 ? p[5] & 15 : 0;
    pos = 6;
    count = (long)uval(p + pos, v < 2 ? 2 : 4);
    pos += v < 2 ? 2 : 4;
    for (i = 0; i < count && pos < n; i++) {
        if (pos + 4 + 2 + 2 + bsz + 2 > n)
            return 0;
        id = uval(p + pos, v < 2 ? 2 : 4);
        pos += v < 2 ? 2 : 4;
        method = 0;
        if (v >= 1) {
            method = p[pos + 1] & 15;
            pos += 2;
        }
        pos += 2;   /* data reference */
        base = uval(p + pos, bsz);
        pos += bsz;
        ext = (long)uval(p + pos, 2);
        pos += 2;
        first = 1;
        for (k = 0; k < ext; k++) {
            if (pos + isz + osz + lsz > n)
                return 0;
            pos += isz;
            eoff = uval(p + pos, osz);
            pos += osz;
            elen = uval(p + pos, lsz);
            pos += lsz;
            if (id != it->id || method == 2 || elen <= 0)
                continue;
            off = base + eoff + (method == 1 ? idat : 0);
            if (off < 0 || off + elen > fsize) {
                why = "a broken HEIF picture";
                return -1;
            }
            if (it->xmp) {
                if (!has_text(fd, off, elen, "<"))
                    continue;
                note("XMP");
                if (at_fill(fd, off, elen, ' ') < 0)
                    return -1;
                continue;
            }
            j = elen > (long long)sizeof b ? (long)sizeof b : (long)elen;
            if (at_read(fd, off, b, j) < 0)
                return -1;
            if (first && elen >= 18 && !memcmp(b, empty_exif, 18) &&
                !any_set(fd, off + 18, elen - 18))
            {
                first = 0;
                continue;   /* clean already */
            }
            if (!first && !any_set(fd, off, elen))
                continue;
            if (first && j >= 4 && 4 + get32be(b) < j)
                exif_note(b + 4 + get32be(b), j - 4 - get32be(b), 0);
            else
                note("EXIF");
            if (at_zero(fd, off, elen) < 0)
                return -1;
            if (first && elen >= 18 &&
                at_write(fd, off, empty_exif, 18) < 0)
            {
                return -1;
            }
            first = 0;
        }
    }
    return 0;
}

int clean_heif(int fd, long long size)
{
    unsigned char h[16];
    unsigned char *m;
    struct item it[MAX_ITEMS];
    long long off, bsize, idat;
    long msize, pos, cs, iloc, iloc_n, n, i;
    int r;

    off = 0;
    m = 0;
    while (off + 8 <= size) {
        if (at_read(fd, off, h, 8) < 0)
            break;
        bsize = (long long)get32be(h) & 0xffffffffLL;
        if (bsize == 0)
            bsize = size - off;
        if (bsize < 8 || bsize > size - off)
            break;
        if (is_word(h + 4, "meta")) {
            if (bsize > 16L * 1024 * 1024) {
                why = "a HEIF picture with too big a meta box";
                return -1;
            }
            msize = (long)bsize;
            m = malloc(msize);
            if (!m || at_read(fd, off, m, msize) < 0) {
                free(m);
                why = "can't read it";
                return -1;
            }
            break;
        }
        off += bsize;
    }
    if (!m) {
        why = "a HEIF picture without its meta box";
        return -1;
    }
    /* iinf: which items are EXIF or XMP; iloc: where; idat: inside */
    n = 0;
    iloc = -1;
    iloc_n = 0;
    idat = 0;
    pos = 12;
    while (pos + 8 <= msize) {
        cs = get32be(m + pos);
        if (cs < 8 || cs > msize - pos)
            break;
        if (is_word(m + pos + 4, "iinf"))
            n = heif_items(m + pos + 8, cs - 8, it);
        else if (is_word(m + pos + 4, "iloc")) {
            iloc = pos + 8;
            iloc_n = cs - 8;
        } else if (is_word(m + pos + 4, "idat")) {
            idat = off + pos + 8;
        }
        pos += cs;
    }
    r = 0;
    for (i = 0; i < n && iloc >= 0 && r == 0; i++)
        r = heif_blank(fd, size, m + iloc, iloc_n, &it[i], idat);
    free(m);
    return r;
}

long long mkv_last(int fd, long long size)
{
    struct elem e;
    long long off, last, data;

    off = 0;
    last = -1;
    while (off < size) {
        if (ebml_head(fd, off, size, &e) < 0)
            return -1;
        data = off + e.idlen + e.szlen;
        if (e.size < 0) {
            if (e.id != ID_SEGMENT)
                return -1;
            off = data;     /* its children go on to the end */
            continue;
        }
        if (e.size > size - data)
            return -1;
        last = off;
        off = data + e.size;
    }
    return last;
}
