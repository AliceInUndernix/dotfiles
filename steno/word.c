/* steno -W: add a word. Opened from a dwm key (a floating st), it
 * starts with the last word you wrote (asked from the engine), shows
 * how it sounds (British and American, in IPA and in Russian
 * letters), its syllables as written and as spoken, its meaning, the
 * outlines made for it by Dotterel's rules (gen.c), Lapwing's and the
 * ones your dictionaries have; you stroke or type an outline, Enter
 * saves it into the dictionary the word belongs in (word-homes.txt:
 * base, proper-nouns, affixes ...) and it quits, so the window goes.
 *
 * While it waits for an outline it holds the engine's "outline" FIFO
 * open, so your strokes come here as strokes and are not typed
 * (tools.c); when the window loses focus it lets go of them.
 *
 * Every action has a key; nothing needs the mouse. */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "steno.h"
#include "gen.h"

#define C_CYAN "\033[38;2;95;175;215m"
#define C_MAGENTA "\033[38;2;215;135;215m"

#define LIST_MAX 24
#define LINE 256

enum { m_word, m_outline };
enum { l_made, l_lapwing, l_dict };

static struct gen g;
static int mode;
static char word[64];          /* as typed or given */
static char outline[LINE];
static char msg[LINE];
static int msg_bad;
static int looked;             /* g is for word */
static char home[32];          /* base, proper-nouns ... */
static char kinds[96];
static char meaning[3][LINE];
static int nmeaning;
static char other_case[64];    /* Bill for bill: ^K switches */
static long common;            /* place in frequency.txt, 0 none */
static char lap[LIST_MAX][GEN_TEXT];
static int nlap;
static char have[LIST_MAX][GEN_TEXT], have_dict[LIST_MAX][64];
static int nhave;
static int list, sel[3];
static int asking_drop;
static int capture_fd = -1, focused = 1;
static volatile int resized, stop_now;
static struct termios old_tty;
static int tty;
static const char *homes[] = { "base", "proper-nouns", "affixes",
    "punctuation", "numbers", "commands", "fingerspelling", "user", 0 };

/* --- the word ---------------------------------------------------------- */

static void say_msg(const char *m, int bad)
{
    scopy(msg, m, sizeof(msg));
    msg_bad = bad;
}

static int home_of(const char *w, char *h, int hs, char *k, int ks)
{
    char l[2][GEN_TEXT], *t;

    if (gen_find("word-homes.txt", w, l, 1) != 1)
        return 0;
    t = strchr(l[0], '\t');
    if (t) {
        *t = 0;
        scopy(k, t + 1, ks);
    } else {
        k[0] = 0;
    }
    scopy(h, l[0], hs);
    return 1;
}

static long freq_place(const char *w)
{
    FILE *f;
    char line[128];
    long n;
    int k;

    f = fopen(dotterel_data("frequency.txt"), "r");
    if (!f)
        return 0;
    n = 0;
    k = strlen(w);
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#')
            continue;
        n++;
        if (!strncmp(line, w, k) && (line[k] == '\n' || line[k] == '\r'))
        {
            fclose(f);
            return n;
        }
    }
    fclose(f);
    return 0;
}

/* trim it to the word: spaces, quotes and full stops around it go */
static void clean_word(char *w)
{
    char t[TEXT_MAX];
    int n, a;

    /* an affix stays one: {^ing}, {re^}; ^ing and re^ get braces */
    n = strlen(w);
    if (n > 1 && w[0] == '{' && w[n - 1] == '}' && strchr(w, '^'))
        return;
    if (n > 1 && n < (int)sizeof(word) - 2 && !strchr(w, '{') &&
        (w[0] == '^' || w[n - 1] == '^'))
    {
        sprintf(t, "{%s}", w);
        strcpy(w, t);
        return;
    }
    while (n > 0 && strchr(" \t\n.,;:!?\"')]}", w[n - 1]))
        n--;
    w[n] = 0;
    a = 0;
    while (w[a] && strchr(" \t\n\"'([{", w[a]))
        a++;
    if (a)
        memmove(w, w + a, strlen(w + a) + 1);
    /* U+2019 at the end (dogs') stays; a typographic quote around goes */
    n = strlen(w);
    if (n >= 3 && !strcmp(w + n - 3, "\342\200\235"))
        w[n - 3] = 0;
    if (!strncmp(w, "\342\200\234", 3))
        memmove(w, w + 3, strlen(w + 3) + 1);
}

