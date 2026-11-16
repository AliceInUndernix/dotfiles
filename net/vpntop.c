/*
 * vpntop: the screen of vpn, in the style of top. Part of net/ in the
 * dotfiles. "vpn" (as root) runs it; "vpn watch" runs "vpntop -w" for
 * anyone, which shows only what everyone may see.
 *
 * It reads what the vpn service writes in /run/vpn (status, events)
 * and the kernel's counters, redraws twice a second, and runs vpn's
 * own commands for the keys. One process, no threads, no libraries
 * but a little of libc, plain ANSI terminal codes.
 *
 *   vpntop       the whole screen, with keys (root)
 *   vpntop -w    the short one, no keys but q (anyone)
 */

/* SIGWINCH and the like are not in C89 headers */
#define _DEFAULT_SOURCE

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <string.h>
#include <linux/fs.h>

#define VPN_PATH "/usr/local/bin/vpn"
#define RUN_DIR "/run/vpn"
#define ETC_DIR "/etc/vpn"
#define TESTS_MAX 3
#define OUT_MAX 262144
#define STAT_MAX 16384
#define KEYS_MAX 64
#define EVENTS_MAX 65536
#define EV_LINES 1024
#define HIST_MAX 512
#define MSG_MAX 512
#define SMALL_MAX 256
#define MIN_COLS 70
#define MIN_ROWS 20

/* --- colours: Habamax, the same as btop/themes/habamax.theme -------- */

struct rgb {
    unsigned char r, g, b;
};

/* fixed colours; c_none is the normal text */
enum {
    c_def, c_none, c_title, c_hi, c_sel, c_dim, c_label, c_meter, c_misc,
    c_box, c_green, c_yellow, c_red, c_blue, c_cyan, c_magenta, c_bg,
    c_bold, pal_fixed
};

#define GRAD_N 12
enum { g_down, g_up, g_load, g_good, grads };

#define PAL_MAX (pal_fixed + grads * GRAD_N)

static struct rgb pal[PAL_MAX] = {
    { 0, 0, 0 },                /* the terminal's own */
    { 0xc7, 0xc7, 0xc7 },       /* main_fg */
    { 0xee, 0xee, 0xee },       /* title */
    { 0x5f, 0x87, 0xaf },       /* hi_fg: keys */
    { 0x58, 0x58, 0x58 },       /* selected_bg */
    { 0x76, 0x76, 0x76 },       /* inactive_fg */
    { 0x9e, 0x9e, 0x9e },       /* graph_text: labels */
    { 0x30, 0x30, 0x30 },       /* meter_bg */
    { 0x87, 0xaf, 0xaf },       /* proc_misc */
    { 0x58, 0x58, 0x58 },       /* the boxes */
    { 0x87, 0xaf, 0x87 },       /* green */
    { 0xaf, 0x87, 0x5f },       /* yellow (brown) */
    { 0xaf, 0x5f, 0x5f },       /* red */
    { 0x5f, 0x87, 0xaf },       /* blue */
    { 0x5f, 0xaf, 0xd7 },       /* cyan */
    { 0xd7, 0x87, 0xd7 },       /* magenta */
    { 0x1c, 0x1c, 0x1c },       /* main_bg, for text on a colour */
    { 0xee, 0xee, 0xee }        /* bold text */
};

/* start, middle, end of each gradient (btop's download, upload, cpu
 * and available) */
static const struct rgb grad_ends[grads][3] = {
    { { 0x5f, 0x87, 0xaf }, { 0x5f, 0xaf, 0xd7 }, { 0x87, 0xaf, 0xaf } },
    { { 0xaf, 0x87, 0xaf }, { 0xd7, 0x87, 0xd7 }, { 0xd7, 0x87, 0xd7 } },
    { { 0x87, 0xaf, 0x87 }, { 0xaf, 0x87, 0x5f }, { 0xaf, 0x5f, 0x5f } },
    { { 0x5f, 0x87, 0x87 }, { 0x87, 0xaf, 0x87 }, { 0x87, 0xd7, 0x87 } }
};

static int mix(int a, int b, int i, int n)
{
    return a + (b - a) * i / n;
}

static void make_gradients(void)
{
    int g, i, half;
    const struct rgb *a, *b;
    struct rgb *p;

    half = (GRAD_N - 1) / 2;
    for (g = 0; g < grads; g++) {
        for (i = 0; i < GRAD_N; i++) {
            p = &pal[pal_fixed + g * GRAD_N + i];
            if (i <= half) {
                a = &grad_ends[g][0];
                b = &grad_ends[g][1];
                p->r = (unsigned char)mix(a->r, b->r, i, half);
                p->g = (unsigned char)mix(a->g, b->g, i, half);
                p->b = (unsigned char)mix(a->b, b->b, i, half);
            } else {
                a = &grad_ends[g][1];
                b = &grad_ends[g][2];
                p->r = (unsigned char)mix(a->r, b->r, i - half,
                    GRAD_N - 1 - half);
                p->g = (unsigned char)mix(a->g, b->g, i - half,
                    GRAD_N - 1 - half);
                p->b = (unsigned char)mix(a->b, b->b, i - half,
                    GRAD_N - 1 - half);
            }
        }
    }
}

/* grad: colour of gradient g at level v of n (0 .. n - 1) */
static int grad(int g, int v, int n)
{
    int i;

    if (n <= 1)
        i = GRAD_N - 1;
    else
        i = v * (GRAD_N - 1) / (n - 1);
    if (i < 0)
        i = 0;
    if (i > GRAD_N - 1)
        i = GRAD_N - 1;
    return pal_fixed + g * GRAD_N + i;
}

/* --- output: everything goes to one buffer, written at once ----- */

static char out_buf[OUT_MAX];
static int out_len;

static void out_c(char ch)
{
    if (out_len < OUT_MAX) {
        out_buf[out_len] = ch;
        out_len++;
    }
}

static void out_s(const char *s)
{
    while (*s) {
        out_c(*s);
        s++;
    }
}

static void out_flush(void)
{
    int done, n;

    done = 0;
    while (done < out_len) {
        n = write(1, out_buf + done, out_len - done);
        if (n <= 0)
            break;
        done += n;
    }
    out_len = 0;
}

/* num: v as decimal into buf (at least 24 bytes); returns buf */
static char *num(long long v, char *buf)
{
    char tmp[24];
    int i, j, neg;

    neg = v < 0;
    if (neg)
        v = -v;
    i = 0;
    do {
        tmp[i] = (char)('0' + v % 10);
        i++;
        v = v / 10;
    } while (v > 0 && i < 22);
    j = 0;
    if (neg) {
        buf[0] = '-';
        j = 1;
    }
    while (i > 0) {
        i--;
        buf[j] = tmp[i];
        j++;
    }
    buf[j] = 0;
    return buf;
}

static void out_n(long long v)
{
    char b[24];

    out_s(num(v, b));
}

/* --- the grid: a frame is drawn here first, then only the rows that
 * changed go to the terminal, at once: no flicker, little output ---- */

#define GRID_ROWS 200
#define GRID_COLS 512
#define AT_DEC 1                /* line drawing (DEC special graphics) */
#define AT_BOLD 2

static char grid_ch[GRID_ROWS][GRID_COLS], prev_ch[GRID_ROWS][GRID_COLS];
static unsigned char grid_fg[GRID_ROWS][GRID_COLS];
static unsigned char grid_bg[GRID_ROWS][GRID_COLS];
static unsigned char grid_at[GRID_ROWS][GRID_COLS];
static unsigned char prev_fg[GRID_ROWS][GRID_COLS];
static unsigned char prev_bg[GRID_ROWS][GRID_COLS];
static unsigned char prev_at[GRID_ROWS][GRID_COLS];
static int grid_r, grid_c, cur_fg = c_none, cur_bg = c_def, prev_ok;

static void go(int row, int col)
{
    grid_r = row - 1;
    grid_c = col - 1;
}

/* color: the text colour from now on (no background) */
static void color(int c)
{
    cur_fg = c;
    cur_bg = c_def;
}

