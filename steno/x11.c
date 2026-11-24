/* Typing through X, the way Plover does it on X11: the XTEST
 * extension presses keys as if they came from the keyboard. A
 * character no key makes (an em dash, a curly quote) gets a spare key
 * of its own for a moment, as Plover does.
 *
 * This talks the X protocol on the X socket itself, so it needs no
 * X library: the binary stays static. Only a local display
 * (DISPLAY=:0) and MIT-MAGIC-COOKIE-1 (the Xauthority file). */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "steno.h"

#define KEY_PRESS 2
#define KEY_RELEASE 3
#define SHIFT_MASK 1
#define MOD5_MASK 128
#define CUSTOM_LEN 3
#define PLOVER_MAPPING 0x01ffffffL   /* marks our spare keys, as Plover */
#define VOID_SYMBOL 0xffffffL

int out_mode;

static int xfd = -1;
static int xtest_op;
static int min_kc, max_kc;
static unsigned seq;
static int need_keymap;

static unsigned char wbuf[8192];
static int wlen;

struct mapping {
    long keysym;
    int keycode;
    int mods;
    int custom;            /* one of our spare keys */
};

static struct mapping *maps;
static int nmaps, cmaps;
static int spare[256];         /* spare keycodes, oldest used first */
static long spare_sym[256][CUSTOM_LEN];
static int nspare;
static int mod_key[8];         /* a keycode for each modifier bit */
static int bs_code, bs_mods;

/* --- the connection ------------------------------------------------- */

static int x_write_all(const unsigned char *p, int n)
{
    int k;

    while (n > 0) {
        k = write(xfd, p, n);
        if (k < 0 && errno == EINTR)
            continue;
        if (k <= 0)
            return -1;
        p += k;
        n -= k;
    }
    return 0;
}

static void x_flush(void)
{
    int res;

    if (wlen && xfd >= 0) {
        res = x_write_all(wbuf, wlen);
        if (res < 0)
            say("lost the X connection", 0);
    }
    wlen = 0;
}

static void put8(int v)
{
    if (wlen >= (int)sizeof(wbuf))
        x_flush();
    wbuf[wlen] = (unsigned char)v;
    wlen++;
}

static void put16(int v)
{
    put8(v & 0xff);
    put8((v >> 8) & 0xff);
}

static void put32(long v)
{
    put16((int)(v & 0xffff));
    put16((int)((v >> 16) & 0xffff));
}

static int x_read(unsigned char *p, int n)
{
    int k;

    while (n > 0) {
        k = read(xfd, p, n);
        if (k < 0 && errno == EINTR)
            continue;
        if (k <= 0)
            return -1;
        p += k;
        n -= k;
    }
    return 0;
}

static long get16(const unsigned char *p)
{
    return p[0] | (p[1] << 8);
}

static long get32(const unsigned char *p)
{
    return get16(p) | (get16(p + 2) << 16);
}

static void x_packet(unsigned char *p)
{
    if (p[0] == 0) {
        if (verbose)
            fprintf(stderr, "steno: X error %d (request %d)\n", p[1],
                p[10]);
    } else if ((p[0] & 0x7f) == 34) {
        need_keymap = 1;       /* MappingNotify */
    }
}

/* Read until the reply to request number s; *extra gets its body. */
static int x_reply(unsigned s, unsigned char *head, unsigned char **extra)
{
    long len;
    int r;

    x_flush();
    for (;;) {
        r = x_read(head, 32);
        if (r < 0)
            return -1;
        if (head[0] == 1) {
            len = get32(head + 4) * 4;
            *extra = 0;
            if (len) {
                *extra = malloc(len);
                if (!*extra)
                    return -1;
                r = x_read(*extra, len);
                if (r < 0)
                    return -1;
            }
            if ((get16(head + 2) & 0xffff) == (s & 0xffff))
                return 0;
            free(*extra);
            continue;
        }
        x_packet(head);
        if (head[0] == 0 && (get16(head + 2) & 0xffff) == (s & 0xffff))
            return -1;
    }
}

static void request(int op, int data, int len)
{
    put8(op);
    put8(data);
    put16(len);
    seq++;
}

static void sync_x(void)
{
    int res;
    unsigned char h[32], *e;

    request(43, 0, 1);         /* GetInputFocus */
    res = x_reply(seq, h, &e);
    if (res == 0)
        free(e);
}

