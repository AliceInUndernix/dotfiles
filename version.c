#include <string.h>
#include "kspkan.h"

static int epoch_of(const char **sp)
{
    const char *s;
    int e;

    s = *sp;
    e = 0;
    while (is_digit(*s)) {
        e = e * 10 + (*s - '0');
        s++;
    }
    if (s != *sp && *s == ':') {
        *sp = s + 1;
        return e;
    }
    return 0;
}

static int cmp_digits(const char **ap, const char **bp)
{
    const char *a, *b, *ae, *be;
    int la, lb, r;

    a = *ap;
    b = *bp;
    while (*a == '0') {
        a++;
    }
    while (*b == '0') {
        b++;
    }
    ae = a;
    while (is_digit(*ae)) {
        ae++;
    }
    be = b;
    while (is_digit(*be)) {
        be++;
    }
    la = (int)(ae - a);
    lb = (int)(be - b);
    r = 0;
    if (la != lb) {
        r = la < lb ? -1 : 1;
    } else if (la > 0) {
        r = memcmp(a, b, la);
    }
    *ap = ae;
    *bp = be;
    return r;
}

static int cmp_text(const char **ap, const char **bp)
{
    const char *a, *b;
    int ca, cb;

    a = *ap;
    b = *bp;
    while ((*a && !is_digit(*a)) || (*b && !is_digit(*b))) {
        ca = (*a && !is_digit(*a)) ? (unsigned char)*a : 0;
        cb = (*b && !is_digit(*b)) ? (unsigned char)*b : 0;
        if (ca != cb) {
            *ap = a;
            *bp = b;
            return ca < cb ? -1 : 1;
        }
        a++;
        b++;
    }
    *ap = a;
    *bp = b;
    return 0;
}

int version_compare(const char *a, const char *b)
{
    int ea, eb, r;

    ea = epoch_of(&a);
    eb = epoch_of(&b);
    if (ea != eb) {
        return ea < eb ? -1 : 1;
    }
    while (*a || *b) {
        r = cmp_text(&a, &b);
        if (r != 0) {
            return r;
        }
        r = cmp_digits(&a, &b);
        if (r != 0) {
            return r < 0 ? -1 : 1;
        }
    }
    return 0;
}