/* A capital only because a sentence started: She -> she, unless the
 * word list has it as a proper noun (London) or knows only the
 * capital. Both (Bill, bill): the small one, ^K for the other. */
static void fix_case(char *w)
{
    char low[64], h[32], k[96];
    int i, has_cap, has_low;

    other_case[0] = 0;
    if (!(w[0] >= 'A' && w[0] <= 'Z'))
        goto other;
    for (i = 1; w[i]; i++)
        if (w[i] >= 'A' && w[i] <= 'Z')
            return;            /* NASA, McDonald: as it is */
    scopy(low, w, sizeof(low));
    low[0] = (char)low_ascii((unsigned char)low[0]);
    has_cap = home_of(w, h, sizeof(h), k, sizeof(k));
    has_low = home_of(low, h, sizeof(h), k, sizeof(k)) ||
        words_rank(low) >= 0;
    if (has_low) {
        if (has_cap)
            scopy(other_case, w, sizeof(other_case));
        scopy(w, low, 64);
    }
    return;
other:
    if (w[0] >= 'a' && w[0] <= 'z') {
        scopy(low, w, sizeof(low));
        low[0] = (char)up_ascii((unsigned char)low[0]);
        if (home_of(low, h, sizeof(h), k, sizeof(k)))
            scopy(other_case, low, sizeof(other_case));
    }
}

static void split_into(const char *s, char out[][GEN_TEXT], int *n, int max)
{
    const char *e;

    *n = 0;
    while (*s && *n < max) {
        while (*s == ' ')
            s++;
        if (!*s)
            break;
        e = s;
        while (*e && *e != ' ')
            e++;
        if (e - s < GEN_TEXT) {
            memcpy(out[*n], s, e - s);
            out[*n][e - s] = 0;
            (*n)++;
        }
        s = e;
    }
}

/* the dictionary an outline comes from: "(dotterel-old-base.dict)" ->
 * "old-base" */
static void dict_short(const char *says, char *out, int size)
{
    const char *a, *b;

    out[0] = 0;
    a = strrchr(says, '(');
    if (!a)
        return;
    a++;
    if (!strncmp(a, "dotterel-", 9))
        a += 9;
    b = strstr(a, ".dict");
    if (!b)
        b = strchr(a, ')');
    if (!b)
        b = a + strlen(a);
    if (b - a >= size)
        b = a + size - 1;
    memcpy(out, a, b - a);
    out[b - a] = 0;
}

static void look_up(void)
{
    char l[4][GEN_TEXT], o[TEXT_MAX * 2], says[TEXT_MAX];
    char *t;
    int i, n;

    clean_word(word);
    if (!word[0]) {
        looked = 0;
        return;
    }
    if (word[0] == '{') {
        char bare[TEXT_MAX];
        int i, j;

        /* an affix: the outlines of its letters */
        j = 0;
        for (i = 0; word[i] && j < TEXT_MAX - 1; i++)
            if (!strchr("{}^", word[i])) {
                bare[j] = word[i];
                j++;
            }
        bare[j] = 0;
        gen_word(bare, &g);
    } else {
        gen_word(word, &g);
    }
    looked = 1;
    if (!home_of(word, home, sizeof(home), kinds, sizeof(kinds))) {
        /* not in the list: a capital is a proper noun, {^...} an
         * affix, else the base */
        kinds[0] = 0;
        if (word[0] >= 'A' && word[0] <= 'Z')
            scopy(home, "proper-nouns", sizeof(home));
        else if (word[0] == '{' && strchr(word, '^'))
            scopy(home, "affixes", sizeof(home));
        else
            scopy(home, "base", sizeof(home));
    }
    if (!strcmp(kinds, "-"))
        kinds[0] = 0;
    n = gen_find("meanings.txt", word, l, 4);
    nmeaning = 0;
    for (i = 0; i < n && nmeaning < 3; i++) {
        t = strchr(l[i], '\t');
        if (!t)
            continue;
        *t = 0;
        sprintf(meaning[nmeaning], "%.12s %.200s", l[i], t + 1);
        t = strchr(meaning[nmeaning], '\t');
        if (t)
            *t = 0;            /* the base word after it */
        nmeaning++;
    }
    common = freq_place(word);
    gen_lapwing(word, o, sizeof(o));
    split_into(o, lap, &nlap, LIST_MAX);
    nhave = 0;
    if (dict_outlines(word, o, sizeof(o), LIST_MAX)) {
        split_into(o, have, &nhave, LIST_MAX);
        for (i = 0; i < nhave; i++) {
            outline_says(have[i], says, sizeof(says));
            dict_short(says, have_dict[i], sizeof(have_dict[i]));
        }
    }
    sel[0] = sel[1] = sel[2] = 0;
    list = l_made;
    asking_drop = 0;
}

