/* ui.c: the screen of diary, in the look of btop with Habamax's
 * colours. The left side shows the diary (calendars, people, places,
 * search); the right side shows what is chosen, or the editor while
 * one runs there. One process, no threads: poll() on the keyboard and
 * the editor's pty. */
#define _XOPEN_SOURCE 700
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "seal.h"
#include "diary.h"
#include "data.h"
#include "ui.h"

/* Habamax (README: "Colours: Habamax everywhere") */
#define C_BG RGB(0x1c1c1c)
#define C_TEXT RGB(0xc7c7c7)
#define C_TITLE RGB(0xeeeeee)
#define C_LABEL RGB(0x9e9e9e)
#define C_DIM RGB(0x767676)
#define C_BOX RGB(0x585858)
#define C_SEL RGB(0x585858)
#define C_METER RGB(0x303030)
#define C_KEY RGB(0x5f87af)
#define C_CYAN RGB(0x5fafd7)
#define C_TEAL RGB(0x87afaf)
#define C_GREEN RGB(0x87af87)
#define C_BROWN RGB(0xaf875f)
#define C_RED RGB(0xaf5f5f)
#define C_MAGENTA RGB(0xd787d7)

#define MIN_ROWS 20
#define MIN_COLS 72
#define PLINES_MAX 3000
#define RESULTS_MAX 500
#define OUT_MAX 65536

struct config cfg;

enum { sc_days, sc_people, sc_places, sc_search };
enum { f_cal, f_list, f_up, f_notes, f_files, f_input, f_results };
enum { p_none, p_new_subj, p_new_note, p_attach, p_attach_name,
    p_rename, p_confirm_del, p_confirm_asis };
enum { k_up = 1000, k_down, k_left, k_right, k_pgup, k_pgdn, k_home,
    k_end, k_esc, k_stab, k_sup, k_sdown, k_sleft, k_sright };

/* a line of the right pane when no editor is there */
struct pline {
    char text[300];
    int fg;
    int attr;
};

struct result {
    char rel[PATH_SIZE / 8];
    char label[48];
    char snip[160];
    int line;
};

struct item {
    char name[NAME_MAX_D];
    char rel[PATH_SIZE / 8];
    char status[8];
};

static int rows, cols, lw;
static struct cell *scr, *old;
static int full_redraw;
static char outbuf[OUT_MAX];
static long outn;
static struct termios tty_orig;
static int tty_raw;
static volatile int got_winch;

static int screen = sc_days;
static int focus = f_cal;
static int right_focus;             /* keys go to the editor */
static int prefix;                  /* Ctrl-b came; 2: in the editor */
static int cur_date;                /* the chosen day, everywhere */
static int mark_date;               /* v: the start of a range */
static int day_sel, up_sel;
static int list_sel[3];             /* per kind */
static int view_subj = -1;          /* people/places: one opened */
static int notes_sel, files_sel;
static int preview_top, show_help;
static int dirty;

static struct term ed;
static int ed_running;
static char ed_title[200];

static struct pline *plines;
static int nplines;

static struct result *results;
static int nresults, res_sel;
static char query[200];
static int qlen;

static struct item notes_items[200];
static int nnotes;
static struct item file_items[300];
static int nfiles;

static int pmode;
static char plabel[200], pbuf[PATH_SIZE], pextra[PATH_SIZE];
static int plen;

static char msg[300];
static int msg_fg;

static char err_path[PATH_SIZE];

static void redraw(void);

/* ------------------------------------------------------- terminal */

static void out(const char *s, long n)
{
    if (outn + n > OUT_MAX) {
        write_all(1, outbuf, outn);
        outn = 0;
    }
    if (n > OUT_MAX) {
        write_all(1, s, n);
        return;
    }
    memcpy(outbuf + outn, s, n);
    outn += n;
}

static void outs(const char *s)
{
    out(s, strlen(s));
}

static void flush_out(void)
{
    if (outn > 0)
        write_all(1, outbuf, outn);
    outn = 0;
}

static void raw_on(void)
{
    struct termios t;
    int r;

    r = tcgetattr(0, &tty_orig);
    if (r < 0)
        return;
    t = tty_orig;
    t.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    t.c_oflag &= ~OPOST;
    t.c_cflag |= CS8;
    t.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSAFLUSH, &t);
    tty_raw = 1;
    outs("\033[?1049h\033[?25l\033[H\033[2J");
    flush_out();
    full_redraw = 1;
}

static void raw_off(void)
{
    if (!tty_raw)
        return;
    outs("\033[0m\033(B\033[?25h\033[2J\033[?1049l");
    flush_out();
    tcsetattr(0, TCSAFLUSH, &tty_orig);
    tty_raw = 0;
}

static void on_winch(int sig)
{
    (void)sig;
    got_winch = 1;
}

static void get_size(void)
{
    struct winsize ws;
    long n, i;
    int r;

    rows = 24;
    cols = 80;
    r = ioctl(1, TIOCGWINSZ, &ws);
    if (r == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        rows = ws.ws_row;
        cols = ws.ws_col;
    }
    free(scr);
    free(old);
    n = (long)rows * cols;
    scr = malloc(n * sizeof *scr);
    old = malloc(n * sizeof *old);
    if (!scr || !old) {
        raw_off();
        warn("%s", "out of memory", 0);
        exit(1);
    }
    for (i = 0; i < n; i++) {
        memset(old + i, 0, sizeof *old);
        old[i].fg = -2;
    }
    lw = cols * 2 / 5;
    if (lw < 38)
        lw = 38;
    if (lw > 62)
        lw = 62;
    if (lw > cols - 30)
        lw = cols - 30;
    full_redraw = 1;
}

/* ---------------------------------------------------- drawing */

static void set_cell(int y, int x, const char *ch, int dec, int fg, int bg,
    int attr)
{
    struct cell *c;
    int i;

    if (y < 0 || y >= rows || x < 0 || x >= cols)
        return;
    c = scr + (long)y * cols + x;
    for (i = 0; i < 4 && ch[i]; i++)
        c->ch[i] = ch[i];
    c->ch[i] = 0;
    c->dec = (char)dec;
    c->fg = fg;
    c->bg = bg;
    c->attr = (unsigned char)attr;
}

static void fill(int y, int x, int h, int w, int bg)
{
    int i, j;

    for (i = y; i < y + h; i++) {
        for (j = x; j < x + w; j++)
            set_cell(i, j, " ", 0, C_TEXT, bg, 0);
    }
}

/* the length in bytes of the character at s (UTF-8 when cfg.utf8) */
static int char_len(const char *s)
{
    int c, n, i;

    c = (unsigned char)s[0];
    if (c < 0x80 || !cfg.utf8)
        return 1;
    n = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
    for (i = 1; i < n; i++) {
        if (((unsigned char)s[i] & 0xc0) != 0x80)
            return 1;
    }
    return n;
}

/* text at y,x up to column xmax (exclusive); returns the next column */
static int put(int y, int x, int xmax, const char *s, int fg, int bg,
    int attr)
{
    char ch[5];
    int n, c;

    while (*s && x < xmax) {
        c = (unsigned char)*s;
        n = char_len(s);
        if (c == '\t') {
            ch[0] = ' ';
            ch[1] = 0;
        } else if (c < ' ' || c == 127 || (c >= 0x80 && n == 1)) {
            ch[0] = '?';
            ch[1] = 0;
        } else {
            memcpy(ch, s, n);
            ch[n] = 0;
        }
        set_cell(y, x, ch, 0, fg, bg, attr);
        s += n;
        x++;
    }
    return x;
}

/* how many columns s takes */
static int text_width(const char *s)
{
    int w;

    w = 0;
    while (*s) {
        s += char_len(s);
        w++;
    }
    return w;
}

/* "enter edit  n new": keys blue, words bright, at y,x */
static int put_hints(int y, int x, int xmax, const char *s, int bg)
{
    char word[64];
    int n, first;

    first = 1;
    while (*s && x < xmax) {
        if (s[0] == ' ' && s[1] == ' ') {
            x = put(y, x, xmax, "  ", C_TEXT, bg, 0);
            s += 2;
            first = 1;
            continue;
        }
        n = 0;
        while (s[n] && s[n] != ' ' && n < 63) {
            word[n] = s[n];
            n++;
        }
        word[n] = 0;
        x = put(y, x, xmax, word, first ? C_KEY : C_TITLE, bg, AT_BOLD);
        s += n;
        if (*s == ' ' && s[1] != ' ') {
            x = put(y, x, xmax, " ", C_TEXT, bg, 0);
            s++;
        }
        first = 0;
    }
    return x;
}

/* a box with its title in the top border, text on the right of the
 * top border, and key hints in the bottom one */
static void box(int y, int x, int h, int w, const char *title,
    const char *right, const char *hints, int focused)
{
    int i, bc, tx;

    if (h < 2 || w < 4)
        return;
    bc = focused ? C_LABEL : C_BOX;
    fill(y + 1, x + 1, h - 2, w - 2, C_BG);
    set_cell(y, x, "l", 1, bc, C_BG, 0);
    set_cell(y, x + w - 1, "k", 1, bc, C_BG, 0);
    set_cell(y + h - 1, x, "m", 1, bc, C_BG, 0);
    set_cell(y + h - 1, x + w - 1, "j", 1, bc, C_BG, 0);
    for (i = x + 1; i < x + w - 1; i++) {
        set_cell(y, i, "q", 1, bc, C_BG, 0);
        set_cell(y + h - 1, i, "q", 1, bc, C_BG, 0);
    }
    for (i = y + 1; i < y + h - 1; i++) {
        set_cell(i, x, "x", 1, bc, C_BG, 0);
        set_cell(i, x + w - 1, "x", 1, bc, C_BG, 0);
    }
    if (title && title[0]) {
        tx = put(y, x + 2, x + w - 3, "[ ", bc, C_BG, 0);
        tx = put(y, tx, x + w - 4, title, C_TITLE, C_BG, AT_BOLD);
        put(y, tx, x + w - 2, " ]", bc, C_BG, 0);
    }
    if (right && right[0]) {
        i = text_width(right);
        tx = x + w - 3 - i;
        if (tx > x + 4) {
            put(y, tx - 1, x + w - 2, " ", C_TEXT, C_BG, 0);
            put(y, tx, x + w - 2, right, C_LABEL, C_BG, 0);
            put(y, tx + i, x + w - 2, " ", C_TEXT, C_BG, 0);
        }
    }
    if (hints && hints[0]) {
        tx = put(y + h - 1, x + 2, x + w - 3, "[ ", bc, C_BG, 0);
        tx = put_hints(y + h - 1, tx, x + w - 4, hints, C_BG);
        put(y + h - 1, tx, x + w - 2, " ]", bc, C_BG, 0);
    }
}

static void put_color(int c)
{
    char b[48];

    if (c & 0x1000000)
        sprintf(b, "2;%d;%d;%d", (c >> 16) & 255, (c >> 8) & 255, c & 255);
    else
        sprintf(b, "5;%d", c & 255);
    outs(b);
}

