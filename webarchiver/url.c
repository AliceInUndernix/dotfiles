/* URLs and paths, handled as bytes (any page encoding passes through) */
#include <stdlib.h>
#include <string.h>

#include "webarchiver.h"

struct url {
    char *scheme;   /* 0 if absent */
    char *netloc;   /* 0 if absent */
    char *path;
    char *query;    /* 0 if absent */
    char *frag;     /* 0 if absent */
};

static int is_alpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int is_digit(int c)
{
    return c >= '0' && c <= '9';
}

int url_has_scheme(const char *s)
{
    if (!is_alpha((unsigned char)*s))
        return 0;
    for (s++; *s; s++) {
        if (*s == ':')
            return 1;
        if (!is_alpha((unsigned char)*s) && !is_digit((unsigned char)*s) &&
            *s != '+' && *s != '-' && *s != '.')
        {
            return 0;
        }
    }
    return 0;
}

int url_is_http(const char *s)
{
    return str_ieq_n(s, "http:", 5) || str_ieq_n(s, "https:", 6);
}

static void url_split(const char *s, struct url *u)
{
    const char *p, *end;

    u->scheme = 0;
    u->netloc = 0;
    u->query = 0;
    u->frag = 0;
    p = strchr(s, '#');
    if (p) {
        u->frag = xstrdup(p + 1);
        end = p;
    } else {
        end = s + strlen(s);
    }
    if (url_has_scheme(s)) {
        p = strchr(s, ':');
        u->scheme = xstrndup(s, p - s);
        s = p + 1;
    }
    if (s[0] == '/' && s[1] == '/') {
        s += 2;
        p = s;
        while (p < end && *p != '/' && *p != '?')
            p++;
        u->netloc = xstrndup(s, p - s);
        s = p;
    }
    p = s;
    while (p < end && *p != '?')
        p++;
    u->path = xstrndup(s, p - s);
    if (p < end)
        u->query = xstrndup(p + 1, end - p - 1);
}

static void url_free(struct url *u)
{
    free(u->scheme);
    free(u->netloc);
    free(u->path);
    free(u->query);
    free(u->frag);
}

static char *url_unsplit(struct url *u)
{
    struct buf b;

    buf_init(&b);
    if (u->scheme) {
        buf_add(&b, u->scheme);
        buf_addc(&b, ':');
    }
    if (u->netloc) {
        buf_add(&b, "//");
        buf_add(&b, u->netloc);
    }
    buf_add(&b, u->path);
    if (u->query) {
        buf_addc(&b, '?');
        buf_add(&b, u->query);
    }
    if (u->frag) {
        buf_addc(&b, '#');
        buf_add(&b, u->frag);
    }
    return b.s;
}

/* remove "." and ".." segments (RFC 3986, section 5.2.4) */
static char *remove_dots(const char *path)
{
    struct strlist seg;
    struct buf b;
    const char *p, *start;
    int i, absolute, trailing;

    absolute = path[0] == '/';
    trailing = ends_with(path, "/") || ends_with(path, "/.") ||
        ends_with(path, "/..") || !strcmp(path, ".") ||
        !strcmp(path, "..");
    sl_init(&seg);
    p = path;
    while (*p) {
        while (*p == '/')
            p++;
        start = p;
        while (*p && *p != '/')
            p++;
        if (p == start)
            break;
        if (p - start == 1 && start[0] == '.')
            continue;
        if (p - start == 2 && start[0] == '.' && start[1] == '.') {
            if (seg.n > 0) {
                seg.n--;
                free(seg.v[seg.n]);
            }
            continue;
        }
        {
            char *part;

            part = xstrndup(start, p - start);
            sl_add(&seg, part);
            free(part);
        }
    }
    buf_init(&b);
    if (absolute)
        buf_addc(&b, '/');
    for (i = 0; i < seg.n; i++) {
        if (i > 0)
            buf_addc(&b, '/');
        buf_add(&b, seg.v[i]);
    }
    if (trailing && seg.n > 0)
        buf_addc(&b, '/');
    sl_free(&seg);
    return b.s;
}