static void gput(char ch, int dec)
{
    if (grid_r >= 0 && grid_r < GRID_ROWS && grid_c >= 0 &&
        grid_c < GRID_COLS)
    {
        grid_ch[grid_r][grid_c] = ch;
        grid_fg[grid_r][grid_c] = (unsigned char)cur_fg;
        grid_bg[grid_r][grid_c] = (unsigned char)cur_bg;
        grid_at[grid_r][grid_c] = (unsigned char)((dec ? AT_DEC : 0) |
            (cur_fg == c_title || cur_fg == c_bold || cur_fg == c_hi ?
            AT_BOLD : 0));
    }
    grid_c++;
}

static void grid_clear(void)
{
    memset(grid_ch, ' ', sizeof grid_ch);
    memset(grid_fg, c_none, sizeof grid_fg);
    memset(grid_bg, c_def, sizeof grid_bg);
    memset(grid_at, 0, sizeof grid_at);
}

/* --- the pen: text on one line of a box, cut at its width ---------- */

static int pen_left;

static void pen_at(int row, int col, int width)
{
    go(row, col);
    pen_left = width;
}

static void pen(int c, const char *s)
{
    color(c);
    while (*s && pen_left > 0) {
        if (*s >= 32 && *s <= 126)
            gput(*s, 0);
        else
            gput('?', 0);
        pen_left--;
        s++;
    }
}

/* pen_pad_to: spaces up to column w of this line (w counted from 0) */
static void pen_pad_to(int width, int w)
{
    color(c_none);
    while (pen_left > width - w && pen_left > 0) {
        gput(' ', 0);
        pen_left--;
    }
}

static void pen_end(void)
{
    color(c_none);
    while (pen_left > 0) {
        gput(' ', 0);
        pen_left--;
    }
}

/* pen_meter: n cells like btop's meters, the first full of them in
 * gradient g, the rest in the meter background */
static void pen_meter(int g, int n, int full)
{
    int i;

    for (i = 0; i < n && pen_left > 0; i++) {
        color(c_none);
        if (i < full)
            cur_bg = grad(g, i, n);
        else
            cur_bg = c_meter;
        gput(' ', 0);
        pen_left--;
    }
    cur_bg = c_def;
}

/* box: a frame like btop's: the title between two corners at the top
 * left, "-+title+------" */
static void box(int top, int left, int h, int w, const char *title)
{
    int r, i, tl;

    if (h < 2 || w < 4)
        return;
    tl = (int)strlen(title);
    if (tl > w - 6)
        tl = w - 6;
    color(c_box);
    go(top, left);
    gput('l', 1);
    gput('q', 1);
    if (tl > 0) {
        gput('k', 1);
        color(c_title);
        for (i = 0; i < tl; i++)
            gput(title[i], 0);
        color(c_box);
        gput('l', 1);
        i = tl + 4;
    } else {
        i = 2;
    }
    for (; i < w - 1; i++)
        gput('q', 1);
    gput('k', 1);
    for (r = 1; r < h - 1; r++) {
        go(top + r, left);
        gput('x', 1);
        go(top + r, left + w - 1);
        gput('x', 1);
    }
    go(top + h - 1, left);
    gput('m', 1);
    for (i = 1; i < w - 1; i++)
        gput('q', 1);
    gput('j', 1);
    color(c_none);
}

/* box_fill: a box over what is already drawn, its inside cleared */
static void box_fill(int top, int left, int h, int w, const char *title)
{
    int r, i;

    color(c_none);
    for (r = 1; r < h - 1; r++) {
        go(top + r, left + 1);
        for (i = 1; i < w - 1; i++)
            gput(' ', 0);
    }
    box(top, left, h, w, title);
}

/* tag: in a border, like btop's "+key text+": the key in the key
 * colour, the text after it; sel: shown as chosen. Returns its width. */
static int tag(int row, int col, int top, const char *k, const char *t,
    int sel)
{
    int n;

    n = 0;
    go(row, col);
    color(c_box);
    gput(top ? 'k' : 'j', 1);
    n++;
    color(c_hi);
    if (sel)
        cur_bg = c_sel;
    while (*k) {
        gput(*k, 0);
        k++;
        n++;
    }
    color(sel ? c_title : c_none);
    if (sel)
        cur_bg = c_sel;
    while (*t) {
        gput(*t, 0);
        t++;
        n++;
    }
    color(c_box);
    gput(top ? 'l' : 'm', 1);
    n++;
    color(c_none);
    return n;
}

static int tag_len(const char *k, const char *t)
{
    return (int)(strlen(k) + strlen(t)) + 2;
}

/* present: the rows that changed since the last frame, to the
 * terminal, in 24-bit colour (autowrap is off, so even the last cell
 * can be written) */
static void present(int nrows, int ncols)
{
    int r, c, dec, end, lf, lb, la, f, b, a;

    for (r = 0; r < nrows && r < GRID_ROWS; r++) {
        if (prev_ok && !memcmp(grid_ch[r], prev_ch[r], ncols) &&
            !memcmp(grid_fg[r], prev_fg[r], ncols) &&
            !memcmp(grid_bg[r], prev_bg[r], ncols) &&
            !memcmp(grid_at[r], prev_at[r], ncols))
        {
            continue;
        }
        out_s("\033[");
        out_n(r + 1);
        out_s(";1H");
        lf = -1;
        lb = -1;
        la = -1;
        dec = 0;
        end = ncols;
        for (c = 0; c < end; c++) {
            f = grid_fg[r][c];
            b = grid_bg[r][c];
            a = grid_at[r][c];
            if ((a & AT_DEC) && !dec) {
                out_s("\033(0");
                dec = 1;
            } else if (!(a & AT_DEC) && dec) {
                out_s("\033(B");
                dec = 0;
            }
            if (f != lf || b != lb || (a & AT_BOLD) != (la & AT_BOLD)) {
                out_s(a & AT_BOLD ? "\033[0;1;38;2;" : "\033[0;38;2;");
                out_n(pal[f].r);
                out_c(';');
                out_n(pal[f].g);
                out_c(';');
                out_n(pal[f].b);
                if (b != c_def) {
                    out_s(";48;2;");
                    out_n(pal[b].r);
                    out_c(';');
                    out_n(pal[b].g);
                    out_c(';');
                    out_n(pal[b].b);
                }
                out_c('m');
                lf = f;
                lb = b;
                la = a;
            }
            out_c(grid_ch[r][c]);
        }
        if (dec)
            out_s("\033(B");
        out_s("\033[0m");
        memcpy(prev_ch[r], grid_ch[r], ncols);
        memcpy(prev_fg[r], grid_fg[r], ncols);
        memcpy(prev_bg[r], grid_bg[r], ncols);
        memcpy(prev_at[r], grid_at[r], ncols);
    }
    prev_ok = 1;
    out_flush();
}

/* --- files ---------------------------------------------------------- */

/* read_small: a whole small file into buf, cut to max - 1 bytes */
static int read_small(const char *path, char *buf, int max)
{
    int fd, n, got;

    buf[0] = 0;
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    got = 0;
    for (;;) {
        if (got >= max - 1)
            break;
        n = read(fd, buf + got, max - 1 - got);
        if (n <= 0)
            break;
        got += n;
    }
    close(fd);
    buf[got] = 0;
    return got;
}

/* first_line: buf cut at its first newline */
static char *first_line(char *buf)
{
    char *p;

    p = strchr(buf, '\n');
    if (p)
        *p = 0;
    return buf;
}

static int exists(const char *path)
{
    struct stat sb;

    return stat(path, &sb) == 0;
}

