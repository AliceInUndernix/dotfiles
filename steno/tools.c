/* The terminal tools around the engine: the paper tape and
 * suggestions (steno -T), adding a stroke (steno -a, -A) and looking
 * one up (steno -l).
 *
 * The engine writes one line a stroke to a FIFO, and only while
 * steno -T has it open, so nothing is kept anywhere:
 *
 *   STROKE <tab> TRANSLATION [<tab> WORDS <tab> OUTLINES]...
 *
 * with tabs, newlines and backslashes in the text written as \t, \n
 * and \\. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include "steno.h"

#define TAPE_LINE 4000         /* under PIPE_BUF: one write, whole */
#define SUGGEST_WORDS 4
#define SUGGEST_MAX 6

char user_dict[1024];          /* where -a writes ("user" setting) */

static int tape_fd = -1;

/* $XDG_STATE_HOME/steno/tape, or ~/.local/state/steno/tape; the
 * directories are made (0700) when missing */
static const char *tape_path(void)
{
    static char p[1024];
    const char *x;
    char *s;

    x = getenv("XDG_STATE_HOME");
    if (x && x[0] == '/')
        scopy(p, x, sizeof(p) - 16);
    else
        scopy(p, config_path("~/.local/state"), sizeof(p) - 16);
    scat(p, "/steno", sizeof(p));
    for (s = p + 1; *s; s++) {
        if (*s == '/') {
            *s = 0;
            mkdir(p, 0700);
            *s = '/';
        }
    }
    mkdir(p, 0700);
    scat(p, "/tape", sizeof(p));
    return p;
}

static void put_escaped(char *out, int size, const char *s)
{
    int n;

    n = strlen(out);
    for (; *s && n < size - 3; s++) {
        if (*s == '\t' || *s == '\n' || *s == '\\') {
            out[n] = '\\';
            n++;
            out[n] = *s == '\t' ? 't' : *s == '\n' ? 'n' : '\\';
        } else if ((unsigned char)*s < 32) {
            out[n] = '?';
        } else {
            out[n] = *s;
        }
        n++;
    }
    out[n] = 0;
}

static void outline_text(const long *st, int n, char *out, int size)
{
    char s[32];
    int i;

    out[0] = 0;
    for (i = 0; i < n; i++) {
        stroke_text(st[i], s);
        if (i)
            scat(out, "/", size);
        scat(out, s, size);
    }
}

static void trim(char *s)
{
    int a, n;

    n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
        s[n - 1] == '\n'))
    {
        n--;
    }
    s[n] = 0;
    a = 0;
    while (s[a] == ' ' || s[a] == '\t' || s[a] == '\n')
        a++;
    if (a)
        memmove(s, s + a, n - a + 1);
}

/* the last few words and how else they can be written */
static void suggestions(char *line, int size)
{
    static char last_key[TEXT_MAX * 2], last_out[TAPE_LINE];
    char key[TEXT_MAX * 2], w[TEXT_MAX], seen[TEXT_MAX];
    char o[TEXT_MAX], out[TAPE_LINE];
    int i;

    key[0] = 0;
    if (!last_words(SUGGEST_WORDS, key, TEXT_MAX))
        return;
    if (strcmp(key, last_key)) {
        scopy(last_key, key, sizeof(last_key));
        out[0] = 0;
        seen[0] = 0;
        for (i = SUGGEST_WORDS; i >= 1; i--) {
            if (!last_words(i, w, sizeof(w)))
                continue;
            trim(w);
            if (!w[0] || !strcmp(w, seen))
                continue;
            scopy(seen, w, sizeof(seen));
            if (!dict_outlines(w, o, sizeof(o), SUGGEST_MAX))
                continue;
            scat(out, "\t", sizeof(out));
            put_escaped(out, sizeof(out) - TEXT_MAX, w);
            scat(out, "\t", sizeof(out));
            scat(out, o, sizeof(out));
        }
        scopy(last_out, out, sizeof(last_out));
    }
    scat(line, last_out, size);
}

