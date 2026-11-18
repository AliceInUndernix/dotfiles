/* snaked - the snake game playing itself on the X desktop (see README)
 *
 * It paints the snake on the root window in clear colours, and puts
 * the board in the root window's _SNAKE property at every step, so
 * that other programs (st and slock, patched) draw it in their own
 * empty background: then it seems to run behind them. No compositor,
 * no transparency. There are two pairs of colours: faint ones for
 * behind text (st), clear ones where nothing covers it (the desktop,
 * slock).
 *
 * _SNAKE is a STRING (format 8): one line of text
 *
 *     snake2 SEQ CELL X Y W H SNAKE FOOD CSNAKE CFOOD
 *
 * (SEQ counts the steps; CELL is a cell's size in pixels; X Y where
 * the board's top left corner is on the screen; W H the board in
 * cells; SNAKE FOOD the faint colours and CSNAKE CFOOD the clear ones,
 * as RRGGBB), a newline, then W * H bytes, one per cell, row by row:
 * '0' empty, '1' snake, '2' food. */

#define _XOPEN_SOURCE 600

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/select.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "game.h"

#define MAX_CHANGED 8
#define WON_PAUSE 3000  /* ms to show a full board before a new game */

/* in two, as C89 only promises strings of 509 characters */
static const char *usage_text[2] = {
    "usage: snaked [-b RRGGBB] [-c PX] [-f|-F|-s|-S RRGGBB] [-t MS]\n"
    "  -b RRGGBB  background (default 1c1c1c, Habamax)\n"
    "  -c PX      size of a cell in pixels (default 16)\n"
    "  -s RRGGBB  snake behind text, faint (default 242424)\n"
    "  -f RRGGBB  food behind text, faint (default 2b2620)\n",
    "  -S RRGGBB  snake on the desktop and the lock screen\n"
    "             (default 87af87, Habamax green)\n"
    "  -F RRGGBB  food on the desktop and the lock screen\n"
    "             (default d787d7, Habamax magenta)\n"
    "  -t MS      milliseconds per step (default 200)\n"
    "stop it with: pkill snaked\n"
};

static unsigned long opt_bg = 0x1c1c1cUL;
static unsigned long opt_snake = 0x242424UL;
static unsigned long opt_food = 0x2b2620UL;
static unsigned long opt_csnake = 0x87af87UL;
static unsigned long opt_cfood = 0xd787d7UL;
static int opt_cell = 16;
static int opt_tick = 200;

static Display *dpy;
static Window root;
static Window owner;          /* holds the _SNAKED selection */
static Atom prop_atom;
static GC gc;
static unsigned long pix_bg, pix_snake, pix_food;
static int scr_w, scr_h;
static int org_x, org_y;      /* the board's top left corner */
static char *prop_buf;        /* the property being written */
static long seq;
static int changed[MAX_CHANGED];
static int nchanged;

static volatile sig_atomic_t got_quit;

static void
die(const char *msg)
{
    fprintf(stderr, "snaked: %s\n", msg);
    exit(1);
}

static long long
now_ms(void)
{
    struct timespec ts;
    long long t;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    t = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    return t;
}

static void
on_signal(int sig)
{
    (void)sig;
    got_quit = 1;
}

static void
set_handler(int sig)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

/* RRGGBB into 0xrrggbb; -1 if it is not that */
static long
parse_rgb(const char *s)
{
    long v;
    int i, c;

    if (s[0] == '#') {
        s++;
    }
    v = 0;
    for (i = 0; i < 6; i++) {
        c = s[i];
        if (c >= '0' && c <= '9') {
            c -= '0';
        } else if (c >= 'a' && c <= 'f') {
            c -= 'a' - 10;
        } else if (c >= 'A' && c <= 'F') {
            c -= 'A' - 10;
        } else {
            return -1;
        }
        v = v * 16 + c;
    }
    return s[6] ? -1 : v;
}

static int
parse_num(const char *s, int min, int max)
{
    int v, i;

    v = 0;
    for (i = 0; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9' || v > max) {
            return -1;
        }
        v = v * 10 + (s[i] - '0');
    }
    return i == 0 || v < min || v > max ? -1 : v;
}