static int display_number(void)
{
    const char *d;
    int n;

    d = getenv("DISPLAY");
    if (!d)
        return -1;
    if (!strncmp(d, "unix:", 5))
        d += 4;
    if (*d != ':')
        return -1;
    d++;
    if (*d < '0' || *d > '9')
        return -1;
    n = 0;
    while (*d >= '0' && *d <= '9') {
        n = n * 10 + (*d - '0');
        d++;
    }
    return n;
}

/* The cookie for display n from the Xauthority file. */
static int read_cookie(int n, unsigned char *cookie)
{
    const char *path;
    char *f, num[16];
    long size, i, len[4], off[4];
    int k, family, found;
    char home[1024];

    path = getenv("XAUTHORITY");
    if (!path || !*path) {
        if (!getenv("HOME"))
            return -1;
        sprintf(home, "%.1000s/.Xauthority", getenv("HOME"));
        path = home;
    }
    f = read_file(path, &size);
    if (!f)
        return -1;
    sprintf(num, "%d", n);
    found = -1;
    i = 0;
    while (i + 2 <= size) {
        family = ((unsigned char)f[i] << 8) | (unsigned char)f[i + 1];
        i += 2;
        for (k = 0; k < 4; k++) {
            if (i + 2 > size)
                goto done;
            len[k] = ((unsigned char)f[i] << 8) | (unsigned char)f[i + 1];
            off[k] = i + 2;
            i += 2 + len[k];
        }
        if (i > size)
            break;
        /* local (256) or any (65535), this display or any */
        if ((family == 256 || family == 65535) &&
            (len[1] == 0 || (len[1] == (long)strlen(num) &&
            !memcmp(f + off[1], num, len[1]))) && len[2] == 18 &&
            !memcmp(f + off[2], "MIT-MAGIC-COOKIE-1", 18) && len[3] == 16)
        {
            memcpy(cookie, f + off[3], 16);
            found = 0;
            break;
        }
    }
done:
    free(f);
    return found;
}

static int x_connect(void)
{
    int res;
    struct sockaddr_un sa;
    unsigned char cookie[16], h[8], *setup;
    int n, have_cookie, i, r;
    long len;

    n = display_number();
    if (n < 0) {
        say("no local X display (DISPLAY=:0)", 0);
        return -1;
    }
    xfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (xfd < 0)
        return -1;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    sprintf(sa.sun_path, "/tmp/.X11-unix/X%d", n);
    res = connect(xfd, (struct sockaddr *)&sa, sizeof(sa));
    if (res < 0) {
        say("can't connect to the X server at %s", sa.sun_path);
        close(xfd);
        xfd = -1;
        return -1;
    }
    have_cookie = read_cookie(n, cookie) == 0;
    put8('l');
    put8(0);
    put16(11);
    put16(0);
    put16(have_cookie ? 18 : 0);
    put16(have_cookie ? 16 : 0);
    put16(0);
    if (have_cookie) {
        for (i = 0; i < 18; i++)
            put8("MIT-MAGIC-COOKIE-1"[i]);
        put16(0);
        for (i = 0; i < 16; i++)
            put8(cookie[i]);
    }
    x_flush();
    r = x_read(h, 8);
    if (r < 0)
        return -1;
    len = get16(h + 6) * 4;
    setup = malloc(len + 1);
    if (!setup)
        return -1;
    r = x_read(setup, len);
    if (r < 0)
        return -1;
    if (h[0] != 1) {
        say("the X server said no (Xauthority?)", 0);
        free(setup);
        return -1;
    }
    /* the reply after its first 8 bytes: the keycodes there are */
    min_kc = setup[26];
    max_kc = setup[27];
    free(setup);
    return 0;
}

static int query_extension(const char *name)
{
    int res;
    int n, i;
    unsigned char h[32], *e;

    n = strlen(name);
    request(98, 0, 2 + (n + 3) / 4);
    put16(n);
    put16(0);
    for (i = 0; i < n; i++)
        put8(name[i]);
    for (; i % 4; i++)
        put8(0);
    res = x_reply(seq, h, &e);
    if (res < 0)
        return -1;
    free(e);
    if (!h[8])
        return -1;
    return h[9];
}

/* --- the keyboard map ------------------------------------------------ */

