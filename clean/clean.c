/* clean: takes metadata out of files (see README).
 *
 *   clean [-n] [-k] [-q] FILE...    clean each file where it is
 *   clean -u FILE...                put the newest backup back
 *
 * The file is replaced by its clean version; the original is kept in
 * ~/.system/state/clean (list: that directory's file "list"). */
#define _XOPEN_SOURCE 500
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "clean.h"

#define MAX_MEMORY (256L * 1024 * 1024) /* files read into memory */
#define FIXED_TIME 946684800L           /* 2000-01-01 00:00:00 UTC */
#define MAX_NOTES 40
#define NOTE_SIZE 120

int dry_run;
int randomized;
const char *why;

static int opt_dry, opt_keep, opt_quiet, opt_random, opt_nobackup;
static char note_text[MAX_NOTES][NOTE_SIZE];
static int note_count[MAX_NOTES];
static int notes;
static char backup_dir[4096];
static int backup_seq;

/* ------------------------------------------------------------ notes */

static void copy_str(char *d, const char *s, int size)
{
    int i;

    i = 0;
    while (s[i] && i < size - 1) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
}

void note(const char *what)
{
    int i;

    for (i = 0; i < notes; i++) {
        if (!strcmp(note_text[i], what)) {
            note_count[i]++;
            return;
        }
    }
    if (notes == MAX_NOTES)
        return;
    copy_str(note_text[notes], what, NOTE_SIZE);
    note_count[notes] = 1;
    notes++;
}

void note2(const char *what, const char *detail)
{
    char s[NOTE_SIZE];
    int n;

    copy_str(s, what, NOTE_SIZE);
    n = strlen(s);
    if (detail && detail[0] && n < NOTE_SIZE - 4) {
        s[n] = ' ';
        s[n + 1] = '(';
        copy_str(s + n + 2, detail, NOTE_SIZE - n - 3);
        n = strlen(s);
        s[n] = ')';
        s[n + 1] = 0;
    }
    note(s);
}

/* notes_saved: a copy of the notes, to compare with */
static char saved_text[MAX_NOTES][NOTE_SIZE];
static int saved_count[MAX_NOTES];
static int saved;

static void save_notes(void)
{
    memcpy(saved_text, note_text, sizeof note_text);
    memcpy(saved_count, note_count, sizeof note_count);
    saved = notes;
}

static void restore_notes(void)
{
    memcpy(note_text, saved_text, sizeof note_text);
    memcpy(note_count, saved_count, sizeof note_count);
    notes = saved;
}

static int same_notes(void)
{
    int i;

    if (notes != saved)
        return 0;
    for (i = 0; i < notes; i++) {
        if (strcmp(note_text[i], saved_text[i]) ||
            note_count[i] != saved_count[i])
        {
            return 0;
        }
    }
    return 1;
}

static void print_notes(const char *path, const char *verb)
{
    int i;

    if (opt_quiet)
        return;
    printf("%s:", path);
    if (notes)
        printf(" %s ", verb);
    for (i = 0; i < notes; i++) {
        printf("%s%s", i ? ", " : "", note_text[i]);
        if (note_count[i] > 1)
            printf(" x%d", note_count[i]);
    }
    if (randomized)
        printf("%s new random bytes (a hash of its own)", notes ? ";" : "");
    printf("\n");
}

/* ------------------------------------------------------------ bytes */