/* send what changed to the terminal */
static void flush_screen(void)
{
    struct cell *c, *o;
    char b[64];
    int y, x, fg, bg, attr, dec, at_y, at_x;

    fg = -3;
    bg = -3;
    attr = -1;
    dec = 0;
    at_y = -1;
    at_x = -1;
    outs("\033[?25l");
    if (full_redraw)
        outs("\033[0m\033[H");
    for (y = 0; y < rows; y++) {
        for (x = 0; x < cols; x++) {
            c = scr + (long)y * cols + x;
            o = old + (long)y * cols + x;
            if (!full_redraw && c->fg == o->fg && c->bg == o->bg &&
                c->attr == o->attr && c->dec == o->dec &&
                !strcmp(c->ch, o->ch))
            {
                continue;
            }
            if (at_y != y || at_x != x) {
                sprintf(b, "\033[%d;%dH", y + 1, x + 1);
                outs(b);
            }
            if (c->attr != attr || c->fg != fg || c->bg != bg) {
                outs("\033[0");
                if (c->attr & AT_BOLD)
                    outs(";1");
                if (c->attr & AT_DIM)
                    outs(";2");
                if (c->attr & AT_ITALIC)
                    outs(";3");
                if (c->attr & AT_UNDER)
                    outs(";4");
                if (c->attr & AT_REV)
                    outs(";7");
                if (c->attr & AT_STRIKE)
                    outs(";9");
                outs(";38;");
                put_color(c->fg < 0 ? C_TEXT : c->fg);
                outs(";48;");
                put_color(c->bg < 0 ? C_BG : c->bg);
                outs("m");
                attr = c->attr;
                fg = c->fg;
                bg = c->bg;
            }
            if (c->dec != dec) {
                outs(c->dec ? "\033(0" : "\033(B");
                dec = c->dec;
            }
            outs(c->ch[0] ? c->ch : " ");
            *o = *c;
            at_y = y;
            at_x = x + 1;
        }
    }
    if (dec)
        outs("\033(B");
    full_redraw = 0;
}

/* ---------------------------------------------------- messages */

static void say(int fg, const char *fmt, const char *a)
{
    sprintf(msg, fmt, a);
    msg_fg = fg;
}

/* the last line diary or a helper wrote to stderr (into err_path) */
static void say_error(const char *what)
{
    char buf[4096];
    long n;
    char *s, *e;

    n = read_file(err_path, buf, sizeof buf - 1);
    s = buf;
    if (n > 0) {
        e = buf + n;
        while (e > buf && (e[-1] == '\n' || e[-1] == ' '))
            e--;
        *e = 0;
        s = e;
        while (s > buf && s[-1] != '\n')
            s--;
    } else {
        buf[0] = 0;
    }
    sprintf(msg, "%.100s: %.180s", what, s);
    msg_fg = C_RED;
}

/* -------------------------------------------------------- saving */

static void autosave(void)
{
    int r;

    dirty = vault_dirty();
    if (!dirty)
        return;
    say(C_LABEL, "%s", "saving ...");
    redraw();
    r = vault_save();
    if (r < 0) {
        say_error("not saved");
        return;
    }
    dirty = 0;
    say(C_GREEN, "saved into %.200s", vault_file);
}

/* ------------------------------------------------- files, paths */

static int mkdirs(const char *path)
{
    char p[PATH_SIZE];
    long i, n;

    n = strlen(path);
    if (n >= PATH_SIZE)
        return -1;
    memcpy(p, path, n + 1);
    for (i = 1; i < n; i++) {
        if (p[i] == '/') {
            p[i] = 0;
            mkdir(p, 0700);
            p[i] = '/';
        }
    }
    mkdir(p, 0700);
    return 0;
}

/* the path relative to the tree of subject s, plus rest */
static void subj_rel(char *out, int s, const char *rest)
{
    if (s == 0)
        sprintf(out, "%.200s", rest);
    else
        sprintf(out, "%s/%.120s/%.200s",
            subjs[s].kind == sk_place ? "places" : "people",
            subjs[s].slug, rest);
}

static void abs_path(char *out, const char *rel)
{
    sprintf(out, "%.3000s/%.1000s", tree_dir, rel);
}

static void day_rel(char *out, int s, int date)
{
    char d[16], rest[64];

    format_date(d, date);
    sprintf(rest, "days/%s", d);
    subj_rel(out, s, rest);
}

/* ------------------------------------------------------- editor */

static void right_size(int *h, int *w)
{
    *h = rows - 4;
    *w = cols - lw - 3;
    if (*h < 1)
        *h = 1;
    if (*w < 1)
        *w = 1;
}

/* is s safe in a Vim :set command as it is? */
static int vim_safe(const char *s)
{
    int i, c;

    for (i = 0; s[i]; i++) {
        c = (unsigned char)s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '/' || c == '.' || c == '_' ||
            c == '-'))
        {
            return 0;
        }
    }
    return 1;
}

/* edit rel (made if missing, with its directory) at line (0: the
 * last line when at_end, else the first).
 *
 * With "private yes" the editor's own state goes into <ram>/editor,
 * not onto the disk: Neovim's state directory (XDG_STATE_HOME: undo
 * files, views, logs) and, for nvim and vim, undodir, viewdir,
 * backupdir and directory. Undo, backups and swap files work as set,
 * but only while the diary is open: vault_close wipes them. */
static void edit(const char *rel, int line, int at_end)
{
    static char setcmd[4 * 3020 + 100];
    char path[PATH_SIZE], dir[PATH_SIZE], base[PATH_SIZE], plus[32];
    char state[PATH_SIZE], oldstate[PATH_SIZE];
    char *argv[24];
    const char *e, *ed_base;
    int i, n, h, w, r, had_state;

    if (ed_running) {
        say(C_BROWN, "%s", "the editor is open already: quit it first "
            "(Ctrl-\\ goes there)");
        return;
    }
    abs_path(path, rel);
    split(path, dir, base);
    mkdirs(dir);
    n = 0;
    for (i = 0; i < cfg.neditor && n < 12; i++) {
        argv[n] = cfg.editor[i];
        n++;
    }
    had_state = 0;
    ed_base = strrchr(cfg.editor[0], '/');
    ed_base = ed_base ? ed_base + 1 : cfg.editor[0];
    if (cfg.private_ed) {
        /* only vim and nvim know -i NONE (no shada/viminfo) */
        if (!strcmp(ed_base, "nvim") || !strcmp(ed_base, "vim")) {
            argv[n] = "-i";
            argv[n + 1] = "NONE";
            n += 2;
        }
        sprintf(state, "%.3000s/editor", ram_base_dir);
        mkdir(state, 0700);
        sprintf(dir, "%.3000s/editor/undo", ram_base_dir);
        mkdir(dir, 0700);
        e = getenv("XDG_STATE_HOME");
        if (e) {
            sprintf(oldstate, "%.4000s", e);
            had_state = 1;
        }
        setenv("XDG_STATE_HOME", state, 1);
        if ((!strcmp(ed_base, "nvim") || !strcmp(ed_base, "vim")) &&
            vim_safe(state))
        {
            sprintf(setcmd, "set undodir=%.3020s/undo// "
                "viewdir=%.3020s/view backupdir=%.3020s// "
                "directory=%.3020s//", state, state, state, state);
            argv[n] = "-c";
            argv[n + 1] = setcmd;
            n += 2;
        }
    }
    if (line > 0) {
        sprintf(plus, "+%d", line);
        argv[n] = plus;
        n++;
    } else if (at_end) {
        argv[n] = "+";
        n++;
    }
    argv[n] = (char *)rel;
    argv[n + 1] = 0;
    right_size(&h, &w);
    r = term_start(&ed, h, w, argv, tree_dir);
    if (cfg.private_ed && had_state)
        setenv("XDG_STATE_HOME", oldstate, 1);
    else if (cfg.private_ed)
        unsetenv("XDG_STATE_HOME");
    if (r < 0) {
        say(C_RED, "can't start %.100s", cfg.editor[0]);
        return;
    }
    ed_running = 1;
    right_focus = 1;
    sprintf(ed_title, "%.190s", rel);
    msg[0] = 0;
}

static void editor_done(void)
{
    int status;

    if (ed.pid > 0)
        waitpid(ed.pid, &status, 0);
    term_free(&ed);
    ed_running = 0;
    right_focus = 0;
    data_load();
    autosave();
}

/* --------------------------------------------------- the lists */

static void load_notes(int s)
{
    char dir[PATH_SIZE], rel[PATH_SIZE];
    struct dirent *e;
    DIR *d;
    long n;

    nnotes = 0;
    strcpy(notes_items[0].name, "note");
    subj_rel(notes_items[0].rel, s, "note");
    strcpy(notes_items[1].name, "plans");
    subj_rel(notes_items[1].rel, s, "events");
    nnotes = 2;
    subj_rel(rel, s, "notes");
    abs_path(dir, rel);
    d = opendir(dir);
    while (d && (e = readdir(d)) != 0 && nnotes < 200) {
        n = strlen(e->d_name);
        if (e->d_name[0] == '.' || n >= NAME_MAX_D ||
            e->d_name[n - 1] == '~')
        {
            continue;
        }
        memcpy(notes_items[nnotes].name, e->d_name, n + 1);
        sprintf(notes_items[nnotes].rel, "%.200s/%.120s", rel, e->d_name);
        nnotes++;
    }
    if (d)
        closedir(d);
    if (notes_sel >= nnotes)
        notes_sel = nnotes - 1;
}

/* the status of each attachment, from attached */
static void file_status(int s, const char *name, char *st)
{
    char rel[PATH_SIZE], path[PATH_SIZE], line[300];
    static char buf[65536];
    const char *p;
    long n, k;

    strcpy(st, "?");
    subj_rel(rel, s, "attached");
    abs_path(path, rel);
    n = read_file(path, buf, sizeof buf - 1);
    if (n <= 0)
        return;
    p = buf;
    while (*p) {
        k = 0;
        while (p[k] && p[k] != '\n' && k < 299) {
            line[k] = p[k];
            k++;
        }
        line[k] = 0;
        n = strlen(name);
        if (!strncmp(line, name, n) && line[n] == '\t')
            sprintf(st, "%.7s", line + n + 1);
        p += k;
        while (*p && *p != '\n')
            p++;
        if (*p)
            p++;
    }
}

