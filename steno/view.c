/* steno -T, -S, -TS: the paper tape and the suggestions, each in its
 * own box, as btop draws them, or one of the two alone. They read the
 * engine's feeds (tools.c); t and s show or hide each box while it
 * runs, and a hidden box's feed is closed, so the engine stops
 * looking for suggestions nobody sees.
 *
 * The tape clears the text of strokes that a later stroke turned into
 * one outline with it (HAOEU high, TKRAU draw, then HREUBG makes
 * hydraulic of all three), and brings it back when that is undone.
 * The suggestions keep each words once: typed again, they move to the
 * bottom. Both scroll (j k, arrows, Ctrl+U Ctrl+D, g G, the mouse
 * wheel), Tab choosing which. u: undone strokes and their * are gone
 * (the start), or a * shows as a line and dims the stroke it undid.
 * l or /
 * looks a word or outline up (steno -l), in a box that is there only
 * while it is used.
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
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "steno.h"

#define FEED_LINE 4096
#define KEEP_LINES 400         /* kept for each box */
#define CELL_MAX 512           /* bytes of one kept line */
#define FOUND_MAX 200          /* lines of a lookup's answer */
#define FIND_MAX 128           /* bytes typed into the lookup */

struct box {
    const char *name;          /* the feed, and the title */
    int fd;
    int shown;
    char got[FEED_LINE * 2];   /* read but no newline yet */
    int ngot;
    char lines[KEEP_LINES][CELL_MAX];
    int nlines;                /* the newest is lines[nlines - 1] */
    int scroll;                /* lines up from the newest (0: follow) */
    /* the tape's lines only: */
    long seq[KEEP_LINES];      /* a number for each, never reused */
    long by[KEEP_LINES];       /* cleared by that line (0: shown) */
    char dead[KEEP_LINES];     /* undone by a * after it */
    char undo[KEEP_LINES];     /* a * line; in the suggestions, the
                                  word being fingerspelled */
};

static struct box boxes[2];    /* 0 tape, 1 suggestions */
static int fullscreen, focus;
static int hide_undone = 1;    /* u: undone strokes and * not shown */
static int box_x[2], box_w[2], box_y[2], box_h[2];   /* where drawn */
static long next_seq = 1;
static volatile int resized, stop_view;
static const char *view_cfg;   /* -c, for steno -l */

/* the lookup: asking (typing), and its last answer */
static int finding;
static char find[FIND_MAX];
static int nfind;
static char found[FOUND_MAX][CELL_MAX];
static int nfound;

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

/* line i out, the ones after it one place up */
static void drop_line(struct box *b, int i)
{
    int k;

    k = b->nlines - i - 1;
    memmove(b->lines[i], b->lines[i + 1], k * CELL_MAX);
    memmove(b->seq + i, b->seq + i + 1, k * sizeof(long));
    memmove(b->by + i, b->by + i + 1, k * sizeof(long));
    memmove(b->dead + i, b->dead + i + 1, k);
    memmove(b->undo + i, b->undo + i + 1, k);
    b->nlines--;
}

static void keep(struct box *b, const char *s)
{
    int i;

    if (b->nlines == KEEP_LINES)
        drop_line(b, 0);
    i = b->nlines;
    scopy(b->lines[i], s, CELL_MAX);
    b->seq[i] = next_seq;
    next_seq++;
    b->by[i] = 0;
    b->dead[i] = 0;
    b->undo[i] = 0;
    b->nlines++;
    if (b->scroll)
        b->scroll++;           /* what you scrolled to stays */
}

/* The newest line i is a stroke of a translation of n strokes: the
 * n - 1 live strokes before it are part of it, so their text goes; a
 * stroke that is * undoes the newest live one, and what that one had
 * cleared comes back. */
static void tape_merge(struct box *b, int i, int n)
{
    int j, k;

    if (b->undo[i]) {
        for (j = i - 1; j >= 0; j--)
            if (!b->undo[j] && !b->dead[j])
                break;
        if (j < 0)
            return;
        b->dead[j] = 1;
        for (k = 0; k < j; k++)
            if (b->by[k] == b->seq[j])
                b->by[k] = 0;
        return;
    }
    for (j = i - 1; j >= 0 && n > 1; j--) {
        if (b->undo[j] || b->dead[j])
            continue;
        if (!b->by[j])
            b->by[j] = b->seq[i];
        n--;
    }
}

