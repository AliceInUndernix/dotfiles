/* seal: turns a directory into one encrypted file, and back (see
 * README).
 *
 *   seal [-knqsv] [-o FILE] DIR           DIR -> DIR.seal, DIR removed
 *   unseal [-fknqsvL] [-d DIR] FILE       FILE -> RAM (or DIR)
 *   unseal -t [-fqs] FILE                 check FILE, list what it holds
 *   seal -l | -a | -c                     list, seal back all, or remove
 *                                         dead links (after a crash)
 *
 * unseal is the same program (a link to seal, or seal -u). */
#define _XOPEN_SOURCE 700
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <termios.h>
#include <unistd.h>

#include "crypto.h"
#include "seal.h"

struct secret *sec;
int opt_force;
int opt_quiet;
int opt_verbose;
int opt_dry;
volatile int stop_now;

const char *prog = "seal";
static int opt_keep, opt_stdin, opt_list, opt_all, opt_clean, opt_nolink;
static int opt_check;      /* unseal -n or -t: read it all, write nothing */
static const char *opt_out, *opt_dest;

static int tty_fd = -1;
static struct termios tty_saved;
static volatile int tty_changed;
static volatile long sink;

/* ------------------------------------------------------------ helpers */

/* "3 directories, 1 file, 2 links, 4.0 KiB" into s */
static void counts_text(char *s, struct counts *c)
{
    double b;
    long d;

    d = c->dirs > 0 ? c->dirs - 1 : 0;    /* not the top one */
    sprintf(s, "%ld director%s, %ld file%s, %ld link%s, ", d,
        d == 1 ? "y" : "ies", c->files, c->files == 1 ? "" : "s",
        c->links, c->links == 1 ? "" : "s");
    s += strlen(s);
    b = (double)c->bytes;
    if (c->bytes < 1024)
        sprintf(s, "%lld byte%s", c->bytes, c->bytes == 1 ? "" : "s");
    else if (b < 1024.0 * 1024)
        sprintf(s, "%.1f KiB", b / 1024);
    else if (b < 1024.0 * 1024 * 1024)
        sprintf(s, "%.1f MiB", b / 1024 / 1024);
    else
        sprintf(s, "%.1f GiB", b / 1024 / 1024 / 1024);
}

static void say_counts(const char *from, const char *to,
    struct counts *c)
{
    char text[200];

    if (opt_quiet)
        return;
    counts_text(text, c);
    printf("%s -> %s (%s)\n", from, to, text);
}

/* ------------------------------------------------- signals, memory */

static void on_signal(int sig)
{
    if (tty_changed) {
        tcsetattr(tty_fd, TCSAFLUSH, &tty_saved);
        sink = write(tty_fd, "\n", 1);
        _exit(128 + sig);
    }
    stop_now = 1;
}

static void setup(void)
{
    struct sigaction sa;
    struct rlimit rl;

    /* no core dump, and no other program of yours (a debugger) may
     * look into this one's memory */
    rl.rlim_cur = 0;
    rl.rlim_max = 0;
    setrlimit(RLIMIT_CORE, &rl);
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);

    sec = malloc(sizeof *sec);
    if (!sec) {
        warn("%s", "out of memory", 0);
        exit(1);
    }
    memset(sec, 0, sizeof *sec);
    mlock(sec, sizeof *sec);       /* best effort: kept out of swap */

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, 0);
    sigaction(SIGTERM, &sa, 0);
    sigaction(SIGHUP, &sa, 0);
}

static void finish(void)
{
    crypto_wipe(sec, sizeof *sec);
}

/* --------------------------------------------------------- password */

/* one line from fd into buf; the length, or -1 */
static long read_line(int fd, char *buf, long size)
{
    long n, k;
    char c;

    n = 0;
    for (;;) {
        k = read(fd, &c, 1);
        if (k < 0 && errno == EINTR && !stop_now)
            continue;
        if (k < 0)
            return -1;
        if (k == 0 || c == '\n')
            break;
        if (n == size - 1)
            return -1;
        buf[n] = c;
        n++;
    }
    buf[n] = 0;
    return n;
}

static long ask(const char *prompt, char *buf)
{
    struct termios t;
    long n;
    int r;

    r = tcgetattr(tty_fd, &tty_saved);
    if (r < 0)
        return -1;
    t = tty_saved;
    t.c_lflag &= ~(ECHO | ECHOE | ECHOK);
    t.c_lflag |= ECHONL;
    write_all(tty_fd, prompt, strlen(prompt));
    tty_changed = 1;
    tcsetattr(tty_fd, TCSAFLUSH, &t);
    n = read_line(tty_fd, buf, sizeof sec->pass);
    tcsetattr(tty_fd, TCSAFLUSH, &tty_saved);
    tty_changed = 0;
    return n;
}