static int same(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

/* --- the vpn service's status: key=value lines --------------------- */

static char stat_buf[STAT_MAX];
static char *stat_key[KEYS_MAX];
static char *stat_val[KEYS_MAX];
static int stat_n;

static int load_status(const char *path)
{
    char *p, *nl, *eq;
    int r;

    stat_n = 0;
    r = read_small(path, stat_buf, STAT_MAX);
    if (r < 0)
        return -1;
    p = stat_buf;
    while (*p && stat_n < KEYS_MAX) {
        nl = strchr(p, '\n');
        if (nl)
            *nl = 0;
        eq = strchr(p, '=');
        if (eq) {
            *eq = 0;
            stat_key[stat_n] = p;
            stat_val[stat_n] = eq + 1;
            stat_n++;
        }
        if (!nl)
            break;
        p = nl + 1;
    }
    return 0;
}

static const char *sv(const char *k)
{
    int i;

    for (i = 0; i < stat_n; i++) {
        if (same(stat_key[i], k))
            return stat_val[i];
    }
    return "";
}

static long long to_num(const char *s)
{
    long long v;

    v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v;
}

/* word: the n-th (from 0) space-separated word of s into out */
static char *word(const char *s, int n, char *out, int max)
{
    int i, k;

    k = 0;
    for (;;) {
        while (*s == ' ')
            s++;
        if (!*s)
            break;
        if (k == n) {
            i = 0;
            while (*s && *s != ' ' && i < max - 1) {
                out[i] = *s;
                i++;
                s++;
            }
            out[i] = 0;
            return out;
        }
        while (*s && *s != ' ')
            s++;
        k++;
    }
    out[0] = 0;
    return out;
}

/* --- events: /run/vpn/events, the newest at the end ---------------- */

static char ev_buf[EVENTS_MAX];
static char *ev_line[EV_LINES];
static int ev_n;

static void load_events(void)
{
    int fd, n, got;
    off_t size;
    char *p, *nl;

    ev_n = 0;
    ev_buf[0] = 0;
    fd = open(RUN_DIR "/events", O_RDONLY);
    if (fd < 0)
        return;
    size = lseek(fd, 0, SEEK_END);
    if (size > EVENTS_MAX - 1)
        lseek(fd, size - (EVENTS_MAX - 1), SEEK_SET);
    else
        lseek(fd, 0, SEEK_SET);
    got = 0;
    for (;;) {
        if (got >= EVENTS_MAX - 1)
            break;
        n = read(fd, ev_buf + got, EVENTS_MAX - 1 - got);
        if (n <= 0)
            break;
        got += n;
    }
    close(fd);
    ev_buf[got] = 0;
    p = ev_buf;
    if (size > EVENTS_MAX - 1) {
        nl = strchr(p, '\n');       /* the first line is cut: skip it */
        if (!nl)
            return;
        p = nl + 1;
    }
    while (*p) {
        nl = strchr(p, '\n');
        if (nl)
            *nl = 0;
        if (ev_n < EV_LINES) {
            ev_line[ev_n] = p;
            ev_n++;
        } else {
            memmove(ev_line, ev_line + 1, (EV_LINES - 1) * sizeof(char *));
            ev_line[EV_LINES - 1] = p;
        }
        if (!nl)
            break;
        p = nl + 1;
    }
}

static int event_color(const char *s)
{
    if (strstr(s, "FAILED") || strstr(s, "STOPPED") ||
        strstr(s, "dropped") || strstr(s, "could not") ||
        strstr(s, "did not"))
    {
        return c_red;
    }
    if (strstr(s, "chose") || strstr(s, "connected") ||
        strstr(s, ": running") || strstr(s, "KB/s"))
    {
        return c_green;
    }
    if (strstr(s, "---"))
        return c_bold;
    if (strstr(s, "black") || strstr(s, "none in"))
        return c_yellow;
    return c_none;
}

/* --- traffic: the kernel's byte counters, once a second ------------ */

static long long hist_rx[HIST_MAX], hist_tx[HIST_MAX];
static int hist_n;
static long long last_rx = -1, last_tx = -1, last_t;
static char iface[64];

static long long counter(const char *ifn, const char *what)
{
    char path[SMALL_MAX], b[64];
    int n;

    if (strlen(ifn) > 32)
        return -1;
    strcpy(path, "/sys/class/net/");
    strcat(path, ifn);
    strcat(path, "/statistics/");
    strcat(path, what);
    n = read_small(path, b, sizeof b);
    if (n <= 0)
        return -1;
    return to_num(b);
}

/* find_iface: the tunnel if it is up, else the default route's */
static void find_iface(void)
{
    char b[4096], *p, *nl;
    char name[64], dest[64];
    int n;

    n = read_small(RUN_DIR "/up", b, sizeof b);
    if (n > 0) {
        p = strstr(b, "dev=");
        if (p) {
            word(p + 4, 0, name, sizeof name);
            nl = strchr(name, '\n');
            if (nl)
                *nl = 0;
            if (name[0]) {
                strcpy(iface, name);
                return;
            }
        }
    }
    iface[0] = 0;
    n = read_small("/proc/net/route", b, sizeof b);
    if (n <= 0)
        return;
    p = b;
    while (*p) {
        nl = strchr(p, '\n');
        if (nl)
            *nl = 0;
        word(p, 0, name, sizeof name);
        word(p, 1, dest, sizeof dest);
        if (same(dest, "00000000")) {
            strcpy(iface, name);
            return;
        }
        if (!nl)
            break;
        p = nl + 1;
    }
}

static void sample(void)
{
    long long t, rx, tx;
    char old[64];

    t = time(0);
    if (t == last_t)
        return;
    strcpy(old, iface);
    find_iface();
    rx = counter(iface, "rx_bytes");
    tx = counter(iface, "tx_bytes");
    if (!same(old, iface) || rx < 0) {
        last_rx = rx;
        last_tx = tx;
        last_t = t;
        hist_n = 0;
        return;
    }
    if (last_rx >= 0 && rx >= last_rx && tx >= last_tx && t > last_t) {
        if (hist_n == HIST_MAX) {
            memmove(hist_rx, hist_rx + 1, (HIST_MAX - 1) * sizeof(long long));
            memmove(hist_tx, hist_tx + 1, (HIST_MAX - 1) * sizeof(long long));
            hist_n--;
        }
        hist_rx[hist_n] = (rx - last_rx) / (t - last_t);
        hist_tx[hist_n] = (tx - last_tx) / (t - last_t);
        hist_n++;
    }
    last_rx = rx;
    last_tx = tx;
    last_t = t;
}

/* rate: bytes as "12.3 KB" style text into buf */
static char *size_text(long long v, char *buf)
{
    char b[24];
    const char *unit;
    long long whole, tenth;

    unit = " B";
    whole = v;
    tenth = -1;
    if (v >= 1024LL * 1024 * 1024) {
        unit = " GB";
        whole = v / (1024LL * 1024 * 1024);
        tenth = v % (1024LL * 1024 * 1024) * 10 / (1024LL * 1024 * 1024);
    } else if (v >= 1024 * 1024) {
        unit = " MB";
        whole = v / (1024 * 1024);
        tenth = v % (1024 * 1024) * 10 / (1024 * 1024);
    } else if (v >= 1024) {
        unit = " KB";
        whole = v / 1024;
        tenth = v % 1024 * 10 / 1024;
    }
    strcpy(buf, num(whole, b));
    if (tenth >= 0) {
        strcat(buf, ".");
        strcat(buf, num(tenth, b));
    }
    strcat(buf, unit);
    return buf;
}

/* --- time ------------------------------------------------------------ */

/* days_from_civil: days since 1970-01-01 (H. Hinnant's method) */
static long long days_from_civil(long long y, long long m, long long d)
{
    long long era, yoe, doy, doe;

    if (m <= 2)
        y = y - 1;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* since_epoch: "2026-11-15 09:27 UTC" as seconds; 0 if not that */
static long long since_epoch(const char *s)
{
    if (strlen(s) < 16 || s[4] != '-' || s[7] != '-' || s[13] != ':')
        return 0;
    return days_from_civil(to_num(s), to_num(s + 5), to_num(s + 8)) *
        86400 + to_num(s + 11) * 3600 + to_num(s + 14) * 60;
}

/* duration: "3 h 02 min" style text into buf */
static char *duration(long long secs, char *buf)
{
    char b[24];

    if (secs < 0)
        secs = 0;
    buf[0] = 0;
    if (secs >= 86400) {
        strcat(buf, num(secs / 86400, b));
        strcat(buf, " d ");
        secs = secs % 86400;
    }
    if (secs >= 3600 || buf[0]) {
        strcat(buf, num(secs / 3600, b));
        strcat(buf, " h ");
        secs = secs % 3600;
        if (secs / 60 < 10)
            strcat(buf, "0");
        strcat(buf, num(secs / 60, b));
        strcat(buf, " min");
        return buf;
    }
    if (secs >= 60) {
        strcat(buf, num(secs / 60, b));
        strcat(buf, " min ");
        secs = secs % 60;
    }
    strcat(buf, num(secs, b));
    strcat(buf, " s");
    return buf;
}

static char *clock_text(char *buf)
{
    long long t;
    int h, m, s;

    t = time(0) % 86400;
    h = (int)(t / 3600);
    m = (int)(t / 60 % 60);
    s = (int)(t % 60);
    buf[0] = (char)('0' + h / 10);
    buf[1] = (char)('0' + h % 10);
    buf[2] = ':';
    buf[3] = (char)('0' + m / 10);
    buf[4] = (char)('0' + m % 10);
    buf[5] = ':';
    buf[6] = (char)('0' + s / 10);
    buf[7] = (char)('0' + s % 10);
    strcpy(buf + 8, " UTC");
    return buf;
}

/* --- the terminal ---------------------------------------------------- */

static struct termios tty_old;
static int tty_saved;
static int rows = 24, cols = 80;
static volatile sig_atomic_t got_winch, got_quit;

static void on_winch(int sig)
{
    (void)sig;
    got_winch = 1;
}

static void on_quit(int sig)
{
    (void)sig;
    got_quit = 1;
}

static void get_size(void)
{
    struct winsize ws;
    int r;

    r = ioctl(1, TIOCGWINSZ, &ws);
    if (r == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        rows = ws.ws_row;
        cols = ws.ws_col;
    }
    if (cols > GRID_COLS)
        cols = GRID_COLS;
    if (rows > GRID_ROWS)
        rows = GRID_ROWS;
}

static void tty_raw(void)
{
    struct termios t;
    int r;

    r = tcgetattr(0, &tty_old);
    if (r < 0)
        return;
    tty_saved = 1;
    t = tty_old;
    t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    t.c_iflag &= ~(IXON | ICRNL);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSAFLUSH, &t);
    out_s("\033[?1049h\033[?25l\033[?7l\033[2J");
    out_flush();
    prev_ok = 0;
}

static void tty_back(void)
{
    out_s("\033[0m\033[?7h\033[?25h\033[?1049l");
    out_flush();
    if (tty_saved)
        tcsetattr(0, TCSAFLUSH, &tty_old);
}

/* key: one key press, as a short code; 0 if none. Arrows and pages
 * become 'k' 'j' 'b' ' ' so all lists scroll the same way. */
static int key(void)
{
    char b[16];
    int n;

    n = read(0, b, sizeof b);
    if (n <= 0)
        return 0;
    if (b[0] != 27)
        return (unsigned char)b[0];
    if (n == 1)
        return 27;
    if (n >= 3 && b[1] == '[') {
        if (b[2] == 'A')
            return 'k';
        if (b[2] == 'B')
            return 'j';
        if (b[2] == '5')
            return 'b';
        if (b[2] == '6')
            return ' ';
        if (b[2] == 'H')
            return 'g';
        if (b[2] == 'F')
            return 'G';
    }
    return 0;
}

/* wait_key: block until a key (for "press a key" in plain mode) */
static void wait_key(void)
{
    struct pollfd p;
    int r;

    p.fd = 0;
    p.events = POLLIN;
    for (;;) {
        r = poll(&p, 1, -1);
        if (r > 0) {
            r = key();
            if (r)
                return;
        }
        if (got_quit)
            return;
    }
}

/* --- running vpn's commands ----------------------------------------- */

static char msg[MSG_MAX];       /* the last command's answer */
static int msg_color;
static long long msg_t;         /* when it was set */
static int job_pid = -1, job_fd = -1;
static long long job_t;
static char job_name[SMALL_MAX];
static char job_out[MSG_MAX];
static int job_len;

static void set_msg(int c, const char *a, const char *b)
{
    msg[0] = 0;
    strncat(msg, a, MSG_MAX - 1);
    strncat(msg, b, MSG_MAX - 1 - strlen(msg));
    msg_color = c;
    msg_t = time(0);
}

static void job_name_set(char **argv)
{
    int i;

    job_name[0] = 0;
    for (i = 0; argv[i]; i++) {
        if (strlen(job_name) + strlen(argv[i]) + 2 >= SMALL_MAX)
            break;
        if (i)
            strcat(job_name, " ");
        strcat(job_name, argv[i]);
    }
}

/* start_job: vpn with these arguments in the background; its output
 * becomes the message. answer: what to type at its question, or 0. */
static void start_job(char **argv, const char *answer)
{
    int out[2], in[2], pid, r, devnull;

    if (job_pid > 0) {
        set_msg(c_yellow, "wait: still running ", job_name);
        return;
    }
    job_name_set(argv);
    r = pipe(out);
    if (r < 0)
        return;
    r = pipe(in);
    if (r < 0) {
        close(out[0]);
        close(out[1]);
        return;
    }
    pid = fork();
    if (pid < 0) {
        close(out[0]);
        close(out[1]);
        close(in[0]);
        close(in[1]);
        set_msg(c_red, "could not start ", job_name);
        return;
    }
    if (pid == 0) {
        devnull = open("/dev/null", O_RDONLY);
        if (answer)
            dup2(in[0], 0);
        else if (devnull >= 0)
            dup2(devnull, 0);
        dup2(out[1], 1);
        dup2(out[1], 2);
        close(out[0]);
        close(out[1]);
        close(in[0]);
        close(in[1]);
        /* its own session: no terminal, so it can't ask for a password
         * or read keys meant for the screen */
        setsid();
        execv(VPN_PATH, argv);
        _exit(127);
    }
    close(out[1]);
    close(in[0]);
    if (answer) {
        r = write(in[1], answer, strlen(answer));
        (void)r;
    }
    close(in[1]);
    job_pid = pid;
    job_fd = out[0];
    job_t = time(0);
    job_len = 0;
    job_out[0] = 0;
    msg[0] = 0;
}

/* job_read: take what the job wrote; at its end, its last line is the
 * message */
static void job_read(void)
{
    char b[512];
    int n, i, st, start;

    n = read(job_fd, b, sizeof b);
    if (n > 0) {
        for (i = 0; i < n; i++) {
            if (job_len < MSG_MAX - 1) {
                job_out[job_len] = b[i];
                job_len++;
            }
        }
        job_out[job_len] = 0;
        return;
    }
    close(job_fd);
    job_fd = -1;
    waitpid(job_pid, &st, 0);
    job_pid = -1;
    /* the last non-empty line, newlines in it as spaces */
    while (job_len > 0 && job_out[job_len - 1] == '\n') {
        job_len--;
        job_out[job_len] = 0;
    }
    start = job_len;
    while (start > 0 && job_out[start - 1] != '\n')
        start--;
    if (WIFEXITED(st) && WEXITSTATUS(st) == 0)
        set_msg(c_green, "done: ", job_out + start);
    else
        set_msg(c_red, "failed: ", job_out[start] ? job_out + start :
            job_name);
}

/* plain_run: vpn in the normal terminal, for commands that ask
 * questions or take long and talk (selftest, refresh, off, on) */
static void plain_run(char **argv)
{
    int pid, st;

    if (job_pid > 0) {
        set_msg(c_yellow, "wait: still running ", job_name);
        return;
    }
    tty_back();
    job_name_set(argv);
    out_s("\033[2J\033[H\033[1m== ");
    out_s(job_name);
    out_s("\033[0m\n\n");
    out_flush();
    signal(SIGINT, SIG_IGN);
    pid = fork();
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        execv(VPN_PATH, argv);
        _exit(127);
    }
    st = 0;
    if (pid > 0)
        waitpid(pid, &st, 0);
    signal(SIGINT, on_quit);
    out_s("\n\033[7m press any key to go back to the screen \033[0m");
    out_flush();
    tty_raw();
    wait_key();
    got_quit = 0;
    if (pid > 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0)
        set_msg(c_green, "done: ", job_name);
    else
        set_msg(c_red, "ended with an error: ", job_name);
}

