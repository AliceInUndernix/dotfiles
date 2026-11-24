/* zip: zip files, and the documents made of them: office documents
 * (DOCX, XLSX, PPTX; ODT, ODS, ODP) and EPUB books. Rewritten in
 * memory: each part that holds metadata is unpacked, cleaned and packed
 * again; every other part is copied as it is. What goes:
 *
 * - in the zip itself: file times, owner IDs, the system it was made
 *   on, comments, anything after it
 * - office: the document's information (author, last editor, company,
 *   dates, editing time, template, program), its thumbnail, names and
 *   dates of comments and tracked changes, Word's editing session IDs
 *   (rsid: they tie documents edited in the same session together),
 *   paths of the computer it was written on, printer names
 * - EPUB: the program's own tags (calibre, Sigil), the change date
 * - every picture inside: what clean takes out of pictures */
#include <stdlib.h>
#include <string.h>

#include "clean.h"

#define MAX_ENTRIES 65535
#define MAX_PART (256L * 1024 * 1024)
#define MAX_ALL (1024L * 1024 * 1024)
#define DOS_DATE 0x21               /* 1980-01-01, the first date zip has */

static const unsigned char tiny_jpeg[631] = {
    255, 216, 255, 224, 0, 16, 74, 70, 73, 70, 0, 1, 1, 0, 0, 1, 0, 1, 0, 0,
    255, 219, 0, 67, 0, 16, 11, 12, 14, 12, 10, 16, 14, 13, 14, 18, 17, 16,
    19, 24, 40, 26, 24, 22, 22, 24, 49, 35, 37, 29, 40, 58, 51, 61, 60, 57,
    51, 56, 55, 64, 72, 92, 78, 64, 68, 87, 69, 55, 56, 80, 109, 81, 87, 95,
    98, 103, 104, 103, 62, 77, 113, 121, 112, 100, 120, 92, 101, 103, 99,
    255, 219, 0, 67, 1, 17, 18, 18, 24, 21, 24, 47, 26, 26, 47, 99, 66, 56,
    66, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 255, 192, 0,
    17, 8, 0, 1, 0, 1, 3, 1, 34, 0, 2, 17, 1, 3, 17, 1, 255, 196, 0, 31, 0,
    0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7,
    8, 9, 10, 11, 255, 196, 0, 181, 16, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4,
    0, 0, 1, 125, 1, 2, 3, 0, 4, 17, 5, 18, 33, 49, 65, 6, 19, 81, 97, 7,
    34, 113, 20, 50, 129, 145, 161, 8, 35, 66, 177, 193, 21, 82, 209, 240,
    36, 51, 98, 114, 130, 9, 10, 22, 23, 24, 25, 26, 37, 38, 39, 40, 41, 42,
    52, 53, 54, 55, 56, 57, 58, 67, 68, 69, 70, 71, 72, 73, 74, 83, 84, 85,
    86, 87, 88, 89, 90, 99, 100, 101, 102, 103, 104, 105, 106, 115, 116,
    117, 118, 119, 120, 121, 122, 131, 132, 133, 134, 135, 136, 137, 138,
    146, 147, 148, 149, 150, 151, 152, 153, 154, 162, 163, 164, 165, 166,
    167, 168, 169, 170, 178, 179, 180, 181, 182, 183, 184, 185, 186, 194,
    195, 196, 197, 198, 199, 200, 201, 202, 210, 211, 212, 213, 214, 215,
    216, 217, 218, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 241,
    242, 243, 244, 245, 246, 247, 248, 249, 250, 255, 196, 0, 31, 1, 0, 3,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    10, 11, 255, 196, 0, 181, 17, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1,
    2, 119, 0, 1, 2, 3, 17, 4, 5, 33, 49, 6, 18, 65, 81, 7, 97, 113, 19, 34,
    50, 129, 8, 20, 66, 145, 161, 177, 193, 9, 35, 51, 82, 240, 21, 98, 114,
    209, 10, 22, 36, 52, 225, 37, 241, 23, 24, 25, 26, 38, 39, 40, 41, 42,
    53, 54, 55, 56, 57, 58, 67, 68, 69, 70, 71, 72, 73, 74, 83, 84, 85, 86,
    87, 88, 89, 90, 99, 100, 101, 102, 103, 104, 105, 106, 115, 116, 117,
    118, 119, 120, 121, 122, 130, 131, 132, 133, 134, 135, 136, 137, 138,
    146, 147, 148, 149, 150, 151, 152, 153, 154, 162, 163, 164, 165, 166,
    167, 168, 169, 170, 178, 179, 180, 181, 182, 183, 184, 185, 186, 194,
    195, 196, 197, 198, 199, 200, 201, 202, 210, 211, 212, 213, 214, 215,
    216, 217, 218, 226, 227, 228, 229, 230, 231, 232, 233, 234, 242, 243,
    244, 245, 246, 247, 248, 249, 250, 255, 218, 0, 12, 3, 1, 0, 2, 17, 3,
    17, 0, 63, 0, 244, 10, 40, 162, 128, 63, 255, 217,
};
static const unsigned char tiny_png[69] = {
    137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82, 0, 0, 0,
    1, 0, 0, 0, 1, 8, 2, 0, 0, 0, 144, 119, 83, 222, 0, 0, 0, 12, 73, 68,
    65, 84, 120, 156, 99, 248, 255, 255, 63, 0, 5, 254, 2, 254, 13, 239, 70,
    184, 0, 0, 0, 0, 73, 69, 78, 68, 174, 66, 96, 130,
};

