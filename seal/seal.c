/* seal: turns a directory into one encrypted file, and back (see
 * README).
 *
 *   seal [-k] [-q] [-s] [-o FILE] DIR     DIR -> DIR.seal, DIR removed
 *   seal -l                               what is unsealed in RAM
 *   unseal [-k] [-q] [-s] [-f] [-d DIR] FILE
 *                                         FILE -> RAM (or DIR)
 *   unseal -t [-q] [-s] [-f] FILE         check FILE, list what it holds
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

#define TMPFS_MAGIC_NO 0x01021994L
#define RAMFS_MAGIC_NO 0x858458f6L

struct secret *sec;
int opt_force;
int opt_quiet;
volatile int stop_now;

static const char *prog = "seal";
static int opt_keep, opt_stdin, opt_list;
static const char *opt_out, *opt_dest;

static int tty_fd = -1;
static struct termios tty_saved;
static volatile int tty_changed;
static volatile long sink;

/* ------------------------------------------------------------ helpers */

void warn(const char *fmt, const char *a, const char *b)
{
    fprintf(stderr, "%s: ", prog);
    fprintf(stderr, fmt, a, b);
    fputc('\n', stderr);
}

int write_all(int fd, const void *p, long n)
{
    const char *s;
    long k;

    s = (const char *)p;
    while (n > 0) {
        k = write(fd, s, n);
        if (k < 0 && errno == EINTR && !stop_now)
            continue;
        if (k <= 0)
            return -1;
        s += k;
        n -= k;
    }
    return 0;
}

/* read n bytes, fewer only at the end of the file; -1 on error */
long read_full(int fd, void *p, long n)
{
    char *d;
    long got, k;

    d = (char *)p;
    got = 0;
    while (got < n) {
        k = read(fd, d + got, n - got);
        if (k < 0 && errno == EINTR && !stop_now)
            continue;
        if (k < 0)
            return -1;
        if (k == 0)
            break;
        got += k;
    }
    return got;
}

/* dst = a b; dst may be a. -1 when too long. */
int join(char *dst, const char *a, const char *b)
{
    long n, m;

    n = strlen(a);
    m = strlen(b);
    if (n + m >= PATH_SIZE)
        return -1;
    memmove(dst, a, n);
    memmove(dst + n, b, m + 1);
    return 0;
}

void rand_bytes(unsigned char *p, long n)
{
    long got;
    int fd;

    got = -1;
    fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        got = read_full(fd, p, n);
        close(fd);
    }
    if (got != n) {
        warn("%s", "can't read /dev/urandom", 0);
        exit(1);
    }
}

static void rand_name(char *s)
{
    static const char hex[] = "0123456789abcdef";
    unsigned char b[6];
    int i;

    rand_bytes(b, sizeof b);
    for (i = 0; i < 6; i++) {
        s[2 * i] = hex[b[i] >> 4];
        s[2 * i + 1] = hex[b[i] & 15];
    }
    s[12] = 0;
}

/* split path into its directory and its last part */
static int split(const char *path, char *dir, char *base)
{
    const char *slash;
    long n;

    slash = strrchr(path, '/');
    if (!slash) {
        strcpy(dir, ".");
        return join(base, path, "");
    }
    n = slash - path;
    if (n == 0)
        n = 1;
    memcpy(dir, path, n);
    dir[n] = 0;
    return join(base, slash + 1, "");
}

static int same_file(const char *a, const char *b)
{
    struct stat sa, sb;
    int r;

    r = stat(a, &sa);
    if (r == 0)
        r = stat(b, &sb);
    if (r < 0)
        return 0;
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

static void sync_dir(const char *dir)
{
    int fd;

    fd = open(dir, O_RDONLY);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
}

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

static int is_ram(const char *path)
{
    struct statfs sf;
    int r;

    r = statfs(path, &sf);
    if (r < 0)
        return 0;
    return (long)sf.f_type == TMPFS_MAGIC_NO ||
        (long)sf.f_type == RAMFS_MAGIC_NO;
}

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

static int cmd_list(void)
{
    char base[PATH_SIZE], from[PATH_SIZE], path[PATH_SIZE];
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
        printf("%s -> %s%s\n", path, from, r < 0 ? " (gone)" : "");
        n++;
    }
    if (d)
        closedir(d);
    if (n == 0)
        printf("nothing is unsealed in RAM\n");
    return 0;
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
        warn("%s: a symbolic link; seal the directory it points to",
            dir, 0);
        return 1;
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
    if (!opt_list) {
        if (opt_dest)
            r = join(dest, opt_dest, "");
        else
            r = ram_base(dest, 1);
        if (r < 0) {
            warn("%s", in_ram ? "found no RAM directory (tmpfs) to "
                "unseal into; -d DIR unseals onto the disk" :
                "the path is too long", 0);
            close(fd);
            return 1;
        }
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

    if (opt_list) {
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
    if (in_ram && !opt_quiet)
        printf("in RAM, gone at shutdown; put it back with: seal %s\n",
            final);
    return 0;
}

/* -------------------------------------------------------------- main */

static void usage(void)
{
    fprintf(stderr,
        "usage: seal [-kqs] [-o FILE] DIR   DIR -> DIR.seal, then DIR "
        "goes\n"
        "       seal -l                     what is unsealed in RAM\n"
        "       unseal [-kqsf] [-d DIR] FILE  FILE -> RAM (-d: into "
        "DIR)\n"
        "       unseal -t [-qsf] FILE       check FILE, list it\n"
        "  -k keep DIR (seal) or FILE (unseal -d)  -q quiet\n"
        "  -s password from standard input  -f go on when it is "
        "damaged\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *s, *arg;
    int i, j, unseal, r, c;

    s = strrchr(argv[0], '/');
    s = s ? s + 1 : argv[0];
    unseal = !strcmp(s, "unseal");
    prog = unseal ? "unseal" : "seal";
    arg = 0;
    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-' || !argv[i][1]) {
            if (arg)
                usage();
            arg = argv[i];
            continue;
        }
        if (!strcmp(argv[i], "--")) {
            if (arg || i + 2 != argc)
                usage();
            arg = argv[i + 1];
            break;
        }
        for (j = 1; argv[i][j]; j++) {
            switch (argv[i][j]) {
            case 'k': opt_keep = 1; break;
            case 'q': opt_quiet = 1; break;
            case 's': opt_stdin = 1; break;
            case 'f': opt_force = 1; break;
            case 'l': opt_list = 1; break;
            case 't': opt_list = 1; break;
            case 'u':
                unseal = 1;
                prog = "unseal";
                break;
            case 'o':
            case 'd':
                c = argv[i][j];
                if (argv[i][j + 1]) {
                    s = argv[i] + j + 1;
                } else if (i + 1 < argc) {
                    i++;
                    s = argv[i];
                } else {
                    usage();
                }
                if (c == 'o')
                    opt_out = s;
                else
                    opt_dest = s;
                goto next;
            default:
                usage();
            }
        }
next:
        ;
    }
    setup();
    if (!unseal && opt_list) {
        if (arg)
            usage();
        return cmd_list();
    }
    if (!arg)
        usage();
    if (unseal)
        r = cmd_unseal(arg);
    else
        r = cmd_seal(arg);
    finish();
    return r;
}
