/* The status screen: when steno runs in the foreground of a
 * terminal, a small btop-like screen shows what it is doing (the
 * keyboard, X, output, raw and movement mode, the last stroke), the
 * dictionaries, which can be turned on and off, moved up and down in
 * priority, added and removed, and its messages. Every change to the
 * list is written to the settings file at once (config_save).
 *
 * steno & (in the background) or steno -q has no screen: messages go
 * to stderr as before. */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "steno.h"

#define LOG_MAX 64
#define LOG_LEN 256
#define INPUT_MAX 1000

int tui_on;
volatile int tui_dirty;

static struct termios saved_tty;
static char logs[LOG_MAX][LOG_LEN];
static int nlogs;
static int sel, top_row;       /* the chosen dictionary, the first shown */
static long nstrokes;
static char last_stroke[32], last_text[LOG_LEN];
static int asking;             /* 0, 'a' (a name to add), 'd' (remove?) */
static char input[INPUT_MAX];
static int ninput;

/* 1: it took the terminal */
int tui_start(void)
{
    struct termios raw;
    int r;

    if (!isatty(0) || !isatty(1))
        return 0;
    /* only in the foreground: steno & keeps quiet */
    if (tcgetpgrp(0) != getpgrp())
        return 0;
    r = tcgetattr(0, &saved_tty);
    if (r < 0)
        return 0;
    raw = saved_tty;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &raw);
    scr_colour = !getenv("NO_COLOR") || !getenv("NO_COLOR")[0];
    scr_open();
    tui_on = 1;
    tui_dirty = 1;
    return 1;
}

void tui_stop(void)
{
    if (!tui_on)
        return;
    tui_on = 0;
    scr_close();
    tcsetattr(0, TCSANOW, &saved_tty);
}

void tui_log(const char *m)
{
    if (nlogs == LOG_MAX) {
        memmove(logs[0], logs[1], (LOG_MAX - 1) * LOG_LEN);
        nlogs--;
    }
    scopy(logs[nlogs], m, LOG_LEN);
    nlogs++;
    tui_dirty = 1;
}

void tui_stroke(long st)
{
    if (!tui_on)
        return;
    nstrokes++;
    stroke_text(st, last_stroke);
    stroke_result(last_text, sizeof(last_text));
    tui_dirty = 1;
}

/* --- drawing ------------------------------------------------------- */

/* " label      value  more" in a box row of width w */
static void field(int y, int x, int w, const char *label, const char *c,
    const char *value, const char *c2, const char *more)
{
    int n;

    scr_move(y, x);
    scr_col(C_LABEL);
    n = scr_text(" ", w);
    n += scr_text(label, w - n);
    while (n < 13 && n < w) {
        scr_put(" ");
        n++;
    }
    scr_col(c);
    n += scr_text(value, w - n);
    if (more && n + 2 < w) {
        scr_put("  ");
        n += 2;
        scr_col(c2);
        n += scr_text(more, w - n);
    }
    scr_fill(' ', w - n);
}

static void draw_status(int y, int w)
{
    static const char *const keys[] = {
        "q quit", "o output", "t tape", "s suggestions", "b both"
    };
    const char *port, *why, *d;
    char s[LOG_LEN + 64];
    int i, on;

    scr_border(y, 0, w, 1, "steno", 0, 0);
    for (i = 1; i <= 5; i++)
        scr_side(y + i, 0, w);
    on = keyboard_state(&port, &why);
    if (on) {
        field(y + 1, 1, w - 2, "keyboard", C_GREEN, "connected", C_TEXT,
            port);
    }
    else {
        sprintf(s, "%.100s, looking at %.200s", why, port);
        field(y + 1, 1, w - 2, "keyboard", C_BROWN, "waiting", C_LABEL, s);
    }
    d = getenv("DISPLAY");
    sprintf(s, "%.60s", d ? d : ":0");
    field(y + 2, 1, w - 2, "X", out_fd() >= 0 ? C_GREEN : C_RED,
        out_fd() >= 0 ? "connected" : "not connected", C_TEXT, s);
    if (!output_on)
        field(y + 3, 1, w - 2, "doing", C_BROWN, "output off", C_LABEL,
            "(o, or PLOVER:TOGGLE)");
    else if (dict_raw_on())
        field(y + 3, 1, w - 2, "doing", C_BROWN, "raw mode", C_LABEL,
            "(strokes typed as steno; RA*U ends it)");
    else if (dict_modal_active())
        field(y + 3, 1, w - 2, "doing", C_TEAL, "movement mode", C_LABEL,
            "(another stroke ends it)");
    else if (!on)
        field(y + 3, 1, w - 2, "doing", C_LABEL, "nothing", C_LABEL,
            "(no keyboard)");
    else
        field(y + 3, 1, w - 2, "doing", C_GREEN, "typing", C_LABEL, 0);
    if (nstrokes)
        field(y + 4, 1, w - 2, "last", C_TEXT, last_stroke, C_TEAL,
            last_text);
    else
        field(y + 4, 1, w - 2, "last", C_LABEL, "no stroke yet", 0, 0);
    sprintf(s, "%ld strokes, %d dictionaries, %ld entries", nstrokes,
        dict_count(), dict_entries());
    field(y + 5, 1, w - 2, "since start", C_TEXT, s, 0, 0);
    scr_border(y + 6, 0, w, 0, 0, keys, 5);
}

