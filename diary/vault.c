/* vault.c: the diary file and its copy in RAM.
 *
 * ~/.diary is an ordinary sealed file (see ../seal/README): unseal
 * opens it too. diary unpacks it into <tmpfs>/diary/diary, keeps the
 * password in locked memory while it is open (every save needs a new
 * salt, so a new key), and on each save writes a hidden file next to
 * ~/.diary, reads it back and compares, keeps the old one as
 * ~/.diary.old and only then puts the new one in its place. Closing
 * overwrites every file in RAM with zeros and removes it. */
#define _XOPEN_SOURCE 700
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "crypto.h"
#include "seal.h"
#include "diary.h"

char vault_file[PATH_SIZE];
char ram_base_dir[PATH_SIZE];
char tree_dir[PATH_SIZE];
char saved_at[16];
volatile int got_signal;

static long pass_len;
static unsigned long long saved_sig;
static int tty_fd = -1;
static int lock_fd = -1;
static struct termios tty_saved;
static volatile int tty_changed;
static volatile long sink;

/* ------------------------------------------------------- signals */

static void on_signal(int sig)
{
    if (tty_changed) {
        tcsetattr(tty_fd, TCSAFLUSH, &tty_saved);
        sink = write(tty_fd, "\n", 1);
        tty_changed = 0;
    }
    got_signal = sig;
}

/* restart = 0 makes a signal break a blocking read(), so Ctrl-C at
   the password prompt really stops instead of reading on */
static void catch_signals(int restart)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sa.sa_flags = restart ? SA_RESTART : 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, 0);
    sigaction(SIGHUP, &sa, 0);
    sigaction(SIGINT, &sa, 0);
}

static void setup(void)
{
    struct sigaction sa;
    struct rlimit rl;

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
    mlock(sec, sizeof *sec);

    catch_signals(1);
    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, 0);
}

/* ------------------------------------------------------ password */

static long read_line(int fd, char *buf, long size)
{
    long n, k;
    char c;

    n = 0;
    for (;;) {
        k = read(fd, &c, 1);
        if (k < 0 && errno == EINTR && !got_signal)
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
    catch_signals(0);
    n = -1;
    if (!got_signal)
        n = read_line(tty_fd, buf, sizeof sec->pass);
    catch_signals(1);
    if (tty_changed)
        tcsetattr(tty_fd, TCSAFLUSH, &tty_saved);
    tty_changed = 0;
    if (got_signal)
        return -1;
    return n;
}

/* the password into sec->pass; twice for a new diary */
static int get_pass(int twice, int from_stdin)
{
    long n, m;

    if (from_stdin) {
        n = read_line(0, sec->pass, sizeof sec->pass);
        m = n;
    } else {
        tty_fd = open("/dev/tty", O_RDWR | O_NOCTTY);
        if (tty_fd < 0) {
            warn("%s", "no terminal to ask the password on", 0);
            return -1;
        }
        n = ask("password for the diary: ", sec->pass);
        m = n;
        if (n > 0 && twice && !got_signal)
            m = ask("again: ", sec->pass2);
        close(tty_fd);
        tty_fd = -1;
        if (got_signal)
            return -1;
    }
    if (n > 0 && twice && !from_stdin &&
        (m != n || memcmp(sec->pass, sec->pass2, n)))
    {
        warn("%s", "the passwords differ; nothing was done", 0);
        crypto_wipe(sec->pass, sizeof sec->pass);
        crypto_wipe(sec->pass2, sizeof sec->pass2);
        return -1;
    }
    crypto_wipe(sec->pass2, sizeof sec->pass2);
    if (n < 0) {
        warn("%s", "can't read the password (at most 1023 bytes)", 0);
        return -1;
    }
    if (n == 0) {
        warn("%s", "no password given; nothing was done", 0);
        return -1;
    }
    pass_len = n;
    return 0;
}

/* the key for salt into sec->key; the password stays */
static int derive(const unsigned char *salt)
{
    void *work;

    work = malloc(KDF_BLOCKS * 1024L);
    if (!work) {
        warn("%s", "out of memory (it needs 32 MiB)", 0);
        return -1;
    }
    argon2id(sec->key, KEY_SIZE, work, KDF_BLOCKS, KDF_PASSES,
        (const u8 *)sec->pass, (u32)pass_len, salt, SALT_SIZE);
    crypto_wipe(work, KDF_BLOCKS * 1024L);
    free(work);
    return 0;
}

/* ------------------------------------------------- the RAM place */

static int find_base(void)
{
    char cand[3][PATH_SIZE];
    struct stat st;
    const char *s;
    long uid;
    int i, r;

    uid = (long)getuid();
    s = getenv("XDG_RUNTIME_DIR");
    cand[0][0] = 0;
    if (s && s[0] == '/' && strlen(s) < PATH_SIZE - 32)
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
            sprintf(ram_base_dir, "%.3000s/diary", cand[i]);
        else
            sprintf(ram_base_dir, "/dev/shm/diary-%ld", uid);
        mkdir(ram_base_dir, 0700);
        r = lstat(ram_base_dir, &st);
        if (r < 0 || !S_ISDIR(st.st_mode) || (long)st.st_uid != uid)
            continue;
        if ((st.st_mode & 0777) != 0700)
            chmod(ram_base_dir, 0700);
        sprintf(tree_dir, "%.3000s/diary", ram_base_dir);
        return 0;
    }
    warn("%s", "found no RAM directory (tmpfs) to open the diary in", 0);
    return -1;
}

