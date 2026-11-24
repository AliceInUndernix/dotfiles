/* term.c: a small terminal for the editor in the right pane.
 *
 * The editor (Neovim or Vim) runs on a pty with TERM=st-256color, and
 * what it writes is kept here as a grid of cells, which the UI draws
 * into its right box. Only what st's terminfo uses is understood
 * (cursor moves, erasing, scrolling regions, inserting and deleting,
 * colours and attributes, the other screen); everything else is read
 * and ignored. One process, no threads: the UI polls the pty. */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "seal.h"
#include "diary.h"

enum { st_ground, st_esc, st_csi, st_osc, st_osc_esc, st_str, st_str_esc,
    st_charset };

/* ---------------------------------------------------------- cells */

static void blank(struct term *t, struct cell *c)
{
    c->ch[0] = ' ';
    c->ch[1] = 0;
    c->dec = 0;
    c->attr = 0;
    c->fg = -1;
    c->bg = t->cur.bg;
}

static struct cell *at(struct term *t, int y, int x)
{
    return t->cells + (long)y * t->cols + x;
}

static void clear_region(struct term *t, int y0, int x0, int y1, int x1)
{
    int y, x;

    if (y0 < 0)
        y0 = 0;
    if (y1 >= t->rows)
        y1 = t->rows - 1;
    if (x0 < 0)
        x0 = 0;
    if (x1 >= t->cols)
        x1 = t->cols - 1;
    for (y = y0; y <= y1; y++) {
        for (x = x0; x <= x1; x++)
            blank(t, at(t, y, x));
    }
}

/* lines top..bot move up by n (n < 0: down) */
static void scroll_up(struct term *t, int top, int bot, int n)
{
    long w;
    int y;

    w = (long)t->cols * sizeof *t->cells;
    if (n > 0) {
        if (n > bot - top + 1)
            n = bot - top + 1;
        for (y = top; y + n <= bot; y++)
            memcpy(at(t, y, 0), at(t, y + n, 0), w);
        clear_region(t, bot - n + 1, 0, bot, t->cols - 1);
    } else if (n < 0) {
        n = -n;
        if (n > bot - top + 1)
            n = bot - top + 1;
        for (y = bot; y - n >= top; y--)
            memcpy(at(t, y, 0), at(t, y - n, 0), w);
        clear_region(t, top, 0, top + n - 1, t->cols - 1);
    }
}

static void move_to(struct term *t, int y, int x)
{
    int lo, hi;

    lo = t->origin ? t->top : 0;
    hi = t->origin ? t->bot : t->rows - 1;
    if (t->origin)
        y += t->top;
    if (y < lo)
        y = lo;
    if (y > hi)
        y = hi;
    if (x < 0)
        x = 0;
    if (x >= t->cols)
        x = t->cols - 1;
    t->cx = x;
    t->cy = y;
    t->wrapnext = 0;
}

static void newline(struct term *t)
{
    if (t->cy == t->bot)
        scroll_up(t, t->top, t->bot, 1);
    else if (t->cy < t->rows - 1)
        t->cy++;
}

static const char *dec_map(int c)
{
    /* DEC special graphics, as UTF-8 for the cell (drawn back as DEC
     * line drawing by the UI when it is one of those) */
    switch (c) {
    case 'j': case 'k': case 'l': case 'm': case 'n': case 'q':
    case 't': case 'u': case 'v': case 'w': case 'x':
        return 0;
    case '`': return "*";
    case 'a': return ":";
    case 'f': return "o";
    case 'g': return "+";
    case '~': return ".";
    default: return "";
    }
}

