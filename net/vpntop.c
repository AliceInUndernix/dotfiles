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
#define MIN_COLS 60
#define MIN_ROWS 18

enum {
    c_none, c_red, c_green, c_yellow, c_blue, c_magenta, c_cyan, c_dim,
    c_bold, c_title, c_key, c_bar_green, c_bar_red, c_bar_cyan,
    c_bar_yellow, c_head, c_head_ok, c_head_bad, c_head_busy
};

static const char *color_codes[] = {
    "\033[0m", "\033[0;31m", "\033[0;32m", "\033[0;33m", "\033[0;34m",
    "\033[0;35m", "\033[0;36m", "\033[0;2m", "\033[0;1m", "\033[0;1;36m",
    "\033[0;1;7m", "\033[0;42m", "\033[0;41m", "\033[0;46m", "\033[0;43m",
    "\033[0;7m", "\033[0;1;30;42m", "\033[0;1;37;41m", "\033[0;1;30;43m"
};

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
#define DEC_FLAG 128            /* line drawing (DEC special graphics) */

static char grid_ch[GRID_ROWS][GRID_COLS], prev_ch[GRID_ROWS][GRID_COLS];
static unsigned char grid_co[GRID_ROWS][GRID_COLS];
static unsigned char prev_co[GRID_ROWS][GRID_COLS];
static int grid_r, grid_c, grid_color, prev_ok;

static void go(int row, int col)
{
    grid_r = row - 1;
    grid_c = col - 1;
}

static void color(int c)
{
    grid_color = c;
}

static void gput(char ch, int dec)
{
    if (grid_r >= 0 && grid_r < GRID_ROWS && grid_c >= 0 &&
        grid_c < GRID_COLS)
    {
        grid_ch[grid_r][grid_c] = ch;
        grid_co[grid_r][grid_c] = (unsigned char)(grid_color |
            (dec ? DEC_FLAG : 0));
    }
    grid_c++;
}