/* --- strokes from the engine --------------------------------------------- */

static void capture(int on)
{
    const char *p;

    if (on && capture_fd < 0) {
        p = feed_path("outline");
        if (mkfifo(p, 0600) < 0 && errno != EEXIST)
            return;
        capture_fd = open(p, O_RDWR | O_NONBLOCK);
    } else if (!on && capture_fd >= 0) {
        close(capture_fd);
        capture_fd = -1;
    }
}

static void want_capture(void)
{
    capture(mode == m_outline && focused);
}

/* the last word written, from the engine; "" when it isn't running */
static void ask_last(char *out, int size)
{
    struct pollfd pf;
    const char *p;
    char b[TEXT_MAX];
    int fd, w, r, n;

    out[0] = 0;
    p = feed_path("lastword");
    if (mkfifo(p, 0600) < 0 && errno != EEXIST)
        return;
    fd = open(p, O_RDWR | O_NONBLOCK);
    if (fd < 0)
        return;
    w = open(feed_path("ask"), O_WRONLY | O_NONBLOCK);
    if (w < 0) {
        close(fd);
        return;
    }
    r = write(w, "last\n", 5);
    close(w);
    n = 0;
    pf.fd = fd;
    pf.events = POLLIN;
    while (r > 0 && n < (int)sizeof(b) - 1 && poll(&pf, 1, 300) > 0) {
        r = read(fd, b + n, sizeof(b) - 1 - n);
        if (r <= 0)
            break;
        n += r;
        if (memchr(b, '\n', n))
            break;
    }
    close(fd);
    b[n] = 0;
    b[strcspn(b, "\n")] = 0;
    scopy(out, b, size);
}

/* --- the screen -------------------------------------------------------- */

static void box(int y, int x, int w, int h, const char *title,
    const char *const *keys, int nkeys)
{
    int i;

    scr_border(y, x, w, 1, title, 0, 0);
    for (i = 1; i < h - 1; i++)
        scr_side(y + i, x, w);
    scr_border(y + h - 1, x, w, 0, 0, keys, nkeys);
}

/* text at y, x, at most w characters, in colour c */
static int put(int y, int x, int w, const char *c, const char *s)
{
    if (w <= 0)
        return 0;
    scr_move(y, x);
    scr_col(c);
    return scr_text(s, w);
}

static void put_sel(int y, int x, int w, int on, const char *c,
    const char *s)
{
    int n;

    scr_move(y, x);
    if (on)
        scr_col(C_SELECT);
    scr_col(c);
    n = scr_text(s, w);
    if (on)
        scr_fill(' ', w - n);
    scr_col(C_PLAIN);
}

static const char *home_file(void)
{
    static char f[64];

    if (!strcmp(home, "user"))
        return "user.dict";
    sprintf(f, "dotterel-%.40s.dict", home);
    return f;
}