static void put_char(struct term *t, const char *s, int dec)
{
    struct cell *c;
    int x;

    if (t->wrapnext && t->autowrap) {
        t->cx = 0;
        newline(t);
    }
    t->wrapnext = 0;
    if (t->insert && t->cx < t->cols - 1) {
        c = at(t, t->cy, t->cx);
        memmove(c + 1, c, (t->cols - t->cx - 1) * sizeof *c);
    }
    c = at(t, t->cy, t->cx);
    *c = t->cur;
    strcpy(c->ch, s);
    c->dec = (char)dec;
    strcpy(t->lastch, s);
    x = t->cx + 1;
    if (x >= t->cols)
        t->wrapnext = 1;
    else
        t->cx = x;
}

/* ------------------------------------------------------ the parser */

static int par(struct term *t, int i, int def)
{
    if (i >= t->npar || t->par[i] <= 0)
        return def;
    return t->par[i];
}

static void answer(struct term *t, const char *s)
{
    if (t->fd >= 0)
        write_all(t->fd, s, strlen(s));
}

static void sgr(struct term *t)
{
    int i, p, k;

    if (t->npar == 0) {
        t->npar = 1;
        t->par[0] = 0;
    }
    for (i = 0; i < t->npar; i++) {
        p = t->par[i];
        if (p == 0) {
            t->cur.attr = 0;
            t->cur.fg = -1;
            t->cur.bg = -1;
        } else if (p == 1) {
            t->cur.attr |= AT_BOLD;
        } else if (p == 2) {
            t->cur.attr |= AT_DIM;
        } else if (p == 3) {
            t->cur.attr |= AT_ITALIC;
        } else if (p == 4) {
            t->cur.attr |= AT_UNDER;
        } else if (p == 7) {
            t->cur.attr |= AT_REV;
        } else if (p == 9) {
            t->cur.attr |= AT_STRIKE;
        } else if (p == 22) {
            t->cur.attr &= ~(AT_BOLD | AT_DIM);
        } else if (p == 23) {
            t->cur.attr &= ~AT_ITALIC;
        } else if (p == 24) {
            t->cur.attr &= ~AT_UNDER;
        } else if (p == 27) {
            t->cur.attr &= ~AT_REV;
        } else if (p == 29) {
            t->cur.attr &= ~AT_STRIKE;
        } else if (p >= 30 && p <= 37) {
            t->cur.fg = p - 30;
        } else if (p >= 40 && p <= 47) {
            t->cur.bg = p - 40;
        } else if (p >= 90 && p <= 97) {
            t->cur.fg = p - 90 + 8;
        } else if (p >= 100 && p <= 107) {
            t->cur.bg = p - 100 + 8;
        } else if (p == 39) {
            t->cur.fg = -1;
        } else if (p == 49) {
            t->cur.bg = -1;
        } else if ((p == 38 || p == 48 || p == 58) && i + 1 < t->npar) {
            k = -2;
            if (t->par[i + 1] == 5 && i + 2 < t->npar) {
                k = t->par[i + 2] & 255;
                i += 2;
            } else if (t->par[i + 1] == 2 && i + 4 < t->npar) {
                k = RGB(((t->par[i + 2] & 255) << 16) |
                    ((t->par[i + 3] & 255) << 8) | (t->par[i + 4] & 255));
                i += 4;
            }
            if (p == 38 && k != -2)
                t->cur.fg = k;
            else if (p == 48 && k != -2)
                t->cur.bg = k;
        }
    }
}

static void swap_screen(struct term *t, int to_alt)
{
    struct cell *c;

    if (to_alt == t->altscreen)
        return;
    c = t->cells;
    t->cells = t->alt;
    t->alt = c;
    t->altscreen = to_alt;
    if (to_alt)
        clear_region(t, 0, 0, t->rows - 1, t->cols - 1);
}