static long read_pid(void)
{
    char path[PATH_SIZE], buf[32];
    long n, pid;
    int fd, i;

    sprintf(path, "%.3000s/pid", ram_base_dir);
    fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0)
        return 0;
    n = read_full(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0)
        return 0;
    buf[n] = 0;
    pid = 0;
    for (i = 0; buf[i] >= '0' && buf[i] <= '9'; i++)
        pid = pid * 10 + (buf[i] - '0');
    return pid;
}

static int write_pid(void)
{
    char path[PATH_SIZE], buf[32];
    int fd, r;

    sprintf(path, "%.3000s/pid", ram_base_dir);
    unlink(path);
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) {
        warn("can't make %s: %s", path, strerror(errno));
        return -1;
    }
    sprintf(buf, "%ld\n", (long)getpid());
    r = write_all(fd, buf, strlen(buf));
    close(fd);
    return r;
}

static void remove_pid(void)
{
    char path[PATH_SIZE];

    sprintf(path, "%.3000s/pid", ram_base_dir);
    unlink(path);
}

/* hold <ram>/lock while the diary is open: a second diary can't take
 * it, and it goes by itself when this one ends, even when killed (a
 * pid alone could be some other program's after a crash) */
static int take_lock(void)
{
    char path[PATH_SIZE];
    int r;

    sprintf(path, "%.3000s/lock", ram_base_dir);
    lock_fd = open(path, O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
    if (lock_fd < 0) {
        warn("can't make %s: %s", path, strerror(errno));
        return -1;
    }
    fcntl(lock_fd, F_SETFD, FD_CLOEXEC);
    r = flock(lock_fd, LOCK_EX | LOCK_NB);
    if (r < 0) {
        close(lock_fd);
        lock_fd = -1;
        return 1;
    }
    return 0;
}

static void drop_lock(void)
{
    char path[PATH_SIZE];

    if (lock_fd < 0)
        return;
    sprintf(path, "%.3000s/lock", ram_base_dir);
    unlink(path);
    close(lock_fd);
    lock_fd = -1;
}

/* ------------------------------------------------------- wiping */

static char wp[PATH_SIZE];
static char zeros[65536];

static void wipe_file(const char *path, long long size)
{
    long long left;
    long k;
    int fd, r;

    fd = open(path, O_WRONLY | O_NOFOLLOW);
    if (fd < 0)
        return;
    left = size;
    r = 0;
    while (left > 0 && r == 0) {
        k = left < (long long)sizeof zeros ? (long)left :
            (long)sizeof zeros;
        r = write_all(fd, zeros, k);
        left -= k;
    }
    fsync(fd);
    close(fd);
}

static int wipe_rec(long len)
{
    struct stat st;
    struct dirent *e;
    DIR *d;
    long n;
    int r, bad;

    r = lstat(wp, &st);
    if (r < 0)
        return errno == ENOENT ? 0 : -1;
    if (!S_ISDIR(st.st_mode)) {
        if (S_ISREG(st.st_mode)) {
            chmod(wp, 0600);
            wipe_file(wp, (long long)st.st_size);
        }
        return unlink(wp);
    }
    chmod(wp, 0700);
    d = opendir(wp);
    if (!d)
        return -1;
    bad = 0;
    while ((e = readdir(d)) != 0) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        n = strlen(e->d_name);
        if (len + 1 + n >= PATH_SIZE) {
            bad = 1;
            continue;
        }
        wp[len] = '/';
        memcpy(wp + len + 1, e->d_name, n + 1);
        r = wipe_rec(len + 1 + n);
        wp[len] = 0;
        if (r < 0)
            bad = 1;
    }
    closedir(d);
    r = rmdir(wp);
    return bad || r < 0 ? -1 : 0;
}