struct entry {
    const unsigned char *name;
    long nlen;
    int flags;
    int method;
    unsigned long crc;
    long csize;
    long usize;
    const unsigned char *data;      /* packed, in the input */
    struct buf part;                /* new content, when it changed */
    int changed;
};

enum { z_plain, z_ooxml, z_odf, z_epub };

static long long all_unpacked;

static int name_is(struct entry *e, const char *w)
{
    return e->nlen == (long)strlen(w) && !memcmp(e->name, w, e->nlen);
}

static int name_starts(struct entry *e, const char *w)
{
    return e->nlen >= (long)strlen(w) && !memcmp(e->name, w, strlen(w));
}

static int name_ends(struct entry *e, const char *w)
{
    long k;

    k = strlen(w);
    return e->nlen >= k && !memcmp(e->name + e->nlen - k, w, k);
}

static unsigned long zip_crc(const unsigned char *p, long n)
{
    return crc32_png(p, n, 0);
}

/* unpack: an entry's content into b; -1 when it can't be read */
static int unpack(struct entry *e, struct buf *b)
{
    int rc;

    if (e->usize > MAX_PART || all_unpacked + e->usize > MAX_ALL)
        return -1;
    if (e->method == 0) {
        if (e->csize != e->usize)
            return -1;
        buf_put(b, e->data, e->csize);
    } else if (e->method == 8) {
        rc = inflate_raw(e->data, e->csize, b, e->usize);
        if (rc < 0)
            return -1;
    } else {
        return -1;
    }
    if (b->n != e->usize || zip_crc(b->p, b->n) != e->crc)
        return -1;
    all_unpacked += e->usize;
    return 0;
}

/* ------------------------------------------------------------ rules */

static const struct xrule odf_rules[] = {
    { x_text, "dc:creator", "", "names of comments and tracked changes" },
    { x_text, "dc:date", "2000-01-01T00:00:00",
        "dates of comments and tracked changes" },
    { x_text, "meta:creator-initials", "",
        "names of comments and tracked changes" },
    { x_end, 0, 0, 0 }
};

static const struct xrule epub_rules[] = {
    { x_elem, "meta", "calibre:", "program" },
    { x_elem, "meta", "Sigil", "program" },
    { x_elem, "dc:contributor", "calibre", "program" },
    { x_elem, "dc:contributor", "Sigil", "program" },
    { x_comments, "", 0, "comments" },
    { x_end, 0, 0, 0 }
};

static const struct xrule rels_rules[] = {
    { x_target, "Target", 0, "paths on the computer" },
    { x_end, 0, 0, 0 }
};