/* = <tab> WORD <tab> N: the last N live strokes fingerspelled WORD;
 * the last of them shows it, the others are cleared as in an outline */
static void tape_spelled(struct box *b, const char *word, int n)
{
    char *t;
    int j, last;

    last = -1;
    for (j = b->nlines - 1; j >= 0 && n > 0; j--) {
        if (b->undo[j] || b->dead[j])
            continue;
        if (last < 0) {
            last = j;
            b->by[j] = 0;      /* shown again when a letter is undone */
            t = strchr(b->lines[j], '\t');
            t = t ? strchr(t + 1, '\t') : 0;
            if (t) {
                t[1] = 0;
                scat(b->lines[j], word, CELL_MAX);
            }
        } else if (!b->by[j]) {
            b->by[j] = b->seq[last];
        }
        n--;
    }
}

/* one line of the tape feed: STROKE <tab> TRANSLATION <tab> N, kept
 * as the stroke in the tape's columns, the stroke as written, the
 * translation, with tabs between */
static void got_tape(char *line)
{
    char cell[CELL_MAX], *tr, *nst;
    long st;
    int i, n;

    tr = strchr(line, '\t');
    if (tr) {
        *tr = 0;
        tr++;
    } else {
        tr = "";
    }
    nst = strchr(tr, '\t');
    if (nst) {
        *nst = 0;
        n = atoi_small(nst + 1);
    } else {
        n = !(tr[0] == '*' && !tr[1]);    /* an engine before N */
    }
    if (!strcmp(line, "=")) {
        tape_spelled(&boxes[0], tr, n);
        return;
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
    i = boxes[0].nlines - 1;
    boxes[0].undo[i] = n == 0;
    tape_merge(&boxes[0], i, n);
    /* hidden, the * and its stroke take no line: what you scrolled
     * to stays */
    if (hide_undone && n == 0 && boxes[0].scroll) {
        boxes[0].scroll -= 2;
        if (boxes[0].scroll < 0)
            boxes[0].scroll = 0;
    }
}

/* one line of the suggest feed: WORDS <tab> OUTLINES ..., kept as one
 * line for each WORDS <tab> OUTLINES; words already there move to the
 * bottom, so each shows once */
static void got_suggest(char *line)
{
    char cell[CELL_MAX], *w, *o, *next;
    struct box *b;
    int i, k;
    int spell;

    /* = ...: the fingerspelled word so far, in place of the last one
     * (undo[] marks it in this box) */
    b = &boxes[1];
    spell = line[0] == '=' && (!line[1] || line[1] == '\t');
    for (i = 0; i < b->nlines; i++) {
        if (b->undo[i] && spell) {
            drop_line(b, i);
            break;
        }
        b->undo[i] = 0;
    }
    for (; i < b->nlines; i++)
        b->undo[i] = 0;
    if (spell)
        line += line[1] ? 2 : 1;

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
        k = strlen(cell);
        scat(cell, o, sizeof(cell));
        b = &boxes[1];
        for (i = 0; i < b->nlines; i++) {
            if (!strncmp(b->lines[i], cell, k)) {
                drop_line(b, i);
                break;
            }
        }
        if (!fullscreen && i == b->nlines)
            printf("%s%s  %s\n", boxes[0].shown ? "= " : "", w, o);
        keep(b, cell);
        b->undo[b->nlines - 1] = spell;
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
    static int shown[KEEP_LINES];
    struct box *b;
    char *tr, *st;
    int i, j, row, n, cols, nshown;

    b = &boxes[0];
    /* the lines to show: all, or without undone strokes and * */
    nshown = 0;
    for (j = 0; j < b->nlines; j++) {
        if (hide_undone && (b->undo[j] || b->dead[j]))
            continue;
        shown[nshown] = j;
        nshown++;
    }
    if (b->scroll > nshown - h)
        b->scroll = nshown - h > 0 ? nshown - h : 0;
    j = nshown - h - b->scroll;
    if (j < 0)
        j = 0;
    for (row = 0; row < h; row++, j++) {
        scr_move(y + row, x);
        n = 0;
        i = j < nshown ? shown[j] : b->nlines;
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
            /* the text: gone when a later stroke made one outline of
             * it; dim when undone */
            if (n + 2 < w && !b->by[i]) {
                scr_put("  ");
                n += 2;
                scr_col(b->undo[i] || b->dead[i] ? C_DIM : C_TEAL);
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
    int i, j, n, row, filled, first, last;

    b = &boxes[1];
    if (b->scroll > b->nlines - 1)
        b->scroll = b->nlines > 0 ? b->nlines - 1 : 0;
    last = b->nlines - b->scroll;
    /* from the newest shown back, until the box is full */
    filled = 0;
    first = last;
    while (first > 0) {
        n = sugg_rows(b->lines[first - 1], w, rows, 64);
        if (filled + n > h && filled)
            break;             /* the newest shows, even cut off */
        filled += n;
        first--;
    }
    row = 0;
    for (i = first; i < last; i++) {
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

/* the keys under the boxes: under one box all of them; with both, the
 * tape's under the tape and the rest under the suggestions */
static const char *const keys_all[] = {
    "quit", "clear", "tape", "undo", "suggestions", "l lookup"
};

static void draw_box(int which, int y, int x, int w, int h)
{
    const char *const *keys;
    int nkeys;

    struct box *b;
    char title[64];
    int row;

    if (w < 8 || h < 3)
        return;
    b = &boxes[which];
    box_x[which] = x;
    box_y[which] = y;
    box_w[which] = w;
    box_h[which] = h;
    for (row = 1; row < h - 1; row++)
        scr_side(y + row, x, w);
    /* the inside first: it says how far it can scroll */
    if (which)
        draw_sugg(y + 1, x + 1, w - 2, h - 2);
    else
        draw_tape(y + 1, x + 1, w - 2, h - 2);
    scopy(title, which ? "suggestions" : "tape", sizeof(title));
    if (b->scroll)
        sprintf(title + strlen(title), " -%d", b->scroll);
    /* with both: the one j and k scroll has its title lit */
    scr_title_dim = boxes[!which].shown && focus != which;
    scr_border(y, x, w, 1, title, 0, 0);
    scr_title_dim = 0;
    keys = keys_all;
    nkeys = 6;
    if (boxes[!which].shown) {
        keys = which ? keys_all + 4 : keys_all;
        nkeys = which ? 2 : 4;
    }
    scr_border(y + h - 1, x, w, 0, 0, keys, nkeys);
}

/* the lookup, over the bottom of the screen: what is typed, then the
 * answer of steno -l */
static void draw_find(int rows, int cols)
{
    static const char *const keys[] = { "Enter look up", "Esc close" };
    int h, y, row, n;

    h = 3 + (nfound ? nfound + 1 : 0);
    if (h > rows)
        h = rows;
    if (h < 3 || cols < 12)
        return;
    y = rows - h;
    scr_border(y, 0, cols, 1, "lookup", 0, 0);
    scr_side(y + 1, 0, cols);
    scr_move(y + 1, 1);
    scr_col(C_BLUE);
    scr_put("> ");
    scr_col(C_TEXT);
    n = 2 + scr_text(find, cols - 5);
    scr_col(C_SELECT);
    scr_put(finding ? " " : "");
    scr_col(C_PLAIN);
    scr_fill(' ', cols - 2 - n - (finding ? 1 : 0));
    for (row = 0; row < h - 3; row++) {
        scr_side(y + 2 + row, 0, cols);
        scr_move(y + 2 + row, 1);
        n = 0;
        if (row > 0 && row - 1 < nfound) {
            /* "X gives" and "X is written" lines; the rest under */
            scr_col(found[row - 1][0] == ' ' ? C_TEAL : C_TEXT);
            n = scr_text(found[row - 1], cols - 2);
        }
        scr_col(C_PLAIN);
        scr_fill(' ', cols - 2 - n);
    }
    scr_border(y + h - 1, 0, cols, 0, 0, keys, 2);
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
            draw_box(0, 0, 0, wt, rows);
            draw_box(1, 0, wt, cols - wt, rows);
        } else {
            draw_box(0, 0, 0, cols, rows / 2);
            draw_box(1, rows / 2, 0, cols, rows - rows / 2);
        }
    } else {
        draw_box(boxes[1].shown, 0, 0, cols, rows);
    }
    if (finding || nfound)
        draw_find(rows, cols);
    scr_end();
}

/* --- the lookup ------------------------------------------------------ */

/* steno [-c CFG] -l WHAT, its answer into found[] */
static void look_up(void)
{
    char buf[FOUND_MAX * 128], *p, *nl;
    const char *argv[6];
    int fd[2], r, n, k;
    pid_t pid;

    nfound = 0;
    if (!find[0])
        return;
    r = pipe(fd);
    if (r < 0)
        return;
    k = 0;
    argv[k] = "steno";
    k++;
    if (view_cfg) {
        argv[k] = "-c";
        k++;
        argv[k] = view_cfg;
        k++;
    }
    argv[k] = "-l";
    k++;
    argv[k] = find;
    k++;
    argv[k] = 0;
    pid = fork();
    if (pid == 0) {
        close(fd[0]);
        dup2(fd[1], 1);
        dup2(fd[1], 2);
        close(fd[1]);
        r = open("/dev/null", O_RDONLY);
        if (r >= 0)
            dup2(r, 0);
        execv("/proc/self/exe", (char *const *)argv);
        _exit(127);
    }
    close(fd[1]);
    n = 0;
    while (pid > 0 && n < (int)sizeof(buf) - 1) {
        r = read(fd[0], buf + n, sizeof(buf) - 1 - n);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            break;
        n += r;
    }
    close(fd[0]);
    if (pid > 0)
        waitpid(pid, 0, 0);
    buf[n] = 0;
    for (p = buf; *p && nfound < FOUND_MAX; p = nl + 1) {
        nl = strchr(p, '\n');
        if (!nl)
            nl = p + strlen(p);
        k = (int)(nl - p);
        if (k > CELL_MAX - 1)
            k = CELL_MAX - 1;
        memcpy(found[nfound], p, k);
        found[nfound][k] = 0;
        nfound++;
        if (!*nl)
            break;
    }
}

/* a key while the lookup is asked */
static void find_key(int c)
{
    if (c == 27) {
        finding = 0;
        nfound = 0;
    } else if (c == '\r' || c == '\n') {
        look_up();
    } else if (c == 127 || c == 8) {
        if (nfind > 0) {
            nfind--;
            find[nfind] = 0;
        }
    } else if (c == 21) {
        nfind = 0;             /* Ctrl+U */
        find[0] = 0;
    } else if (c >= 32 && c < 127 && nfind < FIND_MAX - 1) {
        find[nfind] = (char)c;
        nfind++;
        find[nfind] = 0;
    }
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
    if (!boxes[focus].shown)
        focus = !focus;
}

/* j k and the rest: n lines down (towards the newest) or up */
static void scroll_by(int n)
{
    struct box *b;
    int rows, cols;

    b = &boxes[focus];
    scr_size(&rows, &cols);
    if (n == 1000000 || n == -1000000)
        b->scroll = n > 0 ? 0 : b->nlines;
    else if (n == 500 || n == -500)
        b->scroll -= n > 0 ? rows / 2 : -(rows / 2);
    else
        b->scroll -= n;
    if (b->scroll < 0)
        b->scroll = 0;
    if (b->scroll > b->nlines)
        b->scroll = b->nlines;
}

/* ESC [ < BUTTON ; X ; Y M (the mouse, as xterm's 1006 says): the
 * wheel scrolls the box under it. Gives the bytes it took. */
static int mouse_event(const unsigned char *k, int n)
{
    int i, v[3], j, which;

    v[0] = v[1] = v[2] = 0;
    j = 0;
    for (i = 3; i < n && j < 3; i++) {
        if (k[i] >= '0' && k[i] <= '9')
            v[j] = v[j] * 10 + (k[i] - '0');
        else if (k[i] == ';')
            j++;
        else
            break;
    }
    if (i == n)
        return n;
    if ((v[0] & ~(4 | 8 | 16)) != 64 && (v[0] & ~(4 | 8 | 16)) != 65)
        return i + 1;          /* not the wheel: nothing to do */
    which = boxes[1].shown && (!boxes[0].shown ||
        (v[1] - 1 >= box_x[1] && v[1] - 1 < box_x[1] + box_w[1] &&
        v[2] - 1 >= box_y[1] && v[2] - 1 < box_y[1] + box_h[1]));
    j = focus;
    focus = which;
    scroll_by((v[0] & 1) ? 3 : -3);
    focus = j;
    return i + 1;
}

/* the keys read at once: -1 quit */
static int view_keys(const unsigned char *k, int n)
{
    int i, c;

    for (i = 0; i < n; i++) {
        c = k[i];
        if (c == 27 && i + 2 < n && k[i + 1] == '[' && k[i + 2] == '<') {
            i += mouse_event(k + i, n - i) - 1;
            continue;
        }
        /* arrows and pages: ESC [ A, B, 5~, 6~ (or ESC O A, B) */
        if (c == 27 && i + 2 < n && (k[i + 1] == '[' || k[i + 1] == 'O')) {
            c = k[i + 2];
            i += 2;
            if (c == '5' || c == '6') {
                if (i + 1 < n && k[i + 1] == '~')
                    i++;
                c = c == '5' ? 21 : 4;
            } else {
                c = c == 'A' ? 'k' : c == 'B' ? 'j' : c == 'H' ? 'g' :
                    c == 'F' ? 'G' : 0;
            }
            if (finding || !c)
                continue;
        }
        if (finding) {
            find_key(c);
            continue;
        }
        if (c == 'q' || c == 3)
            return -1;
        if (c == 27 && nfound) {
            nfound = 0;
        } else if (c == 'l' || c == '/') {
            finding = 1;
            nfind = 0;
            find[0] = 0;
            nfound = 0;
        } else if (c == 'c') {
            boxes[0].nlines = 0;
            boxes[1].nlines = 0;
            boxes[0].scroll = 0;
            boxes[1].scroll = 0;
        } else if (c == 'u') {
            hide_undone = !hide_undone;
        } else if (c == 't') {
            toggle(0);
        } else if (c == 's') {
            toggle(1);
        } else if (c == '\t' && boxes[!focus].shown) {
            focus = !focus;
        } else if (c == 'j') {
            scroll_by(1);
        } else if (c == 'k') {
            scroll_by(-1);
        } else if (c == 4) {
            scroll_by(500);    /* Ctrl+D, Page Down */
        } else if (c == 21) {
            scroll_by(-500);   /* Ctrl+U, Page Up */
        } else if (c == 'G') {
            scroll_by(1000000);
        } else if (c == 'g') {
            scroll_by(-1000000);
        }
    }
    return 0;
}

int view_main(int tape, int sugg, const char *cfg)
{
    struct pollfd pf[3];
    struct termios old, raw;
    struct sigaction sa;
    int i, n, r, tty, ix[2];
    unsigned char k[64];

    view_cfg = cfg;
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
    focus = !tape;
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
        /* raw: Ctrl+C, Ctrl+D and the rest come as keys */
        raw = old;
        raw.c_lflag &= ~(ICANON | ECHO | ISIG);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &raw);
    }
    if (fullscreen) {
        /* the other screen, no cursor; given back at the end */
        scr_open();
        scr_mouse(1);
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
            r = read(0, k, sizeof(k));
            if (r <= 0)
                break;
            r = view_keys(k, r);
            if (r < 0)
                break;
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
    if (fullscreen) {
        scr_mouse(0);
        scr_close();
    }
    if (tty)
        tcsetattr(0, TCSANOW, &old);
    return 0;
}