static void load_files(int s)
{
    char dir[PATH_SIZE], rel[PATH_SIZE];
    struct dirent *e;
    DIR *d;
    long n;
    int i, j;
    struct item t;

    nfiles = 0;
    subj_rel(rel, s, "files");
    abs_path(dir, rel);
    d = opendir(dir);
    while (d && (e = readdir(d)) != 0 && nfiles < 300) {
        n = strlen(e->d_name);
        if (e->d_name[0] == '.' || n >= NAME_MAX_D)
            continue;
        strcpy(file_items[nfiles].name, e->d_name);
        sprintf(file_items[nfiles].rel, "%.200s/%.120s", rel, e->d_name);
        file_status(s, e->d_name, file_items[nfiles].status);
        nfiles++;
    }
    if (d)
        closedir(d);
    for (i = 1; i < nfiles; i++) {
        t = file_items[i];
        j = i - 1;
        while (j >= 0 && strcmp(file_items[j].name, t.name) > 0) {
            file_items[j + 1] = file_items[j];
            j--;
        }
        file_items[j + 1] = t;
    }
    if (files_sel >= nfiles)
        files_sel = nfiles - 1;
    if (files_sel < 0)
        files_sel = 0;
}

/* the subjects of the kind shown (people or places), by index */
static int kind_of_screen(void)
{
    return screen == sc_places ? sk_place : sk_person;
}

static int subj_count(int kind)
{
    int i, n;

    n = 0;
    for (i = 1; i < nsubj; i++) {
        if (subjs[i].kind == kind)
            n++;
    }
    return n;
}

static int subj_nth(int kind, int k)
{
    int i;

    for (i = 1; i < nsubj; i++) {
        if (subjs[i].kind == kind) {
            if (k == 0)
                return i;
            k--;
        }
    }
    return -1;
}

static int my_days_count(void)
{
    int i, n;

    n = 0;
    for (i = 0; i < ndays; i++) {
        if (days[i].owner == 0)
            n++;
    }
    return n;
}

static int my_day_nth(int k)
{
    int i;

    for (i = 0; i < ndays; i++) {
        if (days[i].owner == 0) {
            if (k == 0)
                return i;
            k--;
        }
    }
    return -1;
}

/* the events of the next 60 days, for the "coming" box: by date */
#define UP_MAX 200
static int up_date[UP_MAX], up_ev[UP_MAX];
static int nup;

static void load_upcoming(int from)
{
    int d, i, date;

    nup = 0;
    for (d = 0; d < 60 && nup < UP_MAX; d++) {
        date = date_add(from, d);
        for (i = 0; i < nevents && nup < UP_MAX; i++) {
            /* a range shows on its first day only (or today) */
            if (!event_on(events + i, date))
                continue;
            if (events[i].kind == ev_range && date != events[i].from &&
                d != 0)
            {
                continue;
            }
            up_date[nup] = date;
            up_ev[nup] = i;
            nup++;
        }
    }
    if (up_sel >= nup)
        up_sel = nup - 1;
    if (up_sel < 0)
        up_sel = 0;
}

/* --------------------------------------------------- the preview */

static void pl_add(const char *s, int fg, int attr)
{
    if (nplines >= PLINES_MAX)
        return;
    sprintf(plines[nplines].text, "%.299s", s);
    plines[nplines].fg = fg;
    plines[nplines].attr = attr;
    nplines++;
}

/* text wrapped to width w, line by line */
static void pl_text(const char *s, int w, int fg, int attr)
{
    char line[300];
    int n, cw, brk, brkn, k;

    if (!*s)
        pl_add("", fg, attr);
    while (*s) {
        n = 0;
        cw = 0;
        brk = -1;
        brkn = 0;
        while (s[n] && s[n] != '\n' && cw < w && n < 290) {
            if (s[n] == ' ') {
                brk = n;
                brkn = cw;
            }
            n += char_len(s + n);
            cw++;
        }
        if (s[n] && s[n] != '\n' && cw >= w && brk > 0 && brkn > w / 2)
            n = brk + 1;
        k = n;
        memcpy(line, s, k);
        line[k] = 0;
        pl_add(line, fg, attr);
        s += n;
        if (*s == '\n')
            s++;
    }
}

static void pl_file(const char *rel, int w, int fg)
{
    char path[PATH_SIZE];
    static char buf[FILE_MAX + 1];
    long n;

    abs_path(path, rel);
    n = read_file(path, buf, 256 * 1024);
    if (n < 0) {
        pl_add("(nothing yet: enter writes it)", C_DIM, 0);
        return;
    }
    if (n == 0) {
        pl_add("(empty)", C_DIM, 0);
        return;
    }
    pl_text(buf, w, fg, 0);
}

static const char *owner_label(int s, char *buf)
{
    if (s == 0)
        return "you";
    sprintf(buf, "%c%.60s", subjs[s].kind == sk_place ? '#' : '@',
        subjs[s].slug);
    return buf;
}

static void event_when(const struct event *e, char *buf)
{
    char a[16], b[16];
    int w, n;

    switch (e->kind) {
    case ev_range:
        format_date(a, e->from);
        format_date(b, e->to);
        sprintf(buf, "%s..%s", a + 5, b + 5);
        break;
    case ev_weekly:
        buf[0] = 0;
        n = 0;
        for (w = 0; w < 7; w++) {
            if ((e->arg >> w) & 1) {
                if (n)
                    strcat(buf, ",");
                strcat(buf, wday_name(w));
                n++;
            }
        }
        strcat(buf, " weekly");
        break;
    case ev_monthly:
        sprintf(buf, "monthly %d", e->arg);
        break;
    case ev_yearly:
        sprintf(buf, "yearly");
        break;
    default:
        buf[0] = 0;
    }
}

/* the plans on date for subject s (0: everyone's) */
static void pl_plans(int s, int date, int w)
{
    char line[400], who[80], when[64];
    int i, n;

    n = 0;
    for (i = 0; i < nevents; i++) {
        if (!event_on(events + i, date))
            continue;
        if (s > 0 && !event_for(events + i, s))
            continue;
        if (n == 0) {
            pl_add("", C_TEXT, 0);
            pl_add("plans", C_LABEL, AT_BOLD);
        }
        event_when(events + i, when);
        sprintf(line, "%-10.10s %s%s%s%s", owner_label(events[i].owner,
            who), events[i].text, when[0] ? "  (" : "", when,
            when[0] ? ")" : "");
        pl_text(line, w, C_TEAL, 0);
        n++;
    }
}

/* lines of the day file of owner o on date that mention s */
static void pl_mentions(int s, int date, int w)
{
    char rel[PATH_SIZE], path[PATH_SIZE], tok[NAME_MAX_D + 4];
    char line[300], low[300], who[80], shown_line[400];
    static char buf[FILE_MAX + 1];
    const char *p;
    long n, k, i;
    int d, o, shown;

    shown = 0;
    sprintf(tok, "%c%.120s", subjs[s].kind == sk_place ? '#' : '@',
        subjs[s].slug);
    for (d = 0; d < ndays; d++) {
        if (days[d].date != date || days[d].owner == s ||
            !day_mentions(d, s))
        {
            continue;
        }
        o = days[d].owner;
        day_rel(rel, o, date);
        abs_path(path, rel);
        n = read_file(path, buf, FILE_MAX);
        if (n <= 0)
            continue;
        if (!shown) {
            pl_add("", C_TEXT, 0);
            pl_add("in the diary", C_LABEL, AT_BOLD);
            shown = 1;
        }
        p = buf;
        while (*p) {
            k = 0;
            while (p[k] && p[k] != '\n' && k < 290) {
                line[k] = p[k];
                low[k] = (char)lower((unsigned char)p[k]);
                k++;
            }
            line[k] = 0;
            low[k] = 0;
            for (i = 0; i < (long)strlen(tok); i++)
                tok[i] = (char)lower((unsigned char)tok[i]);
            if (strstr(low, tok)) {
                sprintf(shown_line, "%-10.10s %s", owner_label(o, who),
                    line);
                pl_text(shown_line, w, C_TEXT, 0);
            }
            while (*p && *p != '\n')
                p++;
            if (*p)
                p++;
        }
    }
}

/* what others wrote about that day: the days of people and places */
static void pl_others(int date, int w)
{
    char line[300], who[80];
    int d, shown;

    shown = 0;
    for (d = 0; d < ndays; d++) {
        if (days[d].date != date || days[d].owner == 0)
            continue;
        if (!shown) {
            pl_add("", C_TEXT, 0);
            pl_add("about others", C_LABEL, AT_BOLD);
            shown = 1;
        }
        sprintf(line, "%-10.10s %s", owner_label(days[d].owner, who),
            days[d].first);
        pl_text(line, w, C_TEXT, 0);
    }
}

static void help_lines(int w)
{
    static const char *h[] = {
        "everywhere",
        "  1 2 3 4    your days, people, places, search",
        "  Ctrl-b     then, as in tmux: up down o  the boxes,",
        "             right  the editor, left  back from it,",
        "             n p 1-4  the screens, l the one before;",
        "             Ctrl-b again sends Ctrl-b to the editor",
        "  up down    in a list; home end first, last",
        "  pgup pgdn  ten lines; shift-up shift-down scroll",
        "             the right side",
        "  w          save now (it also saves after each edit)",
        "  !          a shell in the diary (exit comes back)",
        "  Ctrl-\\     to the editor and back, while it is open",
        "  q          save, wipe the RAM copy, quit",
        "",
        "calendars",
        "  left right a day back, forward; up down a week",
        "  shift-left shift-right (pgup pgdn)  a month",
        "  t          today",
        "  enter e    write that day",
        "  p          a plan on that day (opens the plans)",
        "  v          mark a day; then p plans from it to here",
        "",
        "people, places",
        "  enter      open; esc back to the list",
        "  n          new person / place; in notes: a new note",
        "  e          edit the note",
        "  D          delete (asks)",
        "  a          attach a file (cleaned by clean first)",
        "  o r        open, rename an attachment",
        "",
        "search",
        "  /          type: it searches as you type",
        "  enter      edit at that line",
        "",
        "plans (events), one a line:",
        "  2026-11-25 dentist at 10:00",
        "  2026-11-25..2026-11-28 riding trip with @mira",
        "  weekly mon,thu chess at 18:00",
        "  monthly 15 rent",
        "  yearly 03-14 birthday",
        "@mira mentions a person, #bakery a place, anywhere.",
        0
    };
    int i;

    for (i = 0; h[i]; i++)
        pl_text(h[i], w, h[i][0] == ' ' || !h[i][0] ? C_TEXT : C_LABEL,
            h[i][0] == ' ' ? 0 : AT_BOLD);
}