static void
options(int argc, char **argv)
{
    long v;
    int i;

    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-' || !argv[i][1] || argv[i][2]) {
            goto bad;
        }
        if (argv[i][1] == 'h') {
            fputs(usage_text[0], stdout);
            fputs(usage_text[1], stdout);
            exit(0);
        }
        if (i + 1 >= argc) {
            goto bad;
        }
        switch (argv[i][1]) {
        case 'b':
        case 'f':
        case 'F':
        case 's':
        case 'S':
            v = parse_rgb(argv[i + 1]);
            if (v < 0) {
                goto bad;
            }
            if (argv[i][1] == 'b') {
                opt_bg = v;
            } else if (argv[i][1] == 'f') {
                opt_food = v;
            } else if (argv[i][1] == 'F') {
                opt_cfood = v;
            } else if (argv[i][1] == 's') {
                opt_snake = v;
            } else {
                opt_csnake = v;
            }
            break;
        case 'c':
            opt_cell = parse_num(argv[i + 1], 2, 256);
            if (opt_cell < 0) {
                goto bad;
            }
            break;
        case 't':
            opt_tick = parse_num(argv[i + 1], 10, 10000);
            if (opt_tick < 0) {
                goto bad;
            }
            break;
        default:
            goto bad;
        }
        i++;
    }
    return;
bad:
    fputs(usage_text[0], stderr);
    fputs(usage_text[1], stderr);
    exit(2);
}

static unsigned long
alloc_color(unsigned long rgb)
{
    XColor c;

    c.red = (unsigned short)((rgb >> 16 & 0xff) * 257);
    c.green = (unsigned short)((rgb >> 8 & 0xff) * 257);
    c.blue = (unsigned short)((rgb & 0xff) * 257);
    c.flags = DoRed | DoGreen | DoBlue;
    if (!XAllocColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), &c)) {
        return BlackPixel(dpy, DefaultScreen(dpy));
    }
    return c.pixel;
}

/* the front end's part of game.h: note the cells a step changed */
void
game_cell(int c)
{
    if (nchanged < MAX_CHANGED) {
        changed[nchanged] = c;
        nchanged++;
    }
}

static void
paint_cell(int c)
{
    int x, y, m;

    x = org_x + c % board_w * opt_cell;
    y = org_y + c / board_w * opt_cell;
    if (c == food) {
        /* food: a smaller square */
        m = opt_cell / 4;
        XSetForeground(dpy, gc, pix_bg);
        XFillRectangle(dpy, root, gc, x, y, opt_cell, opt_cell);
        XSetForeground(dpy, gc, pix_food);
        XFillRectangle(dpy, root, gc, x + m, y + m, opt_cell - 2 * m,
            opt_cell - 2 * m);
        return;
    }
    XSetForeground(dpy, gc, grid[c] ? pix_snake : pix_bg);
    XFillRectangle(dpy, root, gc, x, y, opt_cell, opt_cell);
}

/* paint the snake and the food in a part of the screen (X has cleared
 * it to the background already) */
static void
paint_area(int x, int y, int w, int h)
{
    int x0, y0, x1, y1, cx, cy, c;

    x0 = (x - org_x) / opt_cell;
    y0 = (y - org_y) / opt_cell;
    x1 = (x + w - org_x + opt_cell - 1) / opt_cell;
    y1 = (y + h - org_y + opt_cell - 1) / opt_cell;
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > board_w) {
        x1 = board_w;
    }
    if (y1 > board_h) {
        y1 = board_h;
    }
    for (cy = y0; cy < y1; cy++) {
        for (cx = x0; cx < x1; cx++) {
            c = cy * board_w + cx;
            if (grid[c] || c == food) {
                paint_cell(c);
            }
        }
    }
}

static void
publish(void)
{
    char *p;
    int n, i;

    seq++;
    n = sprintf(prop_buf, "snake2 %ld %d %d %d %d %d"
        " %06lx %06lx %06lx %06lx\n",
        seq, opt_cell, org_x, org_y, board_w, board_h, opt_snake, opt_food,
        opt_csnake, opt_cfood);
    p = prop_buf + n;
    for (i = 0; i < body_cap; i++) {
        p[i] = grid[i] ? '1' : '0';
    }
    if (food >= 0) {
        p[food] = '2';
    }
    XChangeProperty(dpy, root, prop_atom, XA_STRING, 8, PropModeReplace,
        (unsigned char *)prop_buf, n + body_cap);
}