static void draw_word(int y, int w)
{
    static const char *const kw[] = { "Enter look up", "Esc quit" };
    static const char *const ko[] = { "Esc word", "^K case",
        "^T dictionary", "Enter save" };
    char s[LINE];
    int x;

    box(y, 0, w, 4, "word", mode == m_word ? kw : ko, mode == m_word ?
        2 : 4);
    x = 2;
    x += put(y + 1, x, w - 4, mode == m_word ? C_TITLE : C_TEXT, "> ");
    x += put(y + 1, x, w - 4 - x, mode == m_word ? C_TITLE : C_TEXT,
        word);
    if (mode == m_word)
        put(y + 1, x, 1, C_BLUE, "_");
    if (!looked)
        return;
    x = 2;
    if (kinds[0]) {
        x += put(y + 2, x, w - 4, C_TEXT, kinds);
        x += put(y + 2, x, 4, C_TEXT, "    ");
    }
    x += put(y + 2, x, w - 2 - x, C_LABEL, "goes to ");
    x += put(y + 2, x, w - 2 - x, C_TEAL, home_file());
    if (common) {
        sprintf(s, "    common #%ld", common);
        x += put(y + 2, x, w - 2 - x, C_LABEL, s);
    }
    if (other_case[0]) {
        sprintf(s, "    ^K: %.40s", other_case);
        x += put(y + 2, x, w - 2 - x, C_DIM, s);
    }
}

static void draw_sound(int y, int w, int h)
{
    char s[LINE];
    int x, r, i;

    box(y, 0, w, h, "sound", 0, 0);
    if (!looked)
        return;
    r = y + 1;
    x = 2;
    x += put(r, x, 14, C_LABEL, "written       ");
    x += put(r, x, w - 2 - x, C_TEXT, g.written);
    sprintf(s, "    %d syllable%s", g.nsyl, g.nsyl == 1 ? "" : "s");
    x += put(r, x, w - 2 - x, C_LABEL, s);
    if (g.guessed)
        put(r, x, w - 2 - x, C_BROWN, "    sound guessed from the spelling");
    else if (g.uk_from_us)
        put(r, x, w - 2 - x, C_BROWN, "    UK made from the US");
    r++;
    x = 2;
    x += put(r, x, 14, C_LABEL, "UK  spoken    ");
    x += put(r, x, 30, C_TEAL, g.uk_ipa);
    x = 2 + 14 + 30;
    x += put(r, x, 22, C_MAGENTA, g.uk_ru);
    x = 2 + 14 + 30 + 22;
    put(r, x, w - 2 - x, C_DIM, g.spoken);
    r++;
    x = 2;
    if (g.us_ipa[0]) {
        x += put(r, x, 14, C_LABEL, "US  spoken    ");
        x += put(r, x, 30, C_TEAL, g.us_ipa);
        x = 2 + 14 + 30;
        put(r, x, 22, C_MAGENTA, g.us_ru);
    } else {
        put(r, x, w - 4, C_DIM, "US  not in the American list");
    }
    r++;
    for (i = 0; i < nmeaning && r < y + h - 1; i++, r++) {
        x = 2;
        x += put(r, x, 14, C_LABEL, i ? "" : "meaning");
        put(r, 16, w - 18, C_TEXT, meaning[i]);
    }
}

static const char *cand_colour(const struct gcand *c)
{
    if (c->same)
        return C_GREEN;
    if (c->free)
        return C_CYAN;
    return C_RED;
}

