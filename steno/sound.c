/* steno -W's ^F and ^O: how a word sounds in the Cambridge Dictionary,
 * British (^F) or American (^O), played with mpv.
 *
 * Every fetch goes through Tor (proxychains, from net/), never
 * straight: each one tells Cambridge a word you are adding. Without
 * proxychains nothing is fetched. The page's two sound links and each
 * mp3 are kept in $XDG_CACHE_HOME/steno/sound, so a word is fetched
 * once; delete the directory to forget them.
 *
 * It all runs in a child process, so the screen never waits: the
 * child's exit status says how it went (sound_said). */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "steno.h"

#define SOUND_PAGE "https://dictionary.cambridge.org/dictionary/english/"
#define SOUND_SITE "https://dictionary.cambridge.org"
#define SOUND_AGENT "Mozilla/5.0 (X11; Linux x86_64; rv:128.0) " \
    "Gecko/20100101 Firefox/128.0"
#define PAGE_MAX "4000000"
#define MP3_MAX "2000000"
#define LINK_MAX 512
#define NAME_MAX_ 128

enum {
    snd_played = 0, snd_none = 2, snd_net = 3, snd_noproxy = 4,
    snd_noplayer = 5, snd_nodir = 6
};

static char dir[1024];

/* $XDG_CACHE_HOME/steno/sound (or ~/.cache/...), made; 0 when not */
static int sound_dir(void)
{
    const char *x;
    char *s;

    x = getenv("XDG_CACHE_HOME");
    if (x && x[0] == '/') {
        scopy(dir, x, 900);
    } else {
        x = getenv("HOME");
        if (!x || x[0] != '/')
            return 0;
        scopy(dir, x, 900);
        scat(dir, "/.cache", sizeof(dir));
    }
    scat(dir, "/steno/sound", sizeof(dir));
    for (s = dir + 1; *s; s++) {
        if (*s == '/') {
            *s = 0;
            mkdir(dir, 0700);
            *s = '/';
        }
    }
    mkdir(dir, 0700);
    return access(dir, W_OK) == 0;
}

/* the word as a file name: a-z 0-9, the rest _ */
static void file_word(const char *w, char *out, int size)
{
    int i, n;
    char c;

    n = 0;
    for (i = 0; w[i] && n < size - 1; i++) {
        c = w[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
            c = '_';
        out[n] = c;
        n++;
    }
    out[n] = 0;
}

/* the word as Cambridge's page names it: small letters, a space or an
 * apostrophe a -, full stops and braces gone (Mr.{-|}: mr, e.g.: eg,
 * don't: don-t), any other byte %XX */
static void url_word(const char *w, char *out, int size)
{
    static const char hex[] = "0123456789ABCDEF";
    const unsigned char *p;
    int n;
    unsigned char c;

    n = 0;
    out[0] = 0;
    for (p = (const unsigned char *)w; *p && n < size - 4; p++) {
        c = *p;
        if (c == '{') {
            while (p[1] && p[1] != '}')
                p++;
            continue;
        }
        if (c == '}' || c == '.')
            continue;
        if (c == 0xe2 && p[1] == 0x80 && p[2] == 0x99) {
            p += 2;            /* U+2019, the apostrophe */
            c = '\'';
        }
        if (c >= 'A' && c <= 'Z')
            c = (unsigned char)(c + 32);
        if (c == ' ' || c == '\'')
            c = '-';
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
            out[n] = (char)c;
            n++;
        } else {
            out[n] = '%';
            out[n + 1] = hex[c >> 4];
            out[n + 2] = hex[c & 15];
            n += 3;
        }
    }
    out[n] = 0;
}

/* run a program, its output into out (0: nowhere); its exit status,
 * 127 when it isn't there */
