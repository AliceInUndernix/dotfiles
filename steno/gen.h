/* gen.c: a word's sounds, syllables and outlines made for it (steno -W,
 * steno -G). */

#define GEN_UNITS 48
#define GEN_TEXT 160
#define GEN_WHY 8
#define GEN_CANDS 16

struct gsound {
    int ph;                /* the sound (gen.c's list) */
    int stress;            /* vowels: 1, 2 or 0; consonants -1 */
    int silent;            /* an r written but not said in RP */
};

/* a sound with the letters that write it */
struct gunit {
    struct gsound s;
    char let[8];
    char silent_after[8];  /* silent letters after it (the e of make) */
    int at;                /* where its letters start */
};

struct gcand {
    char outline[GEN_TEXT];
    char note[32];         /* write-out, affix, r folded ... */
    char now[GEN_TEXT];    /* what it types now: "TEXT  (dictionary)" */
    int free;              /* nothing has it */
    int same;              /* it already types this word */
    int rank;
    char why[GEN_WHY][GEN_TEXT];
    int nwhy;
};

struct gen {
    char word[64];
    char written[96];      /* sched|ule */
    char spoken[96];       /* sch | e | dule, the letters of each */
    struct gsound uk[GEN_UNITS], us[GEN_UNITS], ukr[GEN_UNITS];
    int nuk, nus, nukr;
    int uk_from_us, guessed;
    int sy[GEN_UNITS], nsyl;
    char uk_ipa[GEN_TEXT], us_ipa[GEN_TEXT];
    char uk_ru[GEN_TEXT], us_ru[GEN_TEXT];
    struct gcand c[GEN_CANDS];
    int nc;
    int hashed;            /* # on the first strokes: a proper noun */
};

int gen_word(const char *word, struct gen *g);
int gen_print(const char *word);
void gen_set_hash(struct gen *g, int on);
int gen_is_proper(const char *word);
int gen_dict(const char *path, long *words, long *skipped);
void gen_hyphenate(const char *w, char *out, int size);
int gen_find(const char *file, const char *word, char out[][GEN_TEXT],
    int max);
void gen_lapwing(const char *word, char *out, int size);
void gen_sounds_text(const struct gsound *s, int n, char *out, int size);