static void draw_lists(int y, int w, int h)
{
    static const char *const k0[] = { "^Y use" };
    static const char *const k1[] = { "Tab next" };
    static const char *const k2[] = { "^D remove" };
    char s[LINE];
    int a, b, c, i, r, top, n;

    a = w * 40 / 100;
    b = w * 25 / 100;
    c = w - a - b;
    scr_title_dim = list != l_made;
    box(y, 0, a, h, "made by the rules", k0, 1);
    scr_title_dim = list != l_lapwing;
    box(y, a, b, h, "lapwing", k1, 1);
    scr_title_dim = list != l_dict;
    box(y, a + b, c, h, "your dictionaries", k2, 1);
    scr_title_dim = 0;
    if (!looked)
        return;
    n = h - 2;
    /* made */
    top = sel[l_made] >= n ? sel[l_made] - n + 1 : 0;
    for (i = top, r = y + 1; i < g.nc && r < y + h - 1; i++, r++) {
        sprintf(s, "%-18.100s %.100s", g.c[i].outline,
            g.c[i].same ? "yours already" : g.c[i].free ? g.c[i].note :
            g.c[i].now);
        put_sel(r, 1, a - 2, list == l_made && i == sel[l_made],
            cand_colour(&g.c[i]), s);
    }
    if (!g.nc)
        put(y + 1, 2, a - 4, C_DIM, "nothing: no sound for it");
    /* lapwing */
    top = sel[l_lapwing] >= n ? sel[l_lapwing] - n + 1 : 0;
    for (i = top, r = y + 1; i < nlap && r < y + h - 1; i++, r++)
        put_sel(r, a + 1, b - 2, list == l_lapwing && i == sel[l_lapwing],
            C_TEXT, lap[i]);
    if (!nlap)
        put(y + 1, a + 2, b - 4, C_DIM, "not in Lapwing");
    /* yours */
    top = sel[l_dict] >= n ? sel[l_dict] - n + 1 : 0;
    for (i = top, r = y + 1; i < nhave && r < y + h - 1; i++, r++) {
        sprintf(s, "%-18.100s %.100s", have[i], have_dict[i]);
        put_sel(r, a + b + 1, c - 2, list == l_dict && i == sel[l_dict],
            strncmp(have_dict[i], "old-", 4) ? C_GREEN : C_TEXT, s);
    }
    if (!nhave)
        put(y + 1, a + b + 2, c - 4, C_DIM, "no outline yet");
}

static void draw_why(int y, int w, int h)
{
    char t[LINE];
    const struct gcand *c;
    int i, r;

    c = looked && list == l_made && sel[l_made] < g.nc ?
        &g.c[sel[l_made]] : 0;
    if (c)
        sprintf(t, "why %.200s", c->outline);
    else
        scopy(t, "why", sizeof(t));
    box(y, 0, w, h, t, 0, 0);
    if (!c)
        return;
    r = y + 1;
    for (i = 0; i < c->nwhy && r < y + h - 1; i++, r++)
        put(r, 2, w - 4, C_TEXT, c->why[i]);
    if (r < y + h - 1 && !c->free && !c->same) {
        sprintf(t, "taken: %.200s", c->now);
        put(r, 2, w - 4, C_BROWN, t);
    }
}

static void draw_outline(int y, int w)
{
    static const char *const ko[] = { "Enter save", "^W stroke back",
        "^U clear", "Up/Down choose", "Tab list" };
    char s[LINE], says[TEXT_MAX];
    long st[STROKE_MAX];
    int x, n;

    box(y, 0, w, 4, mode == m_outline ? (capture_fd >= 0 ?
        "outline: stroke it" : "outline: type it") : "outline", ko, 5);
    x = 2;
    x += put(y + 1, x, 2, mode == m_outline ? C_TITLE : C_DIM, "> ");
    x += put(y + 1, x, w - 4 - x, C_CYAN, outline);
    if (mode == m_outline)
        put(y + 1, x, 1, C_BLUE, "_");
    x = 2;
    if (asking_drop && list == l_dict && sel[l_dict] < nhave) {
        sprintf(s, "take %.100s off %.100s? y / n", have[sel[l_dict]],
            have_dict[sel[l_dict]]);
        put(y + 2, x, w - 4, C_RED, s);
        return;
    }
    if (msg[0]) {
        put(y + 2, x, w - 4, msg_bad ? C_RED : C_GREEN, msg);
        return;
    }
    if (!outline[0]) {
        if (mode == m_outline)
            put(y + 2, x, w - 4, C_DIM, "Enter with nothing here: "
                "the chosen outline");
        return;
    }
    n = outline_parse(outline, st, STROKE_MAX);
    if (n <= 0) {
        put(y + 2, x, w - 4, C_RED, "not an outline yet: keys in steno "
            "order #STKPWHRAO*EUFRPBLGTSDZ, strokes split by /");
        return;
    }
    outline_says(outline, says, sizeof(says));
    if (says[0] && !strstr(says, "(no entry")) {
        x += put(y + 2, x, 5, C_BROWN, "now: ");
        x += put(y + 2, x, w / 2, C_TEXT, says);
    } else {
        x += put(y + 2, x, w / 2, C_GREEN, "free");
    }
    sprintf(s, "   Enter saves it to %.200s", home_file());
    put(y + 2, x, w - 2 - x, C_LABEL, s);
}