static void build_preview(int w, char *title)
{
    char rel[PATH_SIZE], d[16], line[400], when[64], who[80];
    int s, i;

    nplines = 0;
    title[0] = 0;
    if (show_help) {
        strcpy(title, "help");
        help_lines(w);
        return;
    }
    format_date(d, cur_date);
    if (screen == sc_days) {
        if (focus == f_up && nup > 0) {
            i = up_ev[up_sel];
            s = events[i].owner;
            subj_rel(rel, s, "events");
            sprintf(title, "%.100s", rel);
            event_when(events + i, when);
            sprintf(line, "%s %s", owner_label(s, who), events[i].text);
            pl_text(line, w, C_TEAL, 0);
            if (when[0])
                pl_text(when, w, C_LABEL, 0);
            pl_add("", C_TEXT, 0);
            pl_add("enter opens the plans at that line", C_DIM, 0);
            return;
        }
        sprintf(title, "%s %s", wday_name(date_wday(cur_date)), d);
        day_rel(rel, 0, cur_date);
        pl_file(rel, w, C_TEXT);
        pl_plans(0, cur_date, w);
        pl_others(cur_date, w);
        return;
    }
    if (screen == sc_search) {
        if (nresults == 0) {
            strcpy(title, "search");
            pl_add("type what to look for: days, notes and plans", C_DIM,
                0);
            pl_add("of everyone are searched, upper and lower case alike",
                C_DIM, 0);
            return;
        }
        sprintf(title, "%.100s", results[res_sel].rel);
        pl_file(results[res_sel].rel, w, C_TEXT);
        preview_top = results[res_sel].line - 4;
        return;
    }
    /* people, places */
    if (view_subj < 0) {
        s = subj_nth(kind_of_screen(), list_sel[kind_of_screen()]);
        if (s < 0) {
            strcpy(title, screen == sc_places ? "places" : "people");
            pl_add(screen == sc_places ? "no places yet: n makes one" :
                "nobody yet: n makes a person", C_DIM, 0);
            return;
        }
        subj_rel(rel, s, "note");
        sprintf(title, "%.100s", rel);
        pl_file(rel, w, C_TEXT);
        return;
    }
    s = view_subj;
    if (focus == f_cal) {
        sprintf(title, "%s %s  %s", wday_name(date_wday(cur_date)), d,
            owner_label(s, who));
        day_rel(rel, s, cur_date);
        pl_file(rel, w, C_TEXT);
        pl_plans(s, cur_date, w);
        pl_mentions(s, cur_date, w);
        return;
    }
    if (focus == f_notes && nnotes > 0) {
        sprintf(title, "%.100s", notes_items[notes_sel].rel);
        pl_file(notes_items[notes_sel].rel, w, C_TEXT);
        return;
    }
    if (focus == f_files && nfiles > 0) {
        sprintf(title, "%.100s", file_items[files_sel].rel);
        sprintf(line, "metadata: %s", file_items[files_sel].status);
        pl_add(line, !strcmp(file_items[files_sel].status, "clean") ||
            !strcmp(file_items[files_sel].status, "text") ? C_GREEN :
            C_BROWN, 0);
        pl_add("", C_TEXT, 0);
        pl_add("o opens it, r renames it, D deletes it", C_DIM, 0);
        return;
    }
    strcpy(title, "files");
    pl_add("no files yet: a attaches one", C_DIM, 0);
}

/* ---------------------------------------------- the left boxes */

/* the month of cur_date for subject s (0: you) */
static void draw_calendar(int y, int x, int w, int s, int focused)
{
    char title[64], num[80];
    int first, ndm, wd, d, date, row, col, fg, bg, attr, i, has, today;
    int ev, cx;

    sprintf(title, "%s %d", month_name(cur_date / 100 % 100),
        cur_date / 10000);
    box(y, x, 9, w, title, s ? owner_label(s, num) : "",
        focused ? "arrows day  shift month  p plan" : "", focused);
    cx = x + 1 + (w - 2 - 28) / 2;
    if (cx < x + 1)
        cx = x + 1;
    for (i = 0; i < 7; i++)
        put(y + 1, cx + i * 4 + 1, x + w - 1, wday_name(i), C_LABEL,
            C_BG, 0);
    first = cur_date / 100 * 100 + 1;
    ndm = days_in_month(cur_date / 10000, cur_date / 100 % 100);
    wd = date_wday(first);
    today = date_today();
    for (d = 1; d <= ndm; d++) {
        date = first + d - 1;
        row = (wd + d - 1) / 7;
        col = (wd + d - 1) % 7;
        has = 0;
        for (i = 0; i < ndays && !has; i++) {
            if (days[i].date == date && (days[i].owner == s ||
                (s > 0 && day_mentions(i, s))))
            {
                has = 1;
            }
        }
        ev = 0;
        for (i = 0; i < nevents && !ev; i++) {
            if (event_on(events + i, date) && (s == 0 ||
                event_for(events + i, s)))
            {
                ev = 1;
            }
        }
        fg = has ? C_GREEN : ev ? C_TEAL : C_TEXT;
        bg = C_BG;
        attr = has ? AT_BOLD : 0;
        if (date == today)
            fg = has ? C_GREEN : C_TITLE;
        if (date == today)
            attr |= AT_BOLD;
        if (mark_date && ((date >= mark_date && date <= cur_date) ||
            (date <= mark_date && date >= cur_date)))
        {
            bg = C_METER;
        }
        if (date == cur_date)
            bg = focused ? C_SEL : C_METER;
        sprintf(num, "%3d", d);
        put(y + 2 + row, cx + col * 4, x + w - 1, num, fg, bg, attr);
        if (date == today) {
            set_cell(y + 2 + row, cx + col * 4 + 3, "<", 0, C_DIM, bg, 0);
        }
    }
}

/* a list box: lines from line(k), the chosen one highlighted */
static void draw_list(int y, int x, int h, int w, const char *title,
    const char *right, const char *hints, int focused, int n, int sel,
    void (*line)(int k, char *buf, int *fg))
{
    char buf[400];
    int top, i, k, fg, inner;

    box(y, x, h, w, title, right, focused ? hints : "", focused);
    inner = h - 2;
    if (inner < 1)
        return;
    top = 0;
    if (sel >= inner)
        top = sel - inner + 1;
    for (i = 0; i < inner; i++) {
        k = top + i;
        if (k >= n)
            break;
        fg = C_TEXT;
        buf[0] = 0;
        line(k, buf, &fg);
        if (k == sel) {
            fill(y + 1 + i, x + 1, 1, w - 2, focused ? C_SEL : C_METER);
            put(y + 1 + i, x + 2, x + w - 2, buf, C_TITLE,
                focused ? C_SEL : C_METER, 0);
        } else {
            put(y + 1 + i, x + 2, x + w - 2, buf, fg, C_BG, 0);
        }
    }
    if (n == 0)
        put(y + 1, x + 2, x + w - 2, "(none)", C_DIM, C_BG, 0);
}

static void line_day(int k, char *buf, int *fg)
{
    struct dayent *d;
    int i;

    i = my_day_nth(k);
    if (i < 0)
        return;
    d = days + i;
    sprintf(buf, "%s %04d-%02d-%02d %5d  %s", wday_name(date_wday(d->date)),
        d->date / 10000, d->date / 100 % 100, d->date % 100, d->words,
        d->first);
    *fg = d->date == cur_date ? C_GREEN : C_TEXT;
}

static void line_up(int k, char *buf, int *fg)
{
    char who[80];
    int e;

    e = up_ev[k];
    sprintf(buf, "%s %02d-%02d  %-9.9s %s", wday_name(date_wday(
        up_date[k])), up_date[k] / 100 % 100, up_date[k] % 100,
        owner_label(events[e].owner, who), events[e].text);
    *fg = up_date[k] == date_today() ? C_TITLE : C_TEAL;
}

static void line_subj(int k, char *buf, int *fg)
{
    char last[16];
    int s;

    s = subj_nth(kind_of_screen(), k);
    if (s < 0)
        return;
    last[0] = 0;
    if (subjs[s].last)
        format_date(last, subjs[s].last);
    sprintf(buf, "%-12.12s %-18.18s %s %3d", subjs[s].slug,
        subjs[s].name[0] ? subjs[s].name : "-", last[0] ? last + 2 :
        "        ", subjs[s].nfiles);
    *fg = C_TEXT;
}

static void line_note(int k, char *buf, int *fg)
{
    sprintf(buf, "%s", notes_items[k].name);
    *fg = k < 2 ? C_LABEL : C_TEXT;
}

static void line_file(int k, char *buf, int *fg)
{
    sprintf(buf, "%-28.28s %s", file_items[k].name, file_items[k].status);
    *fg = !strcmp(file_items[k].status, "clean") ||
        !strcmp(file_items[k].status, "text") ? C_TEXT : C_BROWN;
}

static void line_result(int k, char *buf, int *fg)
{
    sprintf(buf, "%-16.16s %s", results[k].label, results[k].snip);
    *fg = C_TEXT;
}

static void draw_left(void)
{
    char title[200], right[64];
    int y, h, rest, s, k, i, n, hh;

    y = 1;
    h = rows - 2;
    if (screen == sc_days) {
        draw_calendar(y, 0, lw, 0, focus == f_cal);
        rest = h - 9;
        load_upcoming(date_today());
        hh = rest / 2;
        sprintf(right, "%d", my_days_count());
        draw_list(y + 9, 0, rest - hh, lw, "your days", right,
            "enter edit", focus == f_list, my_days_count(), day_sel,
            line_day);
        draw_list(y + 9 + rest - hh, 0, hh, lw, "coming up", "60 days",
            "enter open", focus == f_up, nup, up_sel, line_up);
        return;
    }
    if (screen == sc_search) {
        box(y, 0, 3, lw, "search", "", "", focus == f_input);
        put(y + 1, 2, lw - 2, "/ ", C_LABEL, C_BG, 0);
        put(y + 1, 4, lw - 2, query, C_TITLE, C_BG, 0);
        sprintf(right, "%d%s", nresults, nresults == RESULTS_MAX ? "+" :
            "");
        draw_list(y + 3, 0, h - 3, lw, "found", right,
            "enter edit  / type", focus == f_results, nresults, res_sel,
            line_result);
        return;
    }
    k = kind_of_screen();
    if (view_subj < 0) {
        n = subj_count(k);
        sprintf(right, "%d", n);
        draw_list(y, 0, h - 7, lw, k == sk_place ? "places" : "people",
            right, "enter open  n new  e edit  D", 1, n, list_sel[k],
            line_subj);
        s = subj_nth(k, list_sel[k]);
        box(y + h - 7, 0, 7, lw, s > 0 ? subjs[s].slug : "", "", "", 0);
        if (s > 0) {
            put(y + h - 6, 2, lw - 2, subjs[s].name[0] ? subjs[s].name :
                "(no name: e edits the note)", C_TITLE, C_BG, AT_BOLD);
            for (i = 0; i < subjs[s].nfields && i < 3; i++) {
                n = put(y + h - 5 + i, 2, lw - 2, subjs[s].fields[i],
                    C_LABEL, C_BG, 0);
                put(y + h - 5 + i, n + 1 > 12 ? n + 1 : 12, lw - 2,
                    subjs[s].values[i], C_TEXT, C_BG, 0);
            }
            put(y + h - 2, 2, lw - 2, subjs[s].summary, C_TEXT, C_BG, 0);
        }
        return;
    }
    s = view_subj;
    hh = 2 + (subjs[s].nfields < 3 ? subjs[s].nfields : 3) + 1;
    box(y, 0, hh, lw, subjs[s].slug, k == sk_place ? "place" : "person",
        "", 0);
    put(y + 1, 2, lw - 2, subjs[s].name[0] ? subjs[s].name : "(no name)",
        C_TITLE, C_BG, AT_BOLD);
    sprintf(title, "seen %d", subjs[s].seen);
    put(y + 1, lw - 2 - text_width(title), lw - 2, title, C_LABEL, C_BG,
        0);
    for (i = 0; i < subjs[s].nfields && i < 3; i++) {
        n = put(y + 2 + i, 2, lw - 2, subjs[s].fields[i], C_LABEL, C_BG, 0);
        put(y + 2 + i, n + 1 > 12 ? n + 1 : 12, lw - 2, subjs[s].values[i],
            C_TEXT, C_BG, 0);
    }
    draw_calendar(y + hh, 0, lw, s, focus == f_cal);
    rest = h - hh - 9;
    if (rest < 6)
        return;
    load_notes(s);
    load_files(s);
    n = rest / 2;
    draw_list(y + hh + 9, 0, rest - n, lw, "notes", "",
        "enter edit  n new  D", focus == f_notes, nnotes, notes_sel,
        line_note);
    sprintf(right, "%d", nfiles);
    draw_list(y + hh + 9 + rest - n, 0, n, lw, "files", right,
        "a add  o open  r name  D", focus == f_files, nfiles, files_sel,
        line_file);
}