/* after each stroke; top is the newest translation before it */
void tape_stroke(long st, struct trans *top)
{
    char line[TAPE_LINE + 64], s[STROKE_MAX * 32];
    struct trans *t;
    int n, r;

    if (tape_fd < 0) {
        tape_fd = open(tape_path(), O_WRONLY | O_NONBLOCK);
        if (tape_fd < 0)
            return;            /* no steno -T: nothing to do */
        fcntl(tape_fd, F_SETFD, FD_CLOEXEC);
    }
    stroke_text(st, line);
    scat(line, "\t", sizeof(line));
    n = history_count();
    t = n ? history_at(n - 1) : 0;
    if (t && t != top && t->nst && t->st[t->nst - 1] == st) {
        if (t->has_english) {
            put_escaped(line, TAPE_LINE, t->english);
        } else {
            outline_text(t->st, t->nst, s, sizeof(s));
            put_escaped(line, TAPE_LINE, s);
        }
    } else {
        scat(line, "*", sizeof(line));
    }
    if (output_on)
        suggestions(line, TAPE_LINE);
    scat(line, "\n", sizeof(line));
    r = write(tape_fd, line, strlen(line));
    if (r < 0 && errno != EAGAIN) {
        close(tape_fd);        /* steno -T went away */
        tape_fd = -1;
    }
}

/* --- steno -T ------------------------------------------------------ */

/* Habamax, as 24-bit colours */
#define C_TEXT "\033[38;2;199;199;199m"
#define C_TITLE "\033[1;38;2;238;238;238m"
#define C_LABEL "\033[38;2;158;158;158m"
#define C_DIM "\033[38;2;118;118;118m"
#define C_BLUE "\033[38;2;95;135;175m"
#define C_TEAL "\033[38;2;135;175;175m"
#define C_RESET "\033[0m"

static int colour;

static const char *col(const char *c)
{
    return colour ? c : "";
}

static const char tape_keys[] = "#STKPWHRAO*EUFRPBLGTSDZ";

static void show_stroke(const char *s)
{
    char p[NUM_KEYS + 1];
    long st;
    int i;

    st = stroke_parse(s, strlen(s));
    if (st < 0) {
        printf("%s%-23s", col(C_TEXT), s);
        return;
    }
    for (i = 0; i < NUM_KEYS; i++)
        p[i] = st & (1L << i) ? tape_keys[i] : ' ';
    p[NUM_KEYS] = 0;
    printf("%s%s", col(C_TEXT), p);
}

static void show_line(char *line)
{
    static char last_sugg[TAPE_LINE];
    char *f[2 + 2 * SUGGEST_WORDS], *p;
    int n, i;

    n = 0;
    p = line;
    f[n] = p;
    n++;
    while (n < 2 + 2 * SUGGEST_WORDS && (p = strchr(p, '\t')) != 0) {
        *p = 0;
        p++;
        f[n] = p;
        n++;
    }
    show_stroke(f[0]);
    printf("  %s%s%s\n", col(C_TEAL), n > 1 ? f[1] : "", col(C_RESET));
    if (n < 4)
        return;
    /* the suggestions only when they change */
    p = f[2];
    if (!strcmp(p, last_sugg))
        return;
    scopy(last_sugg, p, sizeof(last_sugg));
    for (i = 2; i + 1 < n; i += 2)
        printf("%*s%s%s  %s%s%s\n", NUM_KEYS + 2, "", col(C_LABEL), f[i],
            col(C_BLUE), f[i + 1], col(C_RESET));
}

