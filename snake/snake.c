/* snake - terminal snake game (see README)
 *
 * Plain C, libc only: no curses. The terminal is driven with termios
 * and VT100/xterm escape codes (256 colours, DEC line drawing), in the
 * Habamax colours and the look of btop. */

#define _XOPEN_SOURCE 600

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>

#include "game.h"

#define START_TICK 150  /* ms per step at the start */
#define MIN_TICK 60     /* fastest speed */
#define SPEED_UP 2      /* ms faster per food eaten */
#define MIN_WIDTH 10    /* board size in cells; one cell is 2 columns */
#define MIN_HEIGHT 6
#define MAX_SIDE 500
#define OUT_SIZE 65536

/* Habamax, as xterm 256 colour numbers */
enum {
    col_bg = 234,      /* #1c1c1c */
    col_text = 251,    /* #c6c6c6 */
    col_title = 255,   /* #eeeeee */
    col_label = 247,   /* #9e9e9e */
    col_line = 240,    /* #585858 */
    col_blue = 67,     /* #5f87af */
    col_teal = 109,    /* #87afaf */
    col_green = 108,   /* #87af87 */
    col_red = 131,     /* #af5f5f */
    col_magenta = 176  /* #d787d7 */
};

enum { st_ready, st_play, st_pause, st_over, st_won };


static const char *usage_text =
    "usage: snake [-afn] [-s WxH] [-t MS]\n"
    "  -a      autopilot: the game plays itself\n"
    "  -s WxH  board of W by H cells (default: the whole terminal)\n"
    "  -t MS   milliseconds per step at the start (default 150)\n"
    "  -f      fixed speed: no speed-up when eating\n"
    "  -n      no best score file\n"
    "keys: hjkl or arrows move, a autopilot, + - speed, p or space\n"
    "pause, r restart, q quit\n";

static int opt_w, opt_h, opt_fixed, opt_nobest;
static int opt_tick = START_TICK;

/* autopilot (the rest is in game.c) */
static int assisted;         /* the autopilot played in this game */
static int speed_set;        /* speed chosen with + and - */

static struct termios term_orig;
static int term_on;
static char out_buf[OUT_SIZE];
static int out_len;
static int scr_w, scr_h;     /* terminal size */
static int org_x, org_y;     /* top left corner of the box */
static int too_small;

static int state;
static int score;
static int tick;             /* ms per step now */
static long long next;       /* time of the next step */

static int best;
static int best_dirty;         /* best changed, not saved yet */
static int new_best;           /* this game beat the best */
static int best_ok;
static char best_path[1024];


static volatile sig_atomic_t got_quit, got_winch, got_tstp;

/* output, buffered and written at once */

static void
out_flush(void)
{
    int n, off;

    off = 0;
    while (off < out_len) {
        n = write(1, out_buf + off, out_len - off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        off += n;
    }
    out_len = 0;
}

static void
out_str(const char *s)
{
    int n;

    n = strlen(s);
    if (out_len + n > OUT_SIZE) {
        out_flush();
    }
    memcpy(out_buf + out_len, s, n);
    out_len += n;
}

static void
out_ch(int c)
{
    if (out_len >= OUT_SIZE) {
        out_flush();
    }
    out_buf[out_len] = c;
    out_len++;
}

static void
out_num(long long v)
{
    char tmp[24];
    int n;

    n = 0;
    do {
        tmp[n] = '0' + v % 10;
        n++;
        v /= 10;
    } while (v > 0);
    while (n > 0) {
        n--;
        out_ch(tmp[n]);
    }
}

static void
out_move(int y, int x)
{
    out_str("\033[");
    out_num(y + 1);
    out_ch(';');
    out_num(x + 1);
    out_ch('H');
}

static void
out_color(int fg, int bold)
{
    out_str(bold ? "\033[0;1;38;5;" : "\033[0;38;5;");
    out_num(fg);
    out_str(";48;5;");
    out_num(col_bg);
    out_ch('m');
}

/* terminal */

static void
term_restore(void)
{
    if (!term_on) {
        return;
    }
    out_str("\033[0m\033(B\033[?25h\033[?1049l");
    out_flush();
    tcsetattr(0, TCSAFLUSH, &term_orig);
    term_on = 0;
}

static int
term_raw(void)
{
    struct termios t;

    t = term_orig;
    t.c_lflag &= ~(ICANON | ECHO | IEXTEN);
    t.c_iflag &= ~(IXON | ICRNL | INPCK | ISTRIP | BRKINT);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSAFLUSH, &t) < 0) {
        return -1;
    }
    term_on = 1;
    out_str("\033[?1049h\033[?25l\033[H");
    return 0;
}