/* resolve ref against base (RFC 3986, section 5.2.2) */
char *url_join(const char *base, const char *ref)
{
    struct url b, r, t;
    char *res, *merged, *slash;

    url_split(base, &b);
    url_split(ref, &r);
    t.frag = r.frag ? xstrdup(r.frag) : 0;
    if (r.scheme) {
        t.scheme = xstrdup(r.scheme);
        t.netloc = r.netloc ? xstrdup(r.netloc) : 0;
        t.path = remove_dots(r.path);
        t.query = r.query ? xstrdup(r.query) : 0;
    } else {
        t.scheme = b.scheme ? xstrdup(b.scheme) : 0;
        if (r.netloc) {
            t.netloc = xstrdup(r.netloc);
            t.path = remove_dots(r.path);
            t.query = r.query ? xstrdup(r.query) : 0;
        } else {
            t.netloc = b.netloc ? xstrdup(b.netloc) : 0;
            if (!r.path[0]) {
                t.path = xstrdup(b.path);
                t.query = r.query ? xstrdup(r.query) :
                    (b.query ? xstrdup(b.query) : 0);
            } else {
                if (r.path[0] == '/') {
                    t.path = remove_dots(r.path);
                } else {
                    slash = strrchr(b.path, '/');
                    if (b.netloc && !b.path[0]) {
                        merged = str2("/", r.path);
                    } else if (slash) {
                        char *dir;

                        dir = xstrndup(b.path, slash - b.path + 1);
                        merged = str2(dir, r.path);
                        free(dir);
                    } else {
                        merged = xstrdup(r.path);
                    }
                    t.path = remove_dots(merged);
                    free(merged);
                }
                t.query = r.query ? xstrdup(r.query) : 0;
            }
        }
    }
    res = url_unsplit(&t);
    url_free(&t);
    url_free(&b);
    url_free(&r);
    return res;
}

/* "Example.org:8080" from "http://user@Example.org:8080/x", lower case */
char *url_host(const char *s)
{
    struct url u;
    char *host, *at, *p;

    url_split(s, &u);
    host = xstrdup(u.netloc ? u.netloc : "");
    url_free(&u);
    at = strrchr(host, '@');
    if (at)
        memmove(host, at + 1, strlen(at + 1) + 1);
    for (p = host; *p; p++)
        *p = lower((unsigned char)*p);
    return host;
}

char *url_path(const char *s)
{
    struct url u;
    char *res;

    url_split(s, &u);
    res = xstrdup(u.path);
    url_free(&u);
    return res;
}

char *url_query(const char *s)
{
    struct url u;
    char *res;

    url_split(s, &u);
    res = u.query ? xstrdup(u.query) : 0;
    url_free(&u);
    return res;
}

char *url_origin(const char *s)
{
    struct url u;
    struct buf b;

    url_split(s, &u);
    buf_init(&b);
    buf_add(&b, u.scheme ? u.scheme : "https");
    buf_add(&b, "://");
    buf_add(&b, u.netloc ? u.netloc : "");
    url_free(&u);
    return b.s;
}

/* folder name for a host: lower case, ':' -> '_' */
char *host_dir(const char *host)
{
    char *d, *p;

    d = xstrdup(host);
    for (p = d; *p; p++) {
        if (*p == ':')
            *p = '_';
        else
            *p = lower((unsigned char)*p);
    }
    return d;
}

