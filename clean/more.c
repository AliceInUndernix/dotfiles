/* more: AIFF, ASF (WMA, WMV) and TIFF (with the camera raw files built
 * on it: DNG, NEF, CR2, ARW, ORF, RW2, PEF), all changed in place. */
#define _XOPEN_SOURCE 500
#include <stdlib.h>
#include <string.h>

#include "clean.h"

static void add_word(char *d, const char *w)
{
    long n;

    if (strstr(d, w))
        return;
    n = strlen(d);
    if (n + (long)strlen(w) + 3 > 100)
        return;
    if (n)
        strcpy(d + n, ", ");
    strcat(d, w);
}

/* ------------------------------------------------------------ AIFF */

/* AIFF: tags (name, author, copyright, notes, comments), ID3, XMP and
 * program data become JUNK chunks of zeros, which readers skip */
int clean_aiff(int fd, long long size)
{
    unsigned char h[12];
    long long off, end, len;
    const char *what;
    int rc;

    rc = at_read(fd, 4, h, 4);
    if (rc < 0)
        return -1;
    end = 8 + ((long long)get32be(h) & 0xffffffffLL);
    if (end > size)
        end = size;
    off = 12;
    while (off + 8 <= end) {
        rc = at_read(fd, off, h, 8);
        if (rc < 0) {
            why = "a broken AIFF file";
            return -1;
        }
        len = (long long)get32be(h + 4) & 0xffffffffLL;
        what = 0;
        if (is_word(h, "NAME") || is_word(h, "AUTH") ||
            is_word(h, "(c) ") || is_word(h, "ANNO") ||
            is_word(h, "COMT"))
        {
            what = "tags";
        } else if (is_word(h, "ID3 ") || is_word(h, "id3 ")) {
            what = "ID3 tags";
        } else if (is_word(h, "XMP ")) {
            what = "XMP";
        } else if (is_word(h, "APPL")) {
            what = "program data";
        }
        if (what) {
            if (off + 8 + len > end) {
                why = "a broken AIFF file";
                return -1;
            }
            note(what);
            rc = at_write(fd, off, (const unsigned char *)"JUNK", 4);
            if (rc == 0)
                rc = at_zero(fd, off + 8, len);
            if (rc < 0)
                return -1;
        }
        off += 8 + len + (len & 1);
    }
    return 0;
}