/* home as ~, and when it is too long, its end: ..steno/user.dict */
static void short_name(const char *name, int w, char *out)
{
    const char *home;
    int n, k;

    home = getenv("HOME");
    k = home ? strlen(home) : 0;
    if (k > 1 && !strncmp(name, home, k) && name[k] == '/') {
        out[0] = '~';
        scopy(out + 1, name + k, 1000);
    } else {
        scopy(out, name, 1000);
    }
    n = scr_width(out);
    if (w > 3 && n > w) {
        k = u8back(out, strlen(out), w - 2);
        memmove(out + 2, out + strlen(out) - k, k + 1);
        out[0] = '.';
        out[1] = '.';
    }
}

static void draw_dicts(int y, int w, int h)
{
    static const char *const keys[] = {
        "j/k choose", "J/K move", "space on/off", "a add", "d remove",
        "r read again"
    };
    char s[64], num[32], shown[1100];
    const char *name;
    int i, row, n, user, inner, on, right;

    scr_border(y, 0, w, 1, "dictionaries", 0, 0);
    inner = h - 2;
    if (sel < top_row)
        top_row = sel;
    if (sel >= top_row + inner)
        top_row = sel - inner + 1;
    for (row = 0; row < inner; row++) {
        scr_side(y + 1 + row, 0, w);
        scr_move(y + 1 + row, 1);
        i = top_row + row;
        if (i >= dict_count()) {
            scr_fill(' ', w - 2);
            continue;
        }
        name = dict_name(i, &user);
        on = dict_enabled(i);
        if (i == sel)
            scr_col(C_SELECT);
        sprintf(num, i == sel ? ">%2d " : "%3d ", i + 1);
        scr_col(C_LABEL);
        n = scr_text(num, w - 2);
        scr_col(on ? C_GREEN : C_DIM);
        n += scr_text(on ? "on  " : "off ", w - 2 - n);
        sprintf(s, "%7ld  %-21s", dict_size(i), user ? "dict, -a adds here" :
            dict_kind(i));
        right = scr_width(s);
        if (w - 2 - n - right < 12)
            right = 0;         /* narrow: the name only */
        short_name(name, w - 2 - n - right - 1, shown);
        scr_col(on ? C_TEXT : C_DIM);
        n += scr_text(shown, w - 2 - n - right);
        if (right) {
            scr_fill(' ', w - 2 - n - right);
            n = w - 2 - right;
            scr_col(C_LABEL);
            n += scr_text(s, right);
        }
        scr_fill(' ', w - 2 - n);
        if (i == sel)
            scr_put(C_PLAIN);
    }
    scr_border(y + h - 1, 0, w, 0, 0, keys, 6);
}

static void draw_logs(int y, int w, int h)
{
    int i, row, inner, n;

    scr_border(y, 0, w, 1, "messages", 0, 0);
    inner = h - 2;
    i = nlogs - inner;
    if (asking)
        i++;
    if (i < 0)
        i = 0;
    for (row = 0; row < inner; row++) {
        scr_side(y + 1 + row, 0, w);
        scr_move(y + 1 + row, 1);
        n = 0;
        if (asking && row == inner - 1) {
            scr_col(C_BLUE);
            n = scr_text(asking == 'a' ? " add (name or path, Enter; Esc "
                "stops): " : " remove it? y/n ", w - 2);
            scr_col(C_TITLE);
            n += scr_text(input, w - 2 - n);
            if (asking == 'a' && n < w - 2) {
                scr_put("_");
                n++;
            }
        } else if (i < nlogs) {
            scr_col(C_TEXT);
            n = scr_text(" ", w - 2);
            n += scr_text(logs[i], w - 2 - n);
            i++;
        }
        scr_fill(' ', w - 2 - n);
    }
    scr_border(y + h - 1, 0, w, 0, 0, 0, 0);
}

void tui_draw(void)
{
    int rows, cols, hd, hl, left;

    if (!tui_on)
        return;
    tui_dirty = 0;
    scr_size(&rows, &cols);
    scr_begin();
    if (rows < 10 || cols < 30) {
        scr_move(0, 0);
        scr_col(C_LABEL);
        scr_text("steno: a bigger terminal, please (q quits)", cols);
        scr_end();
        return;
    }
    draw_status(0, cols);
    left = rows - 7;
    /* the dictionaries get what they need; messages the rest */
    hd = dict_count() + 2;
    if (hd < 3)
        hd = 3;
    hl = left - hd;
    if (hl < 4) {
        hl = left >= 10 ? 4 : 0;
        hd = left - hl;
    }
    draw_dicts(7, cols, hd);
    if (hl >= 3)
        draw_logs(7 + hd, cols, hl);
    scr_end();
}