static void
die(const char *msg)
{
    term_restore();
    fprintf(stderr, "snake: %s\n", msg);
    exit(1);
}

static void
term_size(void)
{
    struct winsize ws;

    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 &&
        ws.ws_row > 0)
    {
        scr_w = ws.ws_col;
        scr_h = ws.ws_row;
    } else {
        scr_w = 80;
        scr_h = 24;
    }
}

static void
on_signal(int sig)
{
    if (sig == SIGWINCH) {
        got_winch = 1;
    } else if (sig == SIGTSTP) {
        got_tstp = 1;
    } else {
        got_quit = 1;
    }
}

static void
set_handler(int sig, void (*fn)(int))
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = fn;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

/* misc */

static long long
now_ms(void)
{
    struct timespec ts;
    long long t;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    t = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    return t;
}

/* Parse a decimal number in [min, max] up to the byte end (or the end
 * of the string when end is 0). Returns -1 on error. */
static int
parse_num(const char *s, int end, int min, int max)
{
    int v, i;

    v = 0;
    for (i = 0; s[i] && s[i] != end; i++) {
        if (s[i] < '0' || s[i] > '9' || v > max) {
            return -1;
        }
        v = v * 10 + (s[i] - '0');
    }
    if (i == 0 || v < min || v > max) {
        return -1;
    }
    return v;
}

/* best score, in ~/.system/state/snake/best */

static void
best_load(void)
{
    const char *home;
    char buf[16];
    int fd, n, i;

    best = 0;
    home = getenv("HOME");
    if (opt_nobest || !home || !home[0] ||
        strlen(home) > sizeof best_path - 32)
    {
        return;
    }
    sprintf(best_path, "%s/.system/state/snake", home);
    if (mkdir(best_path, 0700) < 0 && errno != EEXIST) {
        return;
    }
    strcat(best_path, "/best");
    best_ok = 1;
    fd = open(best_path, O_RDONLY);
    if (fd < 0) {
        return;
    }
    n = read(fd, buf, 9);
    close(fd);
    for (i = 0; i < n && buf[i] >= '0' && buf[i] <= '9'; i++) {
        best = best * 10 + (buf[i] - '0');
    }
}

static void
best_save(void)
{
    char buf[16];
    int fd, n;

    if (!best_ok || !best_dirty) {
        return;
    }
    best_dirty = 0;
    fd = open(best_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        return;
    }
    n = sprintf(buf, "%d\n", best);
    if (write(fd, buf, n) != n) {
        /* nothing to do: the old score is gone, the game goes on */
    }
    close(fd);
}

/* drawing */

static void
draw_cell(int cell)
{
    out_move(org_y + 1 + cell / board_w, org_x + 1 + cell % board_w * 2);
    if (cell == food) {
        out_color(col_magenta, 1);
        out_str("<>");
    } else if (!grid[cell]) {
        out_color(col_text, 0);
        out_str("  ");
    } else if (cell == body[head]) {
        out_color(state == st_over ? col_red : col_teal, 1);
        out_str("@@");
    } else {
        out_color(state == st_over ? col_red : col_green, 0);
        out_str("[]");
    }
}

static void
draw_hline(int y, int x, int w, int left, int right)
{
    int i;

    out_move(y, x);
    out_color(col_line, 0);
    out_str("\033(0");
    out_ch(left);
    for (i = 0; i < w - 2; i++) {
        out_ch('q');
    }
    out_ch(right);
    out_str("\033(B");
}

static void
draw_box(int y, int x, int h, int w, int fill)
{
    int i, j;

    draw_hline(y, x, w, 'l', 'k');
    for (i = 1; i < h - 1; i++) {
        out_move(y + i, x);
        out_color(col_line, 0);
        out_str("\033(0x\033(B");
        if (fill) {
            out_color(col_text, 0);
            for (j = 0; j < w - 2; j++) {
                out_ch(' ');
            }
            out_color(col_line, 0);
        } else {
            out_move(y + i, x + w - 1);
        }
        out_str("\033(0x\033(B");
    }
    draw_hline(y + h - 1, x, w, 'm', 'j');
}