void buf_put(struct buf *b, const unsigned char *s, long n)
{
    unsigned char *p;
    long cap;

    if (n <= 0)
        return;
    if (b->n + n > b->cap) {
        cap = b->cap ? b->cap : 4096;
        while (cap < b->n + n)
            cap *= 2;
        p = malloc(cap);
        if (!p) {
            fprintf(stderr, "clean: out of memory\n");
            exit(1);
        }
        if (b->n)
            memcpy(p, b->p, b->n);
        free(b->p);
        b->p = p;
        b->cap = cap;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
}

void buf_str(struct buf *b, const char *s)
{
    buf_put(b, (const unsigned char *)s, strlen(s));
}

void buf_byte(struct buf *b, int c)
{
    unsigned char x;

    x = (unsigned char)c;
    buf_put(b, &x, 1);
}

void buf_num(struct buf *b, long long v)
{
    char s[24];
    int i;

    i = 23;
    s[i] = 0;
    if (v < 0) {
        buf_byte(b, '-');
        v = -v;
    }
    do {
        i--;
        s[i] = (char)('0' + v % 10);
        v /= 10;
    } while (v > 0);
    buf_str(b, s + i);
}

void buf_free(struct buf *b)
{
    free(b->p);
    b->p = 0;
    b->n = 0;
    b->cap = 0;
}

int is_word(const unsigned char *p, const char *w)
{
    return !memcmp(p, w, strlen(w));
}

long get16be(const unsigned char *p) { return (long)p[0] << 8 | p[1]; }
long get16le(const unsigned char *p) { return (long)p[1] << 8 | p[0]; }

long get32be(const unsigned char *p)
{
    return (long)p[0] << 24 | (long)p[1] << 16 | (long)p[2] << 8 | p[3];
}

long get32le(const unsigned char *p)
{
    return (long)p[3] << 24 | (long)p[2] << 16 | (long)p[1] << 8 | p[0];
}

void put32be(unsigned char *p, long v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

void put32le(unsigned char *p, long v)
{
    p[3] = (unsigned char)(v >> 24);
    p[2] = (unsigned char)(v >> 16);
    p[1] = (unsigned char)(v >> 8);
    p[0] = (unsigned char)v;
}

unsigned long crc32_png(const unsigned char *p, long n, unsigned long crc)
{
    long i;
    int k;

    crc = ~crc & 0xffffffffUL;
    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (k = 0; k < 8; k++)
            crc = crc & 1 ? (crc >> 1) ^ 0xedb88320UL : crc >> 1;
    }
    return ~crc & 0xffffffffUL;
}

/* ------------------------------------------------------------ files */

int at_read(int fd, long long off, unsigned char *p, long n)
{
    long got;
    long r;

    got = 0;
    while (got < n) {
        r = pread(fd, p + got, n - got, (off_t)(off + got));
        if (r <= 0)
            return -1;
        got += r;
    }
    return 0;
}

int at_write(int fd, long long off, const unsigned char *p, long n)
{
    long done;
    long r;

    if (dry_run)
        return 0;
    done = 0;
    while (done < n) {
        r = pwrite(fd, p + done, n - done, (off_t)(off + done));
        if (r <= 0) {
            why = "could not write";
            return -1;
        }
        done += r;
    }
    return 0;
}

int at_fill(int fd, long long off, long long n, int c)
{
    int rc;
    static unsigned char b[65536];
    long k;

    memset(b, c, sizeof b);
    while (n > 0) {
        k = n > (long long)sizeof b ? (long)sizeof b : (long)n;
        rc = at_write(fd, off, b, k);
        if (rc < 0)
            return -1;
        off += k;
        n -= k;
    }
    return 0;
}

int at_zero(int fd, long long off, long long n)
{
    return at_fill(fd, off, n, 0);
}

static int write_all(int fd, const unsigned char *p, long n)
{
    long done;
    long r;

    done = 0;
    while (done < n) {
        r = write(fd, p + done, n - done);
        if (r <= 0)
            return -1;
        done += r;
    }
    return 0;
}

static int read_file(int fd, long long size, struct buf *b)
{
    long r;

    b->n = 0;
    b->cap = (long)size + 1;
    b->p = malloc(b->cap);
    if (!b->p)
        return -1;
    while (b->n < (long)size) {
        r = read(fd, b->p + b->n, (long)size - b->n);
        if (r <= 0)
            return -1;
        b->n += r;
    }
    return 0;
}

/* copy_file: from to to (made new, mode 600); -1 on error */
static int copy_file(const char *from, const char *to, int excl)
{
    int rc;
    static unsigned char block[65536];
    int in, out, flags;
    long r;
    int bad;

    in = open(from, O_RDONLY);
    if (in < 0)
        return -1;
    flags = O_WRONLY | O_CREAT | (excl ? O_EXCL : O_TRUNC);
    out = open(to, flags, 0600);
    if (out < 0) {
        close(in);
        return -1;
    }
    bad = 0;
    while ((r = read(in, block, sizeof block)) > 0) {
        rc = write_all(out, block, r);
        if (rc < 0) {
            bad = 1;
            break;
        }
    }
    if (r < 0)
        bad = 1;
    close(in);
    r = close(out);
    if (r < 0)
        bad = 1;
    return bad ? -1 : 0;
}

static void join(char *d, int size, const char *a, const char *b)
{
    copy_str(d, a, size);
    copy_str(d + strlen(d), b, size - strlen(d));
}

/* mkdirs: each directory of path, mode 700 */
static int mkdirs(const char *path)
{
    char p[4096];
    int i, r;

    copy_str(p, path, sizeof p);
    for (i = 1; p[i]; i++) {
        if (p[i] != '/')
            continue;
        p[i] = 0;
        r = mkdir(p, 0700);
        if (r < 0 && errno != EEXIST)
            return -1;
        p[i] = '/';
    }
    r = mkdir(p, 0700);
    if (r < 0 && errno != EEXIST)
        return -1;
    return 0;
}

static void set_backup_dir(void)
{
    const char *s;

    s = getenv("XDG_STATE_HOME");
    if (s && s[0] == '/') {
        join(backup_dir, sizeof backup_dir, s, "/clean");
        return;
    }
    s = getenv("HOME");
    join(backup_dir, sizeof backup_dir, s ? s : "/tmp",
        "/.system/state/clean");
}

/* abs_path: path from the root (the current directory before it) */
static int abs_path(const char *path, char *d, int size)
{
    char cwd[4096];
    char *r;

    if (path[0] == '/') {
        copy_str(d, path, size);
        return 0;
    }
    r = getcwd(cwd, sizeof cwd);
    if (!r)
        return -1;
    join(d, size, cwd, "/");
    copy_str(d + strlen(d), path, size - strlen(d));
    return 0;
}

/* stamp: YYYYMMDD-HHMMSS (UTC) into s (16 bytes) */
static void stamp(char *s)
{
    struct timeval tv;
    long long t, days, y, m, d, era, doe, yoe, doy, mp;
    long sec;

    gettimeofday(&tv, 0);
    t = tv.tv_sec;
    days = t / 86400;
    sec = (long)(t % 86400);
    /* days since 1970-01-01 to a date (Howard Hinnant's method) */
    days += 719468;
    era = days / 146097;
    doe = days - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2)
        y++;
    sprintf(s, "%04d%02d%02d-%02d%02d%02d", (int)y, (int)m, (int)d,
        (int)(sec / 3600), (int)(sec / 60 % 60), (int)(sec % 60));
}

