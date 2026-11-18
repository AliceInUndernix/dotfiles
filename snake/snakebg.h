/* snakebg.h - draw snaked's snake in a window's own background
 *
 * For programs that show the desktop's snake behind their content (st,
 * slock): snaked puts the board in the root window's _SNAKE property
 * at every step (its format is in snaked.c). This reads it, keeps a
 * copy, finds the cells that changed, and paints the pieces of the
 * snake that fall in a part of the screen. It is only included once,
 * so everything is static. Plain C89, Xlib only; the same file is in
 * snake, st and slock.
 *
 * Use: sb_init once; on a PropertyNotify with sb_event true, call
 * sb_read: 0 nothing to draw, 1 the cells in sb_chg changed, 2 all of
 * it changed (or the snake came or went). Then sb_paint for each part
 * of the window's background as it is drawn. With SB_LISTEN defined
 * there is sb_listen too, to stop and start hearing of it. */

#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#define SB_MAXCHG 32

static Atom sb_atom;
static int sb_on;              /* there is a snake */
static int sb_cell, sb_x, sb_y, sb_w, sb_h;
static unsigned long sb_rgb[5]; /* as 0xrrggbb: [1] snake, [2] food,
                                  * faint (behind text); [3] snake,
                                  * [4] food, clear (nothing over it) */
static char *sb_grid;          /* sb_w * sb_h cells: 0, 1 snake, 2 food */
static int sb_cap;
static int sb_chg[SB_MAXCHG];  /* the cells that changed */
static int sb_nchg;

static void
sb_init(Display *dpy)
{
    XWindowAttributes wa;
    Window root;

    root = DefaultRootWindow(dpy);
    sb_atom = XInternAtom(dpy, "_SNAKE", False);
    XGetWindowAttributes(dpy, root, &wa);
    XSelectInput(dpy, root, wa.your_event_mask | PropertyChangeMask);
}

#ifdef SB_LISTEN
/* stop or start hearing about it (for a window that is not shown) */
static void
sb_listen(Display *dpy, int on)
{
    XWindowAttributes wa;
    Window root;
    long m;

    root = DefaultRootWindow(dpy);
    XGetWindowAttributes(dpy, root, &wa);
    m = wa.your_event_mask;
    m = on ? m | PropertyChangeMask : m & ~PropertyChangeMask;
    XSelectInput(dpy, root, m);
}
#endif

static int
sb_event(XEvent *e)
{
    return e->type == PropertyNotify && e->xproperty.atom == sb_atom;
}

/* a number from *p up to the byte end; -1 if not one in [0, max] */
static long
sb_num(const unsigned char **p, const unsigned char *lim, int end,
    int base, long max)
{
    const unsigned char *s;
    long v;
    int c;

    s = *p;
    v = 0;
    while (s < lim && *s != end) {
        c = *s;
        if (c >= '0' && c <= '9') {
            c -= '0';
        } else if (base == 16 && c >= 'a' && c <= 'f') {
            c -= 'a' - 10;
        } else {
            return -1;
        }
        if (c >= base || v > max) {
            return -1;
        }
        v = v * base + c;
        s++;
    }
    if (s == *p || s >= lim || v > max) {
        return -1;
    }
    *p = s + 1;
    return v;
}