static void draw(void)
{
    int rows, cols, lh, wh, sh;

    scr_size(&rows, &cols);
    scr_begin();
    sh = 2 + 3 + (nmeaning ? nmeaning : 1);
    if (sh > 8)
        sh = 8;
    wh = 6;
    lh = rows - 4 - sh - wh - 4;
    if (lh < 4) {
        wh = 0;
        lh = rows - 4 - sh - 4;
    }
    if (lh < 3)
        lh = 3;
    draw_word(0, cols);
    draw_sound(4, cols, sh);
    draw_lists(4 + sh, cols, lh);
    if (wh)
        draw_why(4 + sh + lh, cols, wh);
    draw_outline(4 + sh + lh + wh, cols);
    scr_end();
}

/* --- keys -------------------------------------------------------------- */

static void back_stroke(void)
{
    char *s;

    s = strrchr(outline, '/');
    if (s)
        *s = 0;
    else
        outline[0] = 0;
}

static void add_stroke_text(const char *s)
{
    if (outline[0])
        scat(outline, "/", sizeof(outline));
    scat(outline, s, sizeof(outline));
}

static int list_len(int l)
{
    return l == l_made ? g.nc : l == l_lapwing ? nlap : nhave;
}

static const char *selected(void)
{
    if (!looked || sel[list] >= list_len(list))
        return 0;
    if (list == l_made)
        return g.c[sel[list]].outline;
    if (list == l_lapwing)
        return lap[sel[list]];
    return have[sel[list]];
}

static void move_sel(int d)
{
    int n;

    n = list_len(list);
    sel[list] += d;
    if (sel[list] >= n)
        sel[list] = n - 1;
    if (sel[list] < 0)
        sel[list] = 0;
}

static void next_list(int d)
{
    list = (list + 3 + d) % 3;
}

static void to_outline(void)
{
    look_up();
    if (!looked)
        return;
    mode = m_outline;
    msg[0] = 0;
    want_capture();
}

/* 1: saved, so quit */
static int save(void)
{
    const char *o, *path;
    char m[LINE];
    long st[STROKE_MAX];

    o = outline[0] ? outline : selected();
    if (!o) {
        say_msg("nothing to save: stroke an outline", 1);
        return 0;
    }
    if (outline_parse(o, st, STROKE_MAX) <= 0) {
        say_msg("that is not an outline", 1);
        return 0;
    }
    if (!strcmp(home, "user"))
        path = user_dict;
    else
        path = dict_dir_file(home_file());
    if (word_save(path, o, word) < 0) {
        sprintf(m, "could not write %.200s", path);
        say_msg(m, 1);
        return 0;
    }
    return 1;
}

static void cycle_home(void)
{
    int i;

    for (i = 0; homes[i]; i++)
        if (!strcmp(homes[i], home))
            break;
    i = homes[i] && homes[i + 1] ? i + 1 : 0;
    scopy(home, homes[i], sizeof(home));
}

