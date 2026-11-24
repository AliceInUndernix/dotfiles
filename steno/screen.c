/* Drawing for the terminal screens (view.c, tui.c): one buffer a
 * frame, written at once; boxes as btop draws them, with DEC line
 * drawing and Habamax colours. */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "steno.h"

#define SCREEN_MAX 262144

int scr_colour;
int scr_title_dim;         /* the next border's title dim */
static char screen[SCREEN_MAX];
static int nscreen;

void scr_write(const char *s, int n)
{
    int r;

    while (n > 0) {
        r = write(1, s, n);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return;
        s += r;
        n -= r;
    }
}

void scr_put(const char *s)
{
    int n;

    n = strlen(s);
    if (nscreen + n >= SCREEN_MAX)
        return;
    memcpy(screen + nscreen, s, n);
    nscreen += n;
}

void scr_put_n(const char *s, int n)
{
    if (nscreen + n >= SCREEN_MAX)
        return;
    memcpy(screen + nscreen, s, n);
    nscreen += n;
}

void scr_col(const char *c)
{
    if (scr_colour)
        scr_put(c);
}

void scr_move(int y, int x)
{
    char s[32];

    sprintf(s, "\033[%d;%dH", y + 1, x + 1);
    scr_put(s);
}

void scr_fill(int c, int n)
{
    char s[2];

    s[0] = (char)c;
    s[1] = 0;
    while (n > 0) {
        scr_put(s);
        n--;
    }
}

/* Text, at most w characters of it; returns the characters written.
 * A character is a whole UTF-8 sequence, one column wide. */
int scr_text(const char *s, int w)
{
    int n, k, len;
    long c;

    n = 0;
    len = strlen(s);
    while (*s && n < w) {
        k = u8get(s, len, &c);
        if (k < 1)
            k = 1;
        scr_put_n(s, k);
        s += k;
        len -= k;
        n++;
    }
    return n;
}

int scr_width(const char *s)
{
    return u8len(s, strlen(s));
}

/* a top border, corner, line, the title between corners turned
 * inward, as btop draws it; or the bottom one with key labels */
void scr_border(int y, int x, int w, int top, const char *title,
    const char *const *keys, int nkeys)
{
    int used, i, n, k, all;
    const char *sp;
    char letters[64];

    scr_move(y, x);
    scr_col(C_BOX);
    scr_put(DEC_ON);
    scr_put(top ? "lq" : "mq");
    used = 2;
    if (title && used + scr_width(title) + 3 <= w) {
        scr_put(top ? "k" : "j");
        scr_put(DEC_OFF);
        scr_col(scr_title_dim ? C_DIM : C_TITLE);
        used += 1 + scr_text(title, w - used - 3);
        scr_col(C_BOX);
        scr_put(DEC_ON);
        scr_put(top ? "l" : "m");
        used++;
    }
    /* no room for every key: only their letters, "qctsl" */
    all = used + 1;
    for (i = 0; i < nkeys; i++)
        all += scr_width(keys[i]) + 3;
    if (nkeys && all > w) {
        /* single letters run together; longer keys (j/k, space)
         * stand apart */
        letters[0] = 0;
        for (i = 0; i < nkeys; i++) {
            sp = strchr(keys[i], ' ');
            k = sp ? (int)(sp - keys[i]) : 1;
            n = strlen(letters);
            if (n + k + 2 >= (int)sizeof(letters))
                break;
            if (n && k > 1 && letters[n - 1] != ' ') {
                letters[n] = ' ';
                n++;
            }
            memcpy(letters + n, keys[i], k);
            letters[n + k] = 0;
            if (k > 1 && i + 1 < nkeys)
                scat(letters, " ", sizeof(letters));
        }
        n = strlen(letters);
        if (used + n + 4 <= w) {
            scr_put("q");
            scr_put(top ? "k" : "j");
            scr_put(DEC_OFF);
            scr_col(C_BLUE);
            scr_put(letters);
            scr_col(C_BOX);
            scr_put(DEC_ON);
            scr_put(top ? "l" : "m");
            used += n + 3;
        }
        nkeys = 0;
    }
    for (i = 0; i < nkeys; i++) {
        n = scr_width(keys[i]);
        if (used + n + 4 > w)
            break;             /* no room: the rest of the keys go */
        scr_put("q");
        scr_put(top ? "k" : "j");
        scr_put(DEC_OFF);
        /* "j/k move": the key part blue; "quit": its first letter */
        sp = strchr(keys[i], ' ');
        k = sp ? (int)(sp - keys[i]) : 1;
        scr_col(C_BLUE);
        scr_put_n(keys[i], k);
        scr_col(C_LABEL);
        scr_put(keys[i] + k);
        scr_col(C_BOX);
        scr_put(DEC_ON);
        scr_put(top ? "l" : "m");
        used += n + 3;
    }
    scr_fill('q', w - used - 1);
    scr_put(top ? "k" : "j");
    scr_put(DEC_OFF);
}

void scr_side(int y, int x, int w)
{
    scr_move(y, x);
    scr_col(C_BOX);
    scr_put(DEC_ON);
    scr_put("x");
    scr_put(DEC_OFF);
    scr_move(y, x + w - 1);
    scr_col(C_BOX);
    scr_put(DEC_ON);
    scr_put("x");
    scr_put(DEC_OFF);
}

void scr_size(int *rows, int *cols)
{
    struct winsize ws;
    int r;

    *rows = 24;
    *cols = 80;
    r = ioctl(1, TIOCGWINSZ, &ws);
    if (r == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    }
}

void scr_begin(void)
{
    nscreen = 0;
    scr_put(C_PLAIN);
    scr_put("\033[H\033[2J");
}

void scr_end(void)
{
    scr_put(C_PLAIN);
    scr_write(screen, nscreen);
}

static void scr_put_raw(const char *s)
{
    scr_write(s, strlen(s));
}

/* the other screen, without the cursor; scr_close gives them back */
void scr_open(void)
{
    scr_put_raw("\033[?1049h\033[?25l");
}

void scr_close(void)
{
    scr_put_raw("\033[0m\033[?25h\033[?1049l");
}

/* the terminal reports the mouse buttons (the wheel) as keys, or not;
 * Shift still selects text in st and xterm while it is on */
void scr_mouse(int on)
{
    scr_put_raw(on ? "\033[?1000h\033[?1006h" : "\033[?1006l\033[?1000l");
}