static void grid_clear(void)
{
    memset(grid_ch, ' ', sizeof grid_ch);
    memset(grid_co, 0, sizeof grid_co);
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

static void pen_n(int c, long long v)
{
    char b[24];

    pen(c, num(v, b));
}

/* pen_pad: spaces up to column w of this line (w counted from 0) */
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

/* bar: n cells, the first full of them in colour c, the rest dots */
static void pen_bar(int c, int n, int full)
{
    int i;

    for (i = 0; i < n && pen_left > 0; i++) {
        if (i < full) {
            color(c);
            gput(' ', 0);
        } else {
            color(c_dim);
            gput('.', 0);
        }
        pen_left--;
    }
    color(c_none);
}

/* box: a frame with a title */
static void box(int top, int left, int h, int w, const char *title)
{
    int r, i, tl;

    if (h < 2 || w < 4)
        return;
    color(c_blue);
    go(top, left);
    gput('l', 1);
    gput('q', 1);
    tl = (int)strlen(title);
    if (tl > w - 6)
        tl = w - 6;
    color(c_title);
    gput(' ', 0);
    for (i = 0; i < tl; i++)
        gput(title[i], 0);
    gput(' ', 0);
    color(c_blue);
    for (i = tl + 4; i < w - 1; i++)
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

/* present: the rows that changed since the last frame, to the
 * terminal (the very last cell is left out: writing it may scroll) */
static void present(int nrows, int ncols)
{
    int r, c, last, dec, co, end;

    for (r = 0; r < nrows && r < GRID_ROWS; r++) {
        if (prev_ok && !memcmp(grid_ch[r], prev_ch[r], ncols) &&
            !memcmp(grid_co[r], prev_co[r], ncols))
        {
            continue;
        }
        out_s("\033[");
        out_n(r + 1);
        out_s(";1H");
        last = -1;
        dec = 0;
        end = r == nrows - 1 ? ncols - 1 : ncols;
        for (c = 0; c < end; c++) {
            co = grid_co[r][c];
            if (co != last) {
                if ((co & DEC_FLAG) && !dec) {
                    out_s("\033(0");
                    dec = 1;
                } else if (!(co & DEC_FLAG) && dec) {
                    out_s("\033(B");
                    dec = 0;
                }
                if (last < 0 || (co & ~DEC_FLAG) != (last & ~DEC_FLAG))
                    out_s(color_codes[co & ~DEC_FLAG]);
                last = co;
            }
            out_c(grid_ch[r][c]);
        }
        if (dec)
            out_s("\033(B");
        out_s("\033[0m");
        memcpy(prev_ch[r], grid_ch[r], ncols);
        memcpy(prev_co[r], grid_co[r], ncols);
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
    out_s("\033[?1049h\033[?25l\033[2J");
    out_flush();
    prev_ok = 0;
}

static void tty_back(void)
{
    out_s("\033[0m\033[?25h\033[?1049l");
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
        return "your own IP, Cloudflare DNS (no VPN, no Tor)";
    if (same(lv, "1"))
        return "VPN, Cloudflare DNS through the VPN";
    if (same(lv, "2"))
        return "VPN, DNS through Tor (the usual one)";
    if (same(lv, "3"))
        return "VPN, and everything goes through Tor";
    if (same(lv, "sandbox"))
        return "own IP, only programs started with vpn run go online";
    if (same(lv, "offline"))
        return "offline: nothing goes out";
    return "?";
}

static const char *fw_text(const char *mode)
{
    if (same(mode, "closed"))
        return "closed: nothing goes out";
    if (same(mode, "pick"))
        return "only to Riseup's servers";
    if (same(mode, "vpn"))
        return "only through the VPN";
    if (same(mode, "torvpn"))
        return "only through Tor";
    if (same(mode, "direct"))
        return "open (level 0), no IPv6";
    if (same(mode, "sandbox"))
        return "only vpn run programs and Tor";
    if (same(mode, "refresh"))
        return "open for root: new list";
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

/* row 1: the name, the state in colour, the level, the clock */
static void draw_head(void)
{
    const char *st, *lv;
    char b[32];
    int c;

    st = sv("state");
    lv = sv("level");
    pen_at(1, 1, cols);
    pen(c_head, " vpn ");
    pen(c_none, " ");
    if (is_off()) {
        pen(c_head_bad, " OFF ");
        pen(c_bold, "  level -1: no kill switch, VPN or Tor");
    } else if (!st[0]) {
        pen(c_head_bad, " NOT RUNNING ");
    } else {
        c = c_head_busy;
        if (same(st, "ok"))
            c = c_head_ok;
        else if (same(st, "problem") || same(st, "stopped"))
            c = c_head_bad;
        pen(c, " ");
        pen(c, st);
        pen(c, " ");
        if (fw_is_off())
            pen(c_head_bad, " FIREWALL OFF ");
        pen(c_bold, "  level ");
        pen(c_bold, lv);
        pen(c_none, ": ");
        pen(c_none, level_text(lv));
    }
    pen_pad_to(cols, cols - 13);
    pen(c_dim, clock_text(b));
    pen_end();
}

/* row 2: in plain words, what is going on and what to do */
static void draw_now(void)
{
    const char *st, *no, *pr;
    long long step, t;
    char b[64];

    st = sv("state");
    no = sv("note");
    pr = sv("problem");
    t = time(0);
    pen_at(2, 1, cols);
    pen(c_none, " ");
    if (job_pid > 0) {
        pen(c_yellow, "running: vpn ");
        pen(c_yellow, job_name + 4);
        pen(c_yellow, " (");
        pen(c_yellow, duration(t - job_t, b));
        pen(c_yellow, ")");
    } else if (msg[0] && t - msg_t < 10) {
        pen(msg_color, msg);
    } else if (is_off()) {
        pen(c_yellow, "Everything of vpn is off. Press 2 (or any level) to"
            " turn it back on.");
    } else if (!st[0]) {
        pen(c_red, "The vpn service is not running: press r to start it.");
    } else if (pr[0]) {
        pen(c_red, "PROBLEM: ");
        pen(c_red, pr);
        pen(c_none, "  (r: try again, n: new list, ?: help)");
    } else if (no[0]) {
        step = to_num(sv("step"));
        pen(c_yellow, "now: ");
        pen(c_none, no);
        if (step > 0) {
            pen(c_dim, " (");
            pen(c_dim, duration(t - step, b));
            pen(c_dim, ")");
        }
    } else if (same(st, "ok")) {
        pen(c_green, "All good: ");
        if (same(sv("level"), "0"))
            pen(c_none, "you are online with your own IP, no VPN.");
        else if (same(sv("level"), "sandbox"))
            pen(c_none, "only programs started with vpn run are online.");
        else if (same(sv("level"), "3"))
            pen(c_none, "everything goes through the VPN and then Tor.");
        else
            pen(c_none, "everything goes through the VPN; the kill switch"
                " is on.");
    } else {
        pen(c_yellow, st);
        pen(c_none, "...");
    }
    pen_end();
}

/* label: "Name    " in a fixed width, then the value follows */
static void label(const char *s)
{
    int n;

    pen(c_dim, s);
    n = (int)strlen(s);
    while (n < 9) {
        pen(c_none, " ");
        n++;
    }
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

static void draw_connection(int top, int left, int h, int w)
{
    const char *lv, *ip, *ce, *cl;
    long long t0;
    char b[64];
    int r, own;

    box(top, left, h, w, "Connection");
    lv = sv("level");
    ip = sv("server");
    own = same(lv, "0") || same(lv, "sandbox");
    r = top + 1;
    if (watch_only) {
        pen_at(r, left + 2, w - 4);
        label("Level");
        pen(c_bold, lv);
        pen_end();
        pen_at(r + 1, left + 2, w - 4);
        pen(c_dim, "more: run vpn (your password)");
        pen_end();
        return;
    }
    pen_at(r, left + 2, w - 4);
    label("Server");
    if (own || !ip[0]) {
        pen(c_yellow, own ? "none: your own IP" : "none yet");
    } else {
        pen(c_bold, sv("location"));
        pen(c_none, " ");
        pen(c_bold, sv("country"));
        pen(c_none, "  ");
        pen(zone_color(sv("zone")), sv("zone"));
        pen(c_dim, " zone");
    }
    pen_end();
    pen_at(r + 1, left + 2, w - 4);
    label("Address");
    pen(c_none, own ? "-" : ip);
    pen_end();
    pen_at(r + 2, left + 2, w - 4);
    label("Speed");
    pen(c_none, own ? "-" : sv("speed"));
    pen_end();
    pen_at(r + 3, left + 2, w - 4);
    label("Up for");
    t0 = since_epoch(sv("since"));
    if (t0 > 0 && same(sv("state"), "ok"))
        pen(c_none, duration(time(0) - t0, b));
    else
        pen(c_dim, "-");
    pen_end();
    if (h > 6) {
        pen_at(r + 4, left + 2, w - 4);
        label("Clock");
        cl = sv("clock");
        pen(same(cl, "ok") || !strncmp(cl, "set", 3) ? c_green : c_yellow,
            cl[0] ? cl : "-");
        pen_end();
    }
    if (h > 7) {
        pen_at(r + 5, left + 2, w - 4);
        label("Cert");
        ce = sv("certificate");
        if (ce[0]) {
            char m[16], d[16], y[16];

            pen(c_none, "valid until ");
            pen(c_none, word(ce, 0, m, sizeof m));
            pen(c_none, " ");
            pen(c_none, word(ce, 1, d, sizeof d));
            pen(c_none, " ");
            pen(c_none, word(ce, 3, y, sizeof y));
        } else {
            pen(c_none, "-");
        }
        pen_end();
    }
}

static void draw_protection(int top, int left, int h, int w)
{
    char b[SMALL_MAX], dns[64], mode[64];
    int fd, flags, r, rc, locked;
    const char *tor;

    box(top, left, h, w, "Protection");
    r = top + 1;
    pen_at(r, left + 2, w - 4);
    label("Kill sw.");
    read_small(RUN_DIR "/fwmode", mode, sizeof mode);
    first_line(mode);
    if (is_off())
        pen(c_red, "off (level -1)");
    else if (fw_is_off())
        pen(c_red, "OFF: f turns it on");
    else if (!mode[0])
        pen(c_yellow, "?");
    else
        pen(same(mode, "direct") ? c_yellow : c_green, fw_text(mode));
    pen_end();

    pen_at(r + 1, left + 2, w - 4);
    label("DNS");
    dns[0] = 0;
    read_small("/etc/resolv.conf", b, sizeof b);
    {
        char *p;

        /* the first line that starts with "nameserver" */
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
    if (same(dns, "127.0.0.1"))
        pen(c_green, "Tor");
    else if (same(dns, "1.1.1.1"))
        pen(c_yellow, "Cloudflare");
    else
        pen(c_yellow, dns[0] ? dns : "none");
    pen(c_dim, locked ? ", locked" : ", not locked");
    pen_end();

    pen_at(r + 2, left + 2, w - 4);
    label("Tor");
    tor = sv("tor");
    if (watch_only)
        pen(c_dim, "run vpn to see");
    else
        pen(same(tor, "running") ? c_green : c_red, tor[0] ? tor : "-");
    if (same(tor, "running"))
        pen(c_dim, " (9050, 9150)");
    pen_end();

    pen_at(r + 3, left + 2, w - 4);
    label("IPv6");
    read_small("/proc/sys/net/ipv6/conf/all/disable_ipv6", b, sizeof b);
    if (b[0] == '1')
        pen(c_green, "off");
    else if (is_off())
        pen(c_none, "on");
    else
        pen(c_yellow, "on, the firewall drops it");
    pen_end();

    pen_at(r + 4, left + 2, w - 4);
    label("LAN");
    if (exists(ETC_DIR "/lan"))
        pen(c_yellow, "on: router, printer reachable");
    else
        pen(c_green, "off");
    pen_end();

    if (h > 7) {
        pen_at(r + 5, left + 2, w - 4);
        label("MAC");
        if (exists(ETC_DIR "/keep-mac"))
            pen(c_yellow, "the card's own (keep-mac)");
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

static void draw_tests(int top, int left, int h, int w)
{
    char tests[STAT_MAX], *ent[64], *f[5], *p, *semi;
    char zone[32], a[32], bb[32], c[32];
    const char *zones, *pr;
    int n, i, first, r, barw, full;
    long long best, s;

    box(top, left, h, w, "Choosing a server");
    if (watch_only) {
        pen_at(top + 1, left + 2, w - 4);
        pen(c_dim, "run vpn to see the tests");
        pen_end();
        return;
    }
    r = top + 1;
    zones = sv("zones");
    pen_at(r, left + 2, w - 4);
    pen(c_dim, "servers: ");
    pen(c_green, "white ");
    pen(c_none, word(zones, 0, a, sizeof a));
    pen(c_yellow, "  gray ");
    pen(c_none, word(zones, 1, a, sizeof a));
    pen(c_red, "  black ");
    pen(c_none, word(zones, 2, a, sizeof a));
    pen_end();

    /* progress: "gray 2 12 1" testing, "gray done 5 3" chosen */
    pr = sv("progress");
    word(pr, 0, zone, sizeof zone);
    word(pr, 1, a, sizeof a);
    word(pr, 2, bb, sizeof bb);
    word(pr, 3, c, sizeof c);
    pen_at(r + 1, left + 2, w - 4);
    if (same(zone, "none")) {
        pen(c_red, "no server worked in any zone");
    } else if (same(a, "done")) {
        pen(c_dim, "used ");
        pen(zone_color(zone), zone);
        pen(c_dim, ": the fastest of ");
        pen(c_none, c);
        pen(c_dim, " tested (");
        pen(c_none, bb);
        pen(c_dim, " tried)");
    } else if (zone[0]) {
        pen(c_yellow, "testing ");
        pen(zone_color(zone), zone);
        pen(c_dim, ": server ");
        pen(c_none, a);
        pen(c_dim, "/");
        pen(c_none, bb);
        pen(c_dim, ", speed-tested ");
        pen(c_none, c);
        pen(c_dim, "/3 ");
        pen_bar(c_bar_yellow, TESTS_MAX * 3, (int)to_num(c) * 3);
    } else {
        pen(c_dim, "no test since the service started");
    }
    pen_end();

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
    /* the newest that fit */
    first = n - (h - 4);
    if (first < 0)
        first = 0;
    barw = w - 4 - 32 - 11;
    if (barw < 3)
        barw = 3;
    if (barw > 30)
        barw = 30;
    for (i = first; i < n && r + 2 + i - first < top + h - 1; i++) {
        split_test(ent[i], f);
        pen_at(r + 2 + i - first, left + 2, w - 4);
        if (same(f[2], sv("server")) && f[3][0])
            pen(c_green, "> ");
        else
            pen(c_none, "  ");
        pen(c_bold, f[0]);
        pen(c_none, " ");
        pen(c_none, f[1]);
        pen_pad_to(w - 4, 15);
        pen(c_dim, f[2]);
        pen_pad_to(w - 4, 32);
        if (f[3][0]) {
            s = to_num(f[3]);
            full = (int)(s * barw / best);
            if (full < 1)
                full = 1;
            pen_bar(c_bar_green, barw, full);
            pen(c_none, " ");
            pen_n(c_green, s);
            pen(c_dim, " KB/s");
        } else {
            pen(c_red, "FAILED ");
            pen(c_red, f[4]);
        }
        pen_end();
    }
}

static void draw_traffic(int top, int left, int h, int w)
{
    char b[64];
    int gh, gw, i, rr, col, idx, filled;
    long long mx, v, dn, up;

    box(top, left, h, w, "Traffic");
    dn = hist_n > 0 ? hist_rx[hist_n - 1] : 0;
    up = hist_n > 0 ? hist_tx[hist_n - 1] : 0;
    pen_at(top + 1, left + 2, w - 4);
    pen(c_bold, iface[0] ? iface : "no network");
    pen(c_cyan, "  down ");
    pen(c_none, size_text(dn, b));
    pen(c_none, "/s");
    pen(c_yellow, "  up ");
    pen(c_none, size_text(up, b));
    pen(c_none, "/s");
    pen_end();
    gh = h - 4;
    gw = w - 4;
    if (gh < 1 || gw < 1)
        return;
    mx = 1024;
    for (i = 0; i < hist_n && i < gw; i++) {
        v = hist_rx[hist_n - 1 - i];
        if (hist_tx[hist_n - 1 - i] > v)
            v = hist_tx[hist_n - 1 - i];
        if (v > mx)
            mx = v;
    }
    /* columns, newest on the right: down cyan, up yellow in front */
    for (rr = 0; rr < gh; rr++) {
        pen_at(top + 2 + rr, left + 2, gw);
        for (col = 0; col < gw; col++) {
            idx = hist_n - gw + col;
            if (idx < 0) {
                pen(c_none, " ");
                continue;
            }
            filled = gh - rr;
            if (hist_tx[idx] * gh >= mx * filled && hist_tx[idx] > 0)
                pen(c_bar_yellow, " ");
            else if (hist_rx[idx] * gh >= mx * filled && hist_rx[idx] > 0)
                pen(c_bar_cyan, " ");
            else if (rr == gh - 1)
                pen(c_dim, "_");
            else
                pen(c_none, " ");
        }
        pen_end();
    }
    pen_at(top + h - 2, left + 2, w - 4);
    pen(c_dim, "top of the graph ");
    pen(c_none, size_text(mx, b));
    pen(c_none, "/s");
    pen(c_dim, "   up is ");
    pen(c_bar_yellow, " ");
    pen(c_dim, " down is ");
    pen(c_bar_cyan, " ");
    pen_end();
}

static void draw_events(int top, int left, int h, int w)
{
    int i, first, n;
    const char *s;

    box(top, left, h, w, "What happened (newest last; e: all of it)");
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
            {
                char t[10];

                memcpy(t, s, 8);
                t[8] = 0;
                pen(c_dim, t);
            }
            pen(c_none, " ");
            pen(event_color(s + 9), s + 9);
        } else {
            pen(c_none, s);
        }
        pen_end();
    }
}

/* the key bar at the bottom, as many rows as it needs */
static const char *keys_main[] = {
    "0 1 2 3 s o", "level", "-", "all off", "r", "reconnect",
    "n", "new list", "l", "LAN", "f", "firewall", "t", "selftest",
    "e", "log", "?", "help", "q", "quit", 0
};

static const char *keys_watch[] = { "q", "quit", 0 };

static int draw_keys(int bottom, int dry)
{
    const char **k;
    int i, len, row, used, lines;

    k = watch_only ? keys_watch : keys_main;
    lines = 1;
    used = 1;
    for (i = 0; k[i]; i += 2) {
        len = (int)(strlen(k[i]) + strlen(k[i + 1])) + 4;
        if (used + len > cols) {
            lines++;
            used = 1;
        }
        used += len;
    }
    if (dry)
        return lines;
    row = bottom - lines + 1;
    pen_at(row, 1, cols);
    used = 1;
    pen(c_none, " ");
    for (i = 0; k[i]; i += 2) {
        len = (int)(strlen(k[i]) + strlen(k[i + 1])) + 4;
        if (used + len > cols) {
            pen_end();
            row++;
            pen_at(row, 1, cols);
            pen(c_none, " ");
            used = 1;
        }
        pen(c_key, " ");
        pen(c_key, k[i]);
        pen(c_key, " ");
        pen(c_none, " ");
        pen(c_none, k[i + 1]);
        pen(c_none, " ");
        used += len;
    }
    pen_end();
    return lines;
}

static void draw_main(void)
{
    int kl, top, avail, th, mh, eh, lw, rw, tw;

    kl = draw_keys(rows, 1);
    draw_head();
    draw_now();
    top = 3;
    avail = rows - 2 - kl;
    th = 8;
    mh = 7;
    if (avail >= th + mh + 4)
        eh = avail - th - mh;
    else
        eh = 0;
    if (eh > 4 && avail >= 30) {
        th = 9;
        mh = 8 + (avail - 30) / 3;
        if (mh > 14)
            mh = 14;
        eh = avail - th - mh;
    }
    if (eh == 0) {
        mh = avail - th;
        if (mh < 5) {
            th = avail;
            mh = 0;
        }
    }
    lw = cols / 2;
    rw = cols - lw;
    draw_connection(top, 1, th, lw);
    draw_protection(top, 1 + lw, th, rw);
    if (mh > 0) {
        /* the tests need about 56 columns, the graph the rest */
        tw = cols * 3 / 5;
        if (tw < 56)
            tw = 56;
        if (cols - tw < 22)
            tw = cols - 22;
        draw_tests(top + th, 1, mh, tw);
        draw_traffic(top + th, 1 + tw, mh, cols - tw);
    }
    if (eh > 0)
        draw_events(top + th + mh, 1, eh, cols);
    draw_keys(rows, 0);
}

static const char *help_text[] = {
    "Every key does one thing at once, without Enter. The screen",
    "updates itself; green is fine, yellow worth knowing, red a problem.",
    "",
    "Levels (until reboot)",
    "  0  your own IP, Cloudflare DNS     s  sandbox: own IP, only",
    "  1  VPN, Cloudflare DNS                programs started with",
    "  2  VPN, DNS through Tor (usual)       vpn run (and Tor) online",
    "  3  VPN, everything through Tor     o  offline: nothing goes out",
    "  -  everything off (level -1), as if vpn were not installed;",
    "     any level turns it back on (that runs install.sh net)",
    "Other keys",
    "  r  pick a server again            n  new server list, certificate",
    "  l  local network on/off           f  kill switch off/on",
    "  t  try every feature (selftest)   e  everything that happened",
    "  q  quit (Ctrl+C too)              Ctrl+L  draw it all again",
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
    box_fill(top, left, h, w, "Help (any key: back)");
    for (i = 0; help_text[i] && i < h - 2; i++) {
        pen_at(top + 1 + i, left + 2, w - 4);
        if (help_text[i][0] && help_text[i][0] != ' ' && i > 2)
            pen(c_bold, help_text[i]);
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
    int w, h, n, i, top, cnt;
    const char *s;

    w = cols - 4;
    h = rows - 3;
    n = log_rows(w, lr_start, lr_part, EV_LINES * 4);
    if (log_top < 0 || log_top > n - h)
        log_top = n - h;
    if (log_top < 0)
        log_top = 0;
    box(1, 1, rows - 1, cols, "Everything that happened since boot");
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
        } else {
            cnt = (int)strlen(s);
            if (cnt > 9) {
                char t[10];

                memcpy(t, s, 8);
                t[8] = 0;
                pen(c_dim, t);
                pen(c_none, " ");
                pen(event_color(s + 9), s + 9);
            } else {
                pen(c_none, s);
            }
        }
        pen_end();
    }
    if (n == 0) {
        pen_at(2, 3, w);
        pen(c_dim, "nothing yet");
        pen_end();
    }
    pen_at(rows, 1, cols);
    pen(c_key, " j k ");
    pen(c_none, " line  ");
    pen(c_key, " space b ");
    pen(c_none, " page  ");
    pen(c_key, " g G ");
    pen(c_none, " top, end  ");
    pen(c_key, " q ");
    pen(c_none, " back   ");
    pen_n(c_dim, n);
    pen(c_dim, " lines");
    pen_end();
}

static void draw_confirm(void)
{
    int w, top, left;
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
    box_fill(top, left, 7, w, "Are you sure?");
    pen_at(top + 1, left + 2, w - 4);
    pen(c_none, a);
    pen_end();
    pen_at(top + 2, left + 2, w - 4);
    pen(c_none, b);
    pen_end();
    pen_at(top + 4, left + 2, w - 4);
    pen(c_key, " y ");
    pen(c_none, " yes     ");
    pen(c_key, " any other key ");
    pen(c_none, " no");
    pen_end();
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
        pen(c_yellow, "make the window at least 60x18 (q quits)");
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