/* a new game filling the screen */
static void
start(void)
{
    scr_w = DisplayWidth(dpy, DefaultScreen(dpy));
    scr_h = DisplayHeight(dpy, DefaultScreen(dpy));
    if (game_new(scr_w / opt_cell, scr_h / opt_cell) < 0) {
        die("out of memory");
    }
    org_x = (scr_w - board_w * opt_cell) / 2;
    org_y = (scr_h - board_h * opt_cell) / 2;
    free(prop_buf);
    prop_buf = malloc(body_cap + 128);
    if (!prop_buf) {
        die("out of memory");
    }
    nchanged = 0;
    XClearWindow(dpy, root);
    paint_area(0, 0, scr_w, scr_h);
    publish();
    XFlush(dpy);
}

static void
setup(void)
{
    XSetWindowAttributes wa;
    Atom sel;

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        die("cannot open the display");
    }
    root = DefaultRootWindow(dpy);
    /* only one snaked per display: it holds the _SNAKED selection */
    sel = XInternAtom(dpy, "_SNAKED", False);
    if (XGetSelectionOwner(dpy, sel) != None) {
        die("already running");
    }
    wa.override_redirect = True;
    owner = XCreateWindow(dpy, root, -1, -1, 1, 1, 0, 0, InputOnly,
        CopyFromParent, CWOverrideRedirect, &wa);
    XSetSelectionOwner(dpy, sel, owner, CurrentTime);
    if (XGetSelectionOwner(dpy, sel) != owner) {
        die("already running");
    }
    prop_atom = XInternAtom(dpy, "_SNAKE", False);
    pix_bg = alloc_color(opt_bg);
    pix_snake = alloc_color(opt_csnake);
    pix_food = alloc_color(opt_cfood);
    gc = XCreateGC(dpy, root, 0, NULL);
    XSetWindowBackground(dpy, root, pix_bg);
    XSelectInput(dpy, root, ExposureMask | StructureNotifyMask);
}

/* back to a plain background, and no _SNAKE for the others */
static void
cleanup(void)
{
    XDeleteProperty(dpy, root, prop_atom);
    XClearWindow(dpy, root);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, owner);
    XCloseDisplay(dpy);
    game_free();
    free(prop_buf);
}

int
main(int argc, char **argv)
{
    XEvent ev;
    fd_set fds;
    struct timeval tv;
    long long now, next;
    int xfd, r, i, n;

    options(argc, argv);
    set_handler(SIGINT);
    set_handler(SIGTERM);
    set_handler(SIGHUP);
    setup();
    rng_seed();
    ai_on = 1;
    start();
    xfd = ConnectionNumber(dpy);
    next = now_ms() + opt_tick;
    while (!got_quit) {
        while (XPending(dpy)) {
            XNextEvent(dpy, &ev);
            if (ev.type == Expose) {
                paint_area(ev.xexpose.x, ev.xexpose.y, ev.xexpose.width,
                    ev.xexpose.height);
            } else if (ev.type == ConfigureNotify &&
                ev.xconfigure.window == root &&
                (ev.xconfigure.width != scr_w ||
                ev.xconfigure.height != scr_h))
            {
                /* a new screen size (xrandr): a new board */
                start();
            }
        }
        now = now_ms();
        if (now >= next) {
            nchanged = 0;
            r = game_step();
            if (r == step_won || r == step_over) {
                publish();
                XFlush(dpy);
                next = now + WON_PAUSE;
                while (!got_quit && now_ms() < next) {
                    tv.tv_sec = 0;
                    tv.tv_usec = 100000;
                    select(0, NULL, NULL, NULL, &tv);
                }
                start();
            } else {
                for (i = 0; i < nchanged; i++) {
                    paint_cell(changed[i]);
                }
                publish();
            }
            XFlush(dpy);
            next += opt_tick;
            if (next < now) {
                next = now + opt_tick;
            }
            continue;
        }
        FD_ZERO(&fds);
        FD_SET(xfd, &fds);
        tv.tv_sec = (next - now) / 1000;
        tv.tv_usec = (next - now) % 1000 * 1000;
        n = select(xfd + 1, &fds, NULL, NULL, &tv);
        if (n < 0 && errno != EINTR) {
            die("select failed");
        }
    }
    cleanup();
    return 0;
}