/* the password into sec->pass: twice from the terminal when sealing
 * (twice: a typo would lock the directory away for good) */
static long get_pass(const char *what, int twice)
{
    char prompt[300];
    long n, m;

    if (opt_stdin && isatty(0)) {
        warn("%s", "-s reads the password from a pipe; on a terminal "
            "leave -s out (then nothing is echoed)", 0);
        return -1;
    }
    if (opt_stdin) {
        n = read_line(0, sec->pass, sizeof sec->pass);
    } else {
        tty_fd = open("/dev/tty", O_RDWR | O_NOCTTY);
        if (tty_fd < 0) {
            warn("%s", "no terminal to ask the password on (-s reads "
                "it from standard input)", 0);
            return -1;
        }
        sprintf(prompt, "password for %.250s: ", what);
        n = ask(prompt, sec->pass);
        m = n;
        if (n > 0 && twice)
            m = ask("again: ", sec->pass2);
        close(tty_fd);
        if (n > 0 && (m != n || memcmp(sec->pass, sec->pass2, n)) &&
            twice)
        {
            warn("%s", "the passwords differ; nothing was done", 0);
            return -1;
        }
    }
    if (n < 0) {
        warn("%s", "can't read the password (at most 1023 bytes)", 0);
        return -1;
    }
    if (n == 0) {
        warn("%s", "no password given; nothing was done", 0);
        return -1;
    }
    return n;
}

/* the key from the password and salt: deliberately slow, ~0.05 s on a
 * new computer, ~1-2 s on an old netbook */
static int derive_key(const unsigned char *salt, long pass_len)
{
    void *work;

    work = malloc(KDF_BLOCKS * 1024L);
    if (!work) {
        warn("%s", "out of memory (it needs 32 MiB)", 0);
        return -1;
    }
    argon2id(sec->key, KEY_SIZE, work, KDF_BLOCKS, KDF_PASSES,
        (const u8 *)sec->pass, (u32)pass_len, salt, SALT_SIZE);
    free(work);
    crypto_wipe(sec->pass, sizeof sec->pass);
    crypto_wipe(sec->pass2, sizeof sec->pass2);
    return 0;
}

/* ------------------------------------------------- the RAM directory */

/* where unseal puts directories in RAM: $XDG_RUNTIME_DIR/seal,
 * /run/user/UID/seal or /dev/shm/seal-UID, only yours (mode 700).
 * -1 when there is none (create: it is made when missing). */
static int ram_base(char *out, int create)
{
    char cand[3][PATH_SIZE];
    struct stat st;
    const char *s;
    long uid;
    int i, r;

    uid = (long)getuid();
    s = getenv("XDG_RUNTIME_DIR");
    cand[0][0] = 0;
    if (s && s[0] == '/' && strlen(s) < PATH_SIZE - 16)
        strcpy(cand[0], s);
    sprintf(cand[1], "/run/user/%ld", uid);
    strcpy(cand[2], "/dev/shm");
    for (i = 0; i < 3; i++) {
        if (!cand[i][0])
            continue;
        r = stat(cand[i], &st);
        if (r < 0 || !S_ISDIR(st.st_mode) || !is_ram(cand[i]))
            continue;
        if (i < 2 && (long)st.st_uid != uid)
            continue;
        if (i < 2)
            sprintf(out, "%s/seal", cand[i]);
        else
            sprintf(out, "/dev/shm/seal-%ld", uid);
        if (create)
            mkdir(out, 0700);
        r = lstat(out, &st);
        if (r < 0 || !S_ISDIR(st.st_mode) || (long)st.st_uid != uid)
            continue;
        if ((st.st_mode & 0777) != 0700)
            chmod(out, 0700);
        return 0;
    }
    return -1;
}

/* the file a directory in RAM was unsealed from: into from ("" when
 * not known) */
static void read_from(const char *base, const char *name, char *from)
{
    char state[PATH_SIZE];
    long n;
    int fd, r;

    from[0] = 0;
    r = join(state, base, "/.from/");
    if (r == 0)
        r = join(state, state, name);
    if (r < 0)
        return;
    fd = open(state, O_RDONLY | O_NOFOLLOW);
    if (fd < 0)
        return;
    n = read_full(fd, from, PATH_SIZE - 1);
    close(fd);
    if (n < 2 || from[n - 1] != '\n') {
        from[0] = 0;
        return;
    }
    from[n - 1] = 0;
}