/* --- keys ------------------------------------------------------------ */

static void save(void)
{
    int r;

    r = config_save();
    if (r == 0)
        tui_log("saved in the settings file");
}

static void answer(int c)
{
    int r, n, user;
    char m[LOG_LEN];

    if (asking == 'd') {
        if (c == 'y' || c == 'Y') {
            sprintf(m, "removed %.200s", dict_name(sel, &user));
            dict_remove(sel);
            if (sel >= dict_count() && sel > 0)
                sel--;
            tui_log(m);
            save();
        }
        asking = 0;
        return;
    }
    /* adding: a line editor, as small as it gets */
    if (c == 27) {
        asking = 0;
    } else if (c == '\r' || c == '\n') {
        asking = 0;
        if (!ninput)
            return;
        n = dict_count();
        r = add_dict_line(input, 0);
        if (r == 0) {
            sel = n;
            sprintf(m, "added %.200s (last: the lowest priority)", input);
            tui_log(m);
            save();
        }
    } else if (c == 127 || c == 8) {
        if (ninput > 0) {
            ninput--;
            input[ninput] = 0;
        }
    } else if (c == 21) {
        ninput = 0;            /* Ctrl+U */
        input[0] = 0;
    } else if (c >= 32 && c < 127 && ninput < INPUT_MAX - 1) {
        input[ninput] = (char)c;
        ninput++;
        input[ninput] = 0;
    }
}

/* -1: quit */
int tui_key(void)
{
    unsigned char b[32];
    int n, i, c, r, user;
    char m[LOG_LEN];

    n = read(0, b, sizeof(b));
    if (n <= 0)
        return n < 0 && errno == EINTR ? 0 : -1;
    tui_dirty = 1;
    for (i = 0; i < n; i++) {
        c = b[i];
        /* Shift+arrows move, as J and K: ESC [ 1 ; 2 A (st, xterm)
         * or ESC [ a (rxvt) */
        if (c == 27 && i + 5 < n && b[i + 1] == '[' && b[i + 2] == '1' &&
            b[i + 3] == ';' && b[i + 4] == '2')
        {
            c = b[i + 5] == 'A' ? 'K' : b[i + 5] == 'B' ? 'J' : 0;
            i += 5;
            if (asking || !c)
                continue;
        }
        /* the arrows choose: ESC [ A and ESC [ B (or ESC O A) */
        if (c == 27 && i + 2 < n && (b[i + 1] == '[' || b[i + 1] == 'O')) {
            c = b[i + 2] == 'A' ? 'k' : b[i + 2] == 'B' ? 'j' :
                b[i + 2] == 'a' ? 'K' : b[i + 2] == 'b' ? 'J' : 0;
            i += 2;
            if (asking || !c)
                continue;
        }
        if (asking) {
            answer(c);
            continue;
        }
        if (c == 'q' || c == 3 || c == 4)
            return -1;
        if (c == 'j' && sel + 1 < dict_count()) {
            sel++;
        } else if (c == 'k' && sel > 0) {
            sel--;
        } else if (c == 'g') {
            sel = 0;
        } else if (c == 'G' && dict_count()) {
            sel = dict_count() - 1;
        } else if ((c == 'J' || c == 'K') && dict_count()) {
            r = dict_move(sel, c == 'J' ? 1 : -1);
            if (r == 0) {
                sel += c == 'J' ? 1 : -1;
                save();
            }
        } else if (c == ' ' && dict_count()) {
            dict_set_enabled(sel, !dict_enabled(sel));
            sprintf(m, "%.200s %s%s", dict_name(sel, &user),
                dict_enabled(sel) ? "on" : "off",
                dict_raw_on() ? " (when raw mode ends)" : "");
            tui_log(m);
            save();
        } else if (c == 'a') {
            asking = 'a';
            ninput = 0;
            input[0] = 0;
        } else if (c == 'd' && dict_count() > 1) {
            asking = 'd';
            input[0] = 0;
            sprintf(m, "remove %.200s from the list? %s",
                dict_name(sel, &user), dict_is_builtin(dict_path(sel)) ?
                "(a again to bring it back)" : "(the file stays)");
            tui_log(m);
        } else if (c == 'r') {
            dict_reload();
            tui_log("read again the dictionaries that changed");
        } else if (c == 'o') {
            output_toggle();
        } else if (c == 't' || c == 's' || c == 'b') {
            /* a window of its own, where you type: -T, -S or -TS */
            open_viewer(c == 't' ? "-T" : c == 's' ? "-S" : "-TS");
            tui_log(c == 't' ? "opened the tape" : c == 's' ?
                "opened the suggestions" : "opened the tape and "
                "suggestions");
        }
    }
    return 0;
}