int tape_view(void)
{
    const char *path;
    struct pollfd pf[2];
    struct termios old, raw;
    char buf[TAPE_LINE * 2], *nl;
    int fd, len, n, r, tty;
    unsigned char c;

    path = tape_path();
    if (mkfifo(path, 0600) < 0 && errno != EEXIST) {
        say("can't make %s", path);
        return 1;
    }
    /* read and write: the FIFO stays open when the engine restarts */
    fd = open(path, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        say("can't open %s", path);
        return 1;
    }
    colour = isatty(1) && (!getenv("NO_COLOR") || !getenv("NO_COLOR")[0]);
    tty = isatty(0) && tcgetattr(0, &old) == 0;
    if (tty) {
        raw = old;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &raw);
    }
    printf("%s%-23s  %s%s  %sq%s quit  %sc%s clear%s\n", col(C_TITLE),
        tape_keys, "translation", col(C_DIM), col(C_BLUE), col(C_DIM),
        col(C_BLUE), col(C_DIM), col(C_RESET));
    fflush(stdout);
    len = 0;
    for (;;) {
        pf[0].fd = fd;
        pf[0].events = POLLIN;
        pf[1].fd = 0;
        pf[1].events = POLLIN;
        r = poll(pf, tty ? 2 : 1, -1);
        if (r < 0 && errno == EINTR)
            continue;
        if (r < 0)
            break;
        if (tty && pf[1].revents) {
            c = 0;
            n = read(0, &c, 1);
            if (n <= 0 || c == 'q' || c == 4)
                break;
            if (c == 'c' || c == 12) {
                printf("\033[H\033[2J");
                fflush(stdout);
            }
        }
        if (!(pf[0].revents & POLLIN))
            continue;
        n = read(fd, buf + len, sizeof(buf) - 1 - len);
        if (n <= 0)
            continue;
        len += n;
        buf[len] = 0;
        while ((nl = strchr(buf, '\n')) != 0) {
            *nl = 0;
            show_line(buf);
            n = (int)(nl + 1 - buf);
            memmove(buf, nl + 1, len - n + 1);
            len -= n;
        }
        if (len == (int)sizeof(buf) - 1)
            len = 0;           /* no newline in a full buffer: drop */
        fflush(stdout);
    }
    if (tty)
        tcsetattr(0, TCSANOW, &old);
    return 0;
}

/* --- steno -l -------------------------------------------------------- */

static int lookup_outline(const char *s, char *norm, int size)
{
    long st[STROKE_MAX];
    int n;

    n = outline_parse(s, st, STROKE_MAX);
    if (n <= 0)
        return 0;
    outline_text(st, n, norm, size);
    return n;
}

int tool_lookup(const char *s)
{
    char out[TEXT_MAX * 8], norm[STROKE_MAX * 32];
    long st[STROKE_MAX];
    int n, found;

    found = 0;
    n = outline_parse(s, st, STROKE_MAX);
    if (n > 0 && dict_explain(st, n, out, sizeof(out))) {
        outline_text(st, n, norm, sizeof(norm));
        printf("%s gives\n%s", norm, out);
        found = 1;
    }
    if (dict_outlines(s, out, sizeof(out), 50)) {
        printf("%s is written %s\n", s, out);
        found = 1;
    }
    if (!found)
        printf("%s: not in the dictionaries\n", s);
    return found ? 0 : 1;
}

/* --- steno -a, -A ---------------------------------------------------- */

static void json_quote(const char *s, char *out, int size)
{
    static const char hex[] = "0123456789abcdef";
    int n;

    n = 0;
    out[n] = '"';
    n++;
    for (; *s && n < size - 8; s++) {
        if (*s == '"' || *s == '\\') {
            out[n] = '\\';
            n++;
            out[n] = *s;
        } else if (*s == '\n') {
            out[n] = '\\';
            n++;
            out[n] = 'n';
        } else if (*s == '\t') {
            out[n] = '\\';
            n++;
            out[n] = 't';
        } else if ((unsigned char)*s < 32) {
            out[n] = '\\';
            out[n + 1] = 'u';
            out[n + 2] = '0';
            out[n + 3] = '0';
            out[n + 4] = hex[(*s >> 4) & 15];
            out[n + 5] = hex[*s & 15];
            n += 5;
        } else {
            out[n] = *s;
        }
        n++;
    }
    out[n] = '"';
    out[n + 1] = 0;
}

/* Put "outline": "text" in the file, the way Plover writes it: one
 * entry a line. An outline already there gets the new text. */