static int
label_width(const char *key, const char *text)
{
    int n;

    n = strlen(key) + strlen(text) + 4;
    return n;
}

/* Draw a label in a border line, between tee marks, btop style: the
 * key in blue, then the text. Returns the column after the label, or
 * x when it does not fit before limit. */
static int
draw_label(int y, int x, int limit, const char *key, const char *text,
    int bold)
{
    int n;

    n = label_width(key, text);
    if (x + n > limit) {
        return x;
    }
    out_move(y, x);
    out_color(col_line, 0);
    out_str("\033(0u\033(B");
    out_color(col_text, 0);
    out_ch(' ');
    if (key[0]) {
        out_color(col_blue, 1);
        out_str(key);
    }
    out_color(bold ? col_title : col_label, bold);
    out_str(text);
    out_ch(' ');
    out_color(col_line, 0);
    out_str("\033(0t\033(B");
    return x + n;
}

static void
draw_top(void)
{
    char s[32], b[32];
    int w, x, end, xb, xs;

    w = board_w * 2 + 2;
    end = org_x + w - 2;
    draw_hline(org_y, org_x, w, 'l', 'k');
    x = draw_label(org_y, org_x + 2, end, "", "snake", 1) + 1;
    if (ai_on) {
        x = draw_label(org_y, x, end, "", "autopilot", 0) + 1;
    }
    sprintf(s, "score %d", score);
    sprintf(b, "best %d", best);
    /* score and best at the right; when short of room, only score */
    xb = end - label_width("", b);
    xs = xb - 1 - label_width("", s);
    if (xs >= x) {
        draw_label(org_y, xs, end, "", s, 0);
        draw_label(org_y, xb, end, "", b, 0);
    } else if (end - label_width("", s) >= x) {
        draw_label(org_y, end - label_width("", s), end, "", s, 0);
    }
}

static void
draw_bottom(void)
{
    static const char *keys[6] = { "hjkl", "a", "p", "r", "q", "+-" };
    static const char *texts[6] = { " move", " auto", " pause",
        " restart", " quit", " speed" };
    int i, x, y, limit;

    y = org_y + board_h + 1;
    limit = org_x + board_w * 2;
    x = org_x + 2;
    for (i = 0; i < 6; i++) {
        if (x + label_width(keys[i], texts[i]) > limit) {
            break;
        }
        x = draw_label(y, x, limit, keys[i], texts[i], 0) + 1;
    }
}

static void
draw_clear(void)
{
    int x, y;

    out_str("\033[0m");
    out_color(col_text, 0);
    for (y = 0; y < scr_h; y++) {
        out_move(y, 0);
        for (x = 0; x < scr_w; x++) {
            out_ch(' ');
        }
    }
}

/* A small box with up to 3 lines over the upper middle of the board;
 * the first line is bold. */
static void
draw_message(const char *l1, const char *l2, const char *l3)
{
    const char *line[3];
    int i, n, w, h, x, y, len;

    line[0] = l1;
    line[1] = l2;
    line[2] = l3;
    n = 0;
    w = 0;
    for (i = 0; i < 3 && line[i]; i++) {
        len = strlen(line[i]);
        if (len > w) {
            w = len;
        }
        n++;
    }
    w += 4;
    h = n + 2;
    if (w > board_w * 2 || h > board_h) {
        return;
    }
    x = org_x + 1 + (board_w * 2 - w) / 2;
    y = (board_h / 2 - h) / 2;
    if (y < 0) {
        y = 0;
    }
    y += org_y + 1;
    draw_box(y, x, h, w, 1);
    for (i = 0; i < n; i++) {
        len = strlen(line[i]);
        out_move(y + 1 + i, x + (w - len) / 2);
        out_color(i == 0 ? col_title : col_label, i == 0);
        out_str(line[i]);
    }
}