/* a key in the outline field; 1 when saved (quit) */
static int outline_key(int c)
{
    const char *s;

    msg[0] = 0;
    if (asking_drop) {
        asking_drop = 0;
        if ((c == 'y' || c == 'Y') && list == l_dict &&
            sel[l_dict] < nhave)
        {
            char f[96];

            sprintf(f, "dotterel-%.60s.dict", have_dict[sel[l_dict]]);
            if (word_drop(!strcmp(have_dict[sel[l_dict]], "user") ?
                user_dict : dict_dir_file(f), have[sel[l_dict]]) < 0)
            {
                say_msg("could not take it off", 1);
            } else {
                look_up();
                list = l_dict;
                say_msg("taken off", 0);
            }
        }
        return 0;
    }
    switch (c) {
    case '\r':
    case '\n':
        return save();
    case 27:
        mode = m_word;
        want_capture();
        return 0;
    case 127:
    case 8:
        if (outline[0])
            outline[strlen(outline) - 1] = 0;
        return 0;
    case 23:                   /* ^W */
        back_stroke();
        return 0;
    case 21:                   /* ^U */
        outline[0] = 0;
        return 0;
    case 25:                   /* ^Y */
        s = selected();
        if (s)
            scopy(outline, s, sizeof(outline));
        return 0;
    case 20:                   /* ^T */
        cycle_home();
        return 0;
    case 11:                   /* ^K */
        if (other_case[0]) {
            char t[64];

            scopy(t, word, sizeof(t));
            scopy(word, other_case, sizeof(word));
            scopy(other_case, t, sizeof(other_case));
            look_up();
        }
        return 0;
    case 4:                    /* ^D */
        if (list == l_dict && sel[l_dict] < nhave)
            asking_drop = 1;
        else
            say_msg("^D takes an outline off: choose one in your "
                "dictionaries (Tab)", 1);
        return 0;
    case '\t':
        next_list(1);
        return 0;
    case 14:                   /* ^N */
        move_sel(1);
        return 0;
    case 16:                   /* ^P */
        move_sel(-1);
        return 0;
    }
    if (c >= 'a' && c <= 'z')
        c -= 32;
    if ((c >= 'A' && c <= 'Z') || strchr("#*/-0123456789", c)) {
        if (strlen(outline) < sizeof(outline) - 2) {
            int n;

            n = strlen(outline);
            outline[n] = (char)c;
            outline[n + 1] = 0;
        }
    }
    return 0;
}

static void word_key(int c)
{
    int n;

    n = strlen(word);
    if (c == '\r' || c == '\n') {
        to_outline();
    } else if (c == 27) {
        stop_now = 1;
    } else if (c == 127 || c == 8) {
        while (n > 0 && ((unsigned char)word[n - 1] & 0xc0) == 0x80)
            n--;
        if (n > 0)
            n--;
        word[n] = 0;
        looked = 0;
    } else if (c == 21) {
        word[0] = 0;
        looked = 0;
    } else if (c == '\t' && looked) {
        mode = m_outline;
        want_capture();
    } else if (c >= 32 && n < (int)sizeof(word) - 1) {
        word[n] = (char)c;
        word[n + 1] = 0;
        looked = 0;
    }
}

/* the keys read at once; 1: quit */
static int keys(const unsigned char *k, int n)
{
    int i, c;

    for (i = 0; i < n; i++) {
        c = k[i];
        if (c == 27 && i + 2 < n && (k[i + 1] == '[' || k[i + 1] == 'O')) {
            c = k[i + 2];
            i += 2;
            /* focus in and out (st, xterm: mode 1004) */
            if (c == 'I' || c == 'O') {
                focused = c == 'I';
                want_capture();
                continue;
            }
            if (c == 'Z') {            /* Shift+Tab */
                if (mode == m_outline)
                    next_list(-1);
                continue;
            }
            if (mode != m_outline)
                continue;
            if (c == 'A')
                move_sel(-1);
            else if (c == 'B')
                move_sel(1);
            else if (c == 'C')
                next_list(1);
            else if (c == 'D')
                next_list(-1);
            while (i + 1 < n && ((k[i + 1] >= '0' && k[i + 1] <= '9') ||
                k[i + 1] == ';' || k[i + 1] == '~'))
            {
                i++;               /* the rest of ESC [ 5 ~ and such */
            }
            continue;
        }
        if (c == 3)
            return 1;              /* ^C */
        if (mode == m_word) {
            word_key(c);
            if (stop_now)
                return 1;
        } else if (outline_key(c)) {
            return 1;
        }
    }
    return 0;
}