int wipe_tree(const char *path)
{
    long n;

    n = strlen(path);
    if (n >= PATH_SIZE)
        return -1;
    memcpy(wp, path, n + 1);
    return wipe_rec(n);
}

/* ---------------------------------------------- what has changed */

static unsigned long long sig_h;

static void mix(unsigned long long v)
{
    int i;

    for (i = 0; i < 8; i++) {
        sig_h ^= (v >> (8 * i)) & 255;
        sig_h *= 1099511628211ULL;
    }
}

static void sig_rec(long len)
{
    struct stat st;
    struct dirent *e;
    DIR *d;
    long n, i;
    int r;

    r = lstat(wp, &st);
    if (r < 0)
        return;
    mix((unsigned long long)st.st_ino);
    mix((unsigned long long)st.st_size);
    mix((unsigned long long)st.st_mode);
    mix((unsigned long long)st.st_mtim.tv_sec);
    mix((unsigned long long)st.st_mtim.tv_nsec);
    for (i = 0; i < len; i++)
        mix((unsigned long long)(unsigned char)wp[i]);
    if (!S_ISDIR(st.st_mode))
        return;
    d = opendir(wp);
    if (!d)
        return;
    while ((e = readdir(d)) != 0) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        n = strlen(e->d_name);
        if (len + 1 + n >= PATH_SIZE)
            continue;
        wp[len] = '/';
        memcpy(wp + len + 1, e->d_name, n + 1);
        sig_rec(len + 1 + n);
        wp[len] = 0;
    }
    closedir(d);
}

/* a number that changes when anything in the tree changes; the order
 * of the entries doesn't matter, as each is mixed on its own */
static unsigned long long tree_sig(void)
{
    unsigned long long sum;

    strcpy(wp, tree_dir);
    sig_h = 14695981039346656037ULL;
    sum = 0;
    sig_rec(strlen(wp));
    sum = sig_h;
    return sum;
}

int vault_dirty(void)
{
    return tree_sig() != saved_sig;
}

/* -------------------------------------------------------- saving */

