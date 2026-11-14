/* --serve: the archive page on http://127.0.0.1:PORT/ with a form to add
 * websites; one process, one request at a time, no threads */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "webarchiver.h"

#define REQUEST_MAX 200000

static volatile int serve_stop;
static int run_pid;

static void on_signal(int sig)
{
    (void)sig;
    serve_stop = 1;
}

static int send_all(int fd, const char *data, int len)
{
    int n;

    while (len > 0) {
        n = write(fd, data, len);
        if (n <= 0)
            return 0;
        data += n;
        len -= n;
    }
    return 1;
}

static void reply(int fd, int code, const char *type, const char *body,
    int len, const char *location)
{
    struct buf h;

    buf_init(&h);
    buf_add(&h, "HTTP/1.0 ");
    buf_addll(&h, code);
    buf_add(&h, code == 200 ? " OK" : code == 303 ? " See Other" :
        " Not Found");
    buf_add(&h, "\r\nContent-Type: ");
    buf_add(&h, type);
    buf_add(&h, "\r\nContent-Length: ");
    buf_addll(&h, len);
    buf_add(&h, "\r\nCache-Control: no-store\r\n");
    if (location) {
        buf_add(&h, "Location: ");
        buf_add(&h, location);
        buf_add(&h, "\r\n");
    }
    buf_add(&h, "Connection: close\r\n\r\n");
    send_all(fd, h.s, h.len);
    buf_free(&h);
    if (len)
        send_all(fd, body, len);
}

static void reply_html(int fd, struct buf *b)
{
    reply(fd, 200, "text/html; charset=utf-8", b->s, b->len, 0);
}

static void not_found(int fd)
{
    static const char msg[] = "not found\n";

    reply(fd, 404, "text/plain", msg, sizeof(msg) - 1, 0);
}

static const char *content_type(const char *path)
{
    static const char *map[] = {
        ".html", "text/html", ".htm", "text/html", ".css", "text/css",
        ".js", "application/javascript", ".png", "image/png",
        ".jpg", "image/jpeg", ".jpeg", "image/jpeg", ".gif", "image/gif",
        ".svg", "image/svg+xml", ".ico", "image/x-icon",
        ".webp", "image/webp", ".pdf", "application/pdf",
        ".mp4", "video/mp4", ".webm", "video/webm", ".mp3", "audio/mpeg",
        ".ogg", "audio/ogg", ".txt", "text/plain", ".woff", "font/woff",
        ".woff2", "font/woff2", 0 };
    int i, n, lp;

    lp = strlen(path);
    for (i = 0; map[i]; i += 2) {
        n = strlen(map[i]);
        if (lp >= n && str_ieq_n(path + lp - n, map[i], n))
            return map[i + 1];
    }
    return "application/octet-stream";
}

/* a file of the archive; hidden files (.archive-*) are not served */
static void send_file(int fd, const char *urlpath)
{
    struct buf b;
    struct strlist names;
    char tmp[65536];
    char *raw, *rel, *full, *idx;
    int in, n, i;

    raw = pct_unquote(urlpath);
    rel = norm_path(raw[0] == '/' ? raw + 1 : raw);
    free(raw);
    if (!strcmp(rel, ".") || strstr(rel, "/.") || rel[0] == '.' ||
        starts_with(rel, ".."))
    {
        if (strcmp(rel, ".")) {
            free(rel);
            not_found(fd);
            return;
        }
    }
    full = path2(out_dir, rel);
    if (is_dir(full)) {
        idx = path2(full, "index.html");
        if (is_file(idx)) {
            free(full);
            full = idx;
        } else {
            free(idx);
            buf_init(&b);
            page_head(&b, rel, 0);
            buf_add(&b, "<ul>");
            sl_init(&names);
            list_dir(full, &names);
            for (i = 0; i < names.n; i++) {
                if (names.v[i][0] == '.')
                    continue;
                buf_add(&b, "<li><a href=\"");
                raw = pct_quote(names.v[i], "~-._");
                buf_add(&b, raw);
                free(raw);
                buf_add(&b, "\">");
                buf_addhtml(&b, names.v[i]);
                buf_add(&b, "</a></li>");
            }
            sl_free(&names);
            buf_add(&b, "</ul></body></html>\n");
            reply_html(fd, &b);
            buf_free(&b);
            free(full);
            free(rel);
            return;
        }
    }
    in = open(full, O_RDONLY);
    if (in < 0 || !is_file(full)) {
        if (in >= 0)
            close(in);
        not_found(fd);
    } else {
        reply(fd, 200, content_type(full), "", 0, 0);
        while ((n = read(in, tmp, sizeof(tmp))) > 0) {
            if (!send_all(fd, tmp, n))
                break;
        }
        close(in);
    }
    free(full);
    free(rel);
}