static void add_map(long sym, int code, int mods, int custom)
{
    int i;

    for (i = 0; i < nmaps; i++)
        if (maps[i].keysym == sym) {
            /* fewer modifiers win */
            if (mods < maps[i].mods) {
                maps[i].keycode = code;
                maps[i].mods = mods;
                maps[i].custom = custom;
            }
            return;
        }
    if (nmaps == cmaps) {
        struct mapping *p;
        cmaps = cmaps ? cmaps * 2 : 512;
        p = malloc(cmaps * sizeof(*p));
        if (!p)
            return;
        if (maps) {
            memcpy(p, maps, nmaps * sizeof(*p));
            free(maps);
        }
        maps = p;
    }
    maps[nmaps].keysym = sym;
    maps[nmaps].keycode = code;
    maps[nmaps].mods = mods;
    maps[nmaps].custom = custom;
    nmaps++;
}

static struct mapping *find_map(long sym)
{
    int i;

    for (i = 0; i < nmaps; i++)
        if (maps[i].keysym == sym)
            return &maps[i];
    return 0;
}

static int read_keymap(void)
{
    int res;
    unsigned char h[32], *e, *m;
    int count, per, k, i, j, len, custom, mods;
    long sym;

    nmaps = 0;
    nspare = 0;
    count = max_kc - min_kc + 1;
    request(101, 0, 2);            /* GetKeyboardMapping */
    put8(min_kc);
    put8(count);
    put16(0);
    res = x_reply(seq, h, &e);
    if (res < 0)
        return -1;
    per = h[1];
    for (k = 0; k < count; k++) {
        m = e + (long)k * per * 4;
        len = per;
        while (len > 0 && get32(m + (len - 1) * 4) == 0)
            len--;
        custom = 0;
        if (!len) {
            custom = 1;            /* a free key, never used */
            spare_sym[nspare][0] = VOID_SYMBOL;
            spare_sym[nspare][1] = VOID_SYMBOL;
            spare_sym[nspare][2] = PLOVER_MAPPING;
        } else if (len == CUSTOM_LEN &&
            get32(m + (CUSTOM_LEN - 1) * 4) == PLOVER_MAPPING)
        {
            custom = 1;            /* one Plover or we used before */
            for (j = 0; j < CUSTOM_LEN; j++)
                spare_sym[nspare][j] = get32(m + j * 4);
        }
        if (custom && nspare < 256) {
            spare[nspare] = min_kc + k;
            nspare++;
        }
        for (i = 0; i < (len ? len : CUSTOM_LEN); i++) {
            if (i != 0 && i != 1 && i != 4 && i != 5)
                continue;
            sym = len ? get32(m + i * 4) : VOID_SYMBOL;
            if (sym == PLOVER_MAPPING || sym == 0 || sym == VOID_SYMBOL)
                continue;
            mods = (i % 2 ? SHIFT_MASK : 0) | (i >= 4 ? MOD5_MASK : 0);
            add_map(sym, min_kc + k, mods, custom);
        }
    }
    free(e);
    request(119, 0, 1);            /* GetModifierMapping */
    res = x_reply(seq, h, &e);
    if (res < 0)
        return -1;
    per = h[1];
    for (i = 0; i < 8; i++) {
        mod_key[i] = 0;
        for (j = 0; j < per; j++)
            if (e[i * per + j]) {
                mod_key[i] = e[i * per + j];
                break;
            }
    }
    free(e);
    bs_code = 0;
    if (find_map(0xff08L)) {
        bs_code = find_map(0xff08L)->keycode;
        bs_mods = find_map(0xff08L)->mods;
    }
    need_keymap = 0;
    return 0;
}

/* Give keysym a spare key: the one used longest ago. */
static struct mapping *map_spare(long sym)
{
    int kc, i, j;
    long old[CUSTOM_LEN];