static int run(char *const argv[], const char *out)
{
    int pid, st, fd, r;

    pid = fork();
    if (pid < 0)
        return -1;
    if (!pid) {
        fd = open("/dev/null", O_RDWR);
        if (fd >= 0) {
            dup2(fd, 0);
            dup2(fd, 2);
            dup2(fd, 1);
        }
        if (out) {
            fd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd >= 0)
                dup2(fd, 1);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    do {
        r = waitpid(pid, &st, 0);
    } while (r < 0 && errno == EINTR);
    if (r < 0 || !WIFEXITED(st))
        return -1;
    return WEXITSTATUS(st);
}

/* fetch url into file through Tor, at most max bytes; 0 ok */
static int fetch(const char *url, const char *file, const char *max)
{
    char tmp[1200];
    char *argv[16];
    int r;

    sprintf(tmp, "%.1100s.part", file);
    argv[0] = "proxychains";
    argv[1] = "-q";
    argv[2] = "curl";
    argv[3] = "-fsL";
    argv[4] = "--proto";
    argv[5] = "=https";
    argv[6] = "--max-time";
    argv[7] = "40";
    argv[8] = "--max-filesize";
    argv[9] = (char *)max;
    argv[10] = "-A";
    argv[11] = SOUND_AGENT;
    argv[12] = "-o";
    argv[13] = tmp;
    argv[14] = (char *)url;
    argv[15] = 0;
    r = run(argv, 0);
    if (r == 127)
        return snd_noproxy;
    if (r) {
        unlink(tmp);
        return snd_net;
    }
    r = rename(tmp, file);
    if (r < 0) {
        unlink(tmp);
        return snd_net;
    }
    return 0;
}

/* the first sound link of one kind in the page:
 * /media/english/uk_pron/u/ukc/ukcat/ukcatty017.mp3 */
static void find_link(const char *page, const char *kind, char *out)
{
    char key[64];
    const char *a, *b;

    out[0] = 0;
    sprintf(key, "/media/english/%s/", kind);
    a = strstr(page, key);
    if (!a)
        return;
    for (b = a; *b && *b != '"' && *b != '\'' && *b != ' ' &&
        *b != '<' && *b != '>' && b - a < LINK_MAX - 1; b++)
        ;
    if (b - a < 5 || strncmp(b - 4, ".mp3", 4))
        return;
    memcpy(out, a, b - a);
    out[b - a] = 0;
}

/* a whole small file, as text; 0 when it can't be read */
static char *slurp(const char *file, long max)
{
    char *buf;
    long n;
    int fd, r;

    fd = open(file, O_RDONLY);
    if (fd < 0)
        return 0;
    buf = malloc(max + 1);
    if (!buf) {
        close(fd);
        return 0;
    }
    n = 0;
    while (n < max) {
        r = read(fd, buf + n, max - n);
        if (r <= 0)
            break;
        n += r;
    }
    close(fd);
    buf[n] = 0;
    return buf;
}

/* the links file, WORD.links: "uk LINK" and "us LINK" lines ("-" for
 * none); 1 when it was there */
static int read_links(const char *file, char *uk, char *us)
{
    char *t, *p, *nl, *link;

    uk[0] = us[0] = 0;
    t = slurp(file, 4 * LINK_MAX);
    if (!t)
        return 0;
    for (p = t; *p; p = nl + 1) {
        nl = strchr(p, '\n');
        if (nl)
            *nl = 0;
        link = strchr(p, ' ');
        if (link && strcmp(link + 1, "-") &&
            strlen(link + 1) < LINK_MAX)
        {
            if (!strncmp(p, "uk ", 3))
                strcpy(uk, link + 1);
            else if (!strncmp(p, "us ", 3))
                strcpy(us, link + 1);
        }
        if (!nl)
            break;
    }
    free(t);
    return 1;
}

static void write_links(const char *file, const char *uk, const char *us)
{
    FILE *f;

    f = fopen(file, "w");
    if (!f)
        return;
    fprintf(f, "uk %s\nus %s\n", uk[0] ? uk : "-", us[0] ? us : "-");
    fclose(f);
}

/* the child's work: the link, the mp3, mpv; its exit status */
static int sound_child(const char *word, int us)
{
    char name[NAME_MAX_], links[1200], page[1200], mp3[1200];
    char url[1200], uw[NAME_MAX_ * 3], uk_link[LINK_MAX];
    char us_link[LINK_MAX];
    char *text, *link;
    char *argv[8];
    struct stat sb;
    int r;

    if (!sound_dir())
        return snd_nodir;
    file_word(word, name, sizeof(name));
    url_word(word, uw, sizeof(uw));
    if (!name[0] || !uw[0])
        return snd_none;
    sprintf(mp3, "%.1000s/%.100s-%s.mp3", dir, name, us ? "us" : "uk");
    if (stat(mp3, &sb) < 0 || sb.st_size <= 0) {
        sprintf(links, "%.1000s/%.100s.links", dir, name);
        if (!read_links(links, uk_link, us_link)) {
            sprintf(page, "%.1000s/%.100s.html", dir, name);
            sprintf(url, "%s%.300s", SOUND_PAGE, uw);
            r = fetch(url, page, PAGE_MAX);
            if (r)
                return r;
            text = slurp(page, 4000000L);
            unlink(page);
            if (!text)
                return snd_net;
            find_link(text, "uk_pron", uk_link);
            find_link(text, "us_pron", us_link);
            free(text);
            /* only what was found is kept: a page Tor's exit got
             * instead of the dictionary's shouldn't stay as "none" */
            if (uk_link[0] || us_link[0])
                write_links(links, uk_link, us_link);
        }
        link = us ? us_link : uk_link;
        if (!link[0])
            return snd_none;
        sprintf(url, "%s%.600s", SOUND_SITE, link);
        r = fetch(url, mp3, MP3_MAX);
        if (r)
            return r;
    }
    argv[0] = "mpv";
    argv[1] = "--really-quiet";
    argv[2] = "--no-video";
    argv[3] = "--no-terminal";
    argv[4] = mp3;
    argv[5] = 0;
    r = run(argv, 0);
    return r ? snd_noplayer : snd_played;
}

/* start it; the child's pid, or -1. keep: an fd the child closes (the
 * engine's FIFO, which it must not hold) */
int sound_start(const char *word, int us, int keep)
{
    int pid;

    pid = fork();
    if (pid)
        return pid;
    if (keep >= 0)
        close(keep);
    _exit(sound_child(word, us));
    return -1;
}

/* what an exit status of the child means, for the message line */
const char *sound_said(int status, int us)
{
    static char m[128];
    const char *s;

    switch (status) {
    case snd_played:
        s = "played";
        break;
    case snd_none:
        s = "Cambridge has no sound for it";
        break;
    case snd_noproxy:
        s = "not fetched: no proxychains (install.sh net), so not "
            "through Tor";
        break;
    case snd_noplayer:
        s = "mpv could not play it";
        break;
    case snd_nodir:
        s = "no cache directory to keep it in";
        break;
    default:
        s = "could not fetch it through Tor";
        break;
    }
    sprintf(m, "%s: %s", us ? "US" : "UK", s);
    return m;
}
