#ifndef KSPKAN_H
#define KSPKAN_H

#define KSPKAN_VERSION "0.1.0"

/* ---- util.c ---- */

struct strs {
    char **v;
    int n;
    int cap;
};

struct buf {
    char *p;
    long n;
    long cap;
};

void die(const char *fmt, ...);
void *xalloc(long n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, long n);
int is_digit(int c);
int is_space(int c);
void strs_add(struct strs *a, char *s);
void strs_free(struct strs *a);
void buf_add(struct buf *b, const char *s, long n);
void buf_adds(struct buf *b, const char *s);
void buf_free(struct buf *b);
char *read_file(const char *path, long *len, long max);
int write_file(const char *path, const char *data, long len);
int make_dirs(const char *path);
int safe_make_dirs(const char *root, const char *rel);
int path_is_safe(const char *rel);

/* ---- hashes ---- */

struct sha256 {
    unsigned int h[8];
    unsigned char buf[64];
    int n;
    long long total;
};

struct sha1 {
    unsigned int h[5];
    unsigned char buf[64];
    int n;
    long long total;
};

void sha256_init(struct sha256 *s);
void sha256_add(struct sha256 *s, const unsigned char *p, int len);
void sha256_done(struct sha256 *s, unsigned char *out);
void sha256_hex(const unsigned char *d, char *hex);

void sha1_init(struct sha1 *s);
void sha1_add(struct sha1 *s, const unsigned char *p, int len);
void sha1_done(struct sha1 *s, unsigned char *out);
void sha1_hex(const unsigned char *d, char *hex);

unsigned int crc32_update(unsigned int crc, const unsigned char *p, long n);

int hash_file(const char *path, const char *kind, char *hex);

/* ---- version.c ---- */

int version_compare(const char *a, const char *b);

/* ---- inflate.c ---- */

int inflate_raw(const unsigned char *in, long inlen, unsigned char *out,
    long outmax, long *outlen);
unsigned char *gunzip_alloc(const unsigned char *in, long inlen,
    long maxout, long *outlen);

/* ---- archive.c ---- */

struct aent {
    char *name;
    long csize;
    long usize;
    long off;
    unsigned int crc;
    int method;
    int isdir;
};

struct archive {
    const unsigned char *data;
    long len;
    struct aent *e;
    int n;
    int iszip;
};

int zip_open(struct archive *a, const unsigned char *data, long len);
int tar_open(struct archive *a, const unsigned char *data, long len);
void archive_close(struct archive *a);
unsigned char *archive_read(struct archive *a, int i, long maxsize,
    long *len);


/* ---- json.c (reading only; depth limited) ---- */

#define J_NULL 0
#define J_BOOL 1
#define J_NUM 2
#define J_STR 3
#define J_ARR 4
#define J_OBJ 5

struct jnode {
    int type;
    char *key;
    char *str;
    struct jnode *child;
    struct jnode *next;
};

struct jnode *json_parse(const char *text, long len);
void json_free(struct jnode *n);
struct jnode *json_get(const struct jnode *obj, const char *key);
const char *json_str(const struct jnode *obj, const char *key);

/* ---- rx.c: small regular expressions, NFA simulation ---- */

int rx_match(const char *pat, const char *s);


/* ---- index.c: metadata index, update, compatibility ---- */

struct entry {
    char *id;
    char *version;
    char *file;
    char *kexact;
    char *kmin;
    char *kmax;
    char *kind;
    char *status;
    char *provides;
    char *strict;
    struct jnode *j;
};

struct summary {
    char *id;
    char *name;
    char *abstract;
    char *version;
    long downloads;
    char *author;
    char *tags;
    char *license;
    char *date;
    long size;
    char *deps;
};

struct index {
    char *text;
    struct entry *e;
    int n;
    char *stext;
    struct summary *s;
    int ns;
    char *ptext;
    char **prov;
    int np;
};

/* ---- registry.c ---- */

struct inst {
    char *id;
    char *version;
    int autoinst;
    struct strs files;
};

struct registry {
    struct inst *v;
    int n;
    int cap;
};

