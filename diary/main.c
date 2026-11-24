/* diary: a private diary in one encrypted file (see README).
 *
 *   diary              open ~/.diary, the screen, save and wipe at q
 *   diary -n           make a new diary
 *   diary -c CMD       run CMD in the open diary instead of the screen
 *   diary -f FILE      another diary file
 *   diary -s           the password from standard input (scripts) */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "crypto.h"
#include "seal.h"
#include "diary.h"
#include "data.h"
#include "ui.h"

const char *prog = "diary";
struct secret *sec;
int opt_force;
int opt_quiet;
int opt_verbose;
int opt_dry;
volatile int stop_now;

static char cfgbuf[16384];
static char editor_found[PATH_SIZE];

/* --------------------------------------------------------- config */

/* is name a program in PATH (or a path to one)? its path into out */
static int in_path(const char *name, char *out)
{
    char dir[PATH_SIZE];
    const char *p, *e;
    long n;
    int r;

    if (strchr(name, '/')) {
        r = access(name, X_OK);
        if (r == 0)
            sprintf(out, "%.4000s", name);
        return r == 0;
    }
    p = getenv("PATH");
    if (!p)
        p = "/bin:/usr/bin";
    while (*p) {
        e = strchr(p, ':');
        n = e ? e - p : (long)strlen(p);
        if (n > 0 && n < PATH_SIZE - 300) {
            memcpy(dir, p, n);
            sprintf(dir + n, "/%.255s", name);
            r = access(dir, X_OK);
            if (r == 0) {
                strcpy(out, dir);
                return 1;
            }
        }
        p += n;
        if (*p == ':')
            p++;
    }
    return 0;
}

/* split s (changed in place) into words */
static int words(char *s, char **w, int max)
{
    int n;

    n = 0;
    while (*s && n < max - 1) {
        while (*s == ' ' || *s == '\t')
            s++;
        if (!*s)
            break;
        w[n] = s;
        n++;
        while (*s && *s != ' ' && *s != '\t')
            s++;
        if (*s) {
            *s = 0;
            s++;
        }
    }
    w[n] = 0;
    return n;
}

static void add_viewer(const char *exts, char *rest)
{
    int i, k;

    if (cfg.nviewers >= VIEWERS_MAX)
        return;
    i = cfg.nviewers;
    sprintf(cfg.viewer_ext[i], ",%.190s,", exts);
    for (k = 0; cfg.viewer_ext[i][k]; k++)
        cfg.viewer_ext[i][k] = (char)lower((unsigned char)
            cfg.viewer_ext[i][k]);
    cfg.nviewer_args[i] = words(rest, cfg.viewer[i], ARGS_MAX);
    if (cfg.nviewer_args[i] > 0)
        cfg.nviewers++;
}

static void expand_home(char *dst, const char *s)
{
    const char *home;

    home = getenv("HOME");
    if (s[0] == '~' && (s[1] == '/' || s[1] == 0) && home)
        sprintf(dst, "%.2000s%.2000s", home, s + 1);
    else
        sprintf(dst, "%.4000s", s);
}

/* ~/.system/config/diary/config: "key value" lines, # comments */
static void read_config(void)
{
    static char *defviewers[] = {
        "jpg,jpeg,png,gif,webp,bmp,tif,tiff,svg,heic,avif", "feh",
        "pdf,epub,djvu,cbz,mobi", "sioyek",
        "mp4,mkv,webm,mov,avi,mp3,ogg,opus,flac,wav,m4a,aac", "mpv",
        0
    };
    static char vbuf[3][32];
    static char edbuf[256];
    char path[PATH_SIZE], *line, *val, *p, *w[1];
    const char *base;
    long n;
    int i, have_viewers, have_editor;

    cfg.private_ed = 1;
    cfg.utf8 = 1;
    cfg.clean = "clean";
    have_viewers = 0;
    have_editor = 0;
    base = getenv("XDG_CONFIG_HOME");
    if (base && base[0] == '/')
        sprintf(path, "%.3000s/diary/config", base);
    else if (getenv("HOME"))
        sprintf(path, "%.3000s/.config/diary/config", getenv("HOME"));
    else
        path[0] = 0;
    n = path[0] ? read_file(path, cfgbuf, sizeof cfgbuf - 1) : -1;
    line = n > 0 ? cfgbuf : 0;
    while (line && *line) {
        p = line;
        while (*p && *p != '\n')
            p++;
        if (*p) {
            *p = 0;
            p++;
        }
        while (*line == ' ' || *line == '\t')
            line++;
        val = line;
        while (*val && *val != ' ' && *val != '\t')
            val++;
        if (*val) {
            *val = 0;
            val++;
        }
        while (*val == ' ' || *val == '\t')
            val++;
        n = strlen(val);
        while (n > 0 && (val[n - 1] == ' ' || val[n - 1] == '\r'))
            n--;
        val[n] = 0;
        if (line[0] == '#' || line[0] == 0) {
            line = p;
            continue;
        }
        if (!strcmp(line, "file") && val[0]) {
            expand_home(vault_file, val);
        } else if (!strcmp(line, "editor") && val[0]) {
            sprintf(edbuf, "%.250s", val);
            have_editor = 1;
        } else if (!strcmp(line, "viewer") && val[0]) {
            /* viewer jpg,png feh -Z: the endings, then the program */
            w[0] = val;
            while (*val && *val != ' ' && *val != '\t')
                val++;
            if (*val) {
                *val = 0;
                val++;
                add_viewer(w[0], val);
                have_viewers = 1;
            }
        } else if (!strcmp(line, "clean") && val[0]) {
            cfg.clean = val;
        } else if (!strcmp(line, "shell") && val[0]) {
            sprintf(cfg.shell, "%.250s", val);
        } else if (!strcmp(line, "private")) {
            cfg.private_ed = strcmp(val, "no") != 0;
        } else if (!strcmp(line, "utf8")) {
            cfg.utf8 = strcmp(val, "no") != 0;
        } else {
            warn("%s: unknown setting %s (left out)", path, line);
        }
        line = p;
    }
    if (!have_viewers) {
        for (i = 0; defviewers[i]; i += 2) {
            strcpy(vbuf[i / 2], defviewers[i + 1]);
            add_viewer(defviewers[i], vbuf[i / 2]);
        }
    }
    /* the editor: the one set, if it is there; else nvim, vim, vi */
    if (have_editor) {
        cfg.neditor = words(edbuf, cfg.editor, ARGS_MAX);
        if (cfg.neditor > 0 && in_path(cfg.editor[0], editor_found))
            return;
        warn("%s: not found; trying nvim, vim, vi", cfg.neditor > 0 ?
            cfg.editor[0] : "editor", 0);
    }
    cfg.neditor = 1;
    if (in_path("nvim", editor_found))
        cfg.editor[0] = "nvim";
    else if (in_path("vim", editor_found))
        cfg.editor[0] = "vim";
    else
        cfg.editor[0] = "vi";
}