    if (!nspare)
        return 0;
    kc = spare[0];
    for (j = 0; j < CUSTOM_LEN; j++)
        old[j] = spare_sym[0][j];
    memmove(spare, spare + 1, (nspare - 1) * sizeof(int));
    memmove(spare_sym, spare_sym + 1, (nspare - 1) * sizeof(spare_sym[0]));
    spare[nspare - 1] = kc;
    /* only its first place is used (Plover also uses the shifted one) */
    old[0] = sym;
    old[1] = VOID_SYMBOL;
    old[2] = PLOVER_MAPPING;
    for (j = 0; j < CUSTOM_LEN; j++)
        spare_sym[nspare - 1][j] = old[j];
    for (i = 0; i < nmaps; i++)
        if (maps[i].keycode == kc && maps[i].custom) {
            maps[i] = maps[nmaps - 1];
            nmaps--;
            i--;
        }
    request(100, 1, 2 + CUSTOM_LEN);   /* ChangeKeyboardMapping */
    put8(kc);
    put8(CUSTOM_LEN);
    put16(0);
    for (j = 0; j < CUSTOM_LEN; j++)
        put32(old[j]);
    add_map(sym, kc, 0, 1);
    sync_x();
    /* our own change comes back as a MappingNotify: no need to read
     * the map again for it */
    need_keymap = 0;
    return find_map(sym);
}

static void use_spare(struct mapping *m)
{
    int i;
    long syms[CUSTOM_LEN];

    if (!m->custom)
        return;
    for (i = 0; i < nspare; i++)
        if (spare[i] == m->keycode) {
            memcpy(syms, spare_sym[i], sizeof(syms));
            memmove(spare + i, spare + i + 1, (nspare - i - 1) *
                sizeof(int));
            memmove(spare_sym + i, spare_sym + i + 1, (nspare - i - 1) *
                sizeof(spare_sym[0]));
            spare[nspare - 1] = m->keycode;
            memcpy(spare_sym[nspare - 1], syms, sizeof(syms));
            return;
        }
}

/* --- pressing keys ------------------------------------------------- */

static void fake(int type, int code)
{
    request(xtest_op, 2, 9);       /* XTestFakeInput */
    put8(type);
    put8(code);
    put16(0);
    put32(0);                      /* time: now */
    put32(0);                      /* root: none */
    put32(0);
    put32(0);
    put16(0);
    put16(0);
    put32(0);
    put16(0);
    put8(0);
    put8(0);                       /* device */
}

static void change_mods(int from, int to)
{
    int n;

    for (n = 0; n < 8; n++) {
        if ((to & (1 << n)) && !(from & (1 << n)) && mod_key[n])
            fake(KEY_PRESS, mod_key[n]);
        else if (!(to & (1 << n)) && (from & (1 << n)) && mod_key[n])
            fake(KEY_RELEASE, mod_key[n]);
    }
}

static void press(int code, int mods)
{
    change_mods(0, mods);
    fake(KEY_PRESS, code);
    fake(KEY_RELEASE, code);
    change_mods(mods, 0);
}

static long char_keysym(long c)
{
    if ((c >= 0x20 && c <= 0x7e) || (c >= 0xa0 && c <= 0xff))
        return c;
    if (c == '\t')
        return 0xff09L;
    if (c == '\n' || c == '\r')
        return 0xff0dL;
    return c | 0x01000000L;
}

static int ready(void)
{
    if (xfd < 0)
        return 0;
    if (need_keymap)
        read_keymap();
    return 1;
}

/* --- the test output (steno -t): what would be typed ---------------- */

static void print_esc(const char *s, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        if (s[i] == '\n')
            fputs("\\n", stdout);
        else if (s[i] == '\t')
            fputs("\\t", stdout);
        else if (s[i] == '\\')
            fputs("\\\\", stdout);
        else
            putchar(s[i]);
    }
}

/* --- what the formatter calls --------------------------------------- */

void out_backspaces(int n)
{
    int i, on;

    if (out_mode) {
        printf("B %d\n", n);
        return;
    }
    on = ready();
    if (!on || !bs_code)
        return;
    for (i = 0; i < n; i++)
        press(bs_code, bs_mods);
    x_flush();
}

void out_string(const char *s, int n)
{
    int res;
    int i, k, cur, last;
    long c, sym;
    struct mapping *m;

    if (out_mode) {
        printf("S ");
        print_esc(s, n);
        putchar('\n');
        return;
    }
    res = ready();
    if (!res)
        return;
    cur = 0;
    i = 0;
    while (i < n) {
        k = u8get(s + i, n - i, &c);
        if (!k) {
            k = 1;
            c = (unsigned char)s[i];
        }
        i += k;
        last = i >= n;
        sym = char_keysym(c);
        m = find_map(sym);
        if (m)
            use_spare(m);
        else
            m = map_spare(sym);
        if (!m)
            continue;
        change_mods(cur, m->mods);
        cur = m->mods;
        fake(KEY_PRESS, m->keycode);
        fake(KEY_RELEASE, m->keycode);
        if (last)
            change_mods(cur, 0);
        x_flush();
    }
    if (cur)
        change_mods(cur, 0);
    x_flush();
}

