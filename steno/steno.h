/* steno: a steno engine for a Gemini PR keyboard, in the way of
 * Plover. See README. */

/* --- strokes ----------------------------------------------------- */

#define NUM_KEYS 23
#define STROKE_MAX 16        /* strokes in one translation */
#define TEXT_MAX 512         /* bytes in one piece of text */
#define WORD_MAX 256
#define COMBO_MAX 256
#define SPACE_MAX 16

/* A stroke is a set of keys: bit i is steno_keys[i]. 0 is the empty
 * "prefix stroke" Plover puts before a word's first stroke. */
long stroke_parse(const char *s, int n);      /* -1: not steno */
void stroke_text(long st, char *out);         /* out: >= 32 bytes */
int outline_parse(const char *s, long *st, int max);  /* -1: bad */

/* --- dictionaries ------------------------------------------------ */

int dict_add(const char *path);               /* 0 ok, -1 error */
const char *dict_lookup(const long *st, int n);
int dict_longest(void);
void dict_stroked(void);       /* after each stroke (modal state) */
int dict_raw_toggle(void);     /* raw mode: 1 on, 0 off, -1 no raw dict */
int dict_count(void);
void dict_reload(void);        /* files that changed */
const char *dict_path(int i);
const char *dict_peek(const long *st, int n);
int dict_outlines(const char *text, char *out, int size, int max);
long dict_entries(void);
void dict_set_origin(int i, const char *name, int user);
const char *dict_name(int i, int *user);
int dict_enabled(int i);
void dict_set_enabled(int i, int on);
int dict_raw_on(void);
int dict_modal_active(void);
const char *dict_kind(int i);
long dict_size(int i);
int dict_move(int i, int dir);              /* -1: can't */
void dict_remove(int i);
int dict_convert(const char *in, const char *outdir, char *made, int size);
int dict_explain(const long *st, int n, char *out, int size);

/* --- translator -------------------------------------------------- */

extern int undo_levels;
void translate(long st);
void translate_reset(void);
int history_count(void);
struct trans *history_at(int i);
int history_strokes(void);

/* --- formatter --------------------------------------------------- */

extern int start_attached;
extern int start_capitalized;
extern int output_on;          /* PLOVER:TOGGLE */

struct trans;
void format_flush(struct trans **undo, int nundo, struct trans **doo,
    int ndo, struct trans *prev);
void format_reset(void);
void format_free(struct trans *t);
int trans_has_undo(struct trans *t);
int trans_word_finished(struct trans *t);
int last_words(int count, char *out, int size);   /* bytes, or 0 */
int last_fragments(int count, char *out, int size);
int last_chars(int count, char *out, int size);

/* the translations the formatter and the translator share */
struct action;
struct trans {
    long st[STROKE_MAX];
    int nst;
    char english[TEXT_MAX];
    int has_english;
    struct trans **replaced;
    int nreplaced;
    int retro;                 /* is_retrospective_command */
    struct action *act;        /* formatting; nact 0: not formatted */
    int nact;
    long lo;                   /* text: what this translation replaced */
    char *saved;
    int nsaved;
    long end;
    int mark;                  /* for freeing the unused ones */
};
struct trans *trans_new(const long *st, int nst, const char *english);
void trans_free(struct trans *t);   /* not its replaced ones */

/* --- spelling rules (orthography) ------------------------------- */

void add_suffix(const char *word, const char *suffix, char *out, int size);
int words_rank(const char *w);              /* -1: not a word */
int rx_match(const char *pat, const char *s, char *out, int size,
    const char *repl);                      /* 1: matched */

/* --- output ------------------------------------------------------ */

/* mode 0: X, 1: print what would be typed (steno -t) */
extern int out_mode;
int out_open(void);
int out_fd(void);
void out_event(void);          /* the X connection has something */
void out_backspaces(int n);
void out_string(const char *s, int n);
void out_combo(const char *combo);
void run_command(const char *cmd);
int atoi_small(const char *s);         /* -1: not a number */

/* --- terminal tools (tools.c) --------------------------------- */