int vault_save(void)
{
    char dir[PATH_SIZE], base[PATH_SIZE], tmp[PATH_SIZE];
    char old[PATH_SIZE], oldtmp[PATH_SIZE], rnd[16];
    unsigned char salt[SALT_SIZE];
    unsigned long long sig;
    struct counts c;
    struct stat st;
    struct tm *tm;
    long long now;
    time_t tt;
    int fd, r;

    memset(&c, 0, sizeof c);
    sig = tree_sig();
    split(vault_file, dir, base);
    rand_name(rnd);
    r = join(tmp, dir, "/.");
    if (r == 0)
        r = join(tmp, tmp, base);
    if (r == 0)
        r = join(tmp, tmp, ".");
    if (r == 0)
        r = join(tmp, tmp, rnd);
    if (r == 0)
        r = join(old, vault_file, ".old");
    if (r == 0)
        r = join(oldtmp, tmp, ".old");
    if (r < 0) {
        warn("%s: the path is too long", vault_file, 0);
        return -1;
    }
    rand_bytes(salt, SALT_SIZE);
    r = derive(salt);
    if (r < 0)
        return -1;
    fd = open(tmp, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) {
        warn("can't make %s: %s", tmp, strerror(errno));
        return -1;
    }
    r = fstat(fd, &st);
    if (r == 0)
        r = write_all(fd, salt, SALT_SIZE);
    if (r < 0) {
        warn("can't write %s: %s", tmp, strerror(errno));
        goto failed;
    }
    r = pack(fd, tree_dir, "diary", (long)st.st_dev, (long)st.st_ino, &c);
    if (r < 0)
        goto failed;
    r = fsync(fd);
    if (r < 0) {
        warn("can't write %s: %s", tmp, strerror(errno));
        goto failed;
    }
    r = pack_verify(fd, tree_dir, &c);
    if (r < 0)
        goto failed;
    r = close(fd);
    fd = -1;
    if (r < 0) {
        warn("can't write %s: %s", tmp, strerror(errno));
        goto failed;
    }
    /* the old one becomes .old, by a new name for it first, so that
     * vault_file itself is never missing */
    r = link(vault_file, oldtmp);
    if (r == 0) {
        r = rename(oldtmp, old);
        if (r < 0)
            unlink(oldtmp);
    }
    r = rename(tmp, vault_file);
    if (r < 0) {
        warn("can't make %s: %s", vault_file, strerror(errno));
        goto failed;
    }
    sync_dir(dir);
    crypto_wipe(sec->key, sizeof sec->key);
    saved_sig = sig;
    tt = time(0);
    now = (long long)tt;
    tm = localtime(&tt);
    if (tm)
        sprintf(saved_at, "%02d:%02d", tm->tm_hour, tm->tm_min);
    else
        sprintf(saved_at, "%lld", now);
    return 0;
failed:
    if (fd >= 0)
        close(fd);
    unlink(tmp);
    crypto_wipe(sec->key, sizeof sec->key);
    warn("not saved: %s is as it was", vault_file, 0);
    return -1;
}

/* ------------------------------------------------------- opening */

/* is the password right for vault_file? 0 yes, -1 no (said) */
static int check_pass(void)
{
    unsigned char salt[SALT_SIZE];
    char name[256];
    long n;
    int fd, r;

    fd = open(vault_file, O_RDONLY);
    if (fd < 0) {
        warn("%s: %s", vault_file, strerror(errno));
        return -1;
    }
    n = read_full(fd, salt, SALT_SIZE);
    r = n == SALT_SIZE ? derive(salt) : -1;
    if (r == 0)
        r = unpack_open(fd, name);
    close(fd);
    crypto_wipe(sec->key, sizeof sec->key);
    return r;
}