int atoi_small(const char *s)
{
    int n;

    n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    if (*s)
        return -1;
    return n;
}

/* Key names, as Plover's KEY_TO_KEYSYM (X keysym names, any case). */
static const struct {
    const char *name;
    long sym;
} keynames[] = {
    { "backspace", 0xff08 }, { "tab", 0xff09 }, { "linefeed", 0xff0a },
    { "clear", 0xff0b }, { "return", 0xff0d }, { "pause", 0xff13 },
    { "scroll_lock", 0xff14 }, { "sys_req", 0xff15 },
    { "escape", 0xff1b }, { "delete", 0xffff }, { "home", 0xff50 },
    { "left", 0xff51 }, { "up", 0xff52 }, { "right", 0xff53 },
    { "down", 0xff54 }, { "prior", 0xff55 }, { "page_up", 0xff55 },
    { "next", 0xff56 }, { "page_down", 0xff56 }, { "end", 0xff57 },
    { "begin", 0xff58 }, { "select", 0xff60 }, { "print", 0xff61 },
    { "execute", 0xff62 }, { "insert", 0xff63 }, { "undo", 0xff65 },
    { "redo", 0xff66 }, { "menu", 0xff67 }, { "find", 0xff68 },
    { "cancel", 0xff69 }, { "help", 0xff6a }, { "break", 0xff6b },
    { "mode_switch", 0xff7e }, { "num_lock", 0xff7f },
    { "kp_enter", 0xff8d }, { "kp_multiply", 0xffaa },
    { "kp_add", 0xffab }, { "kp_subtract", 0xffad },
    { "kp_decimal", 0xffae }, { "kp_divide", 0xffaf },
    { "shift_l", 0xffe1 }, { "shift_r", 0xffe2 },
    { "control_l", 0xffe3 }, { "control_r", 0xffe4 },
    { "caps_lock", 0xffe5 }, { "shift_lock", 0xffe6 },
    { "meta_l", 0xffe7 }, { "meta_r", 0xffe8 }, { "alt_l", 0xffe9 },
    { "alt_r", 0xffea }, { "super_l", 0xffeb }, { "super_r", 0xffec },
    { "hyper_l", 0xffed }, { "hyper_r", 0xffee },
    { "iso_level3_shift", 0xfe03 }, { "iso_left_tab", 0xfe20 },
    { "control", 0xffe3 }, { "shift", 0xffe1 }, { "alt", 0xffe9 },
    { "option", 0xffe9 }, { "super", 0xffeb }, { "command", 0xffeb },
    { "windows", 0xffeb },
    { "space", 0x20 }, { "exclam", 0x21 }, { "quotedbl", 0x22 },
    { "numbersign", 0x23 }, { "dollar", 0x24 }, { "percent", 0x25 },
    { "ampersand", 0x26 }, { "apostrophe", 0x27 },
    { "quoteright", 0x27 }, { "parenleft", 0x28 },
    { "parenright", 0x29 }, { "asterisk", 0x2a }, { "plus", 0x2b },
    { "comma", 0x2c }, { "minus", 0x2d }, { "period", 0x2e },
    { "slash", 0x2f }, { "colon", 0x3a }, { "semicolon", 0x3b },
    { "less", 0x3c }, { "equal", 0x3d }, { "greater", 0x3e },
    { "question", 0x3f }, { "at", 0x40 }, { "bracketleft", 0x5b },
    { "backslash", 0x5c }, { "bracketright", 0x5d },
    { "asciicircum", 0x5e }, { "underscore", 0x5f }, { "grave", 0x60 },
    { "quoteleft", 0x60 }, { "braceleft", 0x7b }, { "bar", 0x7c },
    { "braceright", 0x7d }, { "asciitilde", 0x7e },
    { "audiolowervolume", 0x1008ff11L }, { "audiomute", 0x1008ff12L },
    { "audioraisevolume", 0x1008ff13L }, { "audioplay", 0x1008ff14L },
    { "audiostop", 0x1008ff15L }, { "audioprev", 0x1008ff16L },
    { "audionext", 0x1008ff17L }, { "audiopause", 0x1008ff31L },
    { "audiomicmute", 0x1008ffb2L },
    { "monbrightnessup", 0x1008ff02L },
    { "monbrightnessdown", 0x1008ff03L }
};