/* --- what the screen shows ------------------------------------------ */

static int watch_only;          /* vpntop -w: public status, no keys */
static int view;                /* 0 main, 1 log, 2 help, 3 confirm */
static int log_top;             /* the log view's first line, -1: end */
static int confirm_what;        /* the key waiting for y */

static const char *level_text(const char *lv)
{
    if (same(lv, "0"))
        return "bare: own IP, no VPN";
    if (same(lv, "1"))
        return "vpn: all through the VPN";
    if (same(lv, "2"))
        return "tor dns: VPN, DNS by Tor";
    if (same(lv, "3"))
        return "tor: VPN, all through Tor";
    if (same(lv, "sandbox"))
        return "only vpn run programs";
    if (same(lv, "offline"))
        return "nothing goes out";
    return "?";
}

static const char *fw_text(const char *mode)
{
    if (same(mode, "closed"))
        return "closed: nothing out";
    if (same(mode, "pick"))
        return "only to Riseup";
    if (same(mode, "vpn"))
        return "only through the VPN";
    if (same(mode, "torvpn"))
        return "only through Tor";
    if (same(mode, "direct"))
        return "open (level 0)";
    if (same(mode, "sandbox"))
        return "only vpn run, Tor";
    if (same(mode, "refresh"))
        return "open for the list";
    return mode;
}

