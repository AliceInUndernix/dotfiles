/* WebArchiver - archive whole websites with wget */

/* ---- growing byte buffer (util.c) */
struct buf {
    char *s;
    int len;
    int cap;
};

void buf_init(struct buf *b);
void buf_add(struct buf *b, const char *s);
void buf_addn(struct buf *b, const char *s, int n);
void buf_addc(struct buf *b, int c);
void buf_addll(struct buf *b, long long v);
void buf_addhtml(struct buf *b, const char *s);
void buf_free(struct buf *b);

/* ---- list of strings (util.c) */
struct strlist {
    char **v;
    int n;
    int cap;
};

void sl_init(struct strlist *l);
void sl_add(struct strlist *l, const char *s);
void sl_sort(struct strlist *l);
int sl_has(struct strlist *l, const char *s);
void sl_free(struct strlist *l);
void sl_split(struct strlist *l, const char *s, int sep);

/* ---- string table: key -> value (util.c) */
struct tentry {
    char *key;
    char *val;
    struct tentry *next;
};

struct table {
    struct tentry **slot;
    int nslot;
    int count;
};

void tab_init(struct table *t);
const char *tab_get(struct table *t, const char *key);
void tab_set(struct table *t, const char *key, const char *val);
void tab_del(struct table *t, const char *key);
void tab_free(struct table *t);
void tab_keys(struct table *t, struct strlist *keys);
int tab_load(struct table *t, const char *path);
int tab_save(struct table *t, const char *path);

/* ---- misc helpers (util.c) */
void die(const char *msg, const char *arg);
void *xmalloc(int n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, int n);
char *str2(const char *a, const char *b);
char *str3(const char *a, const char *b, const char *c);
char *path2(const char *dir, const char *name);
int starts_with(const char *s, const char *prefix);
int ends_with(const char *s, const char *suffix);
int has_ext(const char *path, struct strlist *exts);
int lower(int c);
int str_ieq_n(const char *a, const char *b, int n);
long long parse_ll(const char *s);
void human(long long n, char *out);
void hms(long long secs, char *out);
long long now(void);
void utc_id(long long t, char *out);
void nice_time(const char *id, char *out);
void sleep_ms(int ms);
void say(const char *a, const char *b, const char *c);
void on_signal_do(int sig, void (*fn)(int));

/* files (util.c) */
int is_file(const char *path);
int is_dir(const char *path);
int is_link(const char *path);
long long file_size(const char *path);
long long file_mtime(const char *path);
void set_mtime(const char *path, long long ns);
int mkdirs(const char *path);
char *read_file(const char *path, int *len);
int write_file(const char *path, const char *data, int len);
int copy_file(const char *src, const char *dst);
void list_files(const char *dir, struct strlist *out);
void list_dir(const char *dir, struct strlist *out);
int dir_empty(const char *dir);
void remove_empty_dirs(const char *dir);
long long dir_size(const char *dir);

/* ---- SHA-256 (sha256.c) */
void sha_hex(const unsigned char *digest, char *hex);
int sha_file(const char *path, char *hex);
void sha_data(const char *data, int len, char *hex);

struct sha256 {
    unsigned long h[8];
    unsigned char block[64];
    int used;
    unsigned long long total;
};

void sha_init(struct sha256 *c);
void sha_update(struct sha256 *c, const unsigned char *p, int n);
void sha_final(struct sha256 *c, unsigned char *digest);

/* ---- URLs and paths (url.c) */
char *url_join(const char *base, const char *ref);
char *url_host(const char *url);
char *url_path(const char *url);
char *url_query(const char *url);
char *url_origin(const char *url);
int url_is_http(const char *s);
int url_has_scheme(const char *s);
char *pct_quote(const char *s, const char *safe);
char *pct_unquote(const char *s);
char *html_unescape(const char *s);
char *norm_path(const char *path);
char *rel_path(const char *target, const char *start);
char *host_dir(const char *host);

/* ---- configuration and locations (main.c) */
extern char *out_dir;
extern char *logs_dir;
extern char *state_dir;
extern char *hist_dir;
extern char *obj_dir;
extern struct strlist big_exts;
extern struct strlist req_exts;
extern struct strlist wait_steps;
extern char *fixed_wait;
extern char *user_agent;
extern char *hsts_opt;
extern int use_ipv4;
extern int jobs;
extern int refresh;
extern char *self_path;

char *domain_dir(const char *d);
char *state_file(const char *d, const char *ext);
int stop_requested(void);

/* ---- archive data (store.c) */
struct dstate {
    int complete;
    char phase[16];
    char mode[16];
    long long log_offset;
};

void state_load(const char *d, struct dstate *st);
void state_save(const char *d, struct dstate *st);
void status(const char *d, const char *a, const char *b);
char *rel_out(const char *path);
char *abs_out(const char *rel);

struct urlmap {
    struct table u;
    struct table p;
};

void map_load(const char *d, struct urlmap *m);
void map_save(const char *d, struct urlmap *m);
void map_set(struct urlmap *m, const char *url, const char *rel, int keep);
void map_free(struct urlmap *m);
void update_urlmap(const char *d, struct urlmap *m, long long since,
    struct table *done);
int log_has_downloads(const char *d, long long since);
int parse_log_line(const char *line, char **url, char **path);

void index_load(const char *d, struct table *t);
void index_save(const char *d, struct table *t);
const char *global_get(const char *key);
void global_set(struct strlist *keys, const char *rel);

const char *hash_of(struct table *cache, const char *path);
void cache_load(const char *d, struct table *cache);
void cache_save(const char *d, struct table *cache);

void versions_of(const char *d, struct strlist *ids);
void version_files(const char *d, const char *id, struct table *files);
int version_info(const char *d, const char *id, const char *key);
void version_changes(const char *d, const char *id, struct strlist *lines);
void referenced_shas(const char *d, struct table *shas);
char *obj_path(const char *sha);
void store_object(const char *path, const char *sha, int move);
void store_object_data(const char *data, int len, const char *sha);
void all_domains(struct strlist *out);

/* ---- wget (fetch.c) */
int run_wait(char **argv, const char *log);
int spawn(char **argv, const char *log, int *pid);
char *pick_scheme(const char *url);
void worker(const char *d, const char *mode, struct strlist *links);
int probe(const char *url, char **final, char **size, struct strlist *keys);
int download(const char *url, const char *dest, const char *size,
    char *sha);

/* ---- finishing a site (finish.c) */
void repair(const char *d, struct dstate *st, struct table *pre,
    struct urlmap *m);
int finalize(const char *d, const char *rc, int fetch);
void snapshot(const char *d, struct table *pre);
void pre_load(const char *d, struct table *pre);
void pre_save(const char *d, struct table *pre);
int is_backup(const char *rel, struct table *pre);
void process_backups(const char *d, struct table *pre,
    struct table *referenced, struct table *tree_shas);

/* ---- pages (page.c) */
void write_pages(void);
void render_archive(struct buf *b, int served, const char *note);
void render_history(struct buf *b, int served);
void page_head(struct buf *b, const char *title, int refresh_secs);
int run_active(void);
void cmd_versions(const char *only);
int cmd_restore(const char *d, const char *which);

/* ---- local server (serve.c) */
void cmd_serve(int port);