/* AIFF: a JUNK chunk of 32 random bytes at the end */
int rand_aiff(int fd, long long size)
{
    unsigned char h[8], b[40];
    long long off, len, last, llen, form;
    int r;

    r = at_read(fd, 4, h, 4);
    if (r < 0)
        return 0;
    form = (long long)get32be(h) & 0xffffffffLL;
    if (8 + form != size)
        return 0;
    off = 12;
    last = -1;
    llen = 0;
    while (off + 8 <= size) {
        r = at_read(fd, off, h, 8);
        if (r < 0)
            return 0;
        len = (long long)get32be(h + 4) & 0xffffffffLL;
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
    put32be(b + 4, 32);
    r = at_write(fd, size, b, 40);
    if (r < 0)
        return -1;
    put32be(h, (long)(form + 40));
    r = at_write(fd, 4, h, 4);
    return r < 0 ? -1 : 1;
}

/* ------------------------------------------------------------ ASF */

static const unsigned char asf_header[16] = {
    0x30, 0x26, 0xb2, 0x75, 0x8e, 0x66, 0xcf, 0x11,
    0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c
};
static const unsigned char asf_props[16] = {
    0xa1, 0xdc, 0xab, 0x8c, 0x47, 0xa9, 0xcf, 0x11,
    0x8e, 0xe4, 0x00, 0xc0, 0x0c, 0x20, 0x53, 0x65
};
static const unsigned char asf_content[16] = {
    0x33, 0x26, 0xb2, 0x75, 0x8e, 0x66, 0xcf, 0x11,
    0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c
};
static const unsigned char asf_ext_content[16] = {
    0x40, 0xa4, 0xd0, 0xd2, 0x07, 0xe3, 0xd2, 0x11,
    0x97, 0xf0, 0x00, 0xa0, 0xc9, 0x5e, 0xa8, 0x50
};
static const unsigned char asf_branding[16] = {
    0xfa, 0xb3, 0x11, 0x22, 0x23, 0xbd, 0xd2, 0x11,
    0xb4, 0xb7, 0x00, 0xa0, 0xc9, 0x55, 0xfc, 0x6e
};
static const unsigned char asf_hext[16] = {
    0xb5, 0x03, 0xbf, 0x5f, 0x2e, 0xa9, 0xcf, 0x11,
    0x8e, 0xe3, 0x00, 0xc0, 0x0c, 0x20, 0x53, 0x65
};
static const unsigned char asf_meta[16] = {
    0xea, 0xcb, 0xf8, 0xc5, 0xaf, 0x5b, 0x77, 0x48,
    0x84, 0x67, 0xaa, 0x8c, 0x44, 0xfa, 0x4c, 0xca
};
static const unsigned char asf_metalib[16] = {
    0x94, 0x1c, 0x23, 0x44, 0x98, 0x94, 0xd1, 0x49,
    0xa1, 0x41, 0x1d, 0x13, 0x4e, 0x45, 0x70, 0x54
};
static const unsigned char asf_padding[16] = {
    0x74, 0xd4, 0x06, 0x18, 0xdf, 0xca, 0x09, 0x45,
    0xa4, 0xba, 0x9a, 0xab, 0xcb, 0x96, 0xaa, 0xe8
};
static const unsigned char asf_data[16] = {
    0x36, 0x26, 0xb2, 0x75, 0x8e, 0x66, 0xcf, 0x11,
    0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c
};

static long long get64le(const unsigned char *p)
{
    return ((long long)get32le(p + 4) & 0xffffffffLL) << 32 |
        ((long long)get32le(p) & 0xffffffffLL);
}

/* asf_objects: the objects from off to end; what goes becomes a
 * padding object of the same size. props: where the file's properties
 * object is (for its ID and date) */
static int asf_objects(int fd, long long off, long long end, int depth,
    long long *props)
{
    unsigned char h[24];
    long long size;
    const char *what;
    int rc;

    while (off + 24 <= end) {
        rc = at_read(fd, off, h, 24);
        if (rc < 0)
            return -1;
        size = get64le(h + 16);
        if (size < 24 || size > end - off)
            return -1;
        what = 0;
        if (!memcmp(h, asf_content, 16))
            what = "tags (title, author, copyright, description)";
        else if (!memcmp(h, asf_ext_content, 16))
            what = "tags (program, encoder settings and more)";
        else if (!memcmp(h, asf_meta, 16) || !memcmp(h, asf_metalib, 16))
            what = "more tags";
        else if (!memcmp(h, asf_branding, 16))
            what = "branding (picture, copyright address)";
        if (what) {
            note(what);
            rc = at_write(fd, off, asf_padding, 16);
            if (rc == 0)
                rc = at_zero(fd, off + 24, size - 24);
            if (rc < 0)
                return -1;
        } else if (!memcmp(h, asf_props, 16) && depth == 0) {
            *props = off;
        } else if (!memcmp(h, asf_hext, 16) && depth == 0 && size >= 46) {
            rc = asf_objects(fd, off + 46, off + size, 1, props);
            if (rc < 0)
                return -1;
        }
        off += size;
    }
    return 0;
}

/* ASF: Windows Media (WMA, WMV). The creation date is set to 0 here;
 * the file ID (made when it was written, so it names this one file)
 * gets new random bytes in rand_asf. */
int clean_asf(int fd, long long size)
{
    unsigned char h[30], d[8];
    long long hsize, props;
    int rc;

    rc = at_read(fd, 0, h, 30);
    if (rc < 0 || memcmp(h, asf_header, 16)) {
        why = "a broken ASF file";
        return -1;
    }
    hsize = get64le(h + 16);
    if (hsize < 30 || hsize > size) {
        why = "a broken ASF file";
        return -1;
    }
    props = -1;
    rc = asf_objects(fd, 30, hsize, 0, &props);
    if (rc < 0) {
        why = "a broken ASF file";
        return -1;
    }
    if (props >= 0) {
        rc = at_read(fd, props + 48, d, 8);
        if (rc == 0 && get64le(d) != 0) {
            note("date");
            memset(d, 0, 8);
            rc = at_write(fd, props + 48, d, 8);
        }
        if (rc < 0)
            return -1;
    }
    return 0;
}

/* ASF: a new random file ID, in the properties and the data object */
int rand_asf(int fd, long long size)
{
    unsigned char h[30], id[16];
    long long off, hsize, props, osize;
    int rc;

    rc = at_read(fd, 0, h, 30);
    if (rc < 0 || memcmp(h, asf_header, 16))
        return 0;
    hsize = get64le(h + 16);
    if (hsize < 30 || hsize + 40 > size)
        return 0;
    props = -1;
    off = 30;
    while (off + 40 <= hsize) {
        rc = at_read(fd, off, h, 24);
        if (rc < 0)
            return 0;
        osize = get64le(h + 16);
        if (osize < 24 || osize > hsize - off)
            return 0;
        if (!memcmp(h, asf_props, 16))
            props = off;
        off += osize;
    }
    rc = at_read(fd, hsize, h, 16);
    if (rc < 0 || props < 0 || memcmp(h, asf_data, 16))
        return 0;
    rand_bytes(id, 16);
    rc = at_write(fd, props + 24, id, 16);
    if (rc == 0)
        rc = at_write(fd, hsize + 24, id, 16);
    return rc < 0 ? -1 : 1;
}

/* ------------------------------------------------------------ TIFF */

#define MAX_IFDS 64
#define MAX_ENTRIES 1024

static int tiff_le;         /* byte order: 1 little endian (II) */
static int tiff_raw;        /* a camera raw file: keep what decoders need */
static long long tiff_size;
static long long seen[MAX_IFDS];
static int nseen;
static char tiff_d[104];    /* what went */

static long t16(const unsigned char *p)
{
    return tiff_le ? get16le(p) : get16be(p);
}

static long long t32(const unsigned char *p)
{
    return (long long)(tiff_le ? get32le(p) : get32be(p)) & 0xffffffffLL;
}

static void p16(unsigned char *p, long v)
{
    if (tiff_le) {
        p[0] = (unsigned char)(v & 0xff);
        p[1] = (unsigned char)(v >> 8 & 0xff);
    } else {
        p[0] = (unsigned char)(v >> 8 & 0xff);
        p[1] = (unsigned char)(v & 0xff);
    }
}

static void p32(unsigned char *p, long long v)
{
    if (tiff_le)
        put32le(p, (long)v);
    else
        put32be(p, (long)v);
}

/* value_size: the bytes of an entry's value; -1 for a type unknown */
static long long value_size(const unsigned char *e)
{
    static const int sizes[14] = { 0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8, 4 };
    long type;

    type = t16(e + 2);
    if (type < 1 || type > 13)
        return -1;
    return sizes[type] * t32(e + 4);
}

/* what_tag: why a tag goes (0: it stays). exif: in the EXIF IFD */
static const char *what_tag(long tag, int exif)
{
    if (exif) {
        switch (tag) {
        case 36867: case 36868: case 36880: case 36881: case 36882:
        case 37520: case 37521: case 37522:
            return "date";
        case 37510: return "comment";
        case 42016: return "unique ID";
        case 42032: return "owner's name";
        case 42033: case 42037: return "serial number";
        case 37500: return tiff_raw ? 0 : "maker notes";
        default: return tiff_raw ? 0 : "EXIF";
        }
    }
    switch (tag) {
    case 269: return "document name";
    case 270: return "description";
    case 271: case 272: return tiff_raw ? 0 : "camera";
    case 305: return "program";
    case 306: return "date";
    case 315: return "author";
    case 316: return "computer name";
    case 33432: return "copyright";
    case 700: return "XMP";
    case 33723: case 34377: return "IPTC/Photoshop";
    case 37724: return "Photoshop layers";
    case 34853: return "GPS position";
    case 40091: case 40092: case 40093: case 40094: case 40095:
        return "Windows tags";
    case 18246: case 18249: return "rating";
    case 42016: return "unique ID";
    case 50735: return "serial number";
    case 50827: case 50828: return "original raw file";
    case 34665: return tiff_raw ? 0 : "EXIF";
    default: return 0;
    }
}

static int was_seen(long long off)
{
    int i;

    for (i = 0; i < nseen; i++) {
        if (seen[i] == off)
            return 1;
    }
    if (nseen == MAX_IFDS)
        return 1;
    seen[nseen] = off;
    nseen++;
    return 0;
}

/* read_ifd: an IFD's entries (and its next pointer) into b */
static long read_ifd(int fd, long long off, unsigned char *b)
{
    long n;
    int rc;

    if (off < 8 || off + 2 > tiff_size)
        return -1;
    rc = at_read(fd, off, b, 2);
    if (rc < 0)
        return -1;
    n = t16(b);
    if (n > MAX_ENTRIES || off + 2 + 12 * n + 4 > tiff_size)
        return -1;
    rc = at_read(fd, off + 2, b, 12 * n + 4);
    return rc < 0 ? -1 : n;
}

/* zero_ifd: an IFD that goes (GPS, EXIF), with the values it points to */
static int zero_ifd(int fd, long long off)
{
    static unsigned char b[12 * MAX_ENTRIES + 4];
    long long vs;
    long n, i;
    int rc;

    n = read_ifd(fd, off, b);
    if (n < 0)
        return 0;
    for (i = 0; i < n; i++) {
        vs = value_size(b + 12 * i);
        if (vs > 4 && t32(b + 12 * i + 8) + vs <= tiff_size) {
            rc = at_zero(fd, t32(b + 12 * i + 8), vs);
            if (rc < 0)
                return -1;
        }
    }
    return at_zero(fd, off, 2 + 12 * n + 4);
}

/* sub_ifds: the IFDs an entry points to, one after the other */
static int ifd_walk(int fd, long long off, int exif, int scan,
    long long *next);

static int sub_ifds(int fd, const unsigned char *e, int exif, int scan)
{
    unsigned char p[4 * 16];
    long long vs, next;
    long k, i;
    int rc;

    vs = value_size(e);
    k = (long)t32(e + 4);
    if (vs < 4 || k > 16)
        return 0;
    if (vs == 4) {
        memcpy(p, e + 8, 4);
    } else {
        if (t32(e + 8) + vs > tiff_size)
            return 0;
        rc = at_read(fd, t32(e + 8), p, (long)vs);
        if (rc < 0)
            return 0;
    }
    for (i = 0; i < k; i++) {
        rc = ifd_walk(fd, t32(p + 4 * i), exif, scan, &next);
        if (rc < 0)
            return -1;
    }
    return 0;
}

/* ifd_walk: one IFD; scan only looks whether it is a raw picture */
static int ifd_walk(int fd, long long off, int exif, int scan,
    long long *next)
{
    unsigned char b[12 * MAX_ENTRIES + 4], e[12];
    const char *what;
    long long vs;
    long n, i, kept, tag, v;
    int rc;

    *next = 0;
    rc = was_seen(off);
    if (rc)
        return 0;
    n = read_ifd(fd, off, b);
    if (n < 0) {
        why = "a broken TIFF file";
        return -1;
    }
    *next = t32(b + 12 * n);
    kept = 0;
    for (i = 0; i < n; i++) {
        memcpy(e, b + 12 * i, 12);
        tag = t16(e);
        vs = value_size(e);
        if (scan) {
            v = t16(e + 8);
            if ((tag == 262 && (v == 32803 || v == 34892)) || tag == 50706)
                tiff_raw = 1;
            if (tag == 330) {
                rc = sub_ifds(fd, e, 0, 1);
                if (rc < 0)
                    return -1;
            }
            continue;
        }
        what = what_tag(tag, exif);
        if (!what && (tag == 330 || (tag == 34665 && !exif))) {
            rc = sub_ifds(fd, e, tag == 34665, 0);
            if (rc < 0)
                return -1;
        }
        if (!what) {
            memcpy(b + 12 * kept, e, 12);
            kept++;
            continue;
        }
        add_word(tiff_d, what);
        if (tag == 34853 || tag == 34665) {
            rc = zero_ifd(fd, t32(e + 8));
            if (rc < 0)
                return -1;
        }
        if (vs > 4 && t32(e + 8) + vs <= tiff_size) {
            rc = at_zero(fd, t32(e + 8), vs);
            if (rc < 0)
                return -1;
        }
    }
    if (scan || kept == n)
        return 0;
    memset(b + 12 * kept, 0, 12 * (n - kept) + 4);
    p32(b + 12 * kept, *next);
    p16(e, kept);
    rc = at_write(fd, off, e, 2);
    if (rc == 0)
        rc = at_write(fd, off + 2, b, 12 * n + 4);
    return rc;
}

static int tiff_chain(int fd, long long first, int scan)
{
    long long off, next;
    int rc, k;

    nseen = 0;
    off = first;
    for (k = 0; off && k < MAX_IFDS; k++) {
        rc = ifd_walk(fd, off, 0, scan, &next);
        if (rc < 0)
            return -1;
        off = next;
    }
    return 0;
}

int clean_tiff(int fd, long long size)
{
    unsigned char h[10];
    long magic;
    int rc;

    rc = at_read(fd, 0, h, 10);
    if (rc < 0) {
        why = "a broken TIFF file";
        return -1;
    }
    tiff_le = h[0] == 'I';
    tiff_size = size;
    magic = t16(h + 2);
    if (magic == 43) {
        why = "BigTIFF files are not supported yet";
        return -1;
    }
    /* 42 TIFF; ORF (Olympus) and RW2 (Panasonic) have their own */
    tiff_raw = magic != 42 || (h[8] == 'C' && h[9] == 'R');
    rc = tiff_chain(fd, t32(h + 4), 1);
    if (rc < 0)
        return -1;
    tiff_d[0] = 0;
    rc = tiff_chain(fd, t32(h + 4), 0);
    if (rc < 0)
        return -1;
    if (tiff_d[0])
        note2("tags", tiff_d);
    return 0;
}

/* TIFF: 32 random bytes at the end, which nothing points to */
int rand_tiff(int fd, long long size)
{
    unsigned char b[32];
    int rc;

    rand_bytes(b, 32);
    rc = at_write(fd, size, b, 32);
    return rc < 0 ? -1 : 1;
}