static int is_off(void)
{
    return exists(ETC_DIR "/off");
}

static int fw_is_off(void)
{
    return exists(RUN_DIR "/fw-off");
}

static int zone_color(const char *z)
{
    if (same(z, "white"))
        return c_green;
    if (same(z, "gray"))
        return c_yellow;
    if (same(z, "black"))
        return c_red;
    return c_none;
}

/* label: a name in the label colour, padded to n columns */
static void label(const char *s, int n)
{
    int i;

    pen(c_label, s);
    for (i = (int)strlen(s); i < n; i++)
        pen(c_none, " ");
}

/* fold_line: the part of s that fits in w, cut at a space; returns
 * where the rest starts */
static const char *fold_line(const char *s, int w, char *out)
{
    int n, cut;

    n = (int)strlen(s);
    if (n <= w) {
        strcpy(out, s);
        return s + n;
    }
    cut = w;
    while (cut > w / 2 && s[cut] != ' ')
        cut--;
    if (cut <= w / 2)
        cut = w;
    memcpy(out, s, cut);
    out[cut] = 0;
    s += cut;
    while (*s == ' ')
        s++;
    return s;
}

/* the state in a word and its colour */
static const char *state_word(int *c)
{
    const char *st;

    st = sv("state");
    if (is_off()) {
        *c = c_dim;
        return "OFF";
    }
    if (fw_is_off()) {
        *c = c_red;
        return "NO KILL SWITCH";
    }
    if (!st[0]) {
        *c = c_red;
        return "NOT RUNNING";
    }
    if (same(st, "ok")) {
        *c = c_green;
        return "OK";
    }
    if (same(st, "problem") || same(st, "stopped")) {
        *c = c_red;
        return same(st, "problem") ? "PROBLEM" : "STOPPED";
    }
    *c = c_yellow;
    return st;
}

/* --- the traffic graph: download up from the middle, upload down,
 * each in its gradient, like btop's net box ------------------------ */

static void draw_graph(int top, int left, int h, int w)
{
    char b[64], b2[64];
    int dh, uh, col, rr, idx, lvl, i;
    long long dmax, umax, v;

    if (h < 2 || w < 8)
        return;
    dh = (h + 1) / 2;
    uh = h - dh;
    dmax = 1024;
    umax = 1024;
    for (i = 0; i < hist_n && i < w; i++) {
        if (hist_rx[hist_n - 1 - i] > dmax)
            dmax = hist_rx[hist_n - 1 - i];
        if (hist_tx[hist_n - 1 - i] > umax)
            umax = hist_tx[hist_n - 1 - i];
    }
    for (col = 0; col < w; col++) {
        idx = hist_n - w + col;
        for (rr = 0; rr < h; rr++) {
            go(top + rr, left + col);
            color(c_none);
            if (idx >= 0 && rr < dh) {
                v = hist_rx[idx];
                lvl = dh - 1 - rr;          /* 0 at the middle */
                if (v > 0 && v * dh > dmax * lvl)
                    cur_bg = grad(g_down, lvl, dh);
            } else if (idx >= 0) {
                v = hist_tx[idx];
                lvl = rr - dh;
                if (v > 0 && v * uh > umax * lvl)
                    cur_bg = grad(g_up, lvl, uh);
            }
            gput(' ', 0);
        }
    }
    /* the numbers over it, like btop */
    pen_at(top, left + 1, w - 2);
    pen(c_label, "download ");
    pen(c_cyan, size_text(hist_n ? hist_rx[hist_n - 1] : 0, b));
    pen(c_cyan, "/s");
    pen(c_dim, "  top ");
    pen(c_dim, size_text(dmax, b2));
    pen(c_dim, "/s");
    pen_at(top + h - 1, left + 1, w - 2);
    pen(c_label, "upload ");
    pen(c_magenta, size_text(hist_n ? hist_tx[hist_n - 1] : 0, b));
    pen(c_magenta, "/s");
    pen(c_dim, "  top ");
    pen(c_dim, size_text(umax, b2));
    pen(c_dim, "/s");
}

/* the top box, like btop's cpu box: the graph on the left, the state
 * on the right, the clock in the middle of the top border, the levels
 * in the bottom border */
static const char *level_keys[] = {
    "0", " bare", "1", " vpn", "2", " tor dns", "3", " tor",
    "s", "andbox", "o", "ffline", "-", " all off", 0
};
static const char *level_names[] = {
    "0", "1", "2", "3", "sandbox", "offline", "-1"
};

static void draw_top(int h)
{
    char b[64], line[128];
    const char *lv, *no, *pr, *rest, *word_st;
    int iw, gx, r, c, n, i, col, total, sc;
    long long step, t, t0;

    box(1, 1, h, cols, "vpn");
    /* the state panel on the right, behind a line like btop's */
    iw = cols / 2 - 4;
    if (iw > 46)
        iw = 46;
    if (iw < 30)
        iw = 30;
    gx = cols - iw;
    color(c_box);
    go(1, gx);
    gput('w', 1);
    for (r = 2; r < h; r++) {
        go(r, gx);
        gput('x', 1);
    }
    go(h, gx);
    gput('v', 1);
    /* the clock, like btop's, in the middle of the top border */
    clock_text(b);
    col = (cols - (int)strlen(b) - 2) / 2;
    go(1, col);
    color(c_box);
    gput('k', 1);
    color(c_title);
    for (i = 0; b[i]; i++)
        gput(b[i], 0);
    color(c_box);
    gput('l', 1);
    if (!watch_only) {
        col = cols - tag_len("q", " quit") - 1;
        tag(1, col, 1, "q", " quit", 0);
        col -= tag_len("?", " help") + 1;
        tag(1, col, 1, "?", " help", 0);
        lv = is_off() ? "-1" : sv("level");
        total = 0;
        for (i = 0; level_keys[i]; i += 2)
            total += tag_len(level_keys[i], level_keys[i + 1]);
        col = 3;
        if (total + 4 > cols)
            col = 2;
        for (i = 0; level_keys[i]; i += 2) {
            if (col + tag_len(level_keys[i], level_keys[i + 1]) > cols - 1)
                break;
            col += tag(h, col, 0, level_keys[i], level_keys[i + 1],
                same(lv, level_names[i / 2]));
        }
    } else {
        tag(1, cols - tag_len("q", " quit") - 1, 1, "q", " quit", 0);
    }

    draw_graph(2, 2, h - 2, gx - 2);

    r = 2;
    word_st = state_word(&sc);
    pen_at(r, gx + 2, iw - 3);
    label("state", 7);
    pen(sc, word_st);
    lv = is_off() ? "-1" : sv("level");
    pen_end();
    r++;
    pen_at(r, gx + 2, iw - 3);
    label("level", 7);
    pen(c_title, lv);
    pen(c_none, " ");
    pen(c_none, is_off() ? "vpn is off" : level_text(lv));
    pen_end();
    r++;

    /* what is happening now, in up to three lines */
    t = time(0);
    no = sv("note");
    pr = sv("problem");
    line[0] = 0;
    c = c_none;
    if (job_pid > 0) {
        strcpy(line, "running: vpn ");
        strncat(line, job_name + 4, 60);
        strcat(line, " (");
        strcat(line, duration(t - job_t, b));
        strcat(line, ")");
        c = c_yellow;
    } else if (msg[0] && t - msg_t < 10) {
        strncat(line, msg, 120);
        c = msg_color;
    } else if (pr[0]) {
        strcpy(line, "PROBLEM: ");
        strncat(line, pr, 100);
        c = c_red;
    } else if (no[0]) {
        strncat(line, no, 90);
        step = to_num(sv("step"));
        if (step > 0) {
            strcat(line, " (");
            strcat(line, duration(t - step, b));
            strcat(line, ")");
        }
        c = c_yellow;
    } else if (is_off()) {
        strcpy(line, "press 2 (or any level) to turn it on");
        c = c_dim;
    } else if (same(sv("state"), "ok")) {
        strcpy(line, "all good");
        c = c_green;
    }
    rest = line;
    n = 0;
    while (*rest && r < h - 2 && n < 3) {
        char part[128];

        rest = fold_line(rest, iw - 10, part);
        pen_at(r, gx + 2, iw - 3);
        label(n ? "" : "now", 7);
        pen(c, part);
        pen_end();
        r++;
        n++;
    }
    if (r < h - 1 && !watch_only) {
        pen_at(r, gx + 2, iw - 3);
        label("server", 7);
        if (sv("server")[0] && !same(lv, "0") && !same(lv, "sandbox")) {
            pen(c_title, sv("location"));
            pen(c_none, " ");
            pen(c_none, sv("country"));
            pen(c_none, " ");
            pen(zone_color(sv("zone")), sv("zone"));
        } else {
            pen(c_dim, "none");
        }
        pen_end();
        r++;
    }
    if (r < h - 1 && !watch_only) {
        pen_at(r, gx + 2, iw - 3);
        label("up", 7);
        t0 = since_epoch(sv("since"));
        if (t0 > 0 && same(sv("state"), "ok"))
            pen(c_none, duration(t - t0, b));
        else
            pen(c_dim, "-");
        pen_end();
        r++;
    }
    if (r < h - 1 && watch_only) {
        pen_at(r, gx + 2, iw - 3);
        pen(c_dim, "more: run vpn (your password)");
        pen_end();
    }
}

