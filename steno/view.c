/* steno -T, -S, -TS: the paper tape and the suggestions, each in its
 * own box, as btop draws them, or one of the two alone. They read the
 * engine's feeds (tools.c); t and s show or hide each box while it
 * runs, and a hidden box's feed is closed, so the engine stops
 * looking for suggestions nobody sees.
 *
 * On a terminal it takes the whole screen; into a pipe it just writes
 * the lines. What it shows lives only in its memory. */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include "steno.h"

#define FEED_LINE 4096
#define KEEP_LINES 400         /* kept for each box */
#define CELL_MAX 512           /* bytes of one kept line */

struct box {
    const char *name;          /* the feed, and the title */
    int fd;
    int shown;
    char got[FEED_LINE * 2];   /* read but no newline yet */
    int ngot;
    char lines[KEEP_LINES][CELL_MAX];
    int nlines;                /* the newest is lines[nlines - 1] */
};

static struct box boxes[2];    /* 0 tape, 1 suggestions */
static int fullscreen;
static volatile int resized, stop_view;

static const char tape_keys[] = "#STKPWHRAO*EUFRPBLGTSDZ";

/* --- the feeds ------------------------------------------------------- */

static int feed_show(struct box *b)
{
    const char *path;
    int r;

    path = feed_path(b->name);
    r = mkfifo(path, 0600);
    if (r < 0 && errno != EEXIST) {
        say("can't make %s", path);
        return -1;
    }
    /* read and write: it stays open when the engine restarts */
    b->fd = open(path, O_RDWR | O_NONBLOCK);
    if (b->fd < 0) {
        say("can't open %s", path);
        return -1;
    }
    b->shown = 1;
    b->ngot = 0;
    return 0;
}

static void feed_hide(struct box *b)
{
    if (b->fd >= 0)
        close(b->fd);
    b->fd = -1;
    b->shown = 0;
}

static void keep(struct box *b, const char *s)
{
    if (b->nlines == KEEP_LINES) {
        memmove(b->lines[0], b->lines[1], (KEEP_LINES - 1) * CELL_MAX);
        b->nlines--;
    }
    scopy(b->lines[b->nlines], s, CELL_MAX);
    b->nlines++;
}

/* one line of the tape feed: STROKE <tab> TRANSLATION, kept as the
 * stroke in the tape's columns, the stroke as written, the
 * translation, with tabs between */
static void got_tape(char *line)
{
    char cell[CELL_MAX], *tr;
    long st;
    int i;

    tr = strchr(line, '\t');
    if (tr) {
        *tr = 0;
        tr++;
    } else {
        tr = "";
    }
    st = stroke_parse(line, strlen(line));
    if (st < 0) {
        scopy(cell, line, NUM_KEYS + 1);
    } else {
        for (i = 0; i < NUM_KEYS; i++)
            cell[i] = st & (1L << i) ? tape_keys[i] : ' ';
        cell[NUM_KEYS] = 0;
    }
    if (!fullscreen)
        printf("%s  %s\n", cell, tr);
    scat(cell, "\t", sizeof(cell));
    scat(cell, line, sizeof(cell));
    scat(cell, "\t", sizeof(cell));
    scat(cell, tr, sizeof(cell));
    keep(&boxes[0], cell);
}

/* one line of the suggest feed: WORDS <tab> OUTLINES ..., kept as one
 * line for each WORDS <tab> OUTLINES */
static void got_suggest(char *line)
{
    char cell[CELL_MAX], *w, *o, *next;

    for (w = line; w && *w; w = next) {
        o = strchr(w, '\t');
        if (!o)
            break;
        *o = 0;
        o++;
        next = strchr(o, '\t');
        if (next) {
            *next = 0;
            next++;
        }
        scopy(cell, w, sizeof(cell));
        scat(cell, "\t", sizeof(cell));
        scat(cell, o, sizeof(cell));
        keep(&boxes[1], cell);
        if (!fullscreen)
            printf("%s%s  %s\n", boxes[0].shown ? "= " : "", w, o);
    }
}