static void draw_right(void)
{
    char title[300];
    struct cell *c;
    int y, x, h, w, i, fg;

    x = lw + 1;
    w = cols - lw - 1;
    h = rows - 2;
    if (ed_running) {
        box(1, x, h, w, ed_title, "", right_focus ?
            "Ctrl-b left back to the list" : "Ctrl-b right to the editor",
            right_focus);
        for (y = 0; y < ed.rows && y < h - 2; y++) {
            for (i = 0; i < ed.cols && i < w - 2; i++) {
                c = ed.cells + (long)y * ed.cols + i;
                set_cell(2 + y, x + 1 + i, c->ch, c->dec,
                    c->fg < 0 ? C_TEXT : c->fg,
                    c->bg < 0 ? C_BG : c->bg, c->attr);
            }
        }
        return;
    }
    build_preview(w - 4, title);
    box(1, x, h, w, title, "", show_help ? "? close" : "shift-up down scroll",
        0);
    if (preview_top > nplines - (h - 2))
        preview_top = nplines - (h - 2);
    if (preview_top < 0)
        preview_top = 0;
    for (i = 0; i < h - 2 && preview_top + i < nplines; i++) {
        fg = plines[preview_top + i].fg;
        put(2 + i, x + 2, x + w - 2, plines[preview_top + i].text, fg,
            C_BG, plines[preview_top + i].attr);
    }
}

static void draw_top(void)
{
    static const char *tabs[] = { "days", "people", "places", "search" };
    char n[8], s[64];
    int x, i, on;

    fill(0, 0, 1, cols, C_BG);
    x = put(0, 0, cols, "diary", C_TITLE, C_BG, AT_BOLD);
    x += 2;
    for (i = 0; i < 4; i++) {
        on = screen == i;
        sprintf(n, "%d", i + 1);
        x = put(0, x, cols, " ", C_TEXT, on ? C_SEL : C_BG, 0);
        x = put(0, x, cols, n, C_KEY, on ? C_SEL : C_BG, AT_BOLD);
        x = put(0, x, cols, " ", C_TEXT, on ? C_SEL : C_BG, 0);
        x = put(0, x, cols, tabs[i], on ? C_TITLE : C_TEXT,
            on ? C_SEL : C_BG, on ? AT_BOLD : 0);
        x = put(0, x, cols, " ", C_TEXT, on ? C_SEL : C_BG, 0);
        x++;
    }
    if (dirty)
        strcpy(s, "not saved");
    else if (saved_at[0])
        sprintf(s, "saved %s", saved_at);
    else
        strcpy(s, "opened");
    i = cols - 9 - text_width(s);
    if (i > x) {
        put(0, i, cols, s, dirty ? C_BROWN : C_GREEN, C_BG, 0);
        put(0, cols - 6, cols, "?", C_KEY, C_BG, AT_BOLD);
        put(0, cols - 4, cols, "help", C_TEXT, C_BG, 0);
    }
}

static void draw_bottom(void)
{
    int x;

    fill(rows - 1, 0, 1, cols, C_BG);
    if (pmode != p_none) {
        x = put(rows - 1, 0, cols, plabel, C_KEY, C_BG, AT_BOLD);
        x = put(rows - 1, x, cols, " ", C_TEXT, C_BG, 0);
        x = put(rows - 1, x, cols - 1, pbuf, C_TITLE, C_BG, 0);
        set_cell(rows - 1, x, " ", 0, C_BG, C_TITLE, 0);
        return;
    }
    if (msg[0]) {
        put(rows - 1, 0, cols, msg, msg_fg, C_BG, 0);
        return;
    }
    put_hints(rows - 1, 0, cols, ed_running && right_focus ?
        "Ctrl-b left back to the list  :wq save and close the editor" :
        "Ctrl-b up down next box  ! shell  w save  q quit", C_BG);
}

static void redraw(void)
{
    char b[32];
    int cy, cx;

    if (rows < MIN_ROWS || cols < MIN_COLS) {
        fill(0, 0, rows, cols, C_BG);
        put(0, 0, cols, "diary needs at least 72x20", C_BROWN, C_BG, 0);
        flush_screen();
        flush_out();
        return;
    }
    fill(0, 0, rows, cols, C_BG);
    draw_top();
    draw_left();
    draw_right();
    draw_bottom();
    flush_screen();
    if (ed_running && right_focus && ed.cursor_on && pmode == p_none) {
        cy = 2 + ed.cy;
        cx = lw + 2 + ed.cx;
        sprintf(b, "\033[%d;%dH\033[?25h", cy % 10000 + 1, cx % 10000 + 1);
        outs(b);
    }
    flush_out();
}

/* ------------------------------------------------------- search */

static void search_file(const char *rel, const char *label)
{
    char path[PATH_SIZE], low[300], q[200];
    static char buf[FILE_MAX + 1];
    const char *p, *hit;
    struct result *r;
    long n, k, i, off;
    int line;

    abs_path(path, rel);
    n = read_file(path, buf, FILE_MAX);
    if (n <= 0)
        return;
    for (i = 0; i < qlen; i++)
        q[i] = (char)lower((unsigned char)query[i]);
    q[qlen] = 0;
    p = buf;
    line = 1;
    while (*p && nresults < RESULTS_MAX) {
        k = 0;
        while (p[k] && p[k] != '\n' && k < 299) {
            low[k] = (char)lower((unsigned char)p[k]);
            k++;
        }
        low[k] = 0;
        hit = strstr(low, q);
        if (hit) {
            r = results + nresults;
            sprintf(r->rel, "%.400s", rel);
            sprintf(r->label, "%.47s", label);
            off = hit - low - 20;
            if (off < 0)
                off = 0;
            while (off > 0 && ((unsigned char)p[off] & 0xc0) == 0x80)
                off--;
            sprintf(r->snip, "%s%.150s", off > 0 ? "..." : "", p + off);
            for (i = 0; r->snip[i]; i++) {
                if (r->snip[i] == '\n') {
                    r->snip[i] = 0;
                    break;
                }
            }
            r->line = line;
            nresults++;
        }
        p += k;
        while (*p && *p != '\n')
            p++;
        if (*p)
            p++;
        line++;
    }
}

static void search_dir(int s, const char *sub, const char *prefix)
{
    char rel[PATH_SIZE], dir[PATH_SIZE], frel[PATH_SIZE], label[64];
    struct dirent *e;
    DIR *d;
    long n;

    subj_rel(rel, s, sub);
    abs_path(dir, rel);
    d = opendir(dir);
    while (d && (e = readdir(d)) != 0 && nresults < RESULTS_MAX) {
        n = strlen(e->d_name);
        if (e->d_name[0] == '.' || e->d_name[n - 1] == '~')
            continue;
        sprintf(frel, "%.300s/%.200s", rel, e->d_name);
        sprintf(label, "%.30s%.*s", prefix, (int)(n > 30 ? 30 : n),
            e->d_name);
        search_file(frel, label);
    }
    if (d)
        closedir(d);
}

static void run_search(void)
{
    char rel[PATH_SIZE], label[64], who[80];
    int s;

    nresults = 0;
    res_sel = 0;
    if (qlen == 0)
        return;
    search_dir(0, "days", "");
    search_file("events", "your plans");
    for (s = 1; s < nsubj && nresults < RESULTS_MAX; s++) {
        owner_label(s, who);
        subj_rel(rel, s, "note");
        search_file(rel, who);
        sprintf(label, "%.40s/", who);
        search_dir(s, "notes", label);
        search_dir(s, "days", label);
        subj_rel(rel, s, "events");
        sprintf(label, "%.40s plans", who);
        search_file(rel, label);
    }
}

/* --------------------------------------------------- prompting */

static void prompt(int mode, const char *label, const char *init)
{
    pmode = mode;
    sprintf(plabel, "%.190s", label);
    sprintf(pbuf, "%.1000s", init);
    plen = strlen(pbuf);
}

static int valid_slug(const char *s)
{
    int i;

    if (!s[0] || s[0] == '.' || strlen(s) >= 64)
        return 0;
    for (i = 0; s[i]; i++) {
        if (!is_slug_char((unsigned char)s[i]))
            return 0;
    }
    return 1;
}

static int valid_name(const char *s)
{
    int i;

    if (!s[0] || s[0] == '.' || strlen(s) >= NAME_MAX_D)
        return 0;
    for (i = 0; s[i]; i++) {
        if (s[i] == '/' || (unsigned char)s[i] < ' ')
            return 0;
    }
    return 1;
}

/* Tab: complete the path in pbuf */
static void complete_path(void)
{
    char dir[PATH_SIZE], base[PATH_SIZE], best[PATH_SIZE], full[PATH_SIZE];
    char shown[PATH_SIZE];
    struct dirent *e;
    struct stat st;
    const char *home;
    DIR *d;
    long n, k, m;
    int found, r;
    char *slash;

    home = getenv("HOME");
    if (pbuf[0] == '~' && home)
        sprintf(full, "%.2000s%.2000s", home, pbuf + 1);
    else
        sprintf(full, "%.4000s", pbuf);
    slash = strrchr(full, '/');
    if (slash) {
        n = slash - full;
        memcpy(dir, full, n);
        dir[n ? n : 1] = 0;
        if (!n)
            dir[0] = '/';
        strcpy(base, slash + 1);
    } else {
        strcpy(dir, ".");
        strcpy(base, full);
    }
    n = strlen(base);
    found = 0;
    best[0] = 0;
    d = opendir(dir);
    while (d && (e = readdir(d)) != 0) {
        if (strncmp(e->d_name, base, n))
            continue;
        if (e->d_name[0] == '.' && base[0] != '.')
            continue;
        if (!found) {
            strcpy(best, e->d_name);
        } else {
            for (k = 0; best[k] && best[k] == e->d_name[k]; k++)
                ;
            best[k] = 0;
        }
        found++;
    }
    if (d)
        closedir(d);
    if (!found)
        return;
    m = strlen(pbuf) - n;
    memcpy(shown, pbuf, m);
    sprintf(shown + m, "%.1000s", best);
    if (found == 1) {
        sprintf(full, "%.2000s/%.1000s", dir, best);
        r = stat(full, &st);
        if (r == 0 && S_ISDIR(st.st_mode))
            strcat(shown, "/");
    }
    sprintf(pbuf, "%.1000s", shown);
    plen = strlen(pbuf);
}