/* backup: copy path into the backup directory and add it to the list */
static int backup(const char *path, const char *abs)
{
    int rc;
    char name[64], file[4200], list[4200];
    char st[20];
    int fd, r;

    rc = mkdirs(backup_dir);
    if (rc < 0) {
        why = "can't make the backup directory";
        return -1;
    }
    stamp(st);
    for (;;) {
        backup_seq++;
        sprintf(name, "%s-%d", st, backup_seq);
        join(file, sizeof file, backup_dir, "/");
        copy_str(file + strlen(file), name, sizeof file - strlen(file));
        r = copy_file(path, file, 1);
        if (r == 0)
            break;
        if (errno != EEXIST || backup_seq > 100000) {
            why = "can't make the backup";
            return -1;
        }
    }
    join(list, sizeof list, backup_dir, "/list");
    fd = open(list, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) {
        why = "can't write the backup list";
        return -1;
    }
    r = write_all(fd, (const unsigned char *)name, strlen(name));
    if (r == 0)
        r = write_all(fd, (const unsigned char *)" ", 1);
    if (r == 0)
        r = write_all(fd, (const unsigned char *)abs, strlen(abs));
    if (r == 0)
        r = write_all(fd, (const unsigned char *)"\n", 1);
    close(fd);
    if (r < 0) {
        why = "can't write the backup list";
        return -1;
    }
    return 0;
}