static void draw_connection(int top, int left, int h, int w)
{
    const char *lv, *ip, *ce, *cl;
    char m[16], d[16], y[16];
    int r, own;

    box(top, left, h, w, "connection");
    if (watch_only) {
        pen_at(top + 1, left + 2, w - 4);
        pen(c_dim, "run vpn to see it");
        pen_end();
        return;
    }
    lv = sv("level");
    ip = sv("server");
    own = same(lv, "0") || same(lv, "sandbox");
    r = top + 1;
    if (r < top + h - 1) {
        pen_at(r, left + 2, w - 4);
        label("address", 9);
        pen(own || !ip[0] ? c_dim : c_none, own || !ip[0] ? "-" : ip);
        pen_end();
        r++;
    }
    if (r < top + h - 1) {
        pen_at(r, left + 2, w - 4);
        label("speed", 9);
        pen(c_none, own || !sv("speed")[0] ? "-" : sv("speed"));
        pen_end();
        r++;
    }
    if (r < top + h - 1) {
        pen_at(r, left + 2, w - 4);
        label("Tor", 9);
        pen(same(sv("tor"), "running") ? c_green : c_red,
            sv("tor")[0] ? sv("tor") : "-");
        if (same(sv("tor"), "running"))
            pen(c_dim, " 9100+ 9150+");
        pen_end();
        r++;
    }
    if (r < top + h - 1) {
        pen_at(r, left + 2, w - 4);
        label("clock", 9);
        cl = sv("clock");
        pen(same(cl, "ok") || !strncmp(cl, "set", 3) ? c_green : c_yellow,
            cl[0] ? cl : "-");
        pen_end();
        r++;
    }
    if (r < top + h - 1) {
        pen_at(r, left + 2, w - 4);
        label("cert", 9);
        ce = sv("certificate");
        if (ce[0]) {
            pen(c_none, "until ");
            pen(c_none, word(ce, 0, m, sizeof m));
            pen(c_none, " ");
            pen(c_none, word(ce, 1, d, sizeof d));
            pen(c_none, " ");
            pen(c_none, word(ce, 3, y, sizeof y));
        } else {
            pen(c_dim, "-");
        }
        pen_end();
    }
}

static void draw_protection(int top, int left, int h, int w)
{
    char b[SMALL_MAX], dns[64], mode[64], *p;
    int fd, flags, r, rc, locked, col;

    box(top, left, h, w, "protection");
    if (!watch_only) {
        col = left + 2;
        col += tag(top + h - 1, col, 0, "l", " LAN", 0);
        col += tag(top + h - 1, col, 0, "f", " firewall", 0);
    }
    r = top + 1;
    read_small(RUN_DIR "/fwmode", mode, sizeof mode);
    first_line(mode);
    pen_at(r, left + 2, w - 4);
    label("firewall", 9);
    if (is_off())
        pen(c_dim, "off (level -1)");
    else if (fw_is_off())
        pen(c_red, "OFF: f turns it on");
    else if (!mode[0])
        pen(c_yellow, "?");
    else
        pen(same(mode, "direct") ? c_yellow : c_green, fw_text(mode));
    pen_end();
    r++;

    dns[0] = 0;
    read_small("/etc/resolv.conf", b, sizeof b);
    p = b;
    while (p) {
        if (!strncmp(p, "nameserver", 10)) {
            word(p, 1, dns, sizeof dns);
            first_line(dns);
            break;
        }
        p = strchr(p, '\n');
        if (p)
            p++;
    }
    locked = 0;
    fd = open("/etc/resolv.conf", O_RDONLY);
    if (fd >= 0) {
        flags = 0;
        rc = ioctl(fd, FS_IOC_GETFLAGS, &flags);
        if (rc == 0)
            locked = (flags & FS_IMMUTABLE_FL) != 0;
        close(fd);
    }
    if (r < top + h - 1) {
        pen_at(r, left + 2, w - 4);
        label("DNS", 9);
        if (same(dns, "127.0.0.1"))
            pen(c_green, "Tor");
        else if (dns[0] && same(sv("level"), "1"))
            pen(c_yellow, "Riseup's, through the VPN");
        else if (same(dns, "1.1.1.1"))
            pen(c_yellow, "1.1.1.1, not through the VPN");
        else
            pen(c_yellow, dns[0] ? dns : "none");
        pen(c_dim, locked ? ", locked" : ", not locked");
        pen_end();
        r++;
    }
    if (r < top + h - 1) {
        pen_at(r, left + 2, w - 4);
        label("IPv6", 9);
        read_small("/proc/sys/net/ipv6/conf/all/disable_ipv6", b, sizeof b);
        if (b[0] == '1')
            pen(c_green, "off");
        else if (is_off())
            pen(c_none, "on");
        else
            pen(c_yellow, "on, the firewall drops it");
        pen_end();
        r++;
    }
    if (r < top + h - 1) {
        pen_at(r, left + 2, w - 4);
        label("LAN", 9);
        if (exists(ETC_DIR "/lan"))
            pen(c_yellow, "on: router, printer");
        else
            pen(c_green, "off");
        pen_end();
        r++;
    }
    if (r < top + h - 1) {
        pen_at(r, left + 2, w - 4);
        label("MAC", 9);
        if (exists(ETC_DIR "/keep-mac"))
            pen(c_yellow, "the cards' own");
        else
            pen(c_green, "made up at boot");
        pen_end();
    }
}

/* one test: "Paris|FR|1.2.3.4|2407|" or "Zurich|CH|1.2.3.5||why" */
static void split_test(char *s, char **f)
{
    int i;
    char *p;

    for (i = 0; i < 5; i++)
        f[i] = "";
    p = s;
    for (i = 0; i < 5; i++) {
        f[i] = p;
        p = strchr(p, '|');
        if (!p)
            break;
        *p = 0;
        p++;
    }
}

/* the servers box, like btop's process list: a header, one row each,
 * the one in use selected */
