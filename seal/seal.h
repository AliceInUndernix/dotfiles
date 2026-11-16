/* seal.h: what seal.c and pack.c share. */

#define PATH_SIZE 4096
#define CHUNK_SIZE 65536L
#define SALT_SIZE 16
#define KEY_SIZE 32

/* Argon2id: 32 MiB, 2 passes. Fixed forever: a sealed file has no
 * header to say otherwise. */
#define KDF_BLOCKS 32768
#define KDF_PASSES 2

/* What must not reach swap or a core dump, in one place, so that it
 * can be locked in memory and wiped as one. */
struct secret {
    unsigned char key[KEY_SIZE];
    char pass[1024];
    char pass2[1024];
    unsigned char buf[CHUNK_SIZE];
    unsigned char out[CHUNK_SIZE + 16];
    unsigned char cmp[CHUNK_SIZE];
    unsigned char disk[CHUNK_SIZE];
};

extern struct secret *sec;
extern int opt_force;
extern int opt_quiet;
extern int opt_verbose;   /* -v: name each path (-t: list them) */
extern int opt_dry;       /* -n: check, write nothing */
extern volatile int stop_now;

/* seal.c */
void warn(const char *fmt, const char *a, const char *b);
int write_all(int fd, const void *p, long n);
long read_full(int fd, void *p, long n);
int join(char *dst, const char *a, const char *b);
void rand_bytes(unsigned char *p, long n);

/* pack.c: the counts of what was sealed or unsealed */
struct counts {
    long dirs;
    long files;
    long links;
    long long bytes;
};

/* pack DIR (on disk as disk, in the archive as name) into fd, encrypted
 * with sec->key; skip: the device and inode of the output. Then read it
 * back from fd2 and compare it with DIR, listing everything for
 * pack_remove. 0 when fine, -1 on error (said on stderr). */
int pack(int fd, const char *disk, const char *name, long skip_dev,
    long skip_ino, struct counts *c);
int pack_verify(int fd, const char *disk, struct counts *c);
/* remove what pack_verify listed; 0 when all of it went */
int pack_remove(const char *disk);

/* unpack_open: start reading fd (after its salt); the top directory's
 * name (at most 255 bytes) goes into name. unpack_rest: unpack it
 * into the directory tmp, or with list set only read it all and check
 * it (printing the names with -v).
 * 0 when fine, 1 when there were problems but -f went on, -1 when it
 * failed (the caller then removes what is in tmp). */
int unpack_open(int fd, char *name);
int unpack_rest(const char *tmp, int list, struct counts *c);
int rm_tree(const char *path);
