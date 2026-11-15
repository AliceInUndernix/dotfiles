/*
 * vpnauth: run /usr/local/bin/vpn as root after asking the password of
 * the user who runs it (not root's). Part of net/ in the dotfiles.
 *
 * Installed setuid root, mode 4750, group vpnctl, so only members of
 * vpnctl can run it. It keeps nothing of the caller's environment,
 * allows only vpn's own commands with short plain arguments, and
 * reads the password from the terminal, three tries, two seconds
 * after each wrong one.
 *
 *   vpnauth [vpn command] [argument]...
 *
 * vpn runs it by itself when a command needs root.
 */

/* setgroups(), nanosleep() and crypt() are not in C89 headers */
#define _DEFAULT_SOURCE

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <grp.h>
#include <time.h>
#include <string.h>
#include <crypt.h>

#define VPN_PATH "/usr/local/bin/vpn"
#define FILE_MAX 65536
#define FIELD_MAX 512
#define PASS_MAX 256
#define TRIES_MAX 3
#define ARGS_MAX 3
#define ARG_LEN_MAX 32

static const char *allowed_cmds[] = {
    "level", "default", "retry", "refresh", "lan", "firewall-off",
    "firewall-on", "status", "report", "log", "off", "on", "selftest", 0
};

static char file_buf[FILE_MAX];

static void put_err(const char *s)
{
    int r;

    r = write(2, s, strlen(s));
    (void)r;
}

static int read_file(const char *path)
{
    int fd, n, got;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    got = 0;
    for (;;) {
        if (got >= FILE_MAX - 1)
            break;
        n = read(fd, file_buf + got, FILE_MAX - 1 - got);
        if (n <= 0)
            break;
        got += n;
    }
    close(fd);
    file_buf[got] = 0;
    return got;
}

/*
 * In file_buf (lines of fields split by ':'), find the line whose field
 * number key_field is key and copy its field number want_field to out.
 */
static int find_entry(int key_field, const char *key, int want_field,
    char *out)
{
    const char *line, *p, *end;
    int field, len, found;
    char key_buf[FIELD_MAX], want_buf[FIELD_MAX];

    line = file_buf;
    while (*line) {
        end = strchr(line, '\n');
        if (!end)
            end = line + strlen(line);
        key_buf[0] = 0;
        want_buf[0] = 0;
        field = 0;
        p = line;
        while (p < end) {
            len = 0;
            while (p + len < end && p[len] != ':')
                len++;
            if (len < FIELD_MAX) {
                if (field == key_field) {
                    memcpy(key_buf, p, len);
                    key_buf[len] = 0;
                }
                if (field == want_field) {
                    memcpy(want_buf, p, len);
                    want_buf[len] = 0;
                }
            }
            field++;
            p += len + 1;
        }
        found = strcmp(key_buf, key) == 0;
        if (found && field > want_field) {
            strcpy(out, want_buf);
            return 0;
        }
        if (!*end)
            break;
        line = end + 1;
    }
    return -1;
}

static void wait_secs(int n)
{
    struct timespec ts;

    ts.tv_sec = n;
    ts.tv_nsec = 0;
    nanosleep(&ts, 0);
}

static void wipe(char *s, int n)
{
    volatile char *v;
    int i;

    v = s;
    for (i = 0; i < n; i++)
        v[i] = 0;
}

/* Read a line from the terminal without echo. -1: aborted or no tty. */
static int read_pass(int tty, char *pw)
{
    struct termios old, quiet;
    int n, r;
    char ch;

    r = tcgetattr(tty, &old);
    if (r < 0)
        return -1;
    quiet = old;
    quiet.c_lflag &= ~(ECHO | ISIG);
    quiet.c_lflag |= ECHONL;
    tcsetattr(tty, TCSAFLUSH, &quiet);
    r = write(tty, "your password: ", 15);
    n = 0;
    for (;;) {
        r = read(tty, &ch, 1);
        if (r <= 0 || ch == '\n' || ch == '\r')
            break;
        if (ch == 3 || ch == 4) {       /* Ctrl+C, Ctrl+D */
            r = -1;
            break;
        }
        if (n < PASS_MAX - 1) {
            pw[n] = ch;
            n++;
        }
    }
    pw[n] = 0;
    tcsetattr(tty, TCSAFLUSH, &old);
    if (r < 0) {
        r = write(tty, "\n", 1);
        return -1;
    }
    return n;
}

static int plain_arg(const char *s)
{
    int i;

    for (i = 0; s[i]; i++) {
        if (i >= ARG_LEN_MAX)
            return 0;
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9')
            || s[i] == '-'))
        {
            return 0;
        }
    }
    return i > 0;
}

static int args_ok(int argc, char **argv)
{
    int i, known;

    if (argc == 1)
        return 1;               /* the menu */
    if (argc > ARGS_MAX + 1)
        return 0;
    for (i = 1; i < argc; i++) {
        if (!plain_arg(argv[i]))
            return 0;
    }
    known = 0;
    for (i = 0; allowed_cmds[i]; i++) {
        if (strcmp(argv[1], allowed_cmds[i]) == 0)
            known = 1;
    }
    return known;
}

int main(int argc, char **argv)
{
    char name[FIELD_MAX], uid_str[24], hash[FIELD_MAX], pw[PASS_MAX];
    char *new_argv[ARGS_MAX + 2], *new_env[3];
    const char *got;
    int tty, tries, i, r, ok;
    long long uid;

    if (!args_ok(argc, argv)) {
        put_err("vpnauth: only vpn's own commands (vpn help)\n");
        return 2;
    }
    uid = getuid();
    i = 22;
    uid_str[23] = 0;
    do {
        uid_str[i] = '0' + (char)(uid % 10);
        uid = uid / 10;
        i--;
    } while (uid > 0 && i >= 0);
    r = read_file("/etc/passwd");
    if (r >= 0)
        r = find_entry(2, uid_str + i + 1, 0, name);
    if (r < 0) {
        put_err("vpnauth: you are not in /etc/passwd\n");
        return 1;
    }
    tty = open("/dev/tty", O_RDWR);
    if (tty < 0) {
        put_err("vpnauth: needs a terminal for the password\n");
        return 1;
    }
    ok = 0;
    for (tries = 0; tries < TRIES_MAX; tries++) {
        r = read_pass(tty, pw);
        if (r < 0)
            break;
        /* read /etc/shadow again each time and keep it no longer */
        r = read_file("/etc/shadow");
        hash[0] = 0;
        if (r >= 0)
            find_entry(0, name, 1, hash);
        wipe(file_buf, FILE_MAX);
        if (hash[0] && hash[0] != '!' && hash[0] != '*') {
            got = crypt(pw, hash);
            ok = got && strcmp(got, hash) == 0;
        }
        wipe(pw, PASS_MAX);
        wipe(hash, FIELD_MAX);
        if (ok)
            break;
        wait_secs(2);
        put_err("vpnauth: wrong password\n");
    }
    close(tty);
    if (!ok)
        return 1;

    r = setgroups(0, 0);
    if (r == 0)
        r = setgid(0);
    if (r == 0)
        r = setuid(0);
    if (r != 0) {
        put_err("vpnauth: could not become root (is it setuid?)\n");
        return 1;
    }
    new_argv[0] = "vpn";
    for (i = 1; i < argc; i++)
        new_argv[i] = argv[i];
    new_argv[argc] = 0;
    new_env[0] = "PATH=/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
    new_env[1] = "HOME=/root";
    new_env[2] = 0;
    execve(VPN_PATH, new_argv, new_env);
    put_err("vpnauth: could not run " VPN_PATH "\n");
    return 1;
}