static void draw_servers(int top, int left, int h, int w)
{
    char tests[STAT_MAX], *ent[64], *f[5], *p, *semi;
    char zone[32], a[32], bb[32], c[32], z[64], t[96];
    const char *zones, *pr;
    int n, i, first, r, mw, full, col, inner, sel;
    long long best, s;

    box(top, left, h, w, "servers");
    inner = w - 2;
    if (!watch_only) {
        col = left + 2;
        col += tag(top + h - 1, col, 0, "r", " reconnect", 0);
        col += tag(top + h - 1, col, 0, "n", " new list", 0);
    }
    if (watch_only) {
        pen_at(top + 1, left + 2, w - 4);
        pen(c_dim, "run vpn to see them");
        pen_end();
        return;
    }
    /* how many each zone has, top right */
    zones = sv("zones");
    strcpy(z, " white ");
    strcat(z, word(zones, 0, a, sizeof a));
    strcat(z, " gray ");
    strcat(z, word(zones, 1, a, sizeof a));
    strcat(z, " black ");
    strcat(z, word(zones, 2, a, sizeof a));
    if (zones[0] && (int)strlen(z) + 14 < w)
        tag(top, left + w - tag_len("", z) - 1, 1, "", z, 0);

    /* the step: "gray 2 12 1" testing, "gray done 5 3" chosen */
    pr = sv("progress");
    word(pr, 0, zone, sizeof zone);
    word(pr, 1, a, sizeof a);
    word(pr, 2, bb, sizeof bb);
    word(pr, 3, c, sizeof c);
    r = top + 1;
    pen_at(r, left + 1, inner);
    pen(c_none, " ");
    if (same(zone, "none")) {
        pen(c_red, "no server worked in any zone");
    } else if (same(a, "done")) {
        pen(c_label, "using the fastest of ");
        pen(c_title, c);
        pen(c_label, " tested in ");
        pen(zone_color(zone), zone);
        pen(c_label, " (");
        pen(c_none, bb);
        pen(c_label, " tried)");
    } else if (zone[0]) {
        pen(c_yellow, "testing ");
        pen(zone_color(zone), zone);
        pen(c_label, ": server ");
        pen(c_title, a);
        pen(c_label, " of ");
        pen(c_none, bb);
        pen(c_label, ", tested ");
        pen(c_title, c);
        pen(c_label, " of 3 ");
        pen_meter(g_good, 9, (int)to_num(c) * 3);
    } else {
        pen(c_dim, "no test since the service started");
    }
    pen_end();
    r++;

    /* the header */
    mw = inner - 42;
    if (mw > 30)
        mw = 30;
    pen_at(r, left + 1, inner);
    pen(c_title, " Location        Address");
    pen_pad_to(inner, 32);
    pen(c_title, mw >= 4 ? "Speed" : "");
    pen_end();
    r++;

    strcpy(tests, sv("tests"));
    n = 0;
    p = tests;
    while (*p && n < 64) {
        semi = strchr(p, ';');
        if (semi)
            *semi = 0;
        if (*p) {
            ent[n] = p;
            n++;
        }
        if (!semi)
            break;
        p = semi + 1;
    }
    best = 1;
    for (i = 0; i < n; i++) {
        p = strchr(ent[i], '|');
        if (p)
            p = strchr(p + 1, '|');
        if (p)
            p = strchr(p + 1, '|');
        if (p) {
            s = to_num(p + 1);
            if (s > best)
                best = s;
        }
    }
    first = n - (top + h - 1 - r);
    if (first < 0)
        first = 0;
    for (i = first; i < n && r < top + h - 1; i++) {
        split_test(ent[i], f);
        sel = same(f[2], sv("server")) && f[3][0] &&
            !same(sv("level"), "0") && !same(sv("level"), "sandbox");
        pen_at(r, left + 1, inner);
        pen(c_none, " ");
        pen(sel ? c_title : c_none, f[0]);
        pen(c_label, " ");
        pen(c_label, f[1]);
        pen_pad_to(inner, 16);
        pen(c_misc, f[2]);
        pen_pad_to(inner, 32);
        if (f[3][0]) {
            s = to_num(f[3]);
            if (mw >= 4) {
                full = (int)(s * mw / best);
                if (full < 1)
                    full = 1;
                pen_meter(g_good, mw, full);
                pen(c_none, " ");
            }
            strcpy(t, num(s, a));
            strcat(t, " KB/s");
            pen(c_green, t);
        } else {
            pen(c_red, "failed: ");
            pen(c_red, f[4]);
        }
        pen_end();
        if (sel) {
            /* the row in use, like btop's selected process */
            for (col = left + 1; col < left + w - 1; col++) {
                if (grid_bg[r - 1][col - 1] == c_def)
                    grid_bg[r - 1][col - 1] = c_sel;
            }
        }
        r++;
    }
}

static void draw_events(int top, int left, int h, int w)
{
    int i, first, n, col;
    const char *s;
    char t[10];

    box(top, left, h, w, "log");
    if (!watch_only) {
        col = left + 2;
        col += tag(top + h - 1, col, 0, "e", " whole log", 0);
        col += tag(top + h - 1, col, 0, "t", " selftest", 0);
    }
    n = h - 2;
    if (watch_only) {
        pen_at(top + 1, left + 2, w - 4);
        pen(c_dim, "run vpn to see it");
        pen_end();
        return;
    }
    first = ev_n - n;
    if (first < 0)
        first = 0;
    for (i = first; i < ev_n; i++) {
        s = ev_line[i];
        pen_at(top + 1 + i - first, left + 2, w - 4);
        if (strlen(s) > 9) {
            memcpy(t, s, 8);
            t[8] = 0;
            pen(c_dim, t);
            pen(c_none, " ");
            pen(event_color(s + 9), s + 9);
        } else {
            pen(c_none, s);
        }
        pen_end();
    }
}

static void draw_main(void)
{
    int th, rem, lw, rw, ch, ph, sh, lh;

    th = rows / 3;
    if (th < 8)
        th = 8;
    if (th > 14)
        th = 14;
    rem = rows - th;
    lw = cols * 2 / 5;
    if (lw < 34)
        lw = 34;
    if (lw > 52)
        lw = 52;
    rw = cols - lw;
    ch = 7;
    ph = rem - ch;
    if (ph > 8) {
        ph = 8;
        ch = rem - ph;
        if (ch > 7)
            ch = 7;
    }
    sh = rem / 2;
    if (sh < 8)
        sh = 8;
    if (sh > rem - 4)
        sh = rem - 4;
    lh = rem - sh;
    draw_top(th);
    draw_connection(th + 1, 1, ch, lw);
    draw_protection(th + 1 + ch, 1, rem - ch, lw);
    draw_servers(th + 1, 1 + lw, sh, rw);
    draw_events(th + 1 + sh, 1 + lw, lh, rw);
}

static const char *help_text[] = {
    "One key does one thing, without Enter; the screen updates itself.",
    "Green is fine, brown is worth knowing, red is a problem.",
    "",
    "Levels (until reboot), in the bottom line of the top box",
    "  0  bare: own IP, no VPN           s  sandbox: own IP, only",
    "  1  vpn: all through the VPN          programs started with",
    "  2  tor dns: VPN, DNS by Tor (usual)  vpn run (and Tor) online",
    "  3  tor: VPN, all through Tor      o  offline: nothing goes out",
    "  -  everything off (level -1), as if vpn were not installed;",
    "     any level turns it back on (that runs install.sh net)",
    "Other keys, in the bottom lines of the boxes",
    "  r  pick a server again          n  new server list, certificate",
    "  l  local network on/off         f  kill switch off/on",
    "  e  everything that happened     t  try every feature (selftest)",
    "  q  quit (Ctrl+C too)            Ctrl+L  draw it all again",
    "How a server is chosen",
    "  Zones first (white, gray, black: /etc/vpn/countries), then speed:",
    "  in the first zone with a working server, up to 3 random ones are",
    "  tested and the fastest is used. Black only when nothing else",
    "  works, and then everything goes through Tor.",
    0
};

static void draw_help(void)
{
    int i, w, h, top, left;

    w = 74;
    if (w > cols)
        w = cols;
    h = 0;
    while (help_text[h])
        h++;
    h += 2;
    if (h > rows)
        h = rows;
    top = (rows - h) / 2 + 1;
    left = (cols - w) / 2 + 1;
    box_fill(top, left, h, w, "help");
    tag(top, left + w - tag_len("", " any key: back ") - 1, 1, "",
        " any key: back ", 0);
    for (i = 0; help_text[i] && i < h - 2; i++) {
        pen_at(top + 1 + i, left + 2, w - 4);
        if (help_text[i][0] && help_text[i][0] != ' ' && i > 2)
            pen(c_title, help_text[i]);
        else
            pen(c_none, help_text[i]);
        pen_end();
    }
}