static void set_mode(struct term *t, int on)
{
    int i, p;

    for (i = 0; i < t->npar; i++) {
        p = t->par[i];
        if (t->priv == '?') {
            if (p == 25) {
                t->cursor_on = on;
            } else if (p == 7) {
                t->autowrap = on;
            } else if (p == 6) {
                t->origin = on;
                move_to(t, 0, 0);
            } else if (p == 1049 || p == 1047 || p == 47) {
                if (p == 1049 && on) {
                    t->sx = t->cx;
                    t->sy = t->cy;
                }
                swap_screen(t, on);
                if (p == 1049 && !on)
                    move_to(t, t->sy, t->sx);
            } else if (p == 1048) {
                if (on) {
                    t->sx = t->cx;
                    t->sy = t->cy;
                } else {
                    move_to(t, t->sy, t->sx);
                }
            }
        } else if (!t->priv && p == 4) {
            t->insert = on;
        }
    }
}

static void csi(struct term *t, int f)
{
    char buf[64];
    struct cell *c;
    int n, i, y;

    if (t->inter && f != 'q' && f != 'p')
        return;
    if (t->priv == '?' && f != 'h' && f != 'l')
        return;
    if (t->priv == '>' || t->priv == '=') {
        if (f == 'c' && t->priv == '>')
            answer(t, "\033[>0;0;0c");
        return;
    }
    switch (f) {
    case '@':
        n = par(t, 0, 1);
        c = at(t, t->cy, t->cx);
        if (n > t->cols - t->cx)
            n = t->cols - t->cx;
        memmove(c + n, c, (t->cols - t->cx - n) * sizeof *c);
        clear_region(t, t->cy, t->cx, t->cy, t->cx + n - 1);
        break;
    case 'A':
        move_to(t, t->cy - par(t, 0, 1) - (t->origin ? t->top : 0),
            t->cx);
        break;
    case 'B':
    case 'e':
        move_to(t, t->cy + par(t, 0, 1) - (t->origin ? t->top : 0),
            t->cx);
        break;
    case 'C':
    case 'a':
        move_to(t, t->cy - (t->origin ? t->top : 0), t->cx + par(t, 0, 1));
        break;
    case 'D':
        move_to(t, t->cy - (t->origin ? t->top : 0), t->cx - par(t, 0, 1));
        break;
    case 'E':
        move_to(t, t->cy + par(t, 0, 1) - (t->origin ? t->top : 0), 0);
        break;
    case 'F':
        move_to(t, t->cy - par(t, 0, 1) - (t->origin ? t->top : 0), 0);
        break;
    case 'G':
    case '`':
        move_to(t, t->cy - (t->origin ? t->top : 0), par(t, 0, 1) - 1);
        break;
    case 'H':
    case 'f':
        move_to(t, par(t, 0, 1) - 1, par(t, 1, 1) - 1);
        break;
    case 'd':
        move_to(t, par(t, 0, 1) - 1, t->cx);
        break;
    case 'J':
        n = t->npar ? t->par[0] : 0;
        if (n == 0) {
            clear_region(t, t->cy, t->cx, t->cy, t->cols - 1);
            clear_region(t, t->cy + 1, 0, t->rows - 1, t->cols - 1);
        } else if (n == 1) {
            clear_region(t, 0, 0, t->cy - 1, t->cols - 1);
            clear_region(t, t->cy, 0, t->cy, t->cx);
        } else if (n == 2 || n == 3) {
            clear_region(t, 0, 0, t->rows - 1, t->cols - 1);
        }
        break;
    case 'K':
        n = t->npar ? t->par[0] : 0;
        if (n == 0)
            clear_region(t, t->cy, t->cx, t->cy, t->cols - 1);
        else if (n == 1)
            clear_region(t, t->cy, 0, t->cy, t->cx);
        else if (n == 2)
            clear_region(t, t->cy, 0, t->cy, t->cols - 1);
        break;
    case 'L':
        if (t->cy >= t->top && t->cy <= t->bot)
            scroll_up(t, t->cy, t->bot, -par(t, 0, 1));
        break;
    case 'M':
        if (t->cy >= t->top && t->cy <= t->bot)
            scroll_up(t, t->cy, t->bot, par(t, 0, 1));
        break;
    case 'P':
        n = par(t, 0, 1);
        c = at(t, t->cy, t->cx);
        if (n > t->cols - t->cx)
            n = t->cols - t->cx;
        memmove(c, c + n, (t->cols - t->cx - n) * sizeof *c);
        clear_region(t, t->cy, t->cols - n, t->cy, t->cols - 1);
        break;
    case 'X':
        clear_region(t, t->cy, t->cx, t->cy, t->cx + par(t, 0, 1) - 1);
        break;
    case 'S':
        scroll_up(t, t->top, t->bot, par(t, 0, 1));
        break;
    case 'T':
        scroll_up(t, t->top, t->bot, -par(t, 0, 1));
        break;
    case 'b':
        n = par(t, 0, 1);
        if (n > t->cols * t->rows)
            n = t->cols * t->rows;
        for (i = 0; i < n && t->lastch[0]; i++)
            put_char(t, t->lastch, 0);
        break;
    case 'm':
        sgr(t);
        break;
    case 'r':
        n = par(t, 0, 1) - 1;
        y = par(t, 1, t->rows) - 1;
        if (y >= t->rows)
            y = t->rows - 1;
        if (n < y) {
            t->top = n;
            t->bot = y;
            move_to(t, 0, 0);
        }
        break;
    case 's':
        t->sx = t->cx;
        t->sy = t->cy;
        break;
    case 'u':
        move_to(t, t->sy - (t->origin ? t->top : 0), t->sx);
        break;
    case 'h':
        set_mode(t, 1);
        break;
    case 'l':
        set_mode(t, 0);
        break;
    case 'n':
        n = t->npar ? t->par[0] : 0;
        if (n == 6) {
            sprintf(buf, "\033[%d;%dR", t->cy + 1, t->cx + 1);
            answer(t, buf);
        } else if (n == 5) {
            answer(t, "\033[0n");
        }
        break;
    case 'c':
        if (!t->npar || t->par[0] == 0)
            answer(t, "\033[?6c");
        break;
    default:
        break;
    }
}