/* ----------------------------------------------------------- main */

static void help(FILE *f)
{
    fprintf(f,
        "usage: diary [-ns] [-f FILE] [-c CMD]\n"
        "  (no options)  open ~/.diary: password, the screen; q saves,\n"
        "                wipes the copy in RAM and quits\n"
        "  -n       make a new diary (password twice)\n"
        "  -f FILE  this diary file instead of ~/.diary (or the config's)\n"
        "  -c CMD   run CMD (with sh) in the open diary instead of the\n"
        "           screen, then save and wipe: diary -c ksh is a shell\n"
        "  -s       the password from standard input, for scripts\n"
        "  -h       help\n"
        "settings: ~/.system/config/diary/config; see the README\n");
}

static int run_cmd(const char *cmd)
{
    struct sigaction sa, oldint;
    pid_t pid;
    int status, r;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, &oldint);
    pid = fork();
    if (pid == 0) {
        r = chdir(tree_dir);
        if (r < 0)
            _exit(127);
        signal(SIGINT, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGHUP, SIG_DFL);
        execl("/bin/sh", "sh", "-c", cmd, (char *)0);
        _exit(127);
    }
    status = 0;
    if (pid > 0)
        r = (int)waitpid(pid, &status, 0);
    sigaction(SIGINT, &oldint, 0);
    if (pid < 0)
        return 1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

/* what was said on stderr while the screen was up */
static void show_errors(int fd)
{
    char path[PATH_SIZE];
    static char buf[65536];
    long n;

    sprintf(path, "%.3000s/errors", ram_base_dir);
    n = read_file(path, buf, sizeof buf - 1);
    if (n > 0)
        write_all(fd, buf, n);
    wipe_tree(path);
}

int main(int argc, char **argv)
{
    const char *cmd, *home, *file;
    int i, make_new, pass_stdin, r, rc, saved_err;

    make_new = 0;
    pass_stdin = 0;
    cmd = 0;
    file = 0;
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "--")) {
            i++;
            break;
        }
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            help(stdout);
            return 0;
        } else if (!strcmp(argv[i], "-n")) {
            make_new = 1;
        } else if (!strcmp(argv[i], "-s")) {
            pass_stdin = 1;
        } else if (!strcmp(argv[i], "-f") && i + 1 < argc) {
            i++;
            file = argv[i];
        } else if (!strcmp(argv[i], "-c") && i + 1 < argc) {
            i++;
            cmd = argv[i];
        } else {
            fprintf(stderr, "diary: %s: unknown option (diary -h)\n",
                argv[i]);
            return 2;
        }
    }
    if (i < argc) {
        fprintf(stderr, "diary: %s: options only (diary -h)\n", argv[i]);
        return 2;
    }
    home = getenv("HOME");
    if (!home || home[0] != '/') {
        fprintf(stderr, "diary: HOME is not set\n");
        return 1;
    }
    sprintf(vault_file, "%.3000s/.diary", home);
    read_config();
    if (file)
        sprintf(vault_file, "%.4000s", file);
    if (strlen(vault_file) > 3000) {
        fprintf(stderr, "diary: the file's path is too long\n");
        return 1;
    }
    if (!cmd && (!isatty(0) || !isatty(1))) {
        fprintf(stderr, "diary: the screen needs a terminal (diary -c "
            "CMD runs a command instead)\n");
        return 2;
    }
    r = vault_open(make_new, pass_stdin);
    if (r == 2)
        return 0;
    if (r != 0)
        return 1;
    data_untxt();
    if (cmd) {
        rc = run_cmd(cmd);
    } else {
        saved_err = dup(2);
        rc = ui_run();
        if (saved_err >= 0) {
            dup2(saved_err, 2);
            close(saved_err);
        }
        show_errors(2);
    }
    r = vault_close();
    if (r < 0)
        return 1;
    if (got_signal)
        return 1;
    return rc;
}