/* ------------------------------------------------------------ forms */

static char *form_value(const char *body, const char *name)
{
    struct buf b;
    const char *p, *e;
    char *res;
    int n;

    n = strlen(name);
    for (p = body; p && *p; p = strchr(p, '&') ? strchr(p, '&') + 1 : 0) {
        if (strncmp(p, name, n) || p[n] != '=')
            continue;
        p += n + 1;
        e = strchr(p, '&');
        if (!e)
            e = p + strlen(p);
        buf_init(&b);
        for (; p < e; p++)
            buf_addc(&b, *p == '+' ? ' ' : *p);
        res = pct_unquote(b.s);
        buf_free(&b);
        return res;
    }
    return xstrdup("");
}

/* example.org, http://example.org:8080/path ... */
static int valid_link(const char *s)
{
    const char *p;

    if (str_ieq_n(s, "http://", 7))
        s += 7;
    else if (str_ieq_n(s, "https://", 8))
        s += 8;
    for (p = s; (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
        (*p >= '0' && *p <= '9') || *p == '.' || *p == '-'; p++)
    {
        ;
    }
    if (p == s)
        return 0;
    if (*p == ':') {
        p++;
        if (*p < '0' || *p > '9')
            return 0;
        while (*p >= '0' && *p <= '9')
            p++;
    }
    if (!*p)
        return 1;
    if (*p != '/')
        return 0;
    for (; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '<' || *p == '>' ||
            *p == '"' || *p == '\'')
        {
            return 0;
        }
    }
    return 1;
}

static void queue_add(const char *line)
{
    FILE *f;
    char *p;

    p = path2(state_dir, "web-queue.txt");
    f = fopen(p, "a");
    if (f) {
        fprintf(f, "%s\n", line);
        fclose(f);
    }
    free(p);
}