/* a stroke from the engine: STROKE <tab> what it writes alone */
static int stroke_in(char *line)
{
    char *t;

    t = strchr(line, '\t');
    if (t) {
        *t = 0;
        t++;
    } else {
        t = "";
    }
    if (mode != m_outline)
        return 0;
    msg[0] = 0;
    /* the strokes that move you around do so here too */
    if (strstr(t, "{#Return}") || strstr(t, "\\n"))
        return outline_key('\r');
    if (strstr(t, "{#Escape}"))
        return outline_key(27);
    if (strstr(t, "{#BackSpace}") || !strcmp(t, "=undo") ||
        !strcmp(line, "*"))
    {
        back_stroke();
        return 0;
    }
    if (strstr(t, "{#Tab}")) {
        next_list(1);
        return 0;
    }
    if (strstr(t, "{#Down}")) {
        move_sel(1);
        return 0;
    }
    if (strstr(t, "{#Up}")) {
        move_sel(-1);
        return 0;
    }
    add_stroke_text(line);
    return 0;
}

static int read_strokes(void)
{
    static char got[4096];
    static int ngot;
    char *nl;
    int r;

    r = read(capture_fd, got + ngot, sizeof(got) - 1 - ngot);
    if (r <= 0)
        return 0;
    ngot += r;
    got[ngot] = 0;
    while ((nl = strchr(got, '\n')) != 0) {
        *nl = 0;
        r = stroke_in(got);
        ngot -= (int)(nl + 1 - got);
        memmove(got, nl + 1, ngot + 1);
        if (r)
            return 1;
    }
    if (ngot >= (int)sizeof(got) - 1)
        ngot = 0;
    return 0;
}

static void on_resize(int sig)
{
    (void)sig;
    resized = 1;
}

static void on_stop(int sig)
{
    (void)sig;
    stop_now = 1;
}

static void restore(void)
{
    capture(0);
    scr_put("\033[?1004l");
    scr_write("\033[?1004l", 8);
    scr_close();
    if (tty)
        tcsetattr(0, TCSANOW, &old_tty);
}

int word_main(const char *cfg)
{
    struct pollfd pf[2];
    struct termios raw;
    struct sigaction sa;
    unsigned char k[256];
    int r, n, quit;

    (void)cfg;
    if (!isatty(0) || !isatty(1)) {
        say("steno -W needs a terminal", 0);
        return 1;
    }
    tcgetattr(0, &old_tty);
    raw = old_tty;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &raw);
    tty = 1;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_resize;
    sigaction(SIGWINCH, &sa, 0);
    sa.sa_handler = on_stop;
    sigaction(SIGTERM, &sa, 0);
    sigaction(SIGHUP, &sa, 0);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, 0);
    scr_colour = !getenv("NO_COLOR") || !getenv("NO_COLOR")[0];
    scr_open();
    scr_write("\033[?1004h", 8);
    /* the last word you wrote, looked up: stroke its outline now, or
     * Esc to type another word */
    ask_last(word, sizeof(word));
    clean_word(word);
    fix_case(word);
    mode = m_word;
    if (word[0]) {
        look_up();
        mode = m_outline;
    }
    want_capture();
    draw();
    quit = 0;
    while (!stop_now && !quit) {
        n = 0;
        pf[n].fd = 0;
        pf[n].events = POLLIN;
        n++;
        if (capture_fd >= 0) {
            pf[n].fd = capture_fd;
            pf[n].events = POLLIN;
            n++;
        }
        r = poll(pf, n, -1);
        if (r < 0 && errno != EINTR)
            break;
        if (r > 0 && pf[0].revents) {
            r = read(0, k, sizeof(k));
            if (r <= 0)
                break;
            quit = keys(k, r);
        }
        if (!quit && n > 1 && capture_fd >= 0 && pf[1].revents & POLLIN)
            quit = read_strokes();
        resized = 0;
        if (!quit)
            draw();
    }
    restore();
    return 0;
}
