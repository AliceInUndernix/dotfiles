/* diary.h: what the parts of diary share. seal.h (from ../seal) comes
 * first: diary uses seal's archive, crypto and helpers. */

#define TEXT_MAX 4096
#define NAME_MAX_D 128

/* ------------------------------------------------------- vault.c */

extern char vault_file[PATH_SIZE];   /* ~/.diary */
extern char ram_base_dir[PATH_SIZE]; /* <tmpfs>/diary */
extern char tree_dir[PATH_SIZE];     /* <tmpfs>/diary/diary */
extern char saved_at[16];            /* "21:40", or "" */
extern volatile int got_signal;      /* SIGHUP/SIGTERM came */

/* open: unseal into RAM (or make a new one); 0 when the diary is
 * ready in tree_dir, 1 on failure (said on stderr) */
int vault_open(int make_new, int pass_stdin);
/* save into vault_file, written and checked before it replaces the
 * old one; 0 when saved, -1 when not (said on stderr) */
int vault_save(void);
/* has anything in the tree changed since the last save? */
int vault_dirty(void);
/* save if needed, then wipe the RAM copy; 0 when all is gone */
int vault_close(void);
/* overwrite every file under path with zeros, then remove it all */
int wipe_tree(const char *path);

/* -------------------------------------------------------- term.c */

/* a cell of the screen, both of the UI and of the terminal for
 * Neovim. ch: one character as UTF-8 (at most 4 bytes, no 0), or a
 * DEC line drawing letter when dec is set. Colours: -1 the default,
 * 0-255 the palette, 0x1000000 | rgb a true colour. */
struct cell {
    char ch[5];
    char dec;
    unsigned char attr;
    int fg, bg;
};

#define AT_BOLD 1
#define AT_ITALIC 2
#define AT_UNDER 4
#define AT_REV 8
#define AT_DIM 16
#define AT_STRIKE 32

#define RGB(x) (0x1000000 | (x))

struct term {
    int rows, cols;
    struct cell *cells;
    struct cell *alt;          /* the other screen (?1049) */
    int cx, cy, wrapnext;
    int sx, sy;                /* saved cursor */
    struct cell sattr;
    int top, bot;              /* scrolling region */
    struct cell cur;           /* attributes for new characters */
    int cursor_on, altscreen, autowrap, insert, origin;
    int g0dec, g1dec, shift;   /* DEC line drawing in G0/G1 */
    int state;
    int par[16], npar, priv, inter;
    char osc[256];
    int nosc;
    char utf[5];
    int nutf, need;
    int fd;                    /* the pty, for answers */
    int pid;                   /* Neovim; 0 when none */
    char lastch[5];
};

int term_start(struct term *t, int rows, int cols, char **argv,
    const char *dir);
void term_feed(struct term *t, const char *buf, long n);
void term_resize(struct term *t, int rows, int cols);
void term_free(struct term *t);