/* start the next queued download if nothing is running */
static void start_queued(void)
{
    struct strlist lines;
    struct buf b;
    char *queue, *data, *links, *log;
    char *argv[4];
    char when[32];
    int len, i, all;
    FILE *f;

    if (run_pid > 0 || run_active())
        return;
    queue = path2(state_dir, "web-queue.txt");
    data = read_file(queue, &len);
    if (!data) {
        free(queue);
        return;
    }
    unlink(queue);
    free(queue);
    sl_init(&lines);
    sl_split(&lines, data, '\n');
    free(data);
    if (!lines.n) {
        sl_free(&lines);
        return;
    }
    all = sl_has(&lines, "ALL");
    links = path2(state_dir, "web-links.txt");
    buf_init(&b);
    for (i = 0; i < lines.n; i++) {
        if (strcmp(lines.v[i], "ALL")) {
            buf_add(&b, lines.v[i]);
            buf_addc(&b, '\n');
        }
    }
    write_file(links, b.s, b.len);
    buf_free(&b);
    argv[0] = self_path;
    argv[1] = all ? out_dir : links;
    argv[2] = all ? 0 : out_dir;
    argv[3] = 0;
    log = path2(logs_dir, "web-run.log");
    f = fopen(log, "w");   /* only the newest run is kept */
    if (f) {
        utc_id(now(), when);
        fprintf(f, "\n===== %s UTC  %s\n", when, all ? "update all" :
            "new links");
        fclose(f);
    }
    run_pid = fork();
    if (run_pid == 0) {
        /* own session: Ctrl+C on the server does not stop the run */
        setsid();
        i = open(log, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (i >= 0) {
            dup2(i, 1);
            dup2(i, 2);
            close(i);
        }
        i = open("/dev/null", O_RDONLY);
        if (i >= 0) {
            dup2(i, 0);
            close(i);
        }
        execv(self_path, argv);
        _exit(127);
    }
    free(log);
    free(links);
    sl_free(&lines);
}

static void handle_post(int fd, const char *path, const char *body)
{
    struct buf b, note;
    struct strlist lines;
    char *links, *d, *v, *loc;
    int i, good;

    buf_init(&b);
    buf_init(&note);
    if (!strcmp(path, "/add")) {
        links = form_value(body, "links");
        sl_init(&lines);
        sl_split(&lines, links, '\n');
        good = 0;
        for (i = 0; i < lines.n; i++) {
            v = lines.v[i];
            if (strchr(v, '\r'))
                *strchr(v, '\r') = 0;
            if (!v[0])
                continue;
            if (valid_link(v)) {
                queue_add(v);
                good++;
            } else {
                buf_add(&note, note.len ? ", " : "Ignored (not a web "
                    "address): ");
                buf_addhtml(&note, v);
            }
        }
        if (good) {
            char num[64];

            sprintf(num, "Added %d link(s); the download starts in a few "
                "seconds. ", good);
            buf_add(&b, num);
        }
        buf_add(&b, note.s);
        if (!b.len)
            buf_add(&b, "No links given.");
        buf_free(&note);
        buf_init(&note);
        render_archive(&note, 1, b.s);
        reply_html(fd, &note);
        sl_free(&lines);
        free(links);
    } else if (!strcmp(path, "/update-all")) {
        queue_add("ALL");
        render_archive(&note, 1, "Update of the whole archive queued; it "
            "starts in a few seconds.");
        reply_html(fd, &note);
    } else if (!strcmp(path, "/restore")) {
        d = form_value(body, "domain");
        v = form_value(body, "version");
        sl_init(&lines);
        all_domains(&lines);
        if (sl_has(&lines, d) && v[0] >= '0' && v[0] <= '9')
            cmd_restore(d, v);
        sl_free(&lines);
        loc = pct_quote(d, "~-._");
        free(d);
        d = str2("/history.html#", loc);
        reply(fd, 303, "text/plain", "", 0, d);
        free(loc);
        free(d);
        free(v);
    } else {
        not_found(fd);
    }
    buf_free(&b);
    buf_free(&note);
}

static void run_log_page(int fd)
{
    struct buf b;
    char *p, *data;
    int len, busy;

    p = path2(logs_dir, "web-run.log");
    data = read_file(p, &len);
    free(p);
    busy = run_active() || run_pid > 0;
    buf_init(&b);
    page_head(&b, "Download progress", busy ? 5 : 0);
    buf_add(&b, "<h1>Download progress</h1><p><a href=\"/\">&larr; "
        "archive</a>");
    buf_add(&b, busy ? " &middot; refreshes every 5 seconds" :
        " &middot; finished");
    buf_add(&b, "</p><pre>");
    if (data)
        buf_addhtml(&b, len > 20000 ? data + len - 20000 : data);
    else
        buf_add(&b, "(nothing yet)");
    buf_add(&b, "</pre></body></html>\n");
    reply_html(fd, &b);
    buf_free(&b);
    free(data);
}

/* ------------------------------------------------------------ requests */

static int read_request(int fd, struct buf *req)
{
    struct timeval tv;
    fd_set set;
    char tmp[8192];
    char *end, *cl;
    int n, want;

    want = -1;
    while (req->len < REQUEST_MAX) {
        if (want < 0) {
            end = strstr(req->s, "\r\n\r\n");
            if (end) {
                cl = strstr(req->s, "Content-Length:");
                if (!cl)
                    cl = strstr(req->s, "content-length:");
                want = (end - req->s) + 4 + (cl ? (int)parse_ll(cl + 15) :
                    0);
            }
        }
        if (want >= 0 && req->len >= want)
            return 1;
        FD_ZERO(&set);
        FD_SET(fd, &set);
        tv.tv_sec = 10;
        tv.tv_usec = 0;
        if (select(fd + 1, &set, 0, 0, &tv) <= 0)
            return 0;
        n = read(fd, tmp, sizeof(tmp));
        if (n <= 0)
            return want >= 0;
        buf_addn(req, tmp, n);
    }
    return 0;
}

static void handle(int fd)
{
    struct buf req, page;
    char *line_end, *sp1, *sp2, *path, *body, *q;

    buf_init(&req);
    if (!read_request(fd, &req)) {
        buf_free(&req);
        return;
    }
    line_end = strstr(req.s, "\r\n");
    sp1 = strchr(req.s, ' ');
    sp2 = sp1 ? strchr(sp1 + 1, ' ') : 0;
    if (!line_end || !sp1 || !sp2 || sp2 > line_end) {
        buf_free(&req);
        return;
    }
    path = xstrndup(sp1 + 1, sp2 - sp1 - 1);
    q = strchr(path, '?');
    if (q)
        *q = 0;
    body = strstr(req.s, "\r\n\r\n") + 4;
    buf_init(&page);
    if (starts_with(req.s, "POST ")) {
        handle_post(fd, path, body);
    } else if (!strcmp(path, "/") || !strcmp(path, "/archive.html") ||
        !strcmp(path, "/index.html"))
    {
        render_archive(&page, 1, 0);
        reply_html(fd, &page);
    } else if (!strcmp(path, "/history.html")) {
        render_history(&page, 1);
        reply_html(fd, &page);
    } else if (!strcmp(path, "/run-log")) {
        run_log_page(fd);
    } else {
        send_file(fd, path);
    }
    buf_free(&page);
    free(path);
    buf_free(&req);
}

void cmd_serve(int port)
{
    struct sockaddr_in addr;
    struct timeval tv;
    fd_set set;
    unsigned char *b;
    int fd, conn, one, ws, ready;

    on_signal_do(SIGINT, on_signal);
    on_signal_do(SIGTERM, on_signal);
    on_signal_do(SIGPIPE, SIG_IGN);
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        die("cannot create a socket", 0);
    one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    /* network byte order is big-endian: write the bytes one by one */
    b = (unsigned char *)&addr.sin_port;
    b[0] = (unsigned char)(port >> 8);
    b[1] = (unsigned char)port;
    b = (unsigned char *)&addr.sin_addr;
    b[0] = 127;     /* 127.0.0.1: this computer only */
    b[1] = 0;
    b[2] = 0;
    b[3] = 1;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        die("cannot listen on that port (in use?)", 0);
    listen(fd, 16);
    printf("Archive page: http://127.0.0.1:%d/   (only reachable from "
        "this computer; Ctrl+C to stop)\n", port);
    fflush(stdout);
    write_pages();
    while (!serve_stop) {
        FD_ZERO(&set);
        FD_SET(fd, &set);
        tv.tv_sec = 2;      /* wake up regularly to start queued work */
        tv.tv_usec = 0;
        ready = select(fd + 1, &set, 0, 0, &tv);
        if (ready > 0) {
            conn = accept(fd, 0, 0);
            if (conn >= 0) {
                handle(conn);
                close(conn);
            }
        }
        if (run_pid > 0 && waitpid(run_pid, &ws, WNOHANG) == run_pid)
            run_pid = 0;
        start_queued();
    }
    close(fd);
    printf("\nServer stopped. A download that is still running continues "
        "in the background.\n");
}