/* run argv, waiting; its stderr into err_path; the exit status */
static int run_wait(char **argv)
{
    pid_t pid, w;
    int status, fd;

    pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        fd = open("/dev/null", O_RDWR);
        if (fd >= 0) {
            dup2(fd, 0);
            dup2(fd, 1);
        }
        signal(SIGPIPE, SIG_DFL);
        execvp(argv[0], argv);
        fprintf(stderr, "%s: can't run it: %s\n", argv[0],
            strerror(errno));
        _exit(127);
    }
    w = waitpid(pid, &status, 0);
    if (w < 0)
        return -1;
    if (!WIFEXITED(status))
        return -1;
    return WEXITSTATUS(status);
}

/* start a viewer for an attachment, without waiting */
static void run_detached(char **argv)
{
    pid_t pid;
    int fd;

    pid = fork();
    if (pid > 0)
        waitpid(pid, &fd, 0);
    if (pid != 0)
        return;
    setsid();
    fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
    }
    pid = fork();
    if (pid != 0)
        _exit(0);
    signal(SIGPIPE, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    execvp(argv[0], argv);
    _exit(127);
}

static int is_text(const char *path)
{
    static char buf[65536];
    long n, i;
    int c;

    n = read_file(path, buf, sizeof buf - 1);
    if (n < 0)
        return 0;
    for (i = 0; i < n; i++) {
        c = (unsigned char)buf[i];
        if (c == 0 || (c < 8) || (c > 13 && c < 27 && c != 27))
            return 0;
    }
    return 1;
}

static int copy_file(const char *from, const char *to)
{
    static char buf[65536];
    long n;
    int in, outfd, r;

    in = open(from, O_RDONLY);
    if (in < 0)
        return -1;
    outfd = open(to, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (outfd < 0) {
        close(in);
        return -1;
    }
    r = 0;
    while ((n = read_full(in, buf, sizeof buf)) > 0) {
        r = write_all(outfd, buf, n);
        if (r < 0)
            break;
    }
    if (n < 0)
        r = -1;
    close(in);
    n = close(outfd);
    if (n < 0)
        r = -1;
    return r;
}

static void record(int s, const char *name, const char *status)
{
    char rel[PATH_SIZE], path[PATH_SIZE], line[300];
    int fd;

    subj_rel(rel, s, "attached");
    abs_path(path, rel);
    fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW, 0600);
    if (fd < 0)
        return;
    sprintf(line, "%.200s\t%s\n", name, status);
    write_all(fd, line, strlen(line));
    close(fd);
}

/* attached without name (and with to instead, when renaming) */
static void unrecord(int s, const char *name, const char *to)
{
    char rel[PATH_SIZE], path[PATH_SIZE], tmp[PATH_SIZE], st[16];
    static char buf[65536], outb[65536 + 400];
    const char *p;
    long n, k, o, m;
    int fd;

    st[0] = 0;
    subj_rel(rel, s, "attached");
    abs_path(path, rel);
    n = read_file(path, buf, sizeof buf - 1);
    if (n < 0)
        return;
    m = strlen(name);
    p = buf;
    o = 0;
    while (*p) {
        k = 0;
        while (p[k] && p[k] != '\n')
            k++;
        if (k > m && !strncmp(p, name, m) && p[m] == '\t') {
            sprintf(st, "%.7s", p + m + 1);
            st[k - m - 1 < 7 ? k - m - 1 : 7] = 0;
        } else {
            memcpy(outb + o, p, k);
            o += k;
            outb[o] = '\n';
            o++;
        }
        p += k;
        if (*p)
            p++;
    }
    if (to && st[0])
        o += sprintf(outb + o, "%.200s\t%s\n", to, st);
    sprintf(tmp, "%.3000s.new", path);
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return;
    write_all(fd, outb, o);
    close(fd);
    rename(tmp, path);
}

/* a: attach pextra (the source) as pbuf (the name) to view_subj; when
 * clean can't, as it is after asking (then the name is kept here) */
static char attach_name[NAME_MAX_D];

static void attach(int as_is)
{
    char rel[PATH_SIZE], dest[PATH_SIZE], dir[PATH_SIZE];
    char *argv[8];
    struct stat st;
    int r, s;

    s = view_subj;
    if (!as_is)
        sprintf(attach_name, "%.*s", NAME_MAX_D - 1, pbuf);
    sprintf(pbuf, "%s", attach_name);
    if (!valid_name(pbuf)) {
        say(C_RED, "%s", "not a name for a file (no /, not empty)");
        return;
    }
    subj_rel(rel, s, "files");
    abs_path(dir, rel);
    mkdirs(dir);
    sprintf(dest, "%.3000s/%.200s", dir, pbuf);
    r = lstat(dest, &st);
    if (r == 0) {
        say(C_RED, "%.200s is there already", pbuf);
        return;
    }
    if (as_is) {
        r = copy_file(pextra, dest);
        if (r < 0) {
            unlink(dest);
            say(C_RED, "can't copy %.200s", pextra);
            return;
        }
        record(s, pbuf, "as-is");
        say(C_BROWN, "attached as it is, with its metadata: %.200s", pbuf);
        autosave();
        return;
    }
    argv[0] = cfg.clean;
    argv[1] = "-q";
    argv[2] = "-o";
    argv[3] = dest;
    argv[4] = "--";
    argv[5] = pextra;
    argv[6] = 0;
    r = run_wait(argv);
    if (r == 0) {
        record(s, pbuf, "clean");
        say(C_GREEN, "attached, cleaned: %.200s", pbuf);
        autosave();
        return;
    }
    unlink(dest);
    if (is_text(pextra)) {
        r = copy_file(pextra, dest);
        if (r == 0) {
            record(s, pbuf, "text");
            say(C_GREEN, "attached (plain text, nothing to clean): %.200s",
                pbuf);
            autosave();
            return;
        }
        unlink(dest);
    }
    if (r == 127)
        say_error("clean can't run");
    else
        say_error("clean can't clean it");
    prompt(p_confirm_asis, "attach it as it is, metadata and all? y/n",
        "");
}

static void open_file(int k)
{
    char path[PATH_SIZE], want[40];
    char *argv[12], *ext;
    char **prog;
    int i, n;

    abs_path(path, file_items[k].rel);
    ext = strrchr(file_items[k].name, '.');
    prog = 0;
    n = 0;
    if (ext && strlen(ext) < 30) {
        sprintf(want, ",%s,", ext + 1);
        for (i = 0; want[i]; i++)
            want[i] = (char)lower((unsigned char)want[i]);
        for (i = 0; i < cfg.nviewers; i++) {
            if (strstr(cfg.viewer_ext[i], want)) {
                prog = cfg.viewer[i];
                n = cfg.nviewer_args[i];
                break;
            }
        }
    }
    if (ext && (!strcmp(ext, ".txt") || !strcmp(ext, ".md"))) {
        edit(file_items[k].rel, 0, 0);
        return;
    }
    if (!prog) {
        say(C_BROWN, "no program for %.100s in the config (viewer)",
            ext ? ext : "files without an ending");
        return;
    }
    for (i = 0; i < n && i < 10; i++)
        argv[i] = prog[i];
    argv[n] = path;
    argv[n + 1] = 0;
    run_detached(argv);
    say(C_LABEL, "opened with %.100s", argv[0]);
}

/* -------------------------------------------------------- shell */

static void shell(void)
{
    char hist[PATH_SIZE];
    const char *sh;
    pid_t pid;
    int status;
    struct sigaction sa, oldint, oldquit;

    raw_off();
    printf("A shell in the diary (in RAM): exit comes back.\n");
    fflush(stdout);
    sprintf(hist, "%.3000s/history", ram_base_dir);
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, &oldint);
    sigaction(SIGQUIT, &sa, &oldquit);
    pid = fork();
    if (pid == 0) {
        status = chdir(tree_dir);
        if (status < 0)
            _exit(127);
        setenv("HISTFILE", hist, 1);
        dup2(1, 2);
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGHUP, SIG_DFL);
        sh = cfg.shell[0] ? cfg.shell : getenv("SHELL");
        if (!sh || !sh[0])
            sh = "/bin/sh";
        execl(sh, sh, (char *)0);
        _exit(127);
    }
    if (pid > 0)
        waitpid(pid, &status, 0);
    sigaction(SIGINT, &oldint, 0);
    sigaction(SIGQUIT, &oldquit, 0);
    wipe_tree(hist);
    raw_on();
    data_load();
    autosave();
}

/* ---------------------------------------------------------- keys */

static void move_date(long n)
{
    cur_date = date_add(cur_date, n);
    preview_top = 0;
}

static void move_month(int n)
{
    int y, m, d, dm;

    y = cur_date / 10000;
    m = cur_date / 100 % 100 + n;
    d = cur_date % 100;
    while (m < 1) {
        m += 12;
        y--;
    }
    while (m > 12) {
        m -= 12;
        y++;
    }
    dm = days_in_month(y, m);
    if (d > dm)
        d = dm;
    cur_date = y * 10000 + m * 100 + d;
    preview_top = 0;
}

/* p: add a plan on cur_date (or from mark_date) for subject s */
static void add_plan(int s)
{
    char rel[PATH_SIZE], path[PATH_SIZE], a[16], b[16], line[64];
    struct stat st;
    long n, k;
    int fd, from, to, r;
    char last;

    if (ed_running) {
        say(C_BROWN, "%s", "the editor is open already: quit it first "
            "(Ctrl-\\ goes there)");
        return;
    }
    subj_rel(rel, s, "events");
    abs_path(path, rel);
    from = cur_date;
    to = cur_date;
    if (mark_date) {
        from = mark_date < cur_date ? mark_date : cur_date;
        to = mark_date < cur_date ? cur_date : mark_date;
    }
    format_date(a, from);
    format_date(b, to);
    if (from == to)
        sprintf(line, "%s ", a);
    else
        sprintf(line, "%s..%s ", a, b);
    fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW, 0600);
    if (fd < 0) {
        say(C_RED, "can't write %.200s", rel);
        return;
    }
    n = 0;
    r = fstat(fd, &st);
    if (r == 0)
        n = (long)st.st_size;
    /* a newline before it, unless the file ends with one */
    last = '\n';
    r = n > 0 ? open(path, O_RDONLY) : -1;
    if (r >= 0) {
        lseek(r, n - 1, SEEK_SET);
        k = read(r, &last, 1);
        if (k != 1)
            last = '\n';
        close(r);
    }
    if (last != '\n')
        write_all(fd, "\n", 1);
    write_all(fd, line, strlen(line));
    write_all(fd, "\n", 1);
    close(fd);
    mark_date = 0;
    edit(rel, 0, 1);
}

