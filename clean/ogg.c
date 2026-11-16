/* Ogg: Vorbis, Opus and Theora keep their tags in the second packet of
 * each stream, spread over pages that each carry a checksum. The packet
 * keeps its length: its codec's name, an empty program name, no tags,
 * then padding; the pages it touches get new checksums. Nothing else
 * moves. */
#define _XOPEN_SOURCE 500
#include <stdlib.h>
#include <string.h>

#include "clean.h"

#define MAX_STREAMS 16
#define MAX_PACKET (64L * 1024 * 1024)

struct ext {
    long long off;
    long len;
};

struct stream {
    long serial;
    int codec;          /* 0 other, 1 Vorbis, 2 Opus, 3 Theora */
    int packet;         /* which packet is being read */
    long long plen;     /* bytes of it so far */
    int done;
    struct ext *ex;     /* where the tags packet's bytes are */
    long nex;
    long long *pages;   /* the pages they are on */
    long npages;
};

static const char *magic[4] = { "", "\003vorbis", "OpusTags", "\201theora" };

static unsigned long ogg_crc(const unsigned char *p, long n)
{
    unsigned long crc;
    long i;
    int k;

    crc = 0;
    for (i = 0; i < n; i++) {
        crc ^= (unsigned long)p[i] << 24;
        for (k = 0; k < 8; k++) {
            crc = crc & 0x80000000UL ? (crc << 1) ^ 0x04c11db7UL : crc << 1;
            crc &= 0xffffffffUL;
        }
    }
    return crc;
}

/* fix_crc: the page at off gets the checksum of what it now holds */
static int fix_crc(int fd, long long off)
{
    static unsigned char p[27 + 255 + 255 * 255];
    long n, i;
    int r;

    r = at_read(fd, off, p, 27);
    if (r == 0)
        r = at_read(fd, off + 27, p + 27, p[26]);
    if (r < 0)
        return -1;
    n = 27 + p[26];
    for (i = 0; i < p[26]; i++)
        n += p[27 + i];
    r = at_read(fd, off + 27 + p[26], p + 27 + p[26], n - 27 - p[26]);
    if (r < 0)
        return -1;
    memset(p + 22, 0, 4);
    put32le(p + 22, (long)ogg_crc(p, n));
    return at_write(fd, off + 22, p + 22, 4);
}

static void add_ext(struct stream *s, long long off, long len,
    long long page)
{
    struct ext *e;
    long long *g;

    if (s->nex && s->ex[s->nex - 1].off + s->ex[s->nex - 1].len == off) {
        s->ex[s->nex - 1].len += len;
    } else {
        if ((s->nex & 63) == 0) {
            e = malloc((s->nex + 64) * sizeof *e);
            if (!e)
                return;
            if (s->nex)
                memcpy(e, s->ex, s->nex * sizeof *e);
            free(s->ex);
            s->ex = e;
        }
        s->ex[s->nex].off = off;
        s->ex[s->nex].len = len;
        s->nex++;
    }
    if (s->npages && s->pages[s->npages - 1] == page)
        return;
    if ((s->npages & 63) == 0) {
        g = malloc((s->npages + 64) * sizeof *g);
        if (!g)
            return;
        if (s->npages)
            memcpy(g, s->pages, s->npages * sizeof *g);
        free(s->pages);
        s->pages = g;
    }
    s->pages[s->npages] = page;
    s->npages++;
}

/* tag_words: the names in a tags packet (after its codec's name) */
static void tag_words(const unsigned char *b, long k, char *d)
{
    char w[24];
    long i, n, count, c, j, s;

    if (k < 8)
        return;
    i = 4 + get32le(b);
    if (i < 4 || i + 4 > k)
        return;
    if (i > 4)
        strcat(d, "program");
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
        if (s && !strstr(d, w) && strlen(d) + s + 3 < 100) {
            if (d[0])
                strcat(d, ", ");
            strcat(d, w);
        }
        i += n;
    }
}

/* tags: what to do with one stream's tags packet; mode 0 cleans, 1
 * puts random bytes in its padding */