/* the rules for the names that only some parts use */
static const struct xrule word_rules[] = {
    { x_attr, "w:rsid*", 0, "editing session IDs (rsid)" },
    { x_elem, "w:rsids", 0, "editing session IDs (rsid)" },
    { x_aval, "w:author", "", "names of comments and tracked changes" },
    { x_aval, "w:initials", "", "names of comments and tracked changes" },
    { x_attr, "w:date", 0, "dates of comments and tracked changes" },
    { x_aval, "w15:author", "", "names of comments and tracked changes" },
    { x_elem, "w15:presenceInfo", 0, "user IDs (e-mail addresses)" },
    { x_end, 0, 0, 0 }
};

static const struct xrule xl_rules[] = {
    { x_elem, "mc:AlternateContent", "x15ac:absPath",
        "paths on the computer" },
    { x_elem, "x15ac:absPath", 0, "paths on the computer" },
    { x_end, 0, 0, 0 }
};

static const struct xrule xl_people[] = {
    { x_text, "author", "", "names of comments and tracked changes" },
    { x_aval, "displayName", "", "names of comments and tracked changes" },
    { x_aval, "userId", "", "user IDs (e-mail addresses)" },
    { x_aval, "providerId", "", "user IDs (e-mail addresses)" },
    { x_attr, "dT", 0, "dates of comments and tracked changes" },
    { x_end, 0, 0, 0 }
};

static const struct xrule ppt_people[] = {
    { x_aval, "name", "", "names of comments and tracked changes" },
    { x_aval, "initials", "", "names of comments and tracked changes" },
    { x_aval, "userId", "", "user IDs (e-mail addresses)" },
    { x_aval, "providerId", "", "user IDs (e-mail addresses)" },
    { x_attr, "dt", 0, "dates of comments and tracked changes" },
    { x_attr, "created", 0, "dates of comments and tracked changes" },
    { x_end, 0, 0, 0 }
};

/* ------------------------------------------------------------ parts */

struct word {
    const char *tag;
    const char *what;
};

static const struct word core_words[] = {
    { "creator>", "author" }, { "lastModifiedBy>", "last editor" },
    { "created", "dates" }, { "modified", "dates" },
    { "lastPrinted>", "dates" }, { "revision>", "revision count" },
    { "title>", "title" }, { "subject>", "subject" },
    { "keywords>", "keywords" }, { "description>", "comments" },
    { "category>", "category" }, { "contentStatus>", "status" },
    { "<Application>", "program" }, { "<AppVersion>", "program" },
    { "<Company>", "company" }, { "<Manager>", "manager" },
    { "<TotalTime>", "editing time" }, { "<Template>", "template" },
    { "<HyperlinkBase>", "links" }, { "<property ", "custom properties" },
    { "meta:generator", "program" }, { "meta:initial-creator", "author" },
    { "dc:creator", "last editor" }, { "meta:creation-date", "dates" },
    { "dc:date", "dates" }, { "meta:print-date", "dates" },
    { "meta:printed-by", "printed by" },
    { "meta:editing-cycles", "revision count" },
    { "meta:editing-duration", "editing time" },
    { "meta:template", "template" }, { "dc:title", "title" },
    { "dc:subject", "subject" }, { "dc:description", "comments" },
    { "meta:keyword", "keywords" },
    { "meta:user-defined", "custom properties" },
    { 0, 0 }
};

/* set_part: the entry's content becomes s, when it isn't already */
static void set_part(struct entry *e, const unsigned char *s, long n,
    const struct buf *now)
{
    if (now->n == n && !memcmp(now->p, s, n))
        return;
    buf_free(&e->part);
    buf_put(&e->part, s, n);
    e->changed = 1;
}

/* info_part: a part that is only information (core.xml, app.xml,
 * custom.xml, meta.xml) becomes its empty form */