struct ctx {
    char *game;
    char *state;
    char *shared;
    char *datadir;
    char *instname;
    int kspfixed;
    long listdays;
    struct strs inames;
    struct strs igames;
    char *idefault;
    char *kspver;
    char *repo;
    int force;
    int testing;
    int anyver;
    int assumeyes;
    int bypop;
    int withrec;
    int mono;
    int withsug;
    struct index ix;
    struct registry reg;
};

void sort_strs(char **a, int n, int (*cmp)(const char *, const char *));
char *state_path(struct ctx *c, const char *rel);
char *shared_path(struct ctx *c, const char *rel);
void registry_clear(struct registry *r);
int index_load(struct ctx *c);
int index_update(struct ctx *c, const unsigned char *tar, long len);
struct entry *index_find(struct index *ix, const char *id, int *count);
struct summary *summary_find(struct index *ix, const char *id);
#define SORT_NAME 0
#define SORT_POP 1
#define SORT_NEW 2
#define SORT_SMALL 3
#define SORT_BIG 4
#define SORT_KINDS 5

int *summary_order(struct ctx *c, int mode);
extern const char *sort_names[];
int entry_compat(struct ctx *c, struct entry *e);
struct jnode *entry_json(struct ctx *c, struct entry *e);
int ksp_detect(struct ctx *c);

int registry_load(struct ctx *c);
int registry_save(struct ctx *c);
struct inst *registry_find(struct registry *r, const char *id);
struct inst *registry_add(struct registry *r, const char *id,
    const char *version, int autoinst);
void registry_drop(struct registry *r, const char *id);
const char *registry_owner(struct registry *r, const char *path);


/* ---- fetch.c ---- */

int fetch_url(const char *url, const char *out, int quiet);

/* ---- cmd.c ---- */

#define DEFAULT_REPO \
    "https://github.com/KSP-CKAN/CKAN-meta/archive/master.tar.gz"

int cmd_update(struct ctx *c);
int cmd_search(struct ctx *c, const char *term);
int cmd_show(struct ctx *c, const char *id);
int cmd_list(struct ctx *c);
int cmd_clean(struct ctx *c);
int ctx_ready(struct ctx *c);


/* ---- resolve.c ---- */

int resolve(struct ctx *c, char **want, int nwant, char **skip, int nskip,
    struct entry ***out, int *nout, char *err, int errsz);
int plan_removal(struct ctx *c, char **ids, int n, struct strs *out,
    char *err, int errsz);

/* ---- install.c ---- */

struct pitem {
    int ent;
    char *dest;
};

struct plan {
    struct pitem *v;
    int n;
    int cap;
};

int install_plan(struct archive *a, struct jnode *mod, const char *id,
    struct plan *p, char *err, int errsz);
void plan_free(struct plan *p);
int install_extract(struct ctx *c, struct archive *a, struct plan *p,
    const char *id, const char *version, int autoinst, char *err,
    int errsz);
int remove_module(struct ctx *c, const char *id);
char *cache_file(struct ctx *c, struct entry *e);
int module_download(struct ctx *c, struct entry *e, char **path,
    char *err, int errsz);


int cmd_install(struct ctx *c, char **ids, int n);
int cmd_remove(struct ctx *c, char **ids, int n);
int cmd_upgrade(struct ctx *c, char **ids, int n);
int cmd_tui(struct ctx *c);


/* ---- query.c: advanced search ---- */

#define Q_MAX 16

struct qterm {
    int kind;
    int neg;
    char *text;
    long num;
};

struct query {
    struct qterm t[Q_MAX];
    int n;
    int sort;
    char *copy;
};

void query_parse(struct query *q, const char *text);
void query_free(struct query *q);
int query_match(struct ctx *c, const struct query *q, int sumidx);
long parse_count(const char *s);
void fmt_size(long n, char *out);


/* ---- instances.c: remembered game folders ---- */

char *data_dir(void);
int inst_load(struct ctx *c);
int inst_select(struct ctx *c, const char *name);
int inst_first_run(struct ctx *c);
int is_game_dir(const char *dir);
int cmd_add(struct ctx *c, const char *name, const char *dir);
int cmd_forget(struct ctx *c, const char *name);
int cmd_use(struct ctx *c, const char *name);
int cmd_instances(struct ctx *c);


void collect_extras(struct ctx *c, struct entry **ents, int n,
    const char *key, struct strs *out);

#endif