static void set_times(const char *path, const struct stat *st)
{
    struct timeval tv[2];

    if (opt_keep) {
        tv[0].tv_sec = st->st_atime;
        tv[1].tv_sec = st->st_mtime;
    } else {
        tv[0].tv_sec = FIXED_TIME;
        tv[1].tv_sec = FIXED_TIME;
    }
    tv[0].tv_usec = 0;
    tv[1].tv_usec = 0;
    utimes(path, tv);
}

/* replace: path's content becomes p (through a new file and rename) */
static int replace(const char *path, const unsigned char *p, long n,
    int mode)
{
    int rc;
    char tmp[4200];
    int fd, r;

    join(tmp, sizeof tmp, path, ".clean-new");
    fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, mode & 07777);
    if (fd < 0) {
        why = "can't write next to the file";
        return -1;
    }
    r = write_all(fd, p, n);
    rc = close(fd);
    if (rc < 0)
        r = -1;
    if (r == 0)
        r = rename(tmp, path);
    if (r < 0) {
        unlink(tmp);
        why = "can't write the clean file";
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------ kinds */

static long id3_size(const unsigned char *h)
{
    long n;

    n = (long)(h[6] & 127) << 21 | (long)(h[7] & 127) << 14 |
        (long)(h[8] & 127) << 7 | (h[9] & 127);
    n += 10;
    if (h[5] & 0x10)
        n += 10;
    return n;
}

static int kind_of(int fd, long long size)
{
    static unsigned char x[4096];
    int rc;
    unsigned char h[64], f[4];
    long n;

    memset(h, 0, sizeof h);
    n = size < (long long)sizeof h ? (long)size : (long)sizeof h;
    rc = -1;
    if (n >= 12)
        rc = at_read(fd, 0, h, n);
    if (rc < 0) {
        why = "too short to be anything clean knows";
        return k_unknown;
    }
    if (h[0] == 0xff && h[1] == 0xd8 && h[2] == 0xff)
        return k_jpeg;
    if (!memcmp(h, "\211PNG\r\n\032\n", 8))
        return k_png;
    if (is_word(h, "GIF87a") || is_word(h, "GIF89a"))
        return k_gif;
    if (is_word(h, "RIFF") && is_word(h + 8, "WEBP"))
        return k_webp;
    if (is_word(h, "RIFF") &&
        (is_word(h + 8, "WAVE") || is_word(h + 8, "AVI ")))
    {
        return k_riff;
    }
    if (is_word(h, "%PDF-"))
        return k_pdf;
    if (is_word(h, "fLaC"))
        return k_flac;
    if (is_word(h, "OggS"))
        return k_ogg;
    if (is_word(h, "ID3")) {
        n = id3_size(h);
        rc = -1;
        if (n + 4 <= size)
            rc = at_read(fd, n, f, 4);
        return rc == 0 && is_word(f, "fLaC") ? k_flac : k_mp3;
    }
    if (h[0] == 0xff && (h[1] & 0xe0) == 0xe0)
        return k_mp3;
    if (h[0] == 0x1a && h[1] == 0x45 && h[2] == 0xdf && h[3] == 0xa3)
        return k_mkv;
    if (is_word(h + 4, "ftyp")) {
        if (is_word(h + 8, "heic") || is_word(h + 8, "heix") ||
            is_word(h + 8, "mif1") || is_word(h + 8, "msf1") ||
            is_word(h + 8, "avif") || is_word(h + 8, "avis") ||
            is_word(h + 8, "hevc") || is_word(h + 8, "avci"))
        {
            return k_heif;
        }
        return k_mp4;
    }
    if (is_word(h + 4, "moov") || is_word(h + 4, "mdat") ||
        is_word(h + 4, "wide") || is_word(h + 4, "free"))
    {
        return k_mp4;
    }
    if (is_word(h, "FORM") &&
        (is_word(h + 8, "AIFF") || is_word(h + 8, "AIFC")))
    {
        return k_aiff;
    }
    if (!memcmp(h, "\060\046\262\165\216\146\317\021", 8))
        return k_asf;
    /* TIFF; ORF (Olympus) is IIRO, IIRS or MMOR, RW2 (Panasonic) IIU */
    if (is_word(h, "II*") || !memcmp(h, "MM\000*", 4) || is_word(h, "IIRO") ||
        is_word(h, "IIRS") || is_word(h, "MMOR") || is_word(h, "IIU"))
    {
        return k_tiff;
    }
    if (h[0] == 'B' && h[1] == 'M' && (get32le(h + 14) == 40 ||
        get32le(h + 14) == 52 || get32le(h + 14) == 56 ||
        get32le(h + 14) == 108 || get32le(h + 14) == 124))
    {
        return k_bmp;
    }
    n = size < (long long)sizeof x ? (long)size : (long)sizeof x;
    rc = at_read(fd, 0, x, n);
    if (rc == 0 && is_svg(x, n))
        return k_svg;
    if (is_word(h, "PK\003\004") || is_word(h, "PK\005\006"))
        return k_zip;
    if (h[0] == 0x1f && h[1] == 0x8b && h[2] == 8)
        return k_gz;
    if (rc == 0 && is_tar(x, n))
        return k_tar;
    why = "not a kind of file clean knows";
    return k_unknown;
}

/* ------------------------------------------------------------ one file */

static int run_in_memory(int kind, struct buf *in, struct buf *out)
{
    switch (kind) {
    case k_jpeg: return clean_jpeg(in->p, in->n, out);
    case k_png: return clean_png(in->p, in->n, out);
    case k_gif: return clean_gif(in->p, in->n, out);
    case k_webp: return clean_webp(in->p, in->n, out);
    case k_svg: return clean_svg(in->p, in->n, out);
    case k_bmp: return clean_bmp(in->p, in->n, out);
    case k_zip: return clean_zip(in->p, in->n, out);
    case k_gz: return clean_gz(in->p, in->n, out);
    default: return clean_pdf(in->p, in->n, out);
    }
}

/* Something was taken out when cleaning the clean file again says less
 * than cleaning the file did (a clean JPEG still has an EXIF with its
 * rotation, so "EXIF" alone means nothing). */
static int clean_in_memory(const char *path, const char *abs, int fd,
    struct stat *st, int kind)
{
    int rc;
    struct buf in, out, again;
    int r, changed;

    memset(&in, 0, sizeof in);
    memset(&out, 0, sizeof out);
    memset(&again, 0, sizeof again);
    if (st->st_size > MAX_MEMORY) {
        why = "too big (more than 256 MB)";
        return -1;
    }
    rc = read_file(fd, st->st_size, &in);
    if (rc < 0) {
        why = "can't read it";
        buf_free(&in);
        return -1;
    }
    r = run_in_memory(kind, &in, &out);
    changed = 0;
    if (r == 0 && notes) {
        save_notes();
        notes = 0;
        rc = run_in_memory(kind, &out, &again);
        if (rc == 0 && !same_notes())
            changed = 1;
        restore_notes();
        buf_free(&again);
    }
    if (!changed)
        notes = 0;
    if (r == 0 && (changed || opt_random) && !opt_dry)
        rand_mem(kind, &out);
    if (r == 0 && (notes || randomized) && !opt_dry) {
        if (!opt_nobackup)
            r = backup(path, abs);
        if (r == 0)
            r = replace(path, out.p, out.n, st->st_mode);
        if (r == 0)
            set_times(path, st);
    }
    buf_free(&in);
    buf_free(&out);
    return r;
}

static int run_in_place(int fd, long long size, int kind)
{
    switch (kind) {
    case k_mp4: return clean_mp4(fd, size);
    case k_mkv: return clean_mkv(fd, size);
    case k_mp3: return clean_mp3(fd, size);
    case k_flac: return clean_flac(fd, size);
    case k_heif: return clean_heif(fd, size);
    case k_ogg: return clean_ogg(fd, size);
    case k_aiff: return clean_aiff(fd, size);
    case k_asf: return clean_asf(fd, size);
    case k_tiff: return clean_tiff(fd, size);
    case k_tar: return clean_tar(fd, size);
    default: return clean_riff(fd, size);
    }
}

/* first only look; then, if there is something, back it up and do it */
static int clean_in_place(const char *path, const char *abs, int fd,
    struct stat *st, int kind)
{
    int rc;
    struct stat now;
    int r, wfd, found;

    dry_run = 1;
    r = run_in_place(fd, st->st_size, kind);
    dry_run = 0;
    found = notes;
    if (r < 0 || opt_dry || (!found && !opt_random))
        return r;
    if (!opt_nobackup) {
        r = backup(path, abs);
        if (r < 0)
            return -1;
    }
    wfd = open(path, O_RDWR);
    if (wfd < 0) {
        why = "can't open it for writing";
        return -1;
    }
    notes = 0;
    r = 0;
    if (found)
        r = run_in_place(wfd, st->st_size, kind);
    if (r == 0) {
        rc = fstat(wfd, &now);
        if (rc == 0) {
            rc = rand_place(kind, wfd, now.st_size);
            if (rc < 0)
                r = -1;
        }
    }
    rc = close(wfd);
    if (rc < 0 && r == 0) {
        why = "could not write";
        r = -1;
    }
    if (r < 0)
        fprintf(stderr, "clean: %s: half done; the original is in %s\n",
            path, backup_dir);
    set_times(path, st);
    return r;
}

static int clean_file(const char *path)
{
    int rc;
    struct stat st;
    char abs[4096];
    int fd, kind, r;

    notes = 0;
    randomized = 0;
    why = 0;
    r = lstat(path, &st);
    if (r < 0) {
        fprintf(stderr, "clean: %s: not found\n", path);
        return -1;
    }
    if (S_ISLNK(st.st_mode)) {
        fprintf(stderr, "clean: %s: a link: clean the file it points to\n",
            path);
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "clean: %s: not a file\n", path);
        return -1;
    }
    rc = -1;
    if (!strchr(path, '\n'))
        rc = abs_path(path, abs, sizeof abs);
    if (rc < 0) {
        fprintf(stderr, "clean: %s: can't use this name\n", path);
        return -1;
    }
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "clean: %s: can't read it\n", path);
        return -1;
    }
    kind = kind_of(fd, st.st_size);
    if (kind == k_unknown)
        r = -1;
    else if (kind == k_mp4 || kind == k_mkv || kind == k_mp3 ||
        kind == k_flac || kind == k_riff || kind == k_heif ||
        kind == k_ogg || kind == k_aiff || kind == k_asf || kind == k_tiff ||
        kind == k_tar)
    {
        r = clean_in_place(path, abs, fd, &st, kind);
    } else {
        r = clean_in_memory(path, abs, fd, &st, kind);
    }
    close(fd);
    if (r < 0) {
        fprintf(stderr, "clean: %s: %s (left as it was)\n", path,
            why ? why : "broken");
        return -1;
    }
    if (!notes && !randomized) {
        if (!opt_quiet)
            printf("%s: nothing to remove\n", path);
    } else {
        print_notes(path, opt_dry ? "would remove" : "removed");
    }
    return 0;
}