static void feed_read(struct box *b)
{
    char *nl;
    int n, used;

    n = read(b->fd, b->got + b->ngot, sizeof(b->got) - 1 - b->ngot);
    if (n <= 0)
        return;
    b->ngot += n;
    b->got[b->ngot] = 0;
    while ((nl = strchr(b->got, '\n')) != 0) {
        *nl = 0;
        if (b == &boxes[0])
            got_tape(b->got);
        else
            got_suggest(b->got);
        used = (int)(nl + 1 - b->got);
        memmove(b->got, nl + 1, b->ngot - used + 1);
        b->ngot -= used;
    }
    if (b->ngot == (int)sizeof(b->got) - 1)
        b->ngot = 0;           /* no newline in a full buffer: drop */
}

/* the inside of the tape box: the newest strokes at the bottom */
static void draw_tape(int y, int x, int w, int h)
{
    struct box *b;
    char *tr, *st;
    int i, row, n, cols;

    b = &boxes[0];
    i = b->nlines - h;
    if (i < 0)
        i = 0;
    for (row = 0; row < h; row++, i++) {
        scr_move(y + row, x);
        n = 0;
        if (i < b->nlines) {
            tr = strchr(b->lines[i], '\t');
            cols = (int)(tr - b->lines[i]);
            scr_col(C_TEXT);
            st = tr + 1;
            tr = strchr(st, '\t');
            if (w >= NUM_KEYS + 12) {
                scr_put_n(b->lines[i], cols);
                n = cols;
            } else {
                /* too narrow for the columns: the stroke as written */
                *tr = 0;
                n = scr_text(st, w);
                *tr = '\t';
            }
            if (n + 2 < w) {
                scr_put("  ");
                n += 2;
                scr_col(C_TEAL);
                n += scr_text(tr + 1, w - n);
            }
        }
        scr_fill(' ', w - n);
    }
}

/* the suggestions: each words line, and under it the outlines,
 * wrapped; from the top, like the tape, the newest last */
static int sugg_rows(const char *cell, int w, char out[][CELL_MAX],
    int max)
{
    char o[CELL_MAX], one[CELL_MAX], *tok, *t;
    int n, used, k;

    n = 0;
    t = strchr(cell, '\t');
    if (!t)
        return 0;
    scopy(out[n], cell, (int)(t - cell) + 1);
    n++;
    scopy(o, t + 1, sizeof(o));
    out[n][0] = 0;
    used = 0;
    for (tok = o; *tok && n < max; tok = t) {
        while (*tok == ' ')
            tok++;
        if (!*tok)
            break;
        t = tok;
        while (*t && *t != ' ')
            t++;
        k = (int)(t - tok);
        if (used && used + 2 + k > w - 2) {
            n++;
            if (n == max)
                break;
            out[n][0] = 0;
            used = 0;
        }
        scat(out[n], "  ", CELL_MAX);
        if (k > CELL_MAX - 8)
            k = CELL_MAX - 8;
        memcpy(one, tok, k);
        one[k] = 0;
        scat(out[n], one, CELL_MAX);
        used += 2 + k;
    }
    if (n < max && out[n][0])
        n++;
    return n;
}

static void draw_sugg(int y, int x, int w, int h)
{
    static char rows[64][CELL_MAX];
    struct box *b;
    int i, j, n, row, filled, first;

    b = &boxes[1];
    /* from the newest back, until the box is full */
    filled = 0;
    first = b->nlines;
    while (first > 0) {
        n = sugg_rows(b->lines[first - 1], w, rows, 64);
        if (filled + n > h && filled)
            break;             /* the newest shows, even cut off */
        filled += n;
        first--;
    }
    row = 0;
    for (i = first; i < b->nlines; i++) {
        n = sugg_rows(b->lines[i], w, rows, 64);
        for (j = 0; j < n && row < h; j++, row++) {
            scr_move(y + row, x);
            scr_col(j ? C_BLUE : C_TEXT);
            scr_fill(' ', w - scr_text(rows[j], w));
        }
    }
    for (; row < h; row++) {
        scr_move(y + row, x);
        scr_fill(' ', w);
    }
}

static void draw_box(int which, int y, int x, int w, int h, int last)
{
    static const char *const keys_all[] = {
        "quit", "clear", "tape", "suggestions"
    };
    int row;

    if (w < 8 || h < 3)
        return;
    scr_border(y, x, w, 1, which ? "suggestions" : "tape", 0, 0);
    for (row = 1; row < h - 1; row++)
        scr_side(y + row, x, w);
    if (which)
        draw_sugg(y + 1, x + 1, w - 2, h - 2);
    else
        draw_tape(y + 1, x + 1, w - 2, h - 2);
    scr_border(y + h - 1, x, w, 0, 0, last ? keys_all : 0, last ? 4 : 0);
}