/* percent-encode every byte that is not a letter, digit or in safe */
char *pct_quote(const char *s, const char *safe)
{
    static const char hex[] = "0123456789ABCDEF";
    struct buf b;
    unsigned char c;

    buf_init(&b);
    for (; *s; s++) {
        c = (unsigned char)*s;
        if (is_alpha(c) || is_digit(c) || (c < 128 && strchr(safe, c))) {
            buf_addc(&b, c);
        } else {
            buf_addc(&b, '%');
            buf_addc(&b, hex[c >> 4]);
            buf_addc(&b, hex[c & 15]);
        }
    }
    return b.s;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

char *pct_unquote(const char *s)
{
    struct buf b;
    int hi, lo;

    buf_init(&b);
    while (*s) {
        hi = s[0] == '%' && s[1] ? hexval((unsigned char)s[1]) : -1;
        lo = hi >= 0 && s[2] ? hexval((unsigned char)s[2]) : -1;
        if (hi >= 0 && lo >= 0) {
            buf_addc(&b, hi * 16 + lo);
            s += 3;
        } else {
            buf_addc(&b, *s);
            s++;
        }
    }
    return b.s;
}

static void add_utf8(struct buf *b, long cp)
{
    if (cp < 0x80) {
        buf_addc(b, (int)cp);
    } else if (cp < 0x800) {
        buf_addc(b, 0xc0 | (int)(cp >> 6));
        buf_addc(b, 0x80 | (int)(cp & 0x3f));
    } else if (cp < 0x10000) {
        buf_addc(b, 0xe0 | (int)(cp >> 12));
        buf_addc(b, 0x80 | (int)((cp >> 6) & 0x3f));
        buf_addc(b, 0x80 | (int)(cp & 0x3f));
    } else {
        buf_addc(b, 0xf0 | (int)(cp >> 18));
        buf_addc(b, 0x80 | (int)((cp >> 12) & 0x3f));
        buf_addc(b, 0x80 | (int)((cp >> 6) & 0x3f));
        buf_addc(b, 0x80 | (int)(cp & 0x3f));
    }
}

/* the character references that occur in links: &amp; &#38; &#x26; ... */
char *html_unescape(const char *s)
{
    static const char *name[] = { "amp;", "lt;", "gt;", "quot;", "apos;",
        "#39;" };
    static const char value[] = "&<>\"''";
    struct buf b;
    long cp;
    int i, n, digits, base, v;

    buf_init(&b);
    while (*s) {
        if (*s != '&') {
            buf_addc(&b, *s);
            s++;
            continue;
        }
        for (i = 0; i < 6; i++) {
            n = strlen(name[i]);
            if (!strncmp(s + 1, name[i], n))
                break;
        }
        if (i < 6) {
            buf_addc(&b, value[i]);
            s += 1 + n;
            continue;
        }
        if (s[1] == '#') {
            base = s[2] == 'x' || s[2] == 'X' ? 16 : 10;
            n = base == 16 ? 3 : 2;
            cp = 0;
            digits = 0;
            while ((v = hexval((unsigned char)s[n])) >= 0 && v < base &&
                digits < 7)
            {
                cp = cp * base + v;
                n++;
                digits++;
            }
            if (digits > 0 && cp > 0 && cp < 0x110000) {
                add_utf8(&b, cp);
                s += n + (s[n] == ';');
                continue;
            }
        }
        buf_addc(&b, *s);
        s++;
    }
    return b.s;
}

/* normalise a file path: drop "." and ".." and repeated slashes */
char *norm_path(const char *path)
{
    char *p;
    int n;

    p = remove_dots(path);
    n = strlen(p);
    if (n > 1 && p[n - 1] == '/')
        p[n - 1] = 0;
    if (!p[0]) {
        free(p);
        p = xstrdup(".");
    }
    return p;
}

/* path of target relative to directory start (both absolute) */
char *rel_path(const char *target, const char *start)
{
    struct buf b;
    const char *t, *s;
    int common, i;

    t = target;
    s = start;
    common = 0;
    /* longest common prefix that ends at a path separator */
    for (i = 0; t[i] && t[i] == s[i]; i++) {
        if (t[i] == '/')
            common = i;
    }
    if (!s[i] && (t[i] == '/' || !t[i]))
        common = i;
    buf_init(&b);
    for (s = start + common; *s; s++) {
        if (*s == '/')
            buf_add(&b, b.len ? "/.." : "..");
    }
    t = target + common;
    if (*t == '/')
        t++;
    if (*t) {
        if (b.len)
            buf_addc(&b, '/');
        buf_add(&b, t);
    }
    if (!b.len)
        buf_addc(&b, '.');
    return b.s;
}