/* undo: the newest backup of path back in its place */
static int undo_file(const char *path)
{
    int rc;
    char abs[4096], list[4200], from[4200], name[64], tmp[4200];
    struct stat now;
    struct buf b;
    long i, start, sp;
    int fd, r;

    memset(&b, 0, sizeof b);
    name[0] = 0;
    rc = abs_path(path, abs, sizeof abs);
    if (rc < 0)
        return -1;
    join(list, sizeof list, backup_dir, "/list");
    fd = open(list, O_RDONLY);
    if (fd >= 0) {
        struct stat st;

        r = fstat(fd, &st);
        if (r == 0 && st.st_size < MAX_MEMORY)
            read_file(fd, st.st_size, &b);
        close(fd);
    }
    start = 0;
    for (i = 0; i < b.n; i++) {
        if (b.p[i] != '\n')
            continue;
        sp = start;
        while (sp < i && b.p[sp] != ' ')
            sp++;
        if (sp < i && sp - start < (long)sizeof name &&
            i - sp - 1 == (long)strlen(abs) &&
            !memcmp(b.p + sp + 1, abs, i - sp - 1))
        {
            memcpy(name, b.p + start, sp - start);
            name[sp - start] = 0;
        }
        start = i + 1;
    }
    buf_free(&b);
    if (!name[0]) {
        fprintf(stderr, "clean: %s: no backup of it\n", path);
        return -1;
    }
    join(from, sizeof from, backup_dir, "/");
    copy_str(from + strlen(from), name, sizeof from - strlen(from));
    join(tmp, sizeof tmp, path, ".clean-new");
    r = copy_file(from, tmp, 1);
    rc = -1;
    if (r == 0)
        rc = stat(path, &now);
    if (rc == 0)
        chmod(tmp, now.st_mode & 07777);
    if (r == 0)
        r = rename(tmp, path);
    if (r < 0) {
        unlink(tmp);
        fprintf(stderr, "clean: %s: could not put %s back\n", path, from);
        return -1;
    }
    if (!opt_quiet)
        printf("%s: put back from %s\n", path, from);
    return 0;
}

