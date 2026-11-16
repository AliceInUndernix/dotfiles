/* clean: takes metadata out of files. See README. */

/* a growing byte buffer */
struct buf {
    unsigned char *p;
    long n;
    long cap;
};

/* clean.c */
extern int dry_run;
void note(const char *what);
void note2(const char *what, const char *detail);
void buf_put(struct buf *b, const unsigned char *s, long n);
void buf_str(struct buf *b, const char *s);
void buf_byte(struct buf *b, int c);
void buf_num(struct buf *b, long long v);
void buf_free(struct buf *b);
int is_word(const unsigned char *p, const char *w);
long get16be(const unsigned char *p);
long get32be(const unsigned char *p);
long get16le(const unsigned char *p);
long get32le(const unsigned char *p);
void put32be(unsigned char *p, long v);
void put32le(unsigned char *p, long v);
unsigned long crc32_png(const unsigned char *p, long n, unsigned long crc);
int at_read(int fd, long long off, unsigned char *p, long n);
int at_write(int fd, long long off, const unsigned char *p, long n);
int at_zero(int fd, long long off, long long n);

/* Cleaners that rewrite the file in memory: 0 when it was read (out
 * holds the clean file, notes say what went), -1 when it is broken or
 * not supported (why is set). */
extern const char *why;
int clean_jpeg(const unsigned char *in, long n, struct buf *out);
int clean_png(const unsigned char *in, long n, struct buf *out);
int clean_gif(const unsigned char *in, long n, struct buf *out);
int clean_webp(const unsigned char *in, long n, struct buf *out);
int clean_pdf(const unsigned char *in, long n, struct buf *out);

/* Cleaners that change the file where it is, without moving anything
 * (big files: video, audio). With dry_run set they only look. 0 when
 * done, -1 on error (why is set). */
int clean_mp4(int fd, long long size);
int clean_mkv(int fd, long long size);
int clean_mp3(int fd, long long size);
int clean_flac(int fd, long long size);
int clean_riff(int fd, long long size);
int clean_heif(int fd, long long size);

/* img.c: notes what EXIF (from its TIFF header) holds; orient: its
 * orientation, when not 0 */
void exif_note(const unsigned char *p, long n, int *orient);
int at_fill(int fd, long long off, long long n, int c);

/* inflate.c: zlib data in, at most max bytes out; -1 when broken or
 * bigger than max (a zip bomb stops there) */
int inflate_zlib(const unsigned char *in, long n, struct buf *out, long max);
