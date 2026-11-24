/* svg: SVG drawings (text, XML) and BMP pictures, rewritten in memory.
 * SVG keeps every element that draws something; what goes is what the
 * editor added about the file: comments, metadata, its own settings,
 * file names and directories (Inkscape writes where it exported to),
 * the program's name and Illustrator's private copy of the drawing. */
#include <string.h>

#include "clean.h"

static const struct xrule svg_rules[] = {
    { x_comments, "", 0, "comments" },
    { x_elem, "metadata", 0, "metadata (author, title, licence)" },
    { x_elem, "sodipodi:namedview", 0, "editor settings" },
    { x_elem, "i:pgf", 0, "program data (Illustrator's copy of it)" },
    { x_attr, "inkscape:export-filename", 0, "file names and directories" },
    { x_attr, "sodipodi:docname", 0, "file names and directories" },
    { x_attr, "sodipodi:docbase", 0, "file names and directories" },
    { x_attr, "inkscape:export-xdpi", 0, "export settings" },
    { x_attr, "inkscape:export-ydpi", 0, "export settings" },
    { x_attr, "inkscape:version", 0, "program" },
    { x_attr, "sodipodi:version", 0, "program" },
    { x_attr, "inkscape:output_extension", 0, "program" },
    { x_end, 0, 0, 0 }
};

int clean_svg(const unsigned char *p, long n, struct buf *out)
{
    int rc;

    rc = xml_edit(svg_rules, p, n, out);
    if (rc < 0) {
        why = "a broken SVG file";
        return -1;
    }
    return 0;
}

static int is_white(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int starts(const unsigned char *p, long n, const char *w)
{
    long k;

    k = strlen(w);
    return k <= n && !memcmp(p, w, k);
}

/* is_svg: an XML file whose first element is svg */
int is_svg(const unsigned char *p, long n)
{
    long i, e;

    i = 0;
    if (starts(p, n, "\357\273\277"))
        i = 3;
    for (;;) {
        while (i < n && is_white(p[i]))
            i++;
        if (i + 1 >= n || p[i] != '<')
            return 0;
        if (starts(p + i, n - i, "<svg"))
            return 1;
        if (starts(p + i, n - i, "<!--")) {
            e = xml_find(p, n, i, "-->");
            if (e < 0)
                return 0;
            i = e + 3;
        } else if (p[i + 1] == '?' || p[i + 1] == '!') {
            e = xml_find(p, n, i, ">");
            if (e < 0)
                return 0;
            i = e + 1;
        } else {
            return 0;
        }
    }
}

/* ------------------------------------------------------------ BMP */

/* bmp_end: where a BMP's last part (pixels, colour profile) ends */
long bmp_end(const unsigned char *p, long n)
{
    long hs, w, h, bits, comp, row, end, size, pe;

    if (n < 54 || p[0] != 'B' || p[1] != 'M')
        return -1;
    hs = get32le(p + 14);
    if (hs < 40)
        return -1;
    w = get32le(p + 18);
    h = get32le(p + 22);
    if (h < 0)
        h = -h;
    bits = get16le(p + 28);
    comp = get32le(p + 30);
    size = get32le(p + 34);
    if (w <= 0 || w > 100000 || h > 100000 || bits < 1 || bits > 32)
        return -1;
    row = (w * bits + 31) / 32 * 4;
    if (comp == 0 || comp == 3 || comp == 6)
        end = get32le(p + 10) + row * h;
    else
        end = get32le(p + 10) + size;
    if (hs >= 124 && get32le(p + 70) == 0x4d424544L) {
        /* an embedded colour profile, after the pixels or before */
        pe = 14 + get32le(p + 126) + get32le(p + 130);
        if (pe > end)
            end = pe;
    }
    return end > 0 && end <= n ? end : -1;
}

/* BMP: no metadata, but a V5 header can name a colour profile file
 * (a path on the computer it was made on) */
int clean_bmp(const unsigned char *p, long n, struct buf *out)
{
    long hs, off, len;

    if (bmp_end(p, n) < 0) {
        why = "a broken BMP picture";
        return -1;
    }
    buf_put(out, p, n);
    hs = get32le(p + 14);
    if (hs >= 124 && get32le(p + 70) == 0x4c494e4bL) {   /* LINK */
        off = 14 + get32le(p + 126);
        len = get32le(p + 130);
        if (off > 0 && len > 0 && off + len <= n)
            memset(out->p + off, 0, len);
        put32le(out->p + 70, 0x73524742L);                  /* sRGB */
        put32le(out->p + 126, 0);
        put32le(out->p + 130, 0);
        note("colour profile's file name");
    }
    return 0;
}