static void write_from(const char *base, const char *name,
    const char *file)
{
    char state[PATH_SIZE], abs[PATH_SIZE], cwd[PATH_SIZE];
    const char *s;
    int fd, r;

    if (file[0] == '/') {
        r = join(abs, file, "");
    } else {
        s = getcwd(cwd, sizeof cwd);
        r = s ? 0 : -1;
        if (r == 0)
            r = join(abs, cwd, "/");
        if (r == 0)
            r = join(abs, abs, file);
    }
    if (r == 0)
        r = join(state, base, "/.from");
    if (r < 0)
        return;
    mkdir(state, 0700);
    r = join(state, state, "/");
    if (r == 0)
        r = join(state, state, name);
    if (r < 0)
        return;
    fd = open(state, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return;
    write_all(fd, abs, strlen(abs));
    write_all(fd, "\n", 1);
    close(fd);
}

/* ------------------------------------------------------------ links */

/* unseal leaves a link next to the sealed file, pointing to the
 * directory in RAM. Every link is written down first, in
 * ~/.system/state/seal/links (one "LINK<tab>TARGET" per line), so that
 * after a crash, when RAM is empty, seal -c still knows which links to
 * take away. A link goes when its directory is sealed again. */

static char links_file[PATH_SIZE];

static int links_init(void)
{
    char dir[PATH_SIZE];
    const char *s;
    int r;

    s = getenv("XDG_STATE_HOME");
    if (s && s[0] == '/') {
        r = join(dir, s, "");
    } else {
        s = getenv("HOME");
        if (!s || s[0] != '/')
            return -1;
        r = join(dir, s, "/.system");
        if (r == 0)
            mkdir(dir, 0700);
        if (r == 0)
            r = join(dir, dir, "/state");
    }
    if (r == 0)
        mkdir(dir, 0700);
    if (r == 0)
        r = join(dir, dir, "/seal");
    if (r == 0)
        mkdir(dir, 0700);
    if (r == 0)
        r = join(links_file, dir, "/links");
    return r;
}

/* is link a symbolic link to target? */
static int links_to(const char *link, const char *target)
{
    char buf[PATH_SIZE];
    struct stat st;
    long n;
    int r;

    r = lstat(link, &st);
    if (r < 0 || !S_ISLNK(st.st_mode))
        return 0;
    n = readlink(link, buf, sizeof buf - 1);
    if (n < 0)
        return 0;
    buf[n] = 0;
    return !strcmp(buf, target);
}

/* Rewrites the list: adds add_link -> add_target (when set), takes
 * away the links to drop_target (when set), and the links whose
 * directory is gone (after a reboot or crash) or that no longer point
 * to it. find set: only look up the link to find, into found. Returns
 * how many links it took away, -1 on error. */
static long links_update(const char *add_link, const char *add_target,
    const char *drop_target, const char *find, char *found)
{
    struct stat st;
    char *text, *line, *tab, *end, tmp[PATH_SIZE];
    long n, size, removed;
    int fd, out, r, keep;

    if (!links_file[0])
        return -1;
    fd = open(links_file, O_RDONLY | O_NOFOLLOW);
    size = 0;
    text = 0;
    if (fd >= 0) {
        r = fstat(fd, &st);
        size = r == 0 ? (long)st.st_size : 0;
        text = malloc(size + 1);
        if (!text) {
            close(fd);
            return -1;
        }
        n = read_full(fd, text, size);
        close(fd);
        size = n < 0 ? 0 : n;
        text[size] = 0;
    }
    if (find) {
        found[0] = 0;
        line = text;
        while (line && line < text + size) {
            end = strchr(line, '\n');
            if (!end)
                break;
            *end = 0;
            tab = strchr(line, '\t');
            if (tab && !strcmp(tab + 1, find)) {
                *tab = 0;
                if (links_to(line, find))
                    join(found, line, "");
            }
            line = end + 1;
        }
        free(text);
        return 0;
    }

    r = join(tmp, links_file, ".new");
    out = r < 0 ? -1 :
        open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (out < 0) {
        free(text);
        return -1;
    }
    removed = 0;
    line = text;
    while (line && line < text + size) {
        end = strchr(line, '\n');
        if (!end)
            break;
        *end = 0;
        tab = strchr(line, '\t');
        if (tab) {
            *tab = 0;
            keep = links_to(line, tab + 1) && lstat(tab + 1, &st) == 0;
            if (drop_target && !strcmp(tab + 1, drop_target))
                keep = 0;
            if (!keep && links_to(line, tab + 1)) {
                r = unlink(line);
                if (r == 0) {
                    removed++;
                    if (!opt_quiet)
                        printf("removed the link %s\n", line);
                }
            }
            *tab = '\t';
            if (keep) {
                write_all(out, line, strlen(line));
                write_all(out, "\n", 1);
            }
        }
        line = end + 1;
    }
    if (add_link) {
        write_all(out, add_link, strlen(add_link));
        write_all(out, "\t", 1);
        write_all(out, add_target, strlen(add_target));
        write_all(out, "\n", 1);
    }
    free(text);
    r = fsync(out);
    n = close(out);
    if (r < 0 || n < 0) {
        unlink(tmp);
        return -1;
    }
    r = rename(tmp, links_file);
    return r < 0 ? -1 : removed;
}

/* the link for a directory unsealed into RAM: next to file, named
 * like the directory */
static void make_link(const char *file, const char *name,
    const char *target)
{
    char dir[PATH_SIZE], base[PATH_SIZE], link[PATH_SIZE], cwd[PATH_SIZE];
    struct stat st;
    const char *s;
    long n;
    int r;

    r = split(file, dir, base);
    if (r == 0 && dir[0] != '/') {
        s = getcwd(cwd, sizeof cwd);
        r = s ? 0 : -1;
        if (r == 0 && strcmp(dir, "."))
            r = join(cwd, cwd, "/");
        if (r == 0 && strcmp(dir, "."))
            r = join(dir, cwd, dir);
        else if (r == 0)
            r = join(dir, cwd, "");
    }
    if (r == 0)
        r = join(link, dir, strcmp(dir, "/") ? "/" : "");
    if (r == 0)
        r = join(link, link, name);
    if (r < 0 || strchr(link, '\n') || strchr(link, '\t') ||
        strchr(target, '\n') || strchr(target, '\t'))
    {
        warn("%s: %s", name, "no link made (an odd path)");
        return;
    }
    n = lstat(link, &st);
    if (n == 0) {
        warn("%s exists, so no link to %s was made", link, target);
        return;
    }
    /* written down before it is made: a crash leaves no link unknown */
    n = links_update(link, target, 0, 0, 0);
    if (n < 0) {
        warn("can't write %s, so no link was made", links_file, 0);
        return;
    }
    r = symlink(target, link);
    if (r < 0) {
        warn("can't make the link %s: %s", link, strerror(errno));
        return;
    }
    if (!opt_quiet)
        printf("link: %s -> %s\n", link, target);
}

/* seal -c: take away the links whose directory is gone */
static int cmd_clean(void)
{
    long n;

    n = links_update(0, 0, 0, 0, 0);
    if (n < 0) {
        warn("can't update %s", links_file, 0);
        return 1;
    }
    if (n == 0 && !opt_quiet)
        printf("no dead links\n");
    return 0;
}

static int cmd_list(void)
{
    char base[PATH_SIZE], from[PATH_SIZE], path[PATH_SIZE];
    char link[PATH_SIZE];
    struct stat st;
    struct dirent *e;
    DIR *d;
    long n;
    int r;

    n = 0;
    d = 0;
    r = ram_base(base, 0);
    if (r == 0)
        r = join(path, base, "/.from");
    if (r == 0)
        d = opendir(path);
    while (d && (e = readdir(d)) != 0) {
        if (e->d_name[0] == '.')
            continue;
        read_from(base, e->d_name, from);
        r = join(path, base, "/");
        if (r == 0)
            r = join(path, path, e->d_name);
        if (r < 0 || !from[0])
            continue;
        r = lstat(path, &st);
        links_update(0, 0, 0, path, link);
        printf("%s%s (from %s)", path, r < 0 ? " (gone)" : "", from);
        if (link[0])
            printf(", link %s", link);
        printf("\n");
        n++;
    }
    if (d)
        closedir(d);
    if (n == 0)
        printf("nothing is unsealed in RAM\n");
    return 0;
}

static int cmd_seal(const char *arg);

/* seal -a: everything unsealed in RAM back into its file */
static int cmd_all(void)
{
    char base[PATH_SIZE], from[PATH_SIZE], path[PATH_SIZE];
    char names[64][256];
    struct stat st;
    struct dirent *e;
    DIR *d;
    long n, i;
    int r, bad;

    n = 0;
    d = 0;
    r = ram_base(base, 0);
    if (r == 0)
        r = join(path, base, "/.from");
    if (r == 0)
        d = opendir(path);
    while (d && n < 64 && (e = readdir(d)) != 0) {
        if (e->d_name[0] == '.' || strlen(e->d_name) > 255)
            continue;
        read_from(base, e->d_name, from);
        r = join(path, base, "/");
        if (r == 0)
            r = join(path, path, e->d_name);
        if (r == 0 && from[0])
            r = lstat(path, &st);
        if (r == 0 && from[0]) {
            strcpy(names[n], e->d_name);
            n++;
        }
    }
    if (d)
        closedir(d);
    bad = 0;
    for (i = 0; i < n; i++) {
        r = join(path, base, "/");
        if (r == 0)
            r = join(path, path, names[i]);
        if (r == 0)
            r = cmd_seal(path);
        if (r != 0)
            bad = 1;
    }
    links_update(0, 0, 0, 0, 0);
    if (n == 0 && !opt_quiet)
        printf("nothing is unsealed in RAM\n");
    return bad;
}

/* ------------------------------------------------------------- seal */

static int cmd_seal(const char *arg)
{
    char dir[PATH_SIZE], name[PATH_SIZE], parent[PATH_SIZE];
    char base[PATH_SIZE], from[PATH_SIZE], out[PATH_SIZE];
    char outdir[PATH_SIZE], outname[PATH_SIZE], tmp[PATH_SIZE];
    char rnd[16], state[PATH_SIZE];
    unsigned char salt[SALT_SIZE];
    struct counts c;
    struct stat st, ost;
    const char *home;
    long n, pass_len;
    int fd, r, replacing;

    memset(&c, 0, sizeof c);
    r = join(dir, arg, "");
    if (r < 0) {
        warn("%s: the path is too long", arg, 0);
        return 1;
    }
    n = strlen(dir);
    while (n > 1 && dir[n - 1] == '/') {
        n--;
        dir[n] = 0;
    }
    if (!strcmp(dir, ".")) {
        home = getcwd(dir, sizeof dir);
        if (!home) {
            warn("can't find this directory: %s", strerror(errno), 0);
            return 1;
        }
    }
    split(dir, parent, name);
    if (!name[0] || !strcmp(name, ".") || !strcmp(name, "..") ||
        strlen(name) > 255)
    {
        warn("%s: give the directory by its name (like ../baba)", dir, 0);
        return 1;
    }
    r = lstat(dir, &st);
    if (r < 0) {
        warn("%s: %s", dir, strerror(errno));
        return 1;
    }
    if (S_ISLNK(st.st_mode)) {
        /* a link unseal made: seal the directory in RAM */
        n = readlink(dir, from, sizeof from - 1);
        r = n > 0 ? 0 : -1;
        if (r == 0) {
            from[n] = 0;
            r = split(from, parent, name);
        }
        if (r == 0)
            r = ram_base(base, 0);
        if (r == 0 && !same_file(parent, base))
            r = -1;
        if (r == 0)
            r = join(dir, from, "");
        if (r < 0) {
            warn("%s: a symbolic link; seal the directory it points to",
                dir, 0);
            return 1;
        }
        r = lstat(dir, &st);
        if (r < 0) {
            warn("%s: %s", dir, strerror(errno));
            return 1;
        }
    }
    if (!S_ISDIR(st.st_mode)) {
        warn("%s: not a directory", dir, 0);
        return 1;
    }
    home = getenv("HOME");
    if ((home && same_file(dir, home)) || same_file(dir, "/")) {
        warn("%s: that is your whole home (or /); seal a directory in it",
            dir, 0);
        return 1;
    }

    /* a directory unseal put in RAM goes back where it came from */
    from[0] = 0;
    if (ram_base(base, 0) == 0 && same_file(parent, base))
        read_from(base, name, from);
    if (opt_out)
        r = join(out, opt_out, "");
    else if (from[0])
        r = join(out, from, "");
    else
        r = join(out, dir, ".seal");
    if (r < 0) {
        warn("%s: the path is too long", dir, 0);
        return 1;
    }
    replacing = from[0] && !strcmp(out, from);
    r = lstat(out, &ost);
    if (r == 0 && !replacing) {
        warn("%s: %s", out, "already exists; nothing was done (-o "
            "names another file)");
        return 1;
    }
    if (r == 0 && !S_ISREG(ost.st_mode)) {
        warn("%s: %s", out, "not a file; nothing was done");
        return 1;
    }

    if (opt_dry) {
        r = pack(-1, dir, name, 0, 0, &c);
        if (r < 0) {
            warn("%s: it can't be sealed (above)", dir, 0);
            return 1;
        }
        if (!opt_quiet) {
            counts_text(state, &c);
            printf("would seal %s -> %s (%s)%s\n", dir, out, state,
                opt_keep ? "" : ", then remove it");
        }
        return 0;
    }
    pass_len = get_pass(name, 1);
    if (pass_len < 0)
        return 1;
    rand_bytes(salt, SALT_SIZE);
    r = derive_key(salt, pass_len);
    if (r < 0)
        return 1;

    /* write next to the output under a hidden name, read it back and
     * compare, and only then give it its name */
    r = split(out, outdir, outname);
    rand_name(rnd);
    if (r == 0)
        r = join(tmp, outdir, "/.");
    if (r == 0)
        r = join(tmp, tmp, outname);
    if (r == 0)
        r = join(tmp, tmp, ".");
    if (r == 0)
        r = join(tmp, tmp, rnd);
    if (r < 0) {
        warn("%s: the path is too long", out, 0);
        return 1;
    }
    fd = open(tmp, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) {
        warn("can't make %s: %s", tmp, strerror(errno));
        return 1;
    }
    r = fstat(fd, &ost);
    if (r == 0)
        r = write_all(fd, salt, SALT_SIZE);
    if (r < 0) {
        warn("can't write %s: %s", tmp, strerror(errno));
        goto failed;
    }
    r = pack(fd, dir, name, (long)ost.st_dev, (long)ost.st_ino, &c);
    if (r < 0)
        goto failed;
    r = fsync(fd);
    if (r < 0) {
        warn("can't write %s: %s", tmp, strerror(errno));
        goto failed;
    }
    r = pack_verify(fd, dir, &c);
    if (r < 0)
        goto failed;
    r = close(fd);
    fd = -1;
    if (r < 0) {
        warn("can't write %s: %s", tmp, strerror(errno));
        goto failed;
    }
    if (replacing) {
        r = rename(tmp, out);
    } else {
        /* link: never over a file that appeared meanwhile */
        r = link(tmp, out);
        if (r < 0 && errno != EEXIST)
            r = rename(tmp, out);
        else if (r == 0)
            unlink(tmp);
    }
    if (r < 0) {
        warn("can't make %s: %s", out, strerror(errno));
        goto failed;
    }
    sync_dir(outdir);
    if (stop_now) {
        warn("interrupted: %s is sealed, but %s is left as it was",
            out, dir);
        return 1;
    }

    if (opt_keep) {
        say_counts(dir, out, &c);
        return 0;
    }
    r = pack_remove(dir);
    if (from[0]) {
        n = join(state, base, "/.from/");
        if (n == 0)
            n = join(state, state, name);
        if (n == 0)
            unlink(state);
        n = join(state, base, "/");
        if (n == 0)
            n = join(state, state, name);
        if (n == 0)
            links_update(0, 0, state, 0, 0);
    }
    say_counts(dir, out, &c);
    if (r < 0) {
        warn("%s is sealed, but what is named above is left in %s",
            out, dir);
        return 1;
    }
    return 0;
failed:
    if (fd >= 0)
        close(fd);
    unlink(tmp);
    warn("nothing was changed: %s is as it was", dir, 0);
    return 1;
}

/* ----------------------------------------------------------- unseal */

static int cmd_unseal(const char *file)
{
    char name[256], dest[PATH_SIZE], final[PATH_SIZE], tmp[PATH_SIZE];
    char inner[PATH_SIZE], rnd[16], text[200];
    unsigned char salt[SALT_SIZE];
    struct counts c;
    struct stat st;
    const char *s;
    long n, pass_len;
    int fd, r, in_ram;

    memset(&c, 0, sizeof c);
    fd = open(file, O_RDONLY);
    if (fd < 0) {
        warn("%s: %s", file, strerror(errno));
        return 1;
    }
    r = fstat(fd, &st);
    if (r < 0 || !S_ISREG(st.st_mode)) {
        warn("%s: not a file", file, 0);
        close(fd);
        return 1;
    }
    n = read_full(fd, salt, SALT_SIZE);
    if (n != SALT_SIZE || st.st_size < SALT_SIZE + 16) {
        warn("%s: too short to be a sealed file", file, 0);
        close(fd);
        return 1;
    }
    in_ram = !opt_dest;
    if (opt_dest)
        r = join(dest, opt_dest, "");
    else
        r = ram_base(dest, 1);
    if (r < 0 && opt_check && in_ram) {
        strcpy(dest, "RAM");    /* -t needs none; -n only says so */
        r = 0;
    }
    if (r < 0) {
        warn("%s", in_ram ? "found no RAM directory (tmpfs) to "
            "unseal into; -d DIR unseals onto the disk" :
            "the path is too long", 0);
        close(fd);
        return 1;
    }
    pass_len = get_pass(file, 0);
    if (pass_len < 0) {
        close(fd);
        return 1;
    }
    r = derive_key(salt, pass_len);
    if (r == 0)
        r = unpack_open(fd, name);
    if (r < 0) {
        warn("%s: nothing was unsealed", file, 0);
        close(fd);
        return 1;
    }

    if (opt_check) {
        r = unpack_rest(0, 1, &c);
        close(fd);
        if (r < 0) {
            warn("%s: it is not all right (above)", file, 0);
            return 1;
        }
        if (!opt_quiet) {
            printf("%s: %s, ", file, r ? "read with problems (-f)" :
                "all is well");
            counts_text(text, &c);
            printf("%s/ holds %s\n", name, text);
        }
        n = join(final, dest, "/");
        if (n == 0)
            n = join(final, final, name);
        if (!opt_dry || n < 0)
            return r ? 1 : 0;
        n = lstat(final, &st);
        if (n == 0) {
            warn("%s: already there, so it can't be unsealed into it",
                final, 0);
            return 1;
        }
        if (opt_quiet)
            return r ? 1 : 0;
        if (in_ram && !opt_nolink)
            s = ", with a link next to it";
        else if (!in_ram && !opt_keep)
            s = ", then remove it";
        else
            s = "";
        printf("would unseal %s into %s%s\n", file, final, s);
        return r ? 1 : 0;
    }

    r = join(final, dest, "/");
    if (r == 0)
        r = join(final, final, name);
    rand_name(rnd);
    if (r == 0)
        r = join(tmp, dest, "/.seal-");
    if (r == 0)
        r = join(tmp, tmp, rnd);
    if (r == 0)
        r = join(inner, tmp, "/");
    if (r == 0)
        r = join(inner, inner, name);
    if (r < 0) {
        warn("%s: the path is too long", dest, 0);
        close(fd);
        return 1;
    }
    r = lstat(final, &st);
    if (r == 0) {
        warn("%s: %s", final, in_ram ? "already unsealed (seal it "
            "back first, or unseal -d DIR)" : "already exists");
        close(fd);
        return 1;
    }
    r = mkdir(tmp, 0700);
    if (r < 0) {
        warn("can't make %s: %s", tmp, strerror(errno));
        close(fd);
        return 1;
    }
    r = unpack_rest(tmp, 0, &c);
    close(fd);
    if (r < 0) {
        rm_tree(tmp);
        warn("%s: nothing was unsealed", file, 0);
        return 1;
    }
    n = lstat(inner, &st);
    if (n < 0) {
        rm_tree(tmp);
        warn("%s: nothing could be read", file, 0);
        return 1;
    }
    errno = 0;
    n = lstat(final, &st);
    if (n < 0)
        n = rename(inner, final);
    else
        n = -1;
    if (n < 0) {
        warn("can't make %s: %s", final, errno ? strerror(errno) :
            "it appeared meanwhile");
        rm_tree(tmp);
        return 1;
    }
    rmdir(tmp);
    if (!in_ram)
        sync_dir(dest);
    if (r > 0) {
        say_counts(file, final, &c);
        warn("%s: unsealed with problems (above): check the files; %s "
            "is kept", final, file);
        return 1;
    }
    if (in_ram)
        write_from(dest, name, file);
    else if (!opt_keep)
        unlink(file);
    say_counts(file, final, &c);
    if (in_ram && !opt_nolink)
        make_link(file, name, final);
    if (in_ram && !opt_quiet)
        printf("in RAM, gone at shutdown; put it back with: seal %s "
            "(or seal -a)\n", opt_nolink ? final : name);
    return 0;
}

/* -------------------------------------------------------------- main */

static void help(FILE *f)
{
    fprintf(f,
        "usage: seal [-knqsv] [-o FILE] DIR   DIR -> DIR.seal, then DIR "
        "goes\n"
        "       unseal [-fknqsvL] [-d DIR] FILE  FILE -> RAM, with a "
        "link\n"
        "       unseal -t [-fqs] FILE        check FILE, list what it "
        "holds\n"
        "       seal -l                      what is unsealed in RAM\n"
        "       seal -a                      seal back all of it\n"
        "       seal -c                      remove dead links\n");
    fprintf(f,
        "  -o FILE  seal into FILE (else DIR.seal, or where it came "
        "from)\n"
        "  -d DIR   unseal onto the disk, into DIR, and remove FILE\n"
        "  -k       keep DIR (seal) or FILE (unseal -d)\n"
        "  -L       unseal: make no link next to FILE\n"
        "  -n       dry run: check all, say what would happen, change "
        "nothing\n"
        "  -t       unseal: check FILE and list it (-n -v)\n"
        "  -f       unseal: go on when FILE is damaged\n"
        "  -s       read the password from standard input (a pipe)\n"
        "  -q       say nothing but errors   -v  name each file\n");
    fprintf(f,
        "  -u       seal -u is unseal        -h  this help\n"
        "Exit status: 0 done, 1 failed (it says why), 2 wrong "
        "options.\n");
}

/* a wrong option or argument: say what, and how to get help */
static void usage(const char *what, const char *arg)
{
    fprintf(stderr, "%s: ", prog);
    fprintf(stderr, what, arg);
    fprintf(stderr, " (%s -h for help)\n", prog);
    exit(2);
}

int main(int argc, char **argv)
{
    const char *s, *arg;
    char opt[2];
    int i, j, unseal, r, c, tlist;

    tlist = 0;
    s = strrchr(argv[0], '/');
    s = s ? s + 1 : argv[0];
    unseal = !strcmp(s, "unseal");
    prog = unseal ? "unseal" : "seal";
    arg = 0;
    opt[1] = 0;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--")) {
            i++;
            break;
        }
        if (!strcmp(argv[i], "--help")) {
            help(stdout);
            return 0;
        }
        if (argv[i][0] != '-' || !argv[i][1])
            break;
        for (j = 1; argv[i][j]; j++) {
            c = argv[i][j];
            opt[0] = (char)c;
            switch (c) {
            case 'h':
                help(stdout);
                return 0;
            case 'k': opt_keep = 1; break;
            case 'q': opt_quiet = 1; break;
            case 'v': opt_verbose = 1; break;
            case 'n': opt_dry = 1; break;
            case 's': opt_stdin = 1; break;
            case 'f': opt_force = 1; break;
            case 'l': opt_list = 1; break;
            case 'a': opt_all = 1; break;
            case 'c': opt_clean = 1; break;
            case 'L': opt_nolink = 1; break;
            case 't':
                opt_dry = 1;
                opt_verbose = 1;
                tlist = 1;
                break;
            case 'u':
                unseal = 1;
                prog = "unseal";
                break;
            case 'o':
            case 'd':
                if (argv[i][j + 1]) {
                    s = argv[i] + j + 1;
                } else if (i + 1 < argc) {
                    i++;
                    s = argv[i];
                } else {
                    usage("-%s needs an argument", opt);
                }
                if (!s[0])
                    usage("-%s needs an argument", opt);
                if (c == 'o')
                    opt_out = s;
                else
                    opt_dest = s;
                goto next;
            default:
                usage("unknown option -%s", opt);
            }
        }
next:
        ;
    }
    if (i < argc) {
        arg = argv[i];
        i++;
    }
    if (i < argc && argv[i][0] == '-')
        usage("options go before the name: %s -h", prog);
    if (i < argc)
        usage("one name at a time (%s is one too many)", argv[i]);
    if (opt_list + opt_all + opt_clean > 1)
        usage("%s", "-l, -a and -c go alone");
    if ((opt_list || opt_all || opt_clean) && (unseal || arg || opt_out ||
        opt_keep || tlist || (opt_dry && !opt_all)))
    {
        usage("%s", "seal -l, -a and -c take no name, and no -o, -k, -t "
            "or -u (-a takes -n)");
    }
    if (!unseal && tlist)
        usage("%s", "-t is for unseal (seal -n -v says what would go "
            "in)");
    if (!unseal && (opt_dest || opt_nolink || opt_force))
        usage("%s", "-d, -L and -f are for unseal");
    if (unseal && opt_out)
        usage("%s", "-o is for seal (unseal -d DIR unseals onto the "
            "disk)");
    if (unseal && opt_keep && !opt_dest)
        usage("%s", "unseal -k goes with -d (into RAM, FILE is kept "
            "anyway)");
    if (opt_quiet && !tlist)
        opt_verbose = 0;
    opt_check = unseal && opt_dry;
    if (!arg && !opt_list && !opt_all && !opt_clean)
        usage("%s", unseal ? "give the sealed FILE" : "give the DIR "
            "to seal");
    setup();
    links_init();
    if (opt_clean)
        r = cmd_clean();
    else if (opt_all)
        r = cmd_all();
    else if (opt_list)
        r = cmd_list();
    else if (unseal)
        r = cmd_unseal(arg);
    else
        r = cmd_seal(arg);
    finish();
    return r;
}