static void osc_done(struct term *t)
{
    t->osc[t->nosc] = 0;
    /* the editor asks for the colours to pick light or dark: tell
     * it Habamax's */
    if (!strcmp(t->osc, "11;?"))
        answer(t, "\033]11;rgb:1c1c/1c1c/1c1c\033\\");
    else if (!strcmp(t->osc, "10;?"))
        answer(t, "\033]10;rgb:c7c7/c7c7/c7c7\033\\");
}

static void esc_final(struct term *t, int c)
{
    t->state = st_ground;
    switch (c) {
    case '[':
        t->state = st_csi;
        t->npar = 0;
        t->par[0] = 0;
        t->priv = 0;
        t->inter = 0;
        break;
    case ']':
        t->state = st_osc;
        t->nosc = 0;
        break;
    case 'P': case '_': case '^': case 'X':
        t->state = st_str;
        break;
    case '(': case ')': case '*': case '+':
        t->state = st_charset;
        t->inter = c;
        break;
    case '7':
        t->sx = t->cx;
        t->sy = t->cy;
        t->sattr = t->cur;
        break;
    case '8':
        t->cx = t->sx;
        t->cy = t->sy;
        t->cur = t->sattr;
        t->wrapnext = 0;
        break;
    case 'D':
        newline(t);
        break;
    case 'E':
        t->cx = 0;
        newline(t);
        break;
    case 'M':
        if (t->cy == t->top)
            scroll_up(t, t->top, t->bot, -1);
        else if (t->cy > 0)
            t->cy--;
        break;
    case 'c':
        t->cur.attr = 0;
        t->cur.fg = -1;
        t->cur.bg = -1;
        t->top = 0;
        t->bot = t->rows - 1;
        clear_region(t, 0, 0, t->rows - 1, t->cols - 1);
        move_to(t, 0, 0);
        break;
    default:
        break;
    }
}