static void
redraw(void)
{
    const char *msg;
    char s[32];
    int i, n;

    term_size();
    too_small = board_w * 2 + 2 > scr_w || board_h + 2 > scr_h;
    org_x = (scr_w - (board_w * 2 + 2)) / 2;
    org_y = (scr_h - (board_h + 2)) / 2;
    draw_clear();
    if (too_small) {
        msg = "terminal too small";
        n = strlen(msg);
        if (n <= scr_w) {
            out_move(scr_h / 2, (scr_w - n) / 2);
            out_color(col_title, 1);
            out_str(msg);
        }
        out_flush();
        return;
    }
    draw_box(org_y, org_x, board_h + 2, board_w * 2 + 2, 0);
    draw_top();
    draw_bottom();
    for (i = 0; i < body_cap; i++) {
        if (grid[i] || i == food) {
            draw_cell(i);
        }
    }
    if (new_best) {
        sprintf(s, "new best %d", score);
    } else {
        sprintf(s, "score %d", score);
    }
    switch (state) {
    case st_ready:
        draw_message("ready", "hjkl to play", "a autopilot");
        break;
    case st_pause:
        draw_message("paused", "p to go on", 0);
        break;
    case st_over:
        draw_message("game over", s, "r again  q quit");
        break;
    case st_won:
        draw_message("you win", s, "r again  q quit");
        break;
    }
    out_flush();
}

static void
game_start(void)
{
    int w, h;

    term_size();
    w = opt_w ? opt_w : (scr_w - 2) / 2;
    h = opt_h ? opt_h : scr_h - 2;
    if (w < MIN_WIDTH) {
        w = MIN_WIDTH;
    }
    if (h < MIN_HEIGHT) {
        h = MIN_HEIGHT;
    }
    if (game_new(w, h) < 0) {
        die("out of memory");
    }
    score = 0;
    new_best = 0;
    assisted = 0;
    if (!speed_set) {
        tick = opt_tick;
    }
    state = st_ready;
}

static void
game_end(int st)
{
    state = st;
    best_save();
    redraw();
}

void
game_cell(int c)
{
    draw_cell(c);
}

static void
step(void)
{
    int r;

    if (ai_on) {
        assisted = 1;
    }
    r = game_step();
    if (r == step_over) {
        game_end(st_over);
        return;
    }
    if (r == step_moved) {
        return;
    }
    score++;
    if (!opt_fixed && !speed_set && tick - SPEED_UP >= MIN_TICK) {
        tick -= SPEED_UP;
    }
    /* the best score is a person's: not when the autopilot helped */
    if (!assisted && score > best) {
        best = score;
        best_dirty = 1;
        new_best = 1;
    }
    if (r == step_won) {
        game_end(st_won);
        return;
    }
    draw_top();
}

static void
play(void)
{
    state = st_play;
    next = now_ms() + tick;
    redraw();
}

static void
key(int c)
{
    int d;

    d = -1;
    switch (c) {
    case 'q':
        got_quit = 1;
        return;
    case 'r':
        best_save();
        game_start();
        if (ai_on && !too_small) {
            play();
        } else {
            redraw();
        }
        return;
    case 'a':
        ai_on = !ai_on;
        ai_reset();
        if (ai_on && !too_small &&
            (state == st_ready || state == st_pause))
        {
            play();
        } else {
            redraw();
        }
        return;
    case '+':
    case '=':
        tick /= 2;
        speed_set = 1;
        return;
    case '-':
        tick = tick ? tick * 2 : 1;
        if (tick > 2000) {
            tick = 2000;
        }
        speed_set = 1;
        next = now_ms() + tick;
        return;
    case 'p':
    case ' ':
        if (state == st_play) {
            state = st_pause;
            redraw();
        } else if (state == st_pause && !too_small) {
            play();
        }
        return;
    case 'k':
        d = dir_up;
        break;
    case 'l':
        d = dir_right;
        break;
    case 'j':
        d = dir_down;
        break;
    case 'h':
        d = dir_left;
        break;
    }
    if (d < 0 || too_small) {
        return;
    }
    if (ai_on && (state == st_play || state == st_pause)) {
        /* a person takes over */
        ai_on = 0;
        draw_top();
    }
    if (state == st_play) {
        game_turn(d);
    } else if (state == st_ready || state == st_pause) {
        game_turn(d);
        play();
    }
}