static void new_subject(void)
{
    char rel[PATH_SIZE], path[PATH_SIZE], d[16], text[300];
    int fd, k;

    k = kind_of_screen();
    if (!valid_slug(pbuf)) {
        say(C_RED, "%s", "a short name of letters, digits, - _ . "
            "(it is what @name or #name finds)");
        return;
    }
    for (fd = 0; pbuf[fd]; fd++)
        pbuf[fd] = (char)lower((unsigned char)pbuf[fd]);
    if (find_subj(k, pbuf) > 0) {
        say(C_RED, "%.100s is there already", pbuf);
        return;
    }
    sprintf(rel, "%s/%.100s/note", k == sk_place ? "places" :
        "people", pbuf);
    abs_path(path, rel);
    format_date(d, date_today());
    if (k == sk_place)
        sprintf(text, "name: \nkind: \naddress: \n--\n\n");
    else
        sprintf(text, "name: \nmet: %s\nbirthday: \n--\n\n", d);
    {
        char dir[PATH_SIZE], base[PATH_SIZE];

        split(path, dir, base);
        mkdirs(dir);
    }
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) {
        say(C_RED, "can't make %.200s", rel);
        return;
    }
    write_all(fd, text, strlen(text));
    close(fd);
    data_load();
    for (fd = 0; fd < subj_count(k); fd++) {
        if (!strcmp(subjs[subj_nth(k, fd)].slug, pbuf))
            list_sel[k] = fd;
    }
    edit(rel, 1, 0);
}

static void delete_chosen(void)
{
    char path[PATH_SIZE];
    int s, k;

    k = kind_of_screen();
    if (screen == sc_people || screen == sc_places) {
        if (view_subj < 0) {
            s = subj_nth(k, list_sel[k]);
            if (s < 0)
                return;
            subj_dir(path, s);
            wipe_tree(path);
            if (list_sel[k] > 0)
                list_sel[k]--;
            say(C_LABEL, "%s: deleted", subjs[s].slug);
        } else if (focus == f_notes && notes_sel >= 0) {
            abs_path(path, notes_items[notes_sel].rel);
            wipe_tree(path);
            say(C_LABEL, "%s: deleted", notes_items[notes_sel].name);
        } else if (focus == f_files && nfiles > 0) {
            abs_path(path, file_items[files_sel].rel);
            wipe_tree(path);
            unrecord(view_subj, file_items[files_sel].name, 0);
            say(C_LABEL, "%s: deleted", file_items[files_sel].name);
        }
    } else if (screen == sc_days) {
        abs_path(path, pextra);
        wipe_tree(path);
        say(C_LABEL, "%.200s: deleted", pextra);
    }
    data_load();
    autosave();
}

static void prompt_done(void)
{
    char rel[PATH_SIZE], path[PATH_SIZE], from[PATH_SIZE];
    const char *home, *slash;
    struct stat st;
    int mode, r;

    mode = pmode;
    pmode = p_none;
    msg[0] = 0;
    switch (mode) {
    case p_new_subj:
        new_subject();
        break;
    case p_new_note:
        if (!valid_name(pbuf) || strchr(pbuf, ' ')) {
            say(C_RED, "%s", "a name without spaces or /");
            break;
        }
        sprintf(path, "notes/%.100s", pbuf);
        subj_rel(rel, view_subj, path);
        edit(rel, 0, 0);
        break;
    case p_attach:
        home = getenv("HOME");
        if (pbuf[0] == '~' && home)
            sprintf(pextra, "%.2000s%.2000s", home, pbuf + 1);
        else
            sprintf(pextra, "%.4000s", pbuf);
        r = stat(pextra, &st);
        if (r < 0 || !S_ISREG(st.st_mode)) {
            say(C_RED, "%.200s: not a file", pbuf);
            break;
        }
        slash = strrchr(pextra, '/');
        prompt(p_attach_name, "its name in the diary:", slash ? slash + 1 :
            pextra);
        break;
    case p_attach_name:
        attach(0);
        break;
    case p_confirm_asis:
        if (pbuf[0] == 'y' || pbuf[0] == 'Y')
            attach(1);
        else
            say(C_LABEL, "%s", "not attached");
        break;
    case p_rename:
        if (!valid_name(pbuf) || nfiles == 0)
            break;
        abs_path(from, file_items[files_sel].rel);
        subj_rel(rel, view_subj, "files/");
        strcat(rel, pbuf);
        abs_path(path, rel);
        r = lstat(path, &st);
        if (r == 0) {
            say(C_RED, "%.200s is there already", pbuf);
            break;
        }
        r = rename(from, path);
        if (r < 0) {
            say(C_RED, "can't rename: %s", strerror(errno));
            break;
        }
        unrecord(view_subj, file_items[files_sel].name, pbuf);
        data_load();
        autosave();
        break;
    case p_confirm_del:
        if (pbuf[0] == 'y' || pbuf[0] == 'Y')
            delete_chosen();
        else
            say(C_LABEL, "%s", "nothing deleted");
        break;
    default:
        break;
    }
}

static void prompt_key(int c)
{
    if (pmode == p_confirm_del || pmode == p_confirm_asis) {
        pbuf[0] = (char)c;
        pbuf[1] = 0;
        prompt_done();
        return;
    }
    if (c == k_esc || c == 7) {
        pmode = p_none;
        say(C_LABEL, "%s", "");
        return;
    }
    if (c == '\r' || c == '\n') {
        prompt_done();
        return;
    }
    if (c == 127 || c == 8) {
        if (plen > 0) {
            plen--;
            while (plen > 0 && ((unsigned char)pbuf[plen] & 0xc0) == 0x80)
                plen--;
            pbuf[plen] = 0;
        }
        return;
    }
    if (c == 21) {
        plen = 0;
        pbuf[0] = 0;
        return;
    }
    if (c == 23) {
        while (plen > 0 && pbuf[plen - 1] == ' ')
            plen--;
        while (plen > 0 && pbuf[plen - 1] != ' ' && pbuf[plen - 1] != '/')
            plen--;
        pbuf[plen] = 0;
        return;
    }
    if (c == '\t' && pmode == p_attach) {
        complete_path();
        return;
    }
    if (c >= ' ' && c < 256 && c != 127 && plen < 1000) {
        pbuf[plen] = (char)c;
        plen++;
        pbuf[plen] = 0;
    }
}

static void search_key(int c)
{
    if (focus == f_input) {
        if (c == '\r' || c == k_down) {
            if (nresults > 0)
                focus = f_results;
            return;
        }
        if (c == k_esc) {
            if (nresults > 0)
                focus = f_results;
            return;
        }
        if (c == 127 || c == 8) {
            if (qlen > 0) {
                qlen--;
                while (qlen > 0 && ((unsigned char)query[qlen] & 0xc0) ==
                    0x80)
                {
                    qlen--;
                }
            }
        } else if (c == 21) {
            qlen = 0;
        } else if (c >= ' ' && c < 256 && c != 127 && qlen < 190) {
            query[qlen] = (char)c;
            qlen++;
        } else {
            return;
        }
        query[qlen] = 0;
        run_search();
        preview_top = 0;
        return;
    }
    switch (c) {
    case k_down:
        if (res_sel < nresults - 1)
            res_sel++;
        break;
    case k_up:
        if (res_sel > 0)
            res_sel--;
        else
            focus = f_input;
        break;
    case k_home:
        res_sel = 0;
        break;
    case k_end:
        res_sel = nresults > 0 ? nresults - 1 : 0;
        break;
    case '/': case 'i':
        focus = f_input;
        break;
    case '\r': case 'e':
        if (nresults > 0)
            edit(results[res_sel].rel, results[res_sel].line, 0);
        break;
    default:
        break;
    }
}

static void calendar_key(int c, int s)
{
    char rel[PATH_SIZE];

    switch (c) {
    case k_left: move_date(-1); break;
    case k_right: move_date(1); break;
    case k_down: move_date(7); break;
    case k_up: move_date(-7); break;
    case k_sleft: case k_pgup: move_month(-1); break;
    case k_sright: case k_pgdn: move_month(1); break;
    case 't': cur_date = date_today(); break;
    case 'v':
        mark_date = mark_date ? 0 : cur_date;
        say(C_LABEL, "%s", mark_date ? "marked: move to the last day, "
            "then p" : "");
        break;
    case 'p':
        add_plan(s);
        break;
    case '\r': case 'e':
        day_rel(rel, s, cur_date);
        edit(rel, 0, 0);
        break;
    case k_esc:
        mark_date = 0;
        break;
    default:
        break;
    }
}

static int list_move(int c, int *sel, int n)
{
    switch (c) {
    case k_down:
        if (*sel < n - 1)
            (*sel)++;
        return 1;
    case k_up:
        if (*sel > 0)
            (*sel)--;
        return 1;
    case k_home:
        *sel = 0;
        return 1;
    case k_end:
        *sel = n > 0 ? n - 1 : 0;
        return 1;
    case k_pgdn: case 4:
        *sel += 10;
        if (*sel > n - 1)
            *sel = n > 0 ? n - 1 : 0;
        return 1;
    case k_pgup: case 21:
        *sel -= 10;
        if (*sel < 0)
            *sel = 0;
        return 1;
    default:
        return 0;
    }
}

static void next_focus(int back)
{
    static const int days_f[] = { f_cal, f_list, f_up };
    static const int subj_f[] = { f_cal, f_notes, f_files };
    const int *f;
    int i, n;

    if (screen == sc_search) {
        focus = focus == f_input ? f_results : f_input;
        return;
    }
    if ((screen == sc_people || screen == sc_places) && view_subj < 0)
        return;
    f = screen == sc_days ? days_f : subj_f;
    n = 3;
    for (i = 0; i < n && f[i] != focus; i++)
        ;
    i = back ? (i + n - 1) % n : (i + 1) % n;
    focus = f[i];
    preview_top = 0;
}