static void draw(void)
{
    int rows, cols, wt;

    scr_size(&rows, &cols);
    scr_begin();
    if (boxes[0].shown && boxes[1].shown) {
        if (cols >= 60) {
            /* side by side: the tape wide enough for its columns */
            wt = cols / 2;
            if (wt < NUM_KEYS + 16)
                wt = NUM_KEYS + 16;
            draw_box(0, 0, 0, wt, rows, 0);
            draw_box(1, 0, wt, cols - wt, rows, 1);
        } else {
            draw_box(0, 0, 0, cols, rows / 2, 0);
            draw_box(1, rows / 2, 0, cols, rows - rows / 2, 1);
        }
    } else {
        draw_box(boxes[1].shown, 0, 0, cols, rows, 1);
    }
    scr_end();
}

/* --- the loop ------------------------------------------------------- */

static void on_resize(int sig)
{
    (void)sig;
    resized = 1;
}

static void on_stop(int sig)
{
    (void)sig;
    stop_view = 1;
}

static void toggle(int which)
{
    struct box *b, *other;

    b = &boxes[which];
    other = &boxes[!which];
    if (b->shown) {
        if (other->shown)
            feed_hide(b);      /* never both hidden */
    } else {
        feed_show(b);
    }
}

int view_main(int tape, int sugg)
{
    struct pollfd pf[3];
    struct termios old, raw;
    struct sigaction sa;
    int i, n, r, tty, ix[2];
    unsigned char c;

    boxes[0].name = "tape";
    boxes[1].name = "suggest";
    boxes[0].fd = -1;
    boxes[1].fd = -1;
    r = tape ? feed_show(&boxes[0]) : 0;
    if (r < 0)
        return 1;
    r = sugg ? feed_show(&boxes[1]) : 0;
    if (r < 0)
        return 1;
    fullscreen = isatty(1);
    scr_colour = fullscreen &&
        (!getenv("NO_COLOR") || !getenv("NO_COLOR")[0]);
    tty = 0;
    if (fullscreen && isatty(0)) {
        r = tcgetattr(0, &old);
        tty = r == 0;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_resize;
    sigaction(SIGWINCH, &sa, 0);
    sa.sa_handler = on_stop;
    sigaction(SIGINT, &sa, 0);
    sigaction(SIGTERM, &sa, 0);
    sigaction(SIGHUP, &sa, 0);
    if (tty) {
        raw = old;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &raw);
    }
    if (fullscreen) {
        /* the other screen, no cursor; given back at the end */
        scr_open();
        draw();
    }
    while (!stop_view) {
        n = 0;
        for (i = 0; i < 2; i++) {
            ix[i] = -1;
            if (boxes[i].fd >= 0) {
                pf[n].fd = boxes[i].fd;
                pf[n].events = POLLIN;
                ix[i] = n;
                n++;
            }
        }
        if (tty) {
            pf[n].fd = 0;
            pf[n].events = POLLIN;
            n++;
        }
        r = poll(pf, n, -1);
        if (r < 0 && errno != EINTR)
            break;
        if (r > 0 && tty && pf[n - 1].revents) {
            c = 0;
            r = read(0, &c, 1);
            if (r <= 0 || c == 'q' || c == 4)
                break;
            if (c == 'c') {
                boxes[0].nlines = 0;
                boxes[1].nlines = 0;
            } else if (c == 't') {
                toggle(0);
            } else if (c == 's') {
                toggle(1);
            }
            resized = 1;       /* draw again, also for Ctrl+L */
        }
        for (i = 0; i < 2; i++) {
            if (ix[i] >= 0 && boxes[i].fd >= 0 &&
                pf[ix[i]].revents & POLLIN)
            {
                feed_read(&boxes[i]);
                resized = 1;
            }
        }
        if (fullscreen && resized) {
            resized = 0;
            draw();
        } else if (!fullscreen) {
            fflush(stdout);
        }
    }
    if (fullscreen)
        scr_close();
    if (tty)
        tcsetattr(0, TCSANOW, &old);
    return 0;
}