static void ctrl(struct term *t, int c)
{
    switch (c) {
    case '\b':
        if (t->cx > 0)
            t->cx--;
        t->wrapnext = 0;
        break;
    case '\t':
        t->cx = (t->cx / 8 + 1) * 8;
        if (t->cx >= t->cols)
            t->cx = t->cols - 1;
        break;
    case '\n': case '\v': case '\f':
        newline(t);
        t->wrapnext = 0;
        break;
    case '\r':
        t->cx = 0;
        t->wrapnext = 0;
        break;
    case 14:
        t->shift = 1;
        break;
    case 15:
        t->shift = 0;
        break;
    case 27:
        t->state = st_esc;
        break;
    default:
        break;
    }
}

static void printable(struct term *t, int c)
{
    const char *m;
    char s[2];
    int dec;

    dec = t->shift ? t->g1dec : t->g0dec;
    s[0] = (char)c;
    s[1] = 0;
    if (dec) {
        m = dec_map(c);
        if (!m) {
            put_char(t, s, 1);
            return;
        }
        if (m[0]) {
            put_char(t, m, 0);
            return;
        }
    }
    put_char(t, s, 0);
}

static void byte(struct term *t, int c)
{
    switch (t->state) {
    case st_esc:
        esc_final(t, c);
        return;
    case st_charset:
        if (t->inter == '(')
            t->g0dec = c == '0';
        else if (t->inter == ')')
            t->g1dec = c == '0';
        t->inter = 0;
        t->state = st_ground;
        return;
    case st_csi:
        if (c >= '0' && c <= '9') {
            if (t->npar == 0)
                t->npar = 1;
            if (t->par[t->npar - 1] < 100000)
                t->par[t->npar - 1] = t->par[t->npar - 1] * 10 + c - '0';
        } else if (c == ';' || c == ':') {
            if (t->npar == 0)
                t->npar = 1;
            if (t->npar < 16) {
                t->par[t->npar] = 0;
                t->npar++;
            }
        } else if (c >= '<' && c <= '?') {
            t->priv = c;
        } else if (c >= ' ' && c <= '/') {
            t->inter = c;
        } else if (c >= '@' && c <= '~') {
            t->state = st_ground;
            csi(t, c);
        } else if (c == 27) {
            t->state = st_esc;
        } else if (c < ' ') {
            ctrl(t, c);
        }
        return;
    case st_osc:
        if (c == 7) {
            osc_done(t);
            t->state = st_ground;
        } else if (c == 27) {
            t->state = st_osc_esc;
        } else if (t->nosc < (int)sizeof t->osc - 1) {
            t->osc[t->nosc] = (char)c;
            t->nosc++;
        }
        return;
    case st_osc_esc:
        osc_done(t);
        t->state = st_ground;
        if (c != '\\')
            byte(t, c);
        return;
    case st_str:
        if (c == 27)
            t->state = st_str_esc;
        else if (c == 7)
            t->state = st_ground;
        return;
    case st_str_esc:
        t->state = c == '\\' ? st_ground : st_str;
        return;
    default:
        break;
    }
    /* ground: UTF-8 is put together into one cell per character */
    if (t->need > 0) {
        if ((c & 0xc0) == 0x80) {
            t->utf[t->nutf] = (char)c;
            t->nutf++;
            t->need--;
            if (t->need == 0) {
                t->utf[t->nutf] = 0;
                put_char(t, t->utf, 0);
            }
            return;
        }
        t->need = 0;
        put_char(t, "?", 0);
    }
    if (c >= 0xc2 && c <= 0xf4) {
        t->utf[0] = (char)c;
        t->nutf = 1;
        t->need = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;
        return;
    }
    if (c >= 0x80) {
        put_char(t, "?", 0);
        return;
    }
    if (c < ' ' || c == 127) {
        ctrl(t, c);
        return;
    }
    printable(t, c);
}