static int unpack_file(void)
{
    unsigned char salt[SALT_SIZE];
    char name[256], tmp[PATH_SIZE], inner[PATH_SIZE], rnd[16];
    struct counts c;
    struct stat st;
    long n;
    int fd, r;

    memset(&c, 0, sizeof c);
    fd = open(vault_file, O_RDONLY);
    if (fd < 0) {
        warn("%s: %s", vault_file, strerror(errno));
        return -1;
    }
    r = fstat(fd, &st);
    n = read_full(fd, salt, SALT_SIZE);
    if (r < 0 || !S_ISREG(st.st_mode) || n != SALT_SIZE) {
        warn("%s: not a diary (too short)", vault_file, 0);
        close(fd);
        return -1;
    }
    r = derive(salt);
    if (r == 0)
        r = unpack_open(fd, name);
    if (r < 0) {
        crypto_wipe(sec->key, sizeof sec->key);
        close(fd);
        return -1;
    }
    rand_name(rnd);
    sprintf(tmp, "%.3000s/.unpack-%s", ram_base_dir, rnd);
    r = mkdir(tmp, 0700);
    if (r < 0) {
        warn("can't make %s: %s", tmp, strerror(errno));
        crypto_wipe(sec->key, sizeof sec->key);
        close(fd);
        return -1;
    }
    r = unpack_rest(tmp, 0, &c);
    close(fd);
    crypto_wipe(sec->key, sizeof sec->key);
    if (r != 0) {
        wipe_tree(tmp);
        warn("%s: it is damaged; nothing was opened (unseal -f %s "
            "gets out what it can)", vault_file, vault_file);
        return -1;
    }
    r = join(inner, tmp, "/");
    if (r == 0)
        r = join(inner, inner, name);
    if (r == 0)
        r = rename(inner, tree_dir);
    if (r < 0) {
        warn("can't make %s: %s", tree_dir, strerror(errno));
        wipe_tree(tmp);
        return -1;
    }
    rmdir(tmp);
    return 0;
}

static int make_tree(void)
{
    static const char *dirs[] = { "", "/days", "/people", "/places" };
    char path[PATH_SIZE];
    int i, r;

    for (i = 0; i < 4; i++) {
        sprintf(path, "%.3000s%s", tree_dir, dirs[i]);
        r = mkdir(path, 0700);
        if (r < 0 && errno != EEXIST) {
            warn("can't make %s: %s", path, strerror(errno));
            return -1;
        }
    }
    return 0;
}

/* leftovers of an unpack or a save that never finished */
static void clean_leftovers(void)
{
    char path[PATH_SIZE], dir[PATH_SIZE], base[PATH_SIZE];
    struct dirent *e;
    DIR *d;
    long n;

    d = opendir(ram_base_dir);
    while (d && (e = readdir(d)) != 0) {
        if (!strncmp(e->d_name, ".unpack-", 8)) {
            sprintf(path, "%.3000s/%.200s", ram_base_dir, e->d_name);
            wipe_tree(path);
        }
    }
    /* the editor's undo files and views of a diary that didn't close */
    sprintf(path, "%.3000s/editor", ram_base_dir);
    wipe_tree(path);
    if (d)
        closedir(d);
    split(vault_file, dir, base);
    n = strlen(base);
    d = opendir(dir);
    while (d && (e = readdir(d)) != 0) {
        if (e->d_name[0] == '.' && !strncmp(e->d_name + 1, base, n) &&
            e->d_name[n + 1] == '.' && strlen(e->d_name + n + 2) >= 12 &&
            strcmp(e->d_name + n + 1, ".old"))
        {
            sprintf(path, "%.3000s/%.255s", dir, e->d_name);
            unlink(path);
        }
    }
    if (d)
        closedir(d);
}

static int ask_key(const char *question, const char *keys)
{
    char buf[16];
    long n;
    int fd;

    fd = open("/dev/tty", O_RDWR | O_NOCTTY);
    if (fd < 0)
        return 'q';
    write_all(fd, question, strlen(question));
    n = read_line(fd, buf, sizeof buf);
    close(fd);
    if (n < 1 || !strchr(keys, buf[0]))
        return 'q';
    return buf[0];
}

/* a diary left in RAM by a diary that didn't close (killed, or its
 * terminal gone): open it, save it, or wipe it */