static void info_part(struct entry *e, struct buf *now, const char *empty)
{
    char d[104];
    long n;
    int i;

    n = now->n;
    while (n > 0 && (now->p[n - 1] == ' ' || now->p[n - 1] == '\t' ||
        now->p[n - 1] == '\n' || now->p[n - 1] == '\r'))
    {
        n--;
    }
    if (n == (long)strlen(empty) && !memcmp(now->p, empty, n))
        return;     /* clean already (with random bytes at the end) */
    d[0] = 0;
    for (i = 0; core_words[i].tag; i++) {
        if (xml_find(now->p, now->n, 0, core_words[i].tag) >= 0 &&
            !strstr(d, core_words[i].what))
        {
            n = strlen(d);
            if (n + strlen(core_words[i].what) + 3 > 100)
                break;
            if (n)
                strcat(d, ", ");
            strcat(d, core_words[i].what);
        }
    }
    note2("document information", d);
    set_part(e, (const unsigned char *)empty, strlen(empty), now);
}

static const char empty_core[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
    "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/"
    "package/2006/metadata/core-properties\" xmlns:dc=\"http://purl.org/"
    "dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" "
    "xmlns:dcmitype=\"http://purl.org/dc/dcmitype/\" xmlns:xsi=\"http://"
    "www.w3.org/2001/XMLSchema-instance\"></cp:coreProperties>";

static const char empty_app[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
    "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/"
    "2006/extended-properties\" xmlns:vt=\"http://schemas.openxmlformats."
    "org/officeDocument/2006/docPropsVTypes\"></Properties>";

static const char empty_custom[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
    "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/"
    "2006/custom-properties\" xmlns:vt=\"http://schemas.openxmlformats."
    "org/officeDocument/2006/docPropsVTypes\"></Properties>";

/* ODF's meta.xml keeps its office:version, which must match the rest */
static void odf_meta(struct entry *e, struct buf *now)
{
    struct buf s;
    long v, k;

    memset(&s, 0, sizeof s);
    buf_str(&s, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<office:document-meta xmlns:office=\"urn:oasis:names:tc:"
        "opendocument:xmlns:office:1.0\" xmlns:meta=\"urn:oasis:names:tc:"
        "opendocument:xmlns:meta:1.0\" xmlns:dc=\"http://purl.org/dc/"
        "elements/1.1/\"");
    v = xml_find(now->p, now->n, 0, "office:version=\"");
    if (v >= 0) {
        k = v + 16;
        while (k < now->n && k < v + 40 && now->p[k] != '"')
            k++;
        buf_byte(&s, ' ');
        buf_put(&s, now->p + v, k + 1 - v);
    }
    buf_str(&s, "><office:meta/></office:document-meta>");
    buf_byte(&s, 0);
    info_part(e, now, (const char *)s.p);
    buf_free(&s);
}

/* odf_printer: settings.xml names the printer and keeps its driver's
 * settings (with the printer's name and place) */
static void odf_printer(struct entry *e, struct buf *now)
{
    static const char *items[] = {
        "config:name=\"PrinterName\"", "config:name=\"PrinterSetup\"", 0
    };
    struct buf s;
    long i, a, b;
    int k, found;

    memset(&s, 0, sizeof s);
    found = 0;
    i = 0;
    while (i < now->n) {
        a = -1;
        for (k = 0; items[k]; k++) {
            b = xml_find(now->p, now->n, i, items[k]);
            if (b >= 0 && (a < 0 || b < a))
                a = b;
        }
        if (a < 0)
            break;
        b = xml_find(now->p, now->n, a, ">");
        if (b < 0)
            break;
        if (now->p[b - 1] == '/') {     /* empty already */
            buf_put(&s, now->p + i, b + 1 - i);
            i = b + 1;
            continue;
        }
        a = xml_find(now->p, now->n, b, "</config:config-item");
        if (a < 0)
            break;
        buf_put(&s, now->p + i, b + 1 - i);
        if (a > b + 1)
            found = 1;
        i = a;
    }
    buf_put(&s, now->p + i, now->n - i);
    if (found) {
        note("printer");
        set_part(e, s.p, s.n, now);
    }
    buf_free(&s);
}

/* picture: a picture inside, cleaned as clean cleans pictures */
static void picture(struct entry *e, struct buf *now)
{
    struct buf out;
    const unsigned char *p;
    int rc;

    memset(&out, 0, sizeof out);
    p = now->p;
    rc = -1;
    if (now->n < 12)
        rc = -1;
    else if (p[0] == 0xff && p[1] == 0xd8 && p[2] == 0xff)
        rc = clean_jpeg(p, now->n, &out);
    else if (!memcmp(p, "\211PNG\r\n\032\n", 8))
        rc = clean_png(p, now->n, &out);
    else if (!memcmp(p, "GIF8", 4))
        rc = clean_gif(p, now->n, &out);
    else if (!memcmp(p, "RIFF", 4) && !memcmp(p + 8, "WEBP", 4))
        rc = clean_webp(p, now->n, &out);
    if (rc == 0)
        set_part(e, out.p, out.n, now);
    buf_free(&out);
}

static void thumbnail(struct entry *e, struct buf *now)
{
    if (now->n >= 3 && now->p[0] == 0xff && now->p[1] == 0xd8) {
        if (now->n != (long)sizeof tiny_jpeg ||
            memcmp(now->p, tiny_jpeg, now->n))
        {
            note("thumbnail");
            set_part(e, tiny_jpeg, sizeof tiny_jpeg, now);
        }
    } else if (now->n >= 8 && !memcmp(now->p, "\211PNG", 4)) {
        if (now->n != (long)sizeof tiny_png ||
            memcmp(now->p, tiny_png, now->n))
        {
            note("thumbnail");
            set_part(e, tiny_png, sizeof tiny_png, now);
        }
    }
}

static int is_picture_name(struct entry *e)
{
    static const char *ext[] = {
        ".jpg", ".jpeg", ".JPG", ".JPEG", ".png", ".PNG", ".gif", ".GIF",
        ".webp", ".WEBP", 0
    };
    int i;

    for (i = 0; ext[i]; i++) {
        if (name_ends(e, ext[i]))
            return 1;
    }
    return 0;
}

static void edit(struct entry *e, struct buf *now, const struct xrule *r)
{
    struct buf out;
    int rc;

    memset(&out, 0, sizeof out);
    rc = xml_edit(r, now->p, now->n, &out);
    if (rc == 0)
        set_part(e, out.p, out.n, now);
    buf_free(&out);
}

/* part: what to do with one entry; -1 when it can't be read */
static int part(struct entry *e, int type)
{
    struct buf now;
    const struct xrule *r;
    int rc, info;

    r = 0;
    info = 0;
    if (type == z_ooxml) {
        if (name_is(e, "docProps/core.xml") ||
            name_is(e, "docProps/app.xml") ||
            name_is(e, "docProps/custom.xml"))
        {
            info = 1;
        } else if (name_starts(e, "docProps/thumbnail")) {
            info = 2;
        } else if (name_ends(e, ".rels")) {
            r = rels_rules;
        } else if (name_starts(e, "word/") && name_ends(e, ".xml")) {
            r = word_rules;
        } else if (name_is(e, "xl/workbook.xml")) {
            r = xl_rules;
        } else if ((name_starts(e, "xl/comments") ||
            name_starts(e, "xl/threadedComments") ||
            name_starts(e, "xl/persons/")) && name_ends(e, ".xml"))
        {
            r = xl_people;
        } else if ((name_is(e, "ppt/commentAuthors.xml") ||
            name_starts(e, "ppt/authors") ||
            name_starts(e, "ppt/comments/")) && name_ends(e, ".xml"))
        {
            r = ppt_people;
        }
    } else if (type == z_odf) {
        if (name_is(e, "meta.xml"))
            info = 3;
        else if (name_starts(e, "Thumbnails/"))
            info = 2;
        else if (name_is(e, "settings.xml"))
            info = 4;
        else if (name_ends(e, ".xml"))
            r = odf_rules;
    } else if (type == z_epub) {
        if (name_ends(e, ".opf"))
            r = epub_rules;
    }
    if (!r && !info && !is_picture_name(e))
        return 0;
    memset(&now, 0, sizeof now);
    rc = unpack(e, &now);
    if (rc < 0) {
        buf_free(&now);
        return -1;
    }
    if (info == 1 && name_is(e, "docProps/core.xml"))
        info_part(e, &now, empty_core);
    else if (info == 1 && name_is(e, "docProps/app.xml"))
        info_part(e, &now, empty_app);
    else if (info == 1)
        info_part(e, &now, empty_custom);
    else if (info == 2)
        thumbnail(e, &now);
    else if (info == 3)
        odf_meta(e, &now);
    else if (info == 4)
        odf_printer(e, &now);
    else if (r)
        edit(e, &now, r);
    else
        picture(e, &now);
    buf_free(&now);
    return 0;
}

/* epub_date: EPUB 3 needs its change date; it becomes 2000-01-01 */
static void epub_date(struct entry *e)
{
    static const char fixed[] = "2000-01-01T00:00:00Z";
    struct buf now, s;
    long a, b;
    int rc;

    memset(&now, 0, sizeof now);
    memset(&s, 0, sizeof s);
    if (e->changed) {
        buf_put(&now, e->part.p, e->part.n);
    } else {
        rc = unpack(e, &now);
        if (rc < 0) {
            buf_free(&now);
            return;
        }
    }
    a = xml_find(now.p, now.n, 0, "property=\"dcterms:modified\"");
    if (a >= 0)
        a = xml_find(now.p, now.n, a, ">");
    b = a < 0 ? -1 : xml_find(now.p, now.n, a, "<");
    if (b > a + 1 && (b - a - 1 != (long)strlen(fixed) ||
        memcmp(now.p + a + 1, fixed, b - a - 1)))
    {
        note("dates");
        buf_put(&s, now.p, a + 1);
        buf_str(&s, fixed);
        buf_put(&s, now.p + b, now.n - b);
        buf_free(&e->part);
        e->part = s;
        e->changed = 1;
    } else {
        buf_free(&s);
    }
    buf_free(&now);
}

/* ------------------------------------------------------------ the zip */

static void put16(struct buf *b, long v)
{
    buf_byte(b, (int)(v & 0xff));
    buf_byte(b, (int)(v >> 8 & 0xff));
}

static void put32(struct buf *b, unsigned long v)
{
    put16(b, (long)(v & 0xffff));
    put16(b, (long)(v >> 16 & 0xffff));
}

static void free_entries(struct entry *es, long n)
{
    long i;

    for (i = 0; i < n; i++)
        buf_free(&es[i].part);
    free(es);
}

/* rand_tail: 32 random spaces, tabs and line ends after the end of an
 * XML part (XML allows any there; the old ones go first) */
static void rand_tail(struct entry *e)
{
    static const char white[3] = { ' ', '\t', '\n' };
    struct buf now;
    unsigned char r[32];
    long n;
    int k, rc;

    memset(&now, 0, sizeof now);
    if (e->changed) {
        buf_put(&now, e->part.p, e->part.n);
    } else {
        rc = unpack(e, &now);
        if (rc < 0) {
            buf_free(&now);
            return;
        }
    }
    n = now.n;
    while (n > 0 && (now.p[n - 1] == ' ' || now.p[n - 1] == '\t' ||
        now.p[n - 1] == '\n' || now.p[n - 1] == '\r'))
    {
        n--;
    }
    buf_free(&e->part);
    buf_put(&e->part, now.p, n);
    rand_bytes(r, 32);
    for (k = 0; k < 32; k++)
        buf_byte(&e->part, white[r[k] % 3]);
    e->changed = 1;
    buf_free(&now);
}

/* write: the clean zip; times 1980-01-01, no extra fields, no
 * comments, made on "MS-DOS", parts that changed packed again */
static void write_zip(struct entry *es, long n, struct buf *out,
    const unsigned char *comment, long clen)
{
    struct buf cd, packed;
    unsigned long *offs;
    const unsigned char *data;
    long i, size, cdoff;
    int method;

    memset(&cd, 0, sizeof cd);
    offs = malloc((n + 1) * sizeof *offs);
    if (!offs)
        return;
    for (i = 0; i < n; i++) {
        memset(&packed, 0, sizeof packed);
        if (es[i].changed) {
            es[i].usize = es[i].part.n;
            es[i].crc = zip_crc(es[i].part.p, es[i].part.n);
            deflate_raw(es[i].part.p, es[i].part.n, &packed);
            if (packed.n < es[i].part.n && !name_is(&es[i], "mimetype")) {
                es[i].method = 8;
                data = packed.p;
                size = packed.n;
            } else {
                es[i].method = 0;
                data = es[i].part.p;
                size = es[i].part.n;
            }
            es[i].csize = size;
        } else {
            data = es[i].data;
            size = es[i].csize;
        }
        method = es[i].method;
        offs[i] = (unsigned long)out->n;
        put32(out, 0x04034b50UL);
        put16(out, method == 8 ? 20 : 10);
        put16(out, es[i].flags & 0x0800);
        put16(out, method);
        put16(out, 0);
        put16(out, DOS_DATE);
        put32(out, es[i].crc);
        put32(out, (unsigned long)es[i].csize);
        put32(out, (unsigned long)es[i].usize);
        put16(out, es[i].nlen);
        put16(out, 0);
        buf_put(out, es[i].name, es[i].nlen);
        buf_put(out, data, size);
        buf_free(&packed);
    }
    cdoff = out->n;
    for (i = 0; i < n; i++) {
        put32(&cd, 0x02014b50UL);
        put16(&cd, 20);             /* made on MS-DOS, zip 2.0 */
        put16(&cd, es[i].method == 8 ? 20 : 10);
        put16(&cd, es[i].flags & 0x0800);
        put16(&cd, es[i].method);
        put16(&cd, 0);
        put16(&cd, DOS_DATE);
        put32(&cd, es[i].crc);
        put32(&cd, (unsigned long)es[i].csize);
        put32(&cd, (unsigned long)es[i].usize);
        put16(&cd, es[i].nlen);
        put16(&cd, 0);
        put16(&cd, 0);
        put16(&cd, 0);
        put16(&cd, 0);
        /* a directory is marked as one, the MS-DOS way */
        put32(&cd, es[i].nlen && es[i].name[es[i].nlen - 1] == '/' ?
            0x10UL : 0UL);
        put32(&cd, offs[i]);
        buf_put(&cd, es[i].name, es[i].nlen);
    }
    buf_put(out, cd.p, cd.n);
    put32(out, 0x06054b50UL);
    put16(out, 0);
    put16(out, 0);
    put16(out, n);
    put16(out, n);
    put32(out, (unsigned long)cd.n);
    put32(out, (unsigned long)cdoff);
    put16(out, clen);
    buf_put(out, comment, clen);
    buf_free(&cd);
    free(offs);
}

static int doc_type(struct entry *es, long n)
{
    struct buf m;
    long i;
    int t, rc;

    t = z_plain;
    for (i = 0; i < n; i++) {
        if (name_is(&es[i], "[Content_Types].xml"))
            return z_ooxml;
        if (!name_is(&es[i], "mimetype") || es[i].usize > 200)
            continue;
        memset(&m, 0, sizeof m);
        rc = unpack(&es[i], &m);
        if (rc == 0 && m.n >= 20 &&
            !memcmp(m.p, "application/epub+zip", 20))
        {
            t = z_epub;
        } else if (rc == 0 && m.n >= 35 &&
            !memcmp(m.p, "application/vnd.oasis.opendocument.", 35))
        {
            t = z_odf;
        }
        buf_free(&m);
    }
    return t;
}

/* zip_walk: mode 0 cleans, 1 only varies the bytes the format lets
 * vary (the end of the information part, or the zip's comment) */
static int zip_walk(const unsigned char *in, long n, struct buf *out,
    int mode)
{
    static const char white[3] = { ' ', '\t', '\n' };
    struct entry *es;
    const unsigned char *c, *l;
    unsigned char r[32], com[32];
    long eocd, count, cdoff, cdsize, i, pos, xlen, clen, lo, vary;
    int type, rc, k, seen;

    seen = 0;
    eocd = -1;
    for (i = n - 22; i >= 0 && i >= n - 22 - 65535; i--) {
        if (in[i] == 'P' && !memcmp(in + i, "PK\005\006", 4)) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        why = "a broken zip file";
        return -1;
    }
    count = get16le(in + eocd + 10);
    cdsize = get32le(in + eocd + 12);
    cdoff = get32le(in + eocd + 16);
    clen = get16le(in + eocd + 20);
    if (count == 0xffff || cdoff == 0xffffffffL || cdsize == 0xffffffffL) {
        why = "zip64 files are not supported yet";
        return -1;
    }
    if (cdoff < 0 || cdsize < 0 || cdoff + cdsize > eocd ||
        count > MAX_ENTRIES)
    {
        why = "a broken zip file";
        return -1;
    }
    if (mode == 0 && eocd + 22 + clen < n)
        note("hidden data after the zip");
    for (i = 0; mode == 0 && i < clen && eocd + 22 + i < n; i++) {
        k = in[eocd + 22 + i];
        if (k != ' ' && k != '\t' && k != '\n') {
            note("comment");
            break;
        }
    }
    es = malloc((count + 1) * sizeof *es);
    if (!es)
        return -1;
    memset(es, 0, (count + 1) * sizeof *es);
    pos = cdoff;
    all_unpacked = 0;
    for (i = 0; i < count; i++) {
        c = in + pos;
        if (pos + 46 > cdoff + cdsize || memcmp(c, "PK\001\002", 4))
            goto broken;
        es[i].flags = (int)get16le(c + 8);
        es[i].method = (int)get16le(c + 10);
        es[i].crc = (unsigned long)get32le(c + 16) & 0xffffffffUL;
        es[i].csize = get32le(c + 20);
        es[i].usize = get32le(c + 24);
        es[i].nlen = get16le(c + 28);
        xlen = get16le(c + 30);
        lo = get32le(c + 42);
        es[i].name = c + 46;
        if (es[i].flags & 1) {
            why = "an encrypted zip file";
            goto quit;
        }
        if (pos + 46 + es[i].nlen + xlen + get16le(c + 32) >
            cdoff + cdsize || es[i].csize < 0 || es[i].usize < 0 ||
            lo < 0 || lo + 30 > cdoff)
        {
            goto broken;
        }
        if (get16le(c + 12) || get16le(c + 14) != DOS_DATE)
            seen |= 1;
        if (xlen || get32le(c + 38) > 0x10 || (get16le(c + 4) >> 8) != 0)
            seen |= 2;
        if (get16le(c + 32))
            seen |= 4;
        l = in + lo;
        if (memcmp(l, "PK\003\004", 4))
            goto broken;
        es[i].data = l + 30 + get16le(l + 26) + get16le(l + 28);
        if (es[i].data + es[i].csize > in + cdoff)
            goto broken;
        if (get16le(l + 28))
            seen |= 2;
        pos += 46 + es[i].nlen + xlen + get16le(c + 32);
    }
    if (mode == 0 && (seen & 1))
        note("file times");
    if (mode == 0 && (seen & 2))
        note("file system details (owner IDs, times, system)");
    if (mode == 0 && (seen & 4))
        note("comments");
    type = doc_type(es, count);
    vary = -1;
    for (i = 0; i < count; i++) {
        if (mode == 0) {
            rc = part(&es[i], type);
            if (rc < 0) {
                why = "a zip file with a broken part";
                goto quit;
            }
            if (type == z_epub && name_ends(&es[i], ".opf"))
                epub_date(&es[i]);
        }
        if (vary < 0 && (name_is(&es[i], "docProps/core.xml") ||
            (type == z_odf && name_is(&es[i], "meta.xml")) ||
            (type == z_epub && name_ends(&es[i], ".opf"))))
        {
            vary = i;
        }
    }
    clen = 0;
    if (mode == 1 && vary >= 0) {
        rand_tail(&es[vary]);
    } else if (mode == 1) {
        rand_bytes(r, 32);
        for (k = 0; k < 32; k++)
            com[k] = (unsigned char)white[r[k] % 3];
        clen = 32;
    }
    write_zip(es, count, out, com, clen);
    free_entries(es, count);
    return 0;
broken:
    why = "a broken zip file";
quit:
    free_entries(es, count);
    return -1;
}

int clean_zip(const unsigned char *in, long n, struct buf *out)
{
    return zip_walk(in, n, out, 0);
}

int rand_zip(const unsigned char *in, long n, struct buf *out)
{
    return zip_walk(in, n, out, 1);
}