static int
sb_read(Display *dpy)
{
    Atom type;
    int fmt, i, n, all;
    unsigned long len, rest;
    unsigned char *data;
    const unsigned char *p, *lim;
    long v[10];
    static const long max[10] = {
        0x7fffffffL, 256, 65535, 65535, 4096, 4096,
        0xffffff, 0xffffff, 0xffffff, 0xffffff
    };

    data = NULL;
    if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), sb_atom, 0,
        (4096L * 4096 + 128) / 4, False, XA_STRING, &type, &fmt, &len,
        &rest, &data) != Success || type != XA_STRING || fmt != 8 ||
        len < 7 || memcmp(data, "snake2 ", 7) != 0)
    {
        if (data) {
            XFree(data);
        }
        if (!sb_on) {
            return 0;
        }
        sb_on = 0;
        return 2;
    }
    p = data + 7;
    lim = data + len;
    for (i = 0; i < 10; i++) {
        v[i] = sb_num(&p, lim, i < 9 ? ' ' : '\n', i < 6 ? 10 : 16,
            max[i]);
        if (v[i] < 0 || (i >= 1 && i != 2 && i != 3 && i < 6 &&
            v[i] == 0))
        {
            XFree(data);
            return 0;
        }
    }
    n = (int)(v[4] * v[5]);
    if ((unsigned long)(lim - p) != (unsigned long)n) {
        XFree(data);
        return 0;
    }
    all = !sb_on || v[1] != sb_cell || v[2] != sb_x || v[3] != sb_y ||
        v[4] != sb_w || v[5] != sb_h || (unsigned long)v[6] != sb_rgb[1] ||
        (unsigned long)v[7] != sb_rgb[2] ||
        (unsigned long)v[8] != sb_rgb[3] || (unsigned long)v[9] != sb_rgb[4];
    if (n > sb_cap) {
        free(sb_grid);
        sb_grid = (char *)malloc(n);
        sb_cap = sb_grid ? n : 0;
        if (!sb_grid) {
            XFree(data);
            sb_on = 0;
            return 2;
        }
    }
    sb_nchg = 0;
    for (i = 0; i < n; i++) {
        if (p[i] < '0' || p[i] > '2') {
            XFree(data);
            return 0;
        }
    }
    for (i = 0; i < n; i++) {
        if (!all && sb_grid[i] != p[i] - '0') {
            if (sb_nchg < SB_MAXCHG) {
                sb_chg[sb_nchg] = i;
                sb_nchg++;
            } else {
                all = 1;
            }
        }
        sb_grid[i] = p[i] - '0';
    }
    sb_cell = (int)v[1];
    sb_x = (int)v[2];
    sb_y = (int)v[3];
    sb_w = (int)v[4];
    sb_h = (int)v[5];
    sb_rgb[1] = (unsigned long)v[6];
    sb_rgb[2] = (unsigned long)v[7];
    sb_rgb[3] = (unsigned long)v[8];
    sb_rgb[4] = (unsigned long)v[9];
    sb_on = 1;
    XFree(data);
    if (all) {
        return 2;
    }
    return sb_nchg > 0;
}

/* the screen area of cell c (all of it, also for the food) */
static void
sb_cellarea(int c, int *x, int *y, int *w, int *h)
{
    *x = sb_x + c % sb_w * sb_cell;
    *y = sb_y + c / sb_w * sb_cell;
    *w = sb_cell;
    *h = sb_cell;
}

/* Call fill for every piece of snake and food in the screen area x, y,
 * w, h: fill(what, x, y, w, h, arg), what 1 snake or 2 food (sb_rgb[what]
 * faint, sb_rgb[what + 2] clear), the piece
 * cut to the area. */
static void
sb_paint(int x, int y, int w, int h,
    void (*fill)(int, int, int, int, int, void *), void *arg)
{
    int x0, y0, x1, y1, cx, cy, v, px, py, pw, ph, m;

    if (!sb_on || w <= 0 || h <= 0) {
        return;
    }
    x0 = x - sb_x;
    y0 = y - sb_y;
    x1 = x0 + w;
    y1 = y0 + h;
    x0 = x0 < 0 ? 0 : x0 / sb_cell;
    y0 = y0 < 0 ? 0 : y0 / sb_cell;
    x1 = x1 <= 0 ? 0 : (x1 + sb_cell - 1) / sb_cell;
    y1 = y1 <= 0 ? 0 : (y1 + sb_cell - 1) / sb_cell;
    if (x1 > sb_w) {
        x1 = sb_w;
    }
    if (y1 > sb_h) {
        y1 = sb_h;
    }
    for (cy = y0; cy < y1; cy++) {
        for (cx = x0; cx < x1; cx++) {
            v = sb_grid[cy * sb_w + cx];
            if (!v) {
                continue;
            }
            /* the food is a smaller square */
            m = v == 2 ? sb_cell / 4 : 0;
            px = sb_x + cx * sb_cell + m;
            py = sb_y + cy * sb_cell + m;
            pw = sb_cell - 2 * m;
            ph = sb_cell - 2 * m;
            if (px < x) {
                pw -= x - px;
                px = x;
            }
            if (py < y) {
                ph -= y - py;
                py = y;
            }
            if (px + pw > x + w) {
                pw = x + w - px;
            }
            if (py + ph > y + h) {
                ph = y + h - py;
            }
            if (pw > 0 && ph > 0) {
                fill(v, px, py, pw, ph, arg);
            }
        }
    }
}