static int recover(int pass_stdin, int *opened)
{
    char q[2 * PATH_SIZE + 400], when[64];
    struct stat st;
    struct tm *tm;
    time_t tt;
    int k, r, have_file;

    *opened = 0;
    r = stat(tree_dir, &st);
    if (r < 0)
        return 0;
    tt = st.st_mtime;
    tm = localtime(&tt);
    when[0] = 0;
    if (tm)
        sprintf(when, " (last change %04d-%02d-%02d %02d:%02d)",
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min);
    if (pass_stdin) {
        warn("a diary that wasn't closed is in %s; run diary on a "
            "terminal to open, save or wipe it", tree_dir, 0);
        return -1;
    }
    sprintf(q, "A diary that wasn't closed is still in RAM%s:\n  %s\n"
        "o: open it (and save it as usual)\n"
        "s: save it into %s and close it\n"
        "w: wipe it (what changed since the last save is lost)\n"
        "q: quit, leave it as it is\n> ", when, tree_dir, vault_file);
    k = ask_key(q, "oswq");
    if (k == 'q')
        return -1;
    if (k == 'w') {
        r = wipe_tree(tree_dir);
        if (r < 0) {
            warn("can't wipe all of %s", tree_dir, 0);
            return -1;
        }
        printf("wiped\n");
        return 0;
    }
    have_file = stat(vault_file, &st) == 0;
    r = get_pass(!have_file, 0);
    if (r == 0 && have_file) {
        r = check_pass();
        if (r < 0)
            warn("%s", "wrong password for this diary", 0);
    }
    if (r < 0)
        return -1;
    r = write_pid();
    if (r < 0)
        return -1;
    saved_sig = 0;
    if (k == 'o') {
        *opened = 1;
        return 0;
    }
    r = vault_save();
    if (r < 0)
        return -1;
    printf("saved into %s\n", vault_file);
    r = vault_close();
    return r < 0 ? -1 : 2;
}

static int open_vault(int make_new, int pass_stdin);

/* 0 open, 1 not (said), 2 done already (a recovered diary saved) */
int vault_open(int make_new, int pass_stdin)
{
    int r;

    r = open_vault(make_new, pass_stdin);
    if (r == 1 && lock_fd >= 0) {
        drop_lock();
        rmdir(ram_base_dir);
    }
    return r;
}

static int open_vault(int make_new, int pass_stdin)
{
    struct stat st;
    long pid;
    int r, opened;

    setup();
    r = find_base();
    if (r < 0)
        return 1;
    r = take_lock();
    if (r != 0) {
        pid = read_pid();
        if (r > 0)
            warn("%s", "the diary is open already; close it there first",
                0);
        if (r > 0 && pid > 0)
            fprintf(stderr, "  (process %ld)\n", pid);
        return 1;
    }
    clean_leftovers();
    r = recover(pass_stdin, &opened);
    if (r < 0)
        return 1;
    if (r == 2)
        return 2;
    if (opened)
        return 0;
    r = stat(vault_file, &st);
    if (make_new && r == 0) {
        warn("%s: there is a diary already", vault_file, 0);
        return 1;
    }
    if (!make_new && r < 0) {
        warn("%s: %s (diary -n makes a new diary)", vault_file,
            strerror(errno));
        return 1;
    }
    r = get_pass(make_new, pass_stdin);
    if (r < 0)
        return 1;
    r = write_pid();
    if (r < 0)
        return 1;
    if (make_new) {
        r = make_tree();
        if (r == 0)
            r = vault_save();
        if (r < 0) {
            wipe_tree(tree_dir);
            remove_pid();
            return 1;
        }
        return 0;
    }
    r = unpack_file();
    if (r < 0) {
        warn("%s: wrong password, or it is damaged; nothing was opened",
            vault_file, 0);
        remove_pid();
        return 1;
    }
    make_tree();
    saved_sig = tree_sig();
    return 0;
}

int vault_close(void)
{
    char path[PATH_SIZE];
    int r;

    if (vault_dirty()) {
        r = vault_save();
        if (r < 0) {
            warn("the diary is still in RAM, in %s: run diary again "
                "to save it", tree_dir, 0);
            return -1;
        }
    }
    r = wipe_tree(tree_dir);
    sprintf(path, "%.3000s/editor", ram_base_dir);
    if (wipe_tree(path) < 0)
        r = -1;
    remove_pid();
    drop_lock();
    rmdir(ram_base_dir);
    crypto_wipe(sec, sizeof *sec);
    if (r < 0) {
        warn("saved, but not all of %s could be wiped", tree_dir, 0);
        return -1;
    }
    return 0;
}