static int write_entry(const char *path, const char *outline,
    const char *text)
{
    char *f, *o, *p, *line, *end, *close_at, k[STROKE_MAX * 32 + 8];
    char v[TEXT_MAX * 6 + 8], tmp[1100];
    long size, need;
    int fd, r, w;

    f = read_file(path, &size);
    if (!f) {
        f = malloc(8);
        if (!f)
            return -1;
        strcpy(f, "{\n}\n");
        size = 4;
    }
    json_quote(outline, k, sizeof(k));
    json_quote(text, v, sizeof(v));
    need = size + strlen(k) + strlen(v) + 16;
    o = malloc(need);
    if (!o) {
        free(f);
        return -1;
    }
    /* the outline's line, if it is there */
    line = 0;
    for (p = f; *p; p = end) {
        end = strchr(p, '\n');
        end = end ? end + 1 : p + strlen(p);
        while (*p == ' ' || *p == '\t')
            p++;
        if (!strncmp(p, k, strlen(k)) && p[strlen(k)] == ':') {
            line = p;
            break;
        }
    }
    if (line) {
        p = end;
        while (p > line && (p[-1] == '\n' || p[-1] == '\r'))
            p--;
        w = p > line && p[-1] == ',';
        sprintf(o, "%.*s%s: %s%s%s", (int)(line - f), f, k, v,
            w ? "," : "", p);
    } else {
        close_at = strrchr(f, '}');
        if (!close_at) {
            say("not a dictionary: %s", path);
            free(f);
            free(o);
            return -1;
        }
        p = close_at;
        while (p > f && (p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' ||
            p[-1] == '\r'))
        {
            p--;
        }
        if (p > f && p[-1] == '{')
            sprintf(o, "%.*s\n%s: %s\n%s", (int)(p - f), f, k, v, close_at);
        else
            sprintf(o, "%.*s,\n%s: %s%s", (int)(p - f), f, k, v, p);
    }
    free(f);
    /* a new file, then rename: never half a dictionary */
    sprintf(tmp, "%.1000s.new", path);
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        say("can't write %s", tmp);
        free(o);
        return -1;
    }
    size = strlen(o);
    r = write(fd, o, size);
    free(o);
    w = close(fd);
    if (r != size || w < 0) {
        say("can't write %s", tmp);
        unlink(tmp);
        return -1;
    }
    r = rename(tmp, path);
    if (r < 0) {
        say("can't replace %s", path);
        unlink(tmp);
        return -1;
    }
    return 0;
}

static const char *target(void)
{
    const char *p;
    int i;

    if (user_dict[0])
        return user_dict;
    for (i = 0; i < dict_count(); i++) {
        p = dict_path(i);
        if (strlen(p) > 5 && !strcmp(p + strlen(p) - 5, ".json"))
            return p;
    }
    return 0;
}

static void show_conflicts(const char *norm, const char *text)
{
    char out[TEXT_MAX * 8];
    long st[STROKE_MAX];
    int n;

    n = norm ? outline_parse(norm, st, STROKE_MAX) : 0;
    if (n > 0 && dict_explain(st, n, out, sizeof(out)))
        printf("%s gives now\n%s", norm, out);
    if (text && dict_outlines(text, out, sizeof(out), 20))
        printf("%s is written now %s\n", text, out);
}

int tool_add(const char *outline, const char *text)
{
    char norm[STROKE_MAX * 32];
    const char *t;

    t = target();
    if (!t) {
        say("no .json dictionary to write to", 0);
        return 1;
    }
    if (!lookup_outline(outline, norm, sizeof(norm))) {
        say("not an outline: %s", outline);
        return 1;
    }
    show_conflicts(norm, text);
    if (write_entry(t, norm, text) < 0)
        return 1;
    printf("%s: %s -> %s\n", t, norm, text);
    return 0;
}

static int ask_line(const char *prompt, char *s, int size)
{
    int n;

    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(s, size, stdin))
        return -1;
    n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        n--;
    s[n] = 0;
    return n;
}

int tool_add_loop(void)
{
    char o[1024], norm[STROKE_MAX * 32], text[TEXT_MAX], a[16];
    const char *t;

    t = target();
    if (!t) {
        say("no .json dictionary to write to", 0);
        return 1;
    }
    printf("adding to %s (empty outline: done)\n", t);
    for (;;) {
        if (ask_line("outline: ", o, sizeof(o)) <= 0)
            break;
        trim(o);
        if (!lookup_outline(o, norm, sizeof(norm))) {
            printf("  not an outline\n");
            continue;
        }
        show_conflicts(norm, 0);
        if (ask_line("translation: ", text, sizeof(text)) <= 0)
            continue;
        show_conflicts(0, text);
        if (ask_line("add it? [y/N] ", a, sizeof(a)) < 0)
            break;
        if (a[0] != 'y' && a[0] != 'Y')
            continue;
        if (write_entry(t, norm, text) == 0)
            printf("  added %s -> %s\n", norm, text);
    }
    return 0;
}