void term_feed(struct term *t, const char *buf, long n)
{
    long i;

    for (i = 0; i < n; i++)
        byte(t, (unsigned char)buf[i]);
}

/* -------------------------------------------------------- the pty */

static int alloc_cells(struct term *t, int rows, int cols)
{
    long n, i;

    n = (long)rows * cols;
    t->cells = malloc(n * sizeof *t->cells);
    t->alt = malloc(n * sizeof *t->alt);
    if (!t->cells || !t->alt)
        return -1;
    t->rows = rows;
    t->cols = cols;
    for (i = 0; i < n; i++) {
        blank(t, t->cells + i);
        blank(t, t->alt + i);
    }
    return 0;
}

static void set_size(struct term *t)
{
    struct winsize ws;

    if (t->fd < 0)
        return;
    memset(&ws, 0, sizeof ws);
    ws.ws_row = (unsigned short)t->rows;
    ws.ws_col = (unsigned short)t->cols;
    ioctl(t->fd, TIOCSWINSZ, &ws);
}

/* run argv on a new pty in dir; 0 when it started */
int term_start(struct term *t, int rows, int cols, char **argv,
    const char *dir)
{
    char *name;
    pid_t pid;
    int m, s, r;

    memset(t, 0, sizeof *t);
    t->fd = -1;
    t->cur.fg = -1;
    t->cur.bg = -1;
    r = alloc_cells(t, rows, cols);
    if (r < 0)
        return -1;
    t->bot = rows - 1;
    t->cursor_on = 1;
    t->autowrap = 1;
    m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0)
        return -1;
    r = grantpt(m);
    if (r == 0)
        r = unlockpt(m);
    name = r == 0 ? ptsname(m) : 0;
    if (!name) {
        close(m);
        return -1;
    }
    t->fd = m;
    set_size(t);
    pid = fork();
    if (pid < 0) {
        close(m);
        t->fd = -1;
        return -1;
    }
    if (pid == 0) {
        setsid();
        s = open(name, O_RDWR);
        if (s < 0)
            _exit(127);
        ioctl(s, TIOCSCTTY, 0);
        dup2(s, 0);
        dup2(s, 1);
        dup2(s, 2);
        if (s > 2)
            close(s);
        close(m);
        r = chdir(dir);
        if (r < 0)
            _exit(127);
        setenv("TERM", "st-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        signal(SIGPIPE, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGHUP, SIG_DFL);
        execvp(argv[0], argv);
        _exit(127);
    }
    fcntl(m, F_SETFL, fcntl(m, F_GETFL) | O_NONBLOCK);
    fcntl(m, F_SETFD, FD_CLOEXEC);
    t->pid = (int)pid;
    return 0;
}

void term_resize(struct term *t, int rows, int cols)
{
    struct cell *oc, *oa;
    int orows, ocols, y, w;

    if (rows == t->rows && cols == t->cols)
        return;
    oc = t->cells;
    oa = t->alt;
    orows = t->rows;
    ocols = t->cols;
    if (alloc_cells(t, rows, cols) < 0) {
        t->cells = oc;
        t->alt = oa;
        return;
    }
    w = cols < ocols ? cols : ocols;
    for (y = 0; y < rows && y < orows; y++) {
        memcpy(t->cells + (long)y * cols, oc + (long)y * ocols,
            w * sizeof *oc);
        memcpy(t->alt + (long)y * cols, oa + (long)y * ocols,
            w * sizeof *oa);
    }
    free(oc);
    free(oa);
    t->top = 0;
    t->bot = rows - 1;
    if (t->cx >= cols)
        t->cx = cols - 1;
    if (t->cy >= rows)
        t->cy = rows - 1;
    t->wrapnext = 0;
    set_size(t);
}

void term_free(struct term *t)
{
    if (t->fd >= 0)
        close(t->fd);
    t->fd = -1;
    free(t->cells);
    free(t->alt);
    t->cells = 0;
    t->alt = 0;
    t->pid = 0;
}