static int tags(int fd, struct stream *s, int mode)
{
    unsigned char *b;
    char d[104];
    long long len;
    long i, k, m, used, pos;
    int r;

    len = 0;
    for (i = 0; i < s->nex; i++)
        len += s->ex[i].len;
    m = strlen(magic[s->codec]);
    if (len < m + 9 || len > MAX_PACKET)
        return 0;
    b = malloc((long)len);
    if (!b)
        return 0;
    pos = 0;
    r = 0;
    for (i = 0; i < s->nex && r == 0; i++) {
        r = at_read(fd, s->ex[i].off, b + pos, s->ex[i].len);
        pos += s->ex[i].len;
    }
    if (r < 0) {
        r = 0;
        goto quit;
    }
    if (memcmp(b, magic[s->codec], m))
        goto quit;
    used = m + 8 + (s->codec == 1 ? 1 : 0);
    if (get32le(b + m) == 0 && get32le(b + m + 4) == 0) {
        /* clean already */
        if (mode == 0 || len - used < 32)
            goto quit;
        rand_bytes(b + used, 32);
        if (s->codec == 2)
            b[used] &= 0xfe;    /* Opus: padding, not data to keep */
    } else {
        if (mode == 1)
            goto quit;
        d[0] = 0;
        tag_words(b + m, (long)len - m, d);
        note2("tags", d);
        memset(b + m, 0, (long)len - m);
        if (s->codec == 1)
            b[m + 8] = 1;       /* Vorbis' framing bit */
    }
    pos = 0;
    for (i = 0; i < s->nex && r == 0; i++) {
        r = at_write(fd, s->ex[i].off, b + pos, s->ex[i].len);
        pos += s->ex[i].len;
    }
    for (k = 0; k < s->npages && r == 0 && !dry_run; k++)
        r = fix_crc(fd, s->pages[k]);
    if (r == 0 && mode == 1)
        r = 1;
quit:
    free(b);
    return r;
}

static int ogg_walk(int fd, long long size, int mode)
{
    unsigned char h[27 + 255], id[8];
    struct stream st[MAX_STREAMS];
    struct stream *s;
    long long off, pos;
    long serial, data;
    int nst, i, nseg, r, len, k;

    memset(st, 0, sizeof st);
    nst = 0;
    r = 0;
    off = 0;
    while (off + 27 <= size && r >= 0) {
        k = at_read(fd, off, h, 27);
        if (k == 0 && is_word(h, "OggS"))
            k = at_read(fd, off + 27, h + 27, h[26]);
        else
            k = -1;
        if (k < 0) {
            why = "a broken Ogg file";
            r = -1;
            break;
        }
        nseg = h[26];
        data = 0;
        for (i = 0; i < nseg; i++)
            data += h[27 + i];
        serial = get32le(h + 14);
        s = 0;
        for (i = 0; i < nst; i++) {
            if (st[i].serial == serial)
                s = &st[i];
        }
        if (!s && (h[5] & 2) && nst < MAX_STREAMS) {
            s = &st[nst];
            nst++;
            s->serial = serial;
            k = -1;
            if (nseg)
                k = at_read(fd, off + 27 + nseg, id, 8);
            if (k == 0) {
                if (!memcmp(id, "\001vorbis", 7))
                    s->codec = 1;
                else if (!memcmp(id, "OpusHead", 8))
                    s->codec = 2;
                else if (!memcmp(id, "\200theora", 7))
                    s->codec = 3;
            }
            if (!s->codec)
                s->done = 1;
        }
        pos = off + 27 + nseg;
        for (i = 0; s && !s->done && i < nseg; i++) {
            len = h[27 + i];
            if (s->packet == 1 && len)
                add_ext(s, pos, len, off);
            s->plen += len;
            pos += len;
            if (len < 255) {
                if (s->packet == 1) {
                    s->done = 1;
                    r = tags(fd, s, mode);
                }
                s->packet++;
                s->plen = 0;
            }
        }
        off += 27 + nseg + data;
        for (i = 0; i < nst && st[i].done; i++)
            ;
        if (nst && i == nst && r >= 0)
            break;      /* every stream's tags are done */
    }
    for (i = 0; i < nst; i++) {
        free(st[i].ex);
        free(st[i].pages);
    }
    return r;
}

int clean_ogg(int fd, long long size)
{
    return ogg_walk(fd, size, 0) < 0 ? -1 : 0;
}

int rand_ogg(int fd, long long size)
{
    return ogg_walk(fd, size, 1);
}