static long key_sym(const char *name)
{
    char low[64], *p;
    int i, n;

    scopy(low, name, sizeof(low));
    for (i = 0; low[i]; i++)
        low[i] = (char)low_ascii((unsigned char)low[i]);
    p = low;
    if (!strncmp(p, "xf86_", 5))
        p += 5;
    else if (!strncmp(p, "xf86", 4))
        p += 4;
    n = strlen(p);
    if (n == 1 && ((p[0] >= 'a' && p[0] <= 'z') ||
        (p[0] >= '0' && p[0] <= '9')))
    {
        return p[0];
    }
    if (n >= 2 && n <= 3 && p[0] == 'f' && p[1] >= '1' && p[1] <= '9') {
        i = atoi_small(p + 1);
        if (i >= 1 && i <= 35)
            return 0xffbe + i - 1;
    }
    for (i = 0; i < (int)(sizeof(keynames) / sizeof(keynames[0])); i++)
        if (!strcmp(p, keynames[i].name))
            return keynames[i].sym;
    return -1;
}


/* Plover's key combo language: "Control_L(Shift_L(Left))", "a b". */
void out_combo(const char *combo)
{
    int res;
    int down[32], ndown, ev[256], evtype[256], nev, i, j, k, rel, code;
    char name[64];
    long sym;
    struct mapping *m;

    if (out_mode) {
        printf("K %s\n", combo);
        return;
    }
    res = ready();
    if (!res)
        return;
    ndown = 0;
    nev = 0;
    i = 0;
    while (combo[i]) {
        if (combo[i] == ' ' || combo[i] == '\t' || combo[i] == '\n') {
            i++;
            continue;
        }
        if (combo[i] == ')') {
            if (!ndown)
                goto bad;
            ndown--;
            if (nev < 256) {
                ev[nev] = down[ndown];
                evtype[nev] = KEY_RELEASE;
                nev++;
            }
            i++;
            continue;
        }
        if (!is_wordc_at(combo + i, strlen(combo + i)))
            goto bad;
        k = 0;
        while (combo[i] && is_wordc_at(combo + i, strlen(combo + i)) &&
            k < 63)
        {
            name[k] = combo[i];
            k++;
            i++;
        }
        name[k] = 0;
        j = i;
        while (combo[j] == ' ' || combo[j] == '\t')
            j++;
        rel = 1;
        if (combo[j] == '(') {
            rel = 0;
            i = j + 1;
        }
        sym = key_sym(name);
        m = sym < 0 ? 0 : find_map(sym);
        if (!m) {
            say("unknown key in combo: %s", name);
            return;
        }
        code = m->keycode;
        for (j = 0; j < ndown; j++)
            if (down[j] == code) {
                say("key pressed twice in combo: %s", name);
                return;
            }
        if (nev < 256) {
            ev[nev] = code;
            evtype[nev] = KEY_PRESS;
            nev++;
        }
        if (rel) {
            if (nev < 256) {
                ev[nev] = code;
                evtype[nev] = KEY_RELEASE;
                nev++;
            }
        } else if (ndown < 32) {
            down[ndown] = code;
            ndown++;
        }
    }
    if (ndown)
        goto bad;
    for (i = 0; i < nev; i++)
        fake(evtype[i], ev[i]);
    x_flush();
    return;
bad:
    say("bad key combo: %s", combo);
}

int out_open(void)
{
    int res;

    if (out_mode)
        return 0;
    res = x_connect();
    if (res < 0)
        return -1;
    xtest_op = query_extension("XTEST");
    if (xtest_op < 0) {
        say("the X server has no XTEST", 0);
        return -1;
    }
    return read_keymap();
}

int out_fd(void)
{
    return xfd;
}

/* Events and errors the X server sends on its own. */
void out_event(void)
{
    unsigned char p[32];
    struct pollfd pf;
    int r;

    pf.fd = xfd;
    pf.events = POLLIN;
    while (xfd >= 0 && poll(&pf, 1, 0) > 0) {
        if (pf.revents & (POLLHUP | POLLERR)) {
            say("lost the X connection", 0);
            die(1);
        }
        r = x_read(p, 32);
        if (r < 0) {
            say("lost the X connection", 0);
            die(1);
        }
        x_packet(p);
    }
}