static void days_key(int c)
{
    char rel[PATH_SIZE];
    int i, e;

    if (focus == f_cal) {
        calendar_key(c, 0);
        return;
    }
    if (focus == f_list) {
        if (list_move(c, &day_sel, my_days_count())) {
            i = my_day_nth(day_sel);
            if (i >= 0)
                cur_date = days[i].date;
            preview_top = 0;
            return;
        }
        if (c == '\r' || c == 'e') {
            day_rel(rel, 0, cur_date);
            edit(rel, 0, 0);
        } else if (c == 'D') {
            day_rel(pextra, 0, cur_date);
            prompt(p_confirm_del, "delete this day? y/n", "");
        }
        return;
    }
    if (list_move(c, &up_sel, nup))
        return;
    if ((c == '\r' || c == 'e') && nup > 0) {
        e = up_ev[up_sel];
        subj_rel(rel, events[e].owner, "events");
        edit(rel, events[e].line, 0);
    }
}

static void subj_key(int c)
{
    char rel[PATH_SIZE];
    int k, s;

    k = kind_of_screen();
    if (view_subj < 0) {
        if (list_move(c, &list_sel[k], subj_count(k))) {
            preview_top = 0;
            return;
        }
        s = subj_nth(k, list_sel[k]);
        if ((c == '\r' || c == k_right) && s > 0) {
            view_subj = s;
            focus = f_cal;
            notes_sel = 0;
            files_sel = 0;
            preview_top = 0;
        } else if (c == 'n') {
            prompt(p_new_subj, k == sk_place ? "new place, short name "
                "(#name finds it):" : "new person, short name (@name "
                "finds them):", "");
        } else if (c == 'e' && s > 0) {
            subj_rel(rel, s, "note");
            edit(rel, 0, 0);
        } else if (c == 'D' && s > 0) {
            prompt(p_confirm_del, "delete all about them, files too? y/n",
                "");
        }
        return;
    }
    s = view_subj;
    if (c == k_esc || c == 127 || c == 8) {
        view_subj = -1;
        focus = f_cal;
        mark_date = 0;
        preview_top = 0;
        return;
    }
    if (focus == f_cal) {
        calendar_key(c, s);
        return;
    }
    if (focus == f_notes) {
        if (list_move(c, &notes_sel, nnotes)) {
            preview_top = 0;
            return;
        }
        if ((c == '\r' || c == 'e') && nnotes > 0)
            edit(notes_items[notes_sel].rel, 0, 0);
        else if (c == 'n')
            prompt(p_new_note, "new note, like appearance or schedule:",
                "");
        else if (c == 'D' && notes_sel >= 2)
            prompt(p_confirm_del, "delete this note? y/n", "");
        return;
    }
    if (list_move(c, &files_sel, nfiles)) {
        preview_top = 0;
        return;
    }
    if (c == 'a') {
        prompt(p_attach, "attach (tab completes):", "~/");
    } else if (c == 'o' || c == '\r') {
        if (nfiles > 0)
            open_file(files_sel);
    } else if (c == 'r' && nfiles > 0) {
        prompt(p_rename, "new name:", file_items[files_sel].name);
    } else if (c == 'D' && nfiles > 0) {
        prompt(p_confirm_del, "delete this file (wiped)? y/n", "");
    }
}

static void go_screen(int sc)
{
    static int last;

    /* -1: back to the screen before, like tmux's last window */
    if (sc < 0)
        sc = last;
    if (sc != screen)
        last = screen;
    screen = sc;
    show_help = 0;
    preview_top = 0;
    if (screen == sc_search)
        focus = nresults ? f_results : f_input;
    else
        focus = f_cal;
}

/* the key after Ctrl-b: tmux's own keys for its panes and windows,
 * so the Moonlander's tmux layer works here too */
static void prefix_key(int c, int from_ed)
{
    switch (c) {
    case k_up:
        next_focus(1);
        return;
    case k_down: case 'o':
        next_focus(0);
        return;
    case k_left:
        return;
    case 'n':
        go_screen((screen + 1) % 4);
        return;
    case 'p':
        go_screen((screen + 3) % 4);
        return;
    case 'l':
        go_screen(-1);
        return;
    case '1': case '2': case '3': case '4':
        go_screen(c - '1');
        return;
    case 2:
        if (from_ed)
            write_all(ed.fd, "\002", 1);
        break;
    default:
        break;
    }
    /* right, Ctrl-b again or another key: back to (or into) the
     * editor when it is open */
    if (ed_running && (from_ed || c == k_right))
        right_focus = 1;
}

/* 1 when q asked to quit */
static int key(int c)
{
    int p;

    if (prefix && c) {
        p = prefix;
        prefix = 0;
        prefix_key(c, p == 2);
        return 0;
    }
    if (c == 2 && pmode == p_none) {
        prefix = 1;
        return 0;
    }
    if (pmode != p_none) {
        prompt_key(c);
        return 0;
    }
    if (screen == sc_search && focus == f_input && c != k_sup &&
        c != k_sdown && !(c >= '1' && c <= '4' && qlen == 0))
    {
        if (c == 28 && ed_running) {
            right_focus = 1;
            return 0;
        }
        search_key(c);
        return 0;
    }
    if (c != 0)
        msg[0] = 0;
    switch (c) {
    case 'q':
        if (ed_running) {
            say(C_BROWN, "%s", "the editor is still open: quit it first "
                "(Ctrl-\\ goes there)");
            return 0;
        }
        return 1;
    case '1': case '2': case '3': case '4':
        go_screen(c - '1');
        return 0;
    case '/':
        go_screen(sc_search);
        focus = f_input;
        return 0;
    case '?':
        show_help = !show_help;
        preview_top = 0;
        return 0;
    case k_sdown:
        preview_top++;
        return 0;
    case k_sup:
        if (preview_top > 0)
            preview_top--;
        return 0;
    case 'w':
        dirty = 1;
        say(C_LABEL, "%s", "saving ...");
        redraw();
        if (vault_save() < 0) {
            say_error("not saved");
        } else {
            dirty = 0;
            say(C_GREEN, "saved into %.200s", vault_file);
        }
        return 0;
    case '!':
        if (ed_running) {
            say(C_BROWN, "%s", "quit the editor first");
            return 0;
        }
        shell();
        return 0;
    case 12:
        full_redraw = 1;
        return 0;
    case 28:
        if (ed_running)
            right_focus = 1;
        return 0;
    default:
        break;
    }
    if (show_help && c == k_esc) {
        show_help = 0;
        return 0;
    }
    if (screen == sc_days)
        days_key(c);
    else if (screen == sc_search)
        search_key(c);
    else
        subj_key(c);
    return 0;
}

/* split what was read into keys; escape sequences to k_* */
static int decode(const unsigned char *b, int n, int *i)
{
    int c;

    c = b[*i];
    (*i)++;
    if (c != 27)
        return c;
    if (*i >= n)
        return k_esc;
    if (b[*i] != '[' && b[*i] != 'O')
        return k_esc;
    (*i)++;
    if (*i >= n)
        return 0;
    c = b[*i];
    (*i)++;
    switch (c) {
    case 'A': return k_up;
    case 'B': return k_down;
    case 'C': return k_right;
    case 'D': return k_left;
    case 'H': return k_home;
    case 'F': return k_end;
    case 'Z': return k_stab;
    default: break;
    }
    if (c >= '0' && c <= '9') {
        int v, mod, fin;

        v = c - '0';
        while (*i < n && b[*i] >= '0' && b[*i] <= '9') {
            v = v * 10 + (b[*i] - '0');
            (*i)++;
        }
        /* ESC [ 1 ; 2 A: an arrow with a modifier, 2 is shift */
        mod = 0;
        if (*i < n && b[*i] == ';') {
            (*i)++;
            while (*i < n && b[*i] >= '0' && b[*i] <= '9') {
                mod = mod * 10 + (b[*i] - '0');
                (*i)++;
            }
        }
        while (*i < n && b[*i] < '@')
            (*i)++;
        fin = 0;
        if (*i < n) {
            fin = b[*i];
            (*i)++;
        }
        switch (fin) {
        case 'A': return mod == 2 ? k_sup : k_up;
        case 'B': return mod == 2 ? k_sdown : k_down;
        case 'C': return mod == 2 ? k_sright : k_right;
        case 'D': return mod == 2 ? k_sleft : k_left;
        case 'H': return k_home;
        case 'F': return k_end;
        default: break;
        }
        switch (v) {
        case 1: case 7: return k_home;
        case 4: case 8: return k_end;
        case 5: return k_pgup;
        case 6: return k_pgdn;
        default: return 0;
        }
    }
    return 0;
}

/* ------------------------------------------------------- the loop */

int ui_run(void)
{
    struct pollfd pf[2];
    struct sigaction sa;
    unsigned char buf[4096];
    long n;
    int i, c, r, np, h, w, quit, fd;

    sprintf(err_path, "%.3000s/errors", ram_base_dir);
    fd = open(err_path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd >= 0) {
        dup2(fd, 2);
        close(fd);
    }
    plines = malloc(PLINES_MAX * sizeof *plines);
    results = malloc(RESULTS_MAX * sizeof *results);
    if (!plines || !results) {
        warn("%s", "out of memory", 0);
        return 1;
    }
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_winch;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, 0);
    cur_date = date_today();
    data_load();
    dirty = vault_dirty();
    get_size();
    raw_on();
    quit = 0;
    while (!quit && !got_signal) {
        if (got_winch) {
            got_winch = 0;
            get_size();
            if (ed_running) {
                right_size(&h, &w);
                term_resize(&ed, h, w);
            }
        }
        redraw();
        pf[0].fd = 0;
        pf[0].events = POLLIN;
        np = 1;
        if (ed_running) {
            pf[1].fd = ed.fd;
            pf[1].events = POLLIN;
            np = 2;
        }
        r = poll(pf, np, -1);
        if (r < 0)
            continue;
        if (np == 2 && (pf[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            n = read(ed.fd, buf, sizeof buf);
            if (n > 0) {
                term_feed(&ed, (char *)buf, n);
            } else if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
                editor_done();
                continue;
            }
        }
        if (pf[0].revents & POLLIN) {
            n = read(0, buf, sizeof buf);
            if (n <= 0) {
                if (n < 0 && errno == EINTR)
                    continue;
                break;
            }
            i = 0;
            if (ed_running && right_focus) {
                /* all to the editor but Ctrl-\ and Ctrl-b (tmux's
                 * prefix: the next key may leave the editor) */
                for (i = 0; i < n && buf[i] != 28 && buf[i] != 2; i++)
                    ;
                if (i > 0)
                    write_all(ed.fd, buf, i);
                if (i == n)
                    continue;
                right_focus = 0;
                if (buf[i] == 28)
                    continue;
                prefix = 2;
                i++;
            }
            while (i < n && !quit) {
                if (ed_running && right_focus) {
                    /* the prefix gave the editor back: the rest is
                     * its own */
                    write_all(ed.fd, buf + i, n - i);
                    break;
                }
                c = decode(buf, (int)n, &i);
                if (c)
                    quit = key(c);
            }
        }
    }
    if (ed_running) {
        kill(ed.pid, SIGTERM);
        editor_done();
    }
    raw_off();
    return 0;
}