static void
input(const char *buf, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        if (buf[i] != 27) {
            key(buf[i]);
            continue;
        }
        /* arrows: ESC [ A or ESC O A; other sequences are skipped */
        if (i + 2 < n && (buf[i + 1] == '[' || buf[i + 1] == 'O')) {
            switch (buf[i + 2]) {
            case 'A':
                key('k');
                break;
            case 'B':
                key('j');
                break;
            case 'C':
                key('l');
                break;
            case 'D':
                key('h');
                break;
            }
            i += 2;
        }
        while (i + 1 < n && buf[i + 1] != 27 &&
            (buf[i + 1] < '@' || buf[i + 1] > '~'))
        {
            i++;
        }
    }
}

/* Ctrl-z: give the terminal back, stop, and take it again after fg */
static void
suspend(void)
{
    term_restore();
    set_handler(SIGTSTP, SIG_DFL);
    kill(getpid(), SIGTSTP);
    set_handler(SIGTSTP, on_signal);
    if (term_raw() < 0) {
        die("cannot set up the terminal");
    }
    if (state == st_play) {
        state = st_pause;
    }
    redraw();
}

static void
options(int argc, char **argv)
{
    const char *a, *v;
    int i, j, x;

    for (i = 1; i < argc; i++) {
        a = argv[i];
        if (a[0] != '-' || !a[1]) {
            goto bad;
        }
        for (j = 1; a[j]; j++) {
            switch (a[j]) {
            case 'a':
                ai_on = 1;
                continue;
            case 'f':
                opt_fixed = 1;
                continue;
            case 'n':
                opt_nobest = 1;
                continue;
            case 'h':
                fputs(usage_text, stdout);
                exit(0);
            case 's':
            case 't':
                break;
            default:
                goto bad;
            }
            /* an option with a value: the rest of a, or the next arg */
            v = a + j + 1;
            if (!v[0]) {
                i++;
                if (i >= argc) {
                    goto bad;
                }
                v = argv[i];
            }
            if (a[j] == 't') {
                opt_tick = parse_num(v, 0, 20, 2000);
                if (opt_tick < 0) {
                    goto bad;
                }
            } else {
                opt_w = parse_num(v, 'x', MIN_WIDTH, MAX_SIDE);
                for (x = 0; v[x] && v[x] != 'x'; x++) {
                }
                if (opt_w < 0 || !v[x]) {
                    goto bad;
                }
                opt_h = parse_num(v + x + 1, 0, MIN_HEIGHT, MAX_SIDE);
                if (opt_h < 0) {
                    goto bad;
                }
            }
            break;
        }
    }
    return;
bad:
    fputs(usage_text, stderr);
    exit(2);
}

int
main(int argc, char **argv)
{
    char buf[64];
    fd_set fds;
    struct timeval tv, *tvp;
    long long now;
    int n;

    options(argc, argv);
    if (!isatty(0) || !isatty(1) || tcgetattr(0, &term_orig) < 0) {
        fputs("snake: not a terminal\n", stderr);
        return 1;
    }
    rng_seed();
    best_load();
    set_handler(SIGINT, on_signal);
    set_handler(SIGTERM, on_signal);
    set_handler(SIGHUP, on_signal);
    set_handler(SIGWINCH, on_signal);
    set_handler(SIGTSTP, on_signal);
    if (term_raw() < 0) {
        die("cannot set up the terminal");
    }
    game_start();
    if (ai_on) {
        play();
    } else {
        redraw();
    }
    while (!got_quit) {
        if (got_tstp) {
            got_tstp = 0;
            suspend();
            continue;
        }
        if (got_winch) {
            got_winch = 0;
            if (state == st_play) {
                state = st_pause;
            }
            redraw();
            continue;
        }
        /* one step if it is time, then wait for a key until the next
         * step; at speed 0 there is no wait, only a look for keys */
        now = now_ms();
        if (state == st_play && now >= next) {
            step();
            next += tick;
            if (next < now) {
                next = now + tick;
            }
        }
        out_flush();
        tvp = NULL;
        if (state == st_play) {
            tv.tv_sec = (next - now) / 1000;
            tv.tv_usec = (next - now) % 1000 * 1000;
            tvp = &tv;
        }
        FD_ZERO(&fds);
        FD_SET(0, &fds);
        n = select(1, &fds, NULL, NULL, tvp);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            die("select failed");
        }
        if (n == 0) {
            continue;
        }
        n = read(0, buf, sizeof buf);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        input(buf, n);
    }
    best_save();
    term_restore();
    game_free();
    return 0;
}