/* the log view: all events, long ones folded, scrolled with j k */
static int log_rows(int w, int *starts, int *parts, int max)
{
    int i, len, off, n;

    n = 0;
    for (i = 0; i < ev_n; i++) {
        len = (int)strlen(ev_line[i]);
        off = 0;
        do {
            if (n < max) {
                starts[n] = i;
                parts[n] = off;
                n++;
            }
            off += off ? w - 9 : w;
        } while (off < len);
    }
    return n;
}

static int lr_start[EV_LINES * 4], lr_part[EV_LINES * 4];

static void draw_log(void)
{
    int w, h, n, i, top, cnt, col;
    const char *s;
    char t[10], b[24];

    w = cols - 4;
    h = rows - 2;
    n = log_rows(w, lr_start, lr_part, EV_LINES * 4);
    if (log_top < 0 || log_top > n - h)
        log_top = n - h;
    if (log_top < 0)
        log_top = 0;
    box(1, 1, rows, cols, "log");
    strcpy(t, "");
    tag(1, cols - tag_len("", num(n, b)) - 8, 1, "", num(n, b), 0);
    col = 3;
    col += tag(rows, col, 0, "j k", " line", 0);
    col += tag(rows, col, 0, "space b", " page", 0);
    col += tag(rows, col, 0, "g G", " top end", 0);
    col += tag(rows, col, 0, "q", " back", 0);
    top = log_top;
    for (i = 0; i < h && top + i < n; i++) {
        s = ev_line[lr_start[top + i]];
        pen_at(2 + i, 3, w);
        if (lr_part[top + i]) {
            pen(c_none, "         ");
            cnt = lr_part[top + i];
            if (s[cnt] == ' ')
                cnt++;
            pen(event_color(s), s + cnt);
        } else if (strlen(s) > 9) {
            memcpy(t, s, 8);
            t[8] = 0;
            pen(c_dim, t);
            pen(c_none, " ");
            pen(event_color(s + 9), s + 9);
        } else {
            pen(c_none, s);
        }
        pen_end();
    }
    if (n == 0) {
        pen_at(2, 3, w);
        pen(c_dim, "nothing yet");
        pen_end();
    }
}

static void draw_confirm(void)
{
    int w, top, left, col;
    const char *a, *b;

    if (confirm_what == 'f') {
        a = "Turn the kill switch off? Programs can then go out";
        b = "without the VPN, and DNS can leak, until f again.";
    } else {
        a = "Turn everything of vpn off (level -1)? The system";
        b = "files go back as before; any level turns it on again.";
    }
    w = 60;
    if (w > cols)
        w = cols;
    top = rows / 2 - 3;
    left = (cols - w) / 2 + 1;
    box_fill(top, left, 6, w, "are you sure?");
    pen_at(top + 1, left + 2, w - 4);
    pen(c_none, a);
    pen_end();
    pen_at(top + 2, left + 2, w - 4);
    pen(c_none, b);
    pen_end();
    col = left + 2;
    col += tag(top + 5, col, 0, "y", " yes", 0);
    col += tag(top + 5, col, 0, "any other key", " no", 0);
}

static void draw(void)
{
    if (got_winch) {
        got_winch = 0;
        get_size();
        out_s("\033[0m\033[2J");
        prev_ok = 0;
    }
    grid_clear();
    if (cols < MIN_COLS || rows < MIN_ROWS) {
        pen_at(1, 1, cols);
        pen(c_yellow, "make the window at least 70x20 (q quits)");
        pen_end();
        present(rows, cols);
        return;
    }
    if (view == 1) {
        draw_log();
    } else {
        draw_main();
        if (view == 2)
            draw_help();
        else if (view == 3)
            draw_confirm();
    }
    present(rows, cols);
}

/* --- keys ------------------------------------------------------------- */

static char *av[6];

static void vpn_args(const char *a, const char *b)
{
    av[0] = "vpn";
    av[1] = (char *)a;
    av[2] = (char *)b;
    av[3] = 0;
}

static void do_level(const char *lv)
{
    vpn_args("level", lv);
    if (is_off())
        plain_run(av);          /* that installs it all again */
    else
        start_job(av, 0);
}

/* handle: one key; returns 0 to quit */
static int handle(int k)
{
    char lv[2];

    if (k == 3 || (view == 0 && (k == 'q' || k == 'Q')))
        return 0;
    if (view == 1) {
        if (k == 'q' || k == 'e' || k == 27)
            view = 0;
        else if (k == 'j')
            log_top++;
        else if (k == 'k' && log_top > 0)
            log_top--;
        else if (k == ' ')
            log_top += rows - 4;
        else if (k == 'b') {
            log_top -= rows - 4;
            if (log_top < 0)
                log_top = 0;
        } else if (k == 'g')
            log_top = 0;
        else if (k == 'G')
            log_top = -1;
        return 1;
    }
    if (view == 2) {
        view = 0;
        return 1;
    }
    if (view == 3) {
        view = 0;
        if (k == 'y' || k == 'Y') {
            if (confirm_what == 'f') {
                vpn_args("firewall-off", 0);
                start_job(av, "yes\n");
            } else {
                vpn_args("off", 0);
                plain_run(av);
            }
        } else {
            set_msg(c_dim, "nothing changed", "");
        }
        return 1;
    }
    if (watch_only)
        return 1;
    switch (k) {
    case '0':
    case '1':
    case '2':
    case '3':
        lv[0] = (char)k;
        lv[1] = 0;
        do_level(lv);
        break;
    case 's':
        do_level("sandbox");
        break;
    case 'o':
        do_level("offline");
        break;
    case '-':
        confirm_what = '-';
        view = 3;
        break;
    case 'r':
        vpn_args("retry", 0);
        start_job(av, 0);
        break;
    case 'n':
        vpn_args("refresh", 0);
        plain_run(av);
        break;
    case 'l':
        vpn_args("lan", exists(ETC_DIR "/lan") ? "off" : "on");
        start_job(av, 0);
        break;
    case 'f':
        if (fw_is_off()) {
            vpn_args("firewall-on", 0);
            start_job(av, 0);
        } else {
            confirm_what = 'f';
            view = 3;
        }
        break;
    case 't':
        vpn_args("selftest", 0);
        plain_run(av);
        break;
    case 'e':
        view = 1;
        log_top = -1;
        break;
    case '?':
    case 'h':
        view = 2;
        break;
    case 12:                    /* Ctrl+L: draw everything again */
        got_winch = 1;
        break;
    default:
        if (k > 32 && k < 127) {
            lv[0] = (char)k;
            lv[1] = 0;
            set_msg(c_yellow, "no such key: ", lv);
            strncat(msg, " (? shows them all)", MSG_MAX - 1 - strlen(msg));
        }
        break;
    }
    return 1;
}

static void load_all(void)
{
    int r;

    r = -1;
    if (!watch_only)
        r = load_status(RUN_DIR "/status");
    if (r < 0)
        load_status(RUN_DIR "/public");
    if (!watch_only)
        load_events();
    sample();
}

int main(int argc, char **argv)
{
    struct pollfd p[2];
    int n, k, run, r;

    if (argc > 1 && same(argv[1], "-w"))
        watch_only = 1;
    else if (argc > 1) {
        r = write(2, "usage: vpntop [-w]\n", 19);
        (void)r;
        return 2;
    }
    if (!watch_only && getuid() != 0)
        watch_only = 1;
    if (!isatty(0) || !isatty(1)) {
        r = write(2, "vpntop: needs a terminal (vpn status)\n", 38);
        (void)r;
        return 1;
    }
    make_gradients();
    signal(SIGWINCH, on_winch);
    signal(SIGINT, on_quit);
    signal(SIGTERM, on_quit);
    signal(SIGHUP, on_quit);
    signal(SIGPIPE, SIG_IGN);
    get_size();
    tty_raw();
    run = 1;
    while (run && !got_quit) {
        load_all();
        draw();
        p[0].fd = 0;
        p[0].events = POLLIN;
        p[1].fd = job_fd;
        p[1].events = POLLIN;
        n = poll(p, job_fd >= 0 ? 2 : 1, 500);
        if (n <= 0)
            continue;
        if (job_fd >= 0 && (p[1].revents & (POLLIN | POLLHUP)))
            job_read();
        if (p[0].revents & POLLIN) {
            k = key();
            if (k)
                run = handle(k);
        }
    }
    tty_back();
    return 0;
}