extern char user_dict[1024];
void tape_stroke(long st, struct trans *top, int was_strokes);
int stroke_result(long st, struct trans *top, char *out, int size);
int view_main(int tape, int sugg, const char *cfg);   /* view.c */
void open_viewer(const char *flag);     /* -T, -S, -TS in a terminal */
const char *feed_path(const char *name);
int tool_lookup(const char *s);
void typographic(const char *text, char *out, int size);
int tool_add(const char *outline, const char *text);
int tool_add_loop(void);

/* --- the status screen (tui.c) and its engine side (main.c) ---- */

extern int tui_on;
extern volatile int tui_dirty;
int tui_start(void);
void tui_stop(void);
void tui_draw(void);
int tui_key(void);                          /* -1: quit */
void tui_log(const char *m);
void tui_stroke(long st, struct trans *top);
int keyboard_state(const char **port, const char **why);
int add_dict_line(const char *val, int user);
int config_save(void);
void output_toggle(void);

/* --- the terminal screen (screen.c) --------------------------- */

/* Habamax (README.md, "Colours: Habamax everywhere") */
#define C_TEXT "\033[38;2;199;199;199m"
#define C_TITLE "\033[1;38;2;238;238;238m"
#define C_LABEL "\033[38;2;158;158;158m"
#define C_DIM "\033[38;2;118;118;118m"
#define C_BOX "\033[38;2;88;88;88m"
#define C_BLUE "\033[38;2;95;135;175m"
#define C_TEAL "\033[38;2;135;175;175m"
#define C_GREEN "\033[38;2;135;175;135m"
#define C_BROWN "\033[38;2;175;135;95m"
#define C_RED "\033[38;2;175;95;95m"
#define C_SELECT "\033[48;2;88;88;88m"
#define C_PLAIN "\033[0m"
#define DEC_ON "\033(0"
#define DEC_OFF "\033(B"

extern int scr_colour;
extern int scr_title_dim;
void scr_write(const char *s, int n);
void scr_put(const char *s);
void scr_put_n(const char *s, int n);
void scr_col(const char *c);
void scr_move(int y, int x);
void scr_fill(int c, int n);
int scr_text(const char *s, int w);       /* characters written */
int scr_width(const char *s);
void scr_border(int y, int x, int w, int top, const char *title,
    const char *const *keys, int nkeys);
void scr_side(int y, int x, int w);
void scr_size(int *rows, int *cols);
void scr_begin(void);
void scr_end(void);
void scr_open(void);
void scr_close(void);
void scr_mouse(int on);

/* --- utilities --------------------------------------------------- */

extern int verbose;
void die(int code);            /* exit, giving the terminal back */
void say(const char *fmt, const char *a);
void scopy(char *dst, const char *src, int size);    /* zero fills */
void scat(char *dst, const char *src, int size);
int u8len(const char *s, int n);           /* characters in n bytes */
int u8back(const char *s, int n, int chars);  /* bytes of last chars */
int u8get(const char *s, int n, long *c);  /* bytes of one character */
int is_space_at(const char *s, int n);     /* bytes of a space, or 0 */
int is_wordc_at(const char *s, int n);     /* bytes of a \w, or 0 */
int case_at(char *s, int n, int up);       /* bytes of that char */
int up_ascii(int c);
int low_ascii(int c);
char *read_file(const char *path, long *size);
const char *config_path(const char *name);   /* ~ expanded, static */
const char *dotterel_data(const char *name); /* next to dictionaries */
const char *dict_dir_file(const char *name); /* in dictionaries */
int dict_is_builtin(const char *name);   /* dotterel-movement ... */

/* dotterel.c: Dotterel's built-in dictionaries, as data */
struct builtin_line {
    int kind;              /* 'e' entry, 'd' dict */
    const char *text;
    const char *outlines;
};
extern const struct builtin_line dotterel_movement[];
extern const char *const dotterel_mod_strokes[];
extern const int dotterel_mod_bits[];
extern const char *const dotterel_mod_keys[];
extern const char dotterel_raw_text[];
extern const char dotterel_raw_outline[];