static void usage(void)
{
    fprintf(stderr,
        "usage: clean [-n] [-k] [-q] [-r] [-b] FILE...\n"
        "       clean -u FILE...\n"
        "Takes the metadata out of each file, in place.\n"
        "Pictures: JPEG, PNG, GIF, WebP, HEIF/AVIF, TIFF, camera raw,\n"
        "SVG, BMP. Documents: PDF, DOCX/XLSX/PPTX, ODT/ODS/ODP, EPUB,\n"
        "zip, tar, gzip. Video and audio: MP4/MOV/M4A, MKV/WebM, AVI,\n");
    fprintf(stderr,
        "WMV/WMA, MP3, AAC, FLAC, Ogg (Vorbis, Opus, Theora), WAV, AIFF.\n");
    fprintf(stderr,
        "The original goes to ~/.system/state/clean first.\n"
        "  -n  only say what would go; change nothing\n"
        "  -k  keep the file's times (else: 2000-01-01 00:00 UTC)\n"
        "  -q  say nothing but errors\n"
        "  -r  new random bytes, a new hash, even when nothing else\n"
        "      goes (each copy you share then differs)\n"
        "  -b  no backup (then -u can't undo it)\n"
        "  -u  undo: put the newest backup of each file back\n");
    exit(2);
}

int main(int argc, char **argv)
{
    int i, j, undo, bad, r;

    undo = 0;
    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "--")) {
            i++;
            break;
        }
        for (j = 1; argv[i][j]; j++) {
            switch (argv[i][j]) {
            case 'n': opt_dry = 1; break;
            case 'k': opt_keep = 1; break;
            case 'q': opt_quiet = 1; break;
            case 'r': opt_random = 1; break;
            case 'b': opt_nobackup = 1; break;
            case 'u': undo = 1; break;
            default: usage();
            }
        }
    }
    if (i >= argc)
        usage();
    set_backup_dir();
    bad = 0;
    for (; i < argc; i++) {
        r = undo ? undo_file(argv[i]) : clean_file(argv[i]);
        if (r)
            bad = 1;
    }
    return bad;
}
