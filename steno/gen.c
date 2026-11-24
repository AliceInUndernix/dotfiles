/* steno -W's word knowledge: how a word sounds (British and American,
 * in IPA and in Russian letters), its syllables, as written and as
 * spoken, and outlines made for it from its sound and spelling by
 * Lapwing's rules with Dotterel's British ones (dotterel repo,
 * docs/lapwing-rules.md and docs/rp-rules.md).
 *
 * The sounds come from Dotterel's data directory: pron-uk.txt
 * (Britfone) and pron-us.txt (CMUdict). A word only the American list
 * has gets its British sound made from the American one; a word
 * neither has gets one guessed from its spelling (and says so).
 *
 * Making an outline:
 *   1. the letters and the sounds are lined up (which letters make
 *      which sound), so a short vowel can follow the spelling and an
 *      r that RP doesn't say can still be written (car: KAR);
 *   2. the sounds are split into syllables, each stroke after the
 *      first starting with a consonant (or KWR) and no consonant
 *      written twice;
 *   3. each syllable becomes a stroke: the consonants before the
 *      vowel on the left hand, the vowel by its sound (long) or its
 *      letter (short), the consonants after it on the right;
 *      what can't go in steno order goes to a stroke of its own
 *      (film: TPEUL/-PL);
 *   4. then the shorter forms: -shun, -ment, -able and their friends
 *      folded in, a final -r or -l folded into the stroke before, an
 *      unstressed vowel dropped; and the word's affixes, from the
 *      dictionaries (cats: KAT/-S).
 * Each outline says why it is written so, and what it types now. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "steno.h"
#include "gen.h"

/* --- sounds ---------------------------------------------------------- */

enum {
    P_NONE,
    P_P, P_B, P_T, P_D, P_K, P_G, P_F, P_V, P_TH, P_DH, P_S, P_Z, P_SH,
    P_ZH, P_HH, P_M, P_N, P_NG, P_L, P_R, P_W, P_Y, P_CH, P_JH,
    V_AE, V_EH, V_IH, V_OH, V_AH, V_UH, V_AX, V_IY, V_UW, V_AA, V_AO,
    V_ER, V_EY, V_AY, V_OY, V_OW, V_AW, V_IA, V_EA, V_UA,
    P_COUNT
};
#define IS_VOWEL(p) ((p) >= V_AE)

static const char *const ph_name[P_COUNT] = {
    "",
    "P", "B", "T", "D", "K", "G", "F", "V", "TH", "DH", "S", "Z", "SH",
    "ZH", "HH", "M", "N", "NG", "L", "R", "W", "Y", "CH", "JH",
    "AE", "EH", "IH", "OH", "AH", "UH", "AX", "IY", "UW", "AA", "AO",
    "ER", "EY", "AY", "OY", "OW", "AW", "IA", "EA", "UA"
};

/* IPA, British and American */
static const char *const ipa_uk[P_COUNT] = {
    "",
    "p", "b", "t", "d", "k", "\311\241", "f", "v", "\316\270",
    "\303\260", "s", "z", "\312\203", "\312\222", "h", "m", "n",
    "\305\213", "l", "r", "w", "j", "t\312\203", "d\312\222",
    "\303\246", "e", "\311\252", "\311\222", "\312\214", "\312\212",
    "\311\231", "i\313\220", "u\313\220", "\311\221\313\220",
    "\311\224\313\220", "\311\234\313\220", "e\311\252", "a\311\252",
    "\311\224\311\252", "\311\231\312\212", "a\312\212",
    "\311\252\311\231", "e\311\231", "\312\212\311\231"
};
static const char *const ipa_us[P_COUNT] = {
    "",
    "p", "b", "t", "d", "k", "\311\241", "f", "v", "\316\270",
    "\303\260", "s", "z", "\312\203", "\312\222", "h", "m", "n",
    "\305\213", "l", "r", "w", "j", "t\312\203", "d\312\222",
    "\303\246", "\311\233", "\311\252", "\311\221", "\312\214",
    "\312\212", "\311\231", "i", "u", "\311\221", "\311\224",
    "\311\235", "e\311\252", "a\311\252", "\311\224\311\252",
    "o\312\212", "a\312\212", "\311\252\311\231", "\311\233\311\231",
    "\312\212\311\231"
};

/* The sound in Russian letters, as Dotterel's Builder wrote it:
 * vowels, and the ones that merge with a y before them (you: ю:) */
static const char *const ru_sound[P_COUNT] = {
    "",
    "\320\277", "\320\261", "\321\202", "\320\264", "\320\272",
    "\320\263", "\321\204", "\320\262", "\321\201", "\320\267",
    "\321\201", "\320\267", "\321\210", "\320\266", "\321\205",
    "\320\274", "\320\275", "\320\275", "\320\273", "\321\200",
    "\321\203", "\320\271", "\321\207", "\320\264\320\266",
    "\321\215", "\321\215", "\320\270", "\320\276", "\320\260",
    "\321\203", "\321\215", "\320\270:", "\321\203:", "\320\260:",
    "\320\276:", "\321\221:", "\321\215\320\271", "\320\260\320\271",
    "\320\276\320\271", "\321\215\321\203", "\320\260\321\203",
    "\320\270\321\215", "\321\215:", "\321\203\321\215"
};
static const char *const ru_after_y[P_COUNT] = {
    "",
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0,
    "\321\217", "\320\265", 0, "\321\221", "\321\217", "\321\216", 0, 0,
    "\321\216:", "\321\217:", "\321\221:", "\321\221:", 0, 0, 0, 0, 0,
    0, 0, "\321\216\321\215"
};

static int ph_code(const char *s, int n)
{
    int i;

    for (i = 1; i < P_COUNT; i++)
        if ((int)strlen(ph_name[i]) == n && !strncmp(ph_name[i], s, n))
            return i;
    return 0;
}

/* "SH EH1 JH UW0 L" -> sounds; 0 when it isn't one */
static int parse_sounds(const char *s, struct gsound *out, int max)
{
    int n, k, c, st;
    const char *e;

    n = 0;
    while (*s) {
        while (*s == ' ')
            s++;
        if (!*s)
            break;
        e = s;
        while (*e && *e != ' ')
            e++;
        k = (int)(e - s);
        st = -1;
        if (k > 1 && s[k - 1] >= '0' && s[k - 1] <= '2') {
            st = s[k - 1] - '0';
            k--;
        }
        c = ph_code(s, k);
        if (!c || n == max)
            return 0;
        out[n].ph = c;
        out[n].stress = IS_VOWEL(c) ? (st < 0 ? 0 : st) : -1;
        out[n].silent = 0;
        n++;
        s = e;
    }
    return n;
}

/* A data file in memory, its lines sorted by word for a binary
 * search: read once, when first asked. */
struct wordfile {
    const char *name;
    char *data;
    char **line;
    long n;
    int tried;
};

#define NFILES 5
static struct wordfile files[NFILES] = {
    { "pron-us.txt", 0, 0, 0, 0 }, { "pron-uk.txt", 0, 0, 0, 0 },
    { "lapwing.txt", 0, 0, 0, 0 }, { "word-homes.txt", 0, 0, 0, 0 },
    { "meanings.txt", 0, 0, 0, 0 }
};

/* compare up to the tab */
static int word_cmp(const char *a, const char *b)
{
    while (*a && *a != '\t' && *a == *b) {
        a++;
        b++;
    }
    return (*a == '\t' ? 0 : (unsigned char)*a) -
        (*b == '\t' ? 0 : (unsigned char)*b);
}

static int line_cmp(const void *a, const void *b)
{
    int c;
    const char *x, *y;

    x = *(char *const *)a;
    y = *(char *const *)b;
    c = word_cmp(x, y);
    if (c)
        return c;
    return x < y ? -1 : x > y;     /* the file's order for one word */
}

static void wordfile_load(struct wordfile *f)
{
    long size, n, c;
    char *p, *e;

    f->tried = 1;
    f->data = read_file(dotterel_data(f->name), &size);
    if (!f->data)
        return;
    n = 0;
    for (p = f->data; *p; p++)
        n += *p == '\n';
    f->line = malloc((n + 1) * sizeof(f->line[0]));
    if (!f->line)
        return;
    c = 0;
    for (p = f->data; *p; p = e) {
        e = strchr(p, '\n');
        if (e) {
            *e = 0;
            e++;
        } else {
            e = p + strlen(p);
        }
        if (p[0] == '#' || !strchr(p, '\t'))
            continue;
        p[strcspn(p, "\r")] = 0;
        f->line[c] = p;
        c++;
    }
    f->n = c;
    qsort(f->line, c, sizeof(f->line[0]), line_cmp);
}

/* the lines of WORD in a data file, what is after its tab a line, at
 * most max; -1 when the file can't be read */
int gen_find(const char *file, const char *word, char out[][GEN_TEXT],
    int max)
{
    struct wordfile *f;
    long lo, hi, mid, i;
    int c, n;
    char key[128];

    f = 0;
    for (i = 0; i < NFILES; i++)
        if (!strcmp(files[i].name, file))
            f = &files[i];
    if (!f)
        return -1;
    if (!f->tried)
        wordfile_load(f);
    if (!f->line)
        return -1;
    scopy(key, word, sizeof(key) - 2);
    scat(key, "\t", sizeof(key));
    lo = 0;
    hi = f->n - 1;
    while (lo < hi) {
        mid = (lo + hi) / 2;
        c = word_cmp(f->line[mid], key);
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    n = 0;
    for (i = lo; i < f->n && n < max; i++) {
        if (word_cmp(f->line[i], key))
            break;
        scopy(out[n], strchr(f->line[i], '\t') + 1, GEN_TEXT);
        n++;
    }
    return n;
}

/* the Lapwing outlines of a word (lapwing.txt), "" when none */
void gen_lapwing(const char *word, char *out, int size)
{
    char l[1][GEN_TEXT];

    out[0] = 0;
    if (gen_find("lapwing.txt", word, l, 1) == 1)
        scopy(out, l[0], size);
}

static int is_v(int c)
{
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' ||
        c == 'y';
}

static int vowel_letters(const char *l)
{
    int n;

    for (n = 0; *l; l++)
        if (is_v((unsigned char)*l))
            n++;
    return n;
}

/* American to British (RP), for a word Britfone doesn't have: no r
 * after a vowel unless a vowel follows, the vowels RP has there, and
 * o spellings with the lot vowel */
static int us_to_uk(const struct gsound *us, int n, const char *w,
    struct gsound *uk)
{
    int i, m, p, next_v, o_word;

    m = 0;
    o_word = strchr(w, 'o') != 0;
    for (i = 0; i < n; i++) {
        p = us[i].ph;
        next_v = i + 1 < n && IS_VOWEL(us[i + 1].ph);
        uk[m] = us[i];
        if (p == P_R && m > 0 && IS_VOWEL(uk[m - 1].ph) && !next_v) {
            /* car, near, square, cure: the vowel changes, no r */
            switch (uk[m - 1].ph) {
            case V_IY: case V_IH: uk[m - 1].ph = V_IA; break;
            case V_EH: case V_EY: case V_AE: uk[m - 1].ph = V_EA; break;
            case V_UH: case V_UW: uk[m - 1].ph = V_UA; break;
            case V_AA: uk[m - 1].ph = V_AA; break;
            case V_OW: case V_OH: uk[m - 1].ph = V_AO; break;
            }
            continue;
        }
        if (p == V_ER && us[i].stress == 0)
            uk[m].ph = V_AX;
        if (p == V_AA && o_word)
            uk[m].ph = next_v || i + 1 >= n || us[i + 1].ph != P_R ?
                V_OH : V_AA;
        if (p == V_AO && o_word && !(i + 1 < n && us[i + 1].ph == P_R))
            uk[m].ph = V_OH;
        m++;
    }
    return m;
}

/* --- the letters and the sounds lined up ------------------------------ */

struct rule {
    const char *let;
    const char *ph;
    int cost;
};

/* consonant spellings; vowels are handled by letter groups below */
static const struct rule rules[] = {
    { "b", "B", 0 }, { "bb", "B", 0 }, { "mb", "M", 1 },
    { "c", "K", 0 }, { "c", "S", 1 }, { "cc", "K", 0 },
    { "cc", "K S", 1 }, { "ck", "K", 0 }, { "ch", "CH", 0 },
    { "ch", "K", 1 }, { "ch", "SH", 1 }, { "tch", "CH", 0 },
    { "c", "SH", 3 }, { "c", "CH", 3 },
    { "d", "D", 0 }, { "dd", "D", 0 }, { "d", "JH", 2 },
    { "dg", "JH", 0 }, { "d", "T", 2 },
    { "f", "F", 0 }, { "ff", "F", 0 }, { "ph", "F", 0 }, { "gh", "F", 2 },
    { "g", "G", 0 }, { "gg", "G", 0 }, { "g", "JH", 1 }, { "gg", "JH", 1 },
    { "g", "ZH", 3 }, { "gu", "G", 1 }, { "gh", "G", 2 }, { "gn", "N", 1 },
    { "h", "HH", 0 }, { "wh", "W", 0 }, { "wh", "HH", 1 },
    { "j", "JH", 0 }, { "j", "Y", 3 }, { "j", "ZH", 3 },
    { "k", "K", 0 }, { "kk", "K", 0 }, { "kn", "N", 1 },
    { "l", "L", 0 }, { "ll", "L", 0 },
    { "m", "M", 0 }, { "mm", "M", 0 }, { "mn", "M", 2 },
    { "n", "N", 0 }, { "nn", "N", 0 }, { "n", "NG", 1 }, { "ng", "NG", 0 },
    { "ng", "NG G", 0 }, { "ng", "N JH", 1 },
    { "p", "P", 0 }, { "pp", "P", 0 }, { "ps", "S", 2 },
    { "q", "K", 1 }, { "qu", "K W", 0 }, { "qu", "K", 1 },
    { "r", "R", 0 }, { "rr", "R", 0 }, { "wr", "R", 1 }, { "rh", "R", 1 },
    { "s", "S", 0 }, { "s", "Z", 0 }, { "ss", "S", 0 }, { "ss", "Z", 1 },
    { "s", "SH", 2 }, { "s", "ZH", 1 }, { "ss", "SH", 1 }, { "sh", "SH", 0 },
    { "sc", "S", 1 }, { "sch", "S K", 0 }, { "sch", "SH", 1 },
    { "sc", "S K", 0 }, { "sc", "SH", 2 },
    { "t", "T", 0 }, { "tt", "T", 0 }, { "t", "CH", 1 }, { "t", "SH", 2 },
    { "th", "TH", 0 }, { "th", "DH", 0 }, { "th", "T", 2 }, { "t", "D", 2 },
    { "v", "V", 0 }, { "vv", "V", 0 },
    { "w", "W", 0 },
    { "x", "K S", 0 }, { "x", "G Z", 0 }, { "x", "Z", 1 },
    { "x", "K SH", 1 },
    { "y", "Y", 0 },
    { "z", "Z", 0 }, { "zz", "Z", 0 }, { "z", "S", 2 }, { "z", "ZH", 2 },
    { "z", "T S", 2 },
    { 0, 0, 0 }
};

static struct gsound rule_ph[sizeof(rules) / sizeof(rules[0])][3];
static int rule_nph[sizeof(rules) / sizeof(rules[0])];
static int rules_made;

static void make_rules(void)
{
    int i;

    for (i = 0; rules[i].let; i++)
        rule_nph[i] = parse_sounds(rules[i].ph, rule_ph[i], 3);
    rules_made = 1;
}

#define AL_LET 48
#define AL_PH 48
#define AL_INF 1000000

/* how a cell was reached: from (i - nl, j - np) */
struct step {
    int nl, np;
    int phantom;               /* a written r RP doesn't say */
    int cost;
};

static struct step al[AL_LET + 1][AL_PH + 1];

static int fix_units(struct gunit *u, int n);

/* letters w[i..i+k) a vowel group: aeiouy, with a w, gh or h after
 * (aw, ew, ow, igh, augh, oh) */
static int vowel_group(const char *w, int i, int n, int *ks, int max)
{
    int k, nk;

    nk = 0;
    if (!is_v((unsigned char)w[i]))
        return 0;
    /* y before a vowel is a consonant (yes, beyond) */
    if (w[i] == 'y' && i + 1 < n && is_v((unsigned char)w[i + 1]) &&
        w[i + 1] != 'y')
    {
        if (i == 0)
            return 0;
    }
    for (k = 1; i + k <= n && k <= 4 && nk < max; k++) {
        if (k > 1 && !is_v((unsigned char)w[i + k - 1]) &&
            !(w[i + k - 1] == 'w' && k == 2) &&
            !(w[i + k - 1] == 'h' && i + k == n))
        {
            if (w[i + k - 1] == 'g' && i + k < n && w[i + k] == 'h') {
                ks[nk] = k + 1;     /* igh, ough */
                nk++;
            }
            break;
        }
        ks[nk] = k;
        nk++;
    }
    return nk;
}

static void relax(int i, int j, int nl, int np, int cost, int phantom)
{
    int c;

    if (al[i][j].cost >= AL_INF)
        return;
    c = al[i][j].cost + cost;
    if (c < al[i + nl][j + np].cost) {
        al[i + nl][j + np].cost = c;
        al[i + nl][j + np].nl = nl;
        al[i + nl][j + np].np = np;
        al[i + nl][j + np].phantom = phantom;
    }
}

/* Line up w and its sounds: units[] get each sound with its letters
 * (an r written but not said is a unit of its own, silent). Returns
 * the units, or 0. */
static int align(const char *w, const struct gsound *s, int m,
    struct gunit *u, int max)
{
    int n, i, j, k, r, ks[6], nk, q, ok, c, t, nu;
    int path_i[AL_LET + AL_PH + 2], path_j[AL_LET + AL_PH + 2], np;
    struct step *st;

    if (!rules_made)
        make_rules();
    n = strlen(w);
    if (n > AL_LET || m > AL_PH)
        return 0;
    for (i = 0; i <= n; i++)
        for (j = 0; j <= m; j++)
            al[i][j].cost = AL_INF;
    al[0][0].cost = 0;
    for (i = 0; i <= n; i++) {
        for (j = 0; j <= m; j++) {
            if (al[i][j].cost >= AL_INF)
                continue;
            /* a consonant spelling */
            for (r = 0; rules[r].let; r++) {
                k = strlen(rules[r].let);
                q = rule_nph[r];
                if (i + k > n || j + q > m ||
                    strncmp(w + i, rules[r].let, k))
                {
                    continue;
                }
                ok = 1;
                for (t = 0; t < q; t++)
                    if (s[j + t].ph != rule_ph[r][t].ph)
                        ok = 0;
                if (ok)
                    relax(i, j, k, q, rules[r].cost, 0);
            }
            if (i < n && j < m && IS_VOWEL(s[j].ph)) {
                /* a vowel group for one vowel sound, or a u for "yoo" */
                nk = vowel_group(w, i, n, ks, 6);
                for (t = 0; t < nk; t++)
                    relax(i, j, ks[t], 1, 1 + (ks[t] > 2), 0);
            }
            if (i < n && j + 1 < m && s[j].ph == P_Y &&
                IS_VOWEL(s[j + 1].ph))
            {
                /* u, ew, eu, ue: y + oo */
                nk = vowel_group(w, i, n, ks, 6);
                for (t = 0; t < nk; t++)
                    if (memchr(w + i, 'u', ks[t]) ||
                        memchr(w + i, 'w', ks[t]))
                    {
                        relax(i, j, ks[t], 2, 1, 0);
                    }
                /* i before a vowel, as y: onion, union */
                if (w[i] == 'i' || w[i] == 'e')
                    relax(i, j, 1, 1, 2, 0);
            }
            if (i < n) {
                c = (unsigned char)w[i];
                /* an r after a vowel that RP doesn't say */
                if (c == 'r' && j > 0 && IS_VOWEL(s[j - 1].ph))
                    relax(i, j, 1, 0, 1, 1);
                if (c == 'r' && i + 1 < n && w[i + 1] == 'r' && j > 0 &&
                    IS_VOWEL(s[j - 1].ph))
                {
                    relax(i, j, 2, 0, 1, 1);
                }
                /* silent letters: an e (the end is cheap), others dear */
                if (c == 'e')
                    relax(i, j, 1, 0, i == n - 1 ? 1 : 3, 0);
                else if (c == 'h' || c == 'w' || c == 'u')
                    relax(i, j, 1, 0, 4, 0);
                else
                    relax(i, j, 1, 0, 7, 0);
            }
            if (j < m) {
                /* a sound with no letter: a schwa (rhythm), else dear */
                relax(i, j, 0, 1, s[j].ph == V_AX ? 3 : 9, 0);
            }
        }
    }
    if (al[n][m].cost >= AL_INF)
        return 0;
    /* back from the end */
    np = 0;
    i = n;
    j = m;
    while (i > 0 || j > 0) {
        path_i[np] = i;
        path_j[np] = j;
        np++;
        st = &al[i][j];
        i -= st->nl;
        j -= st->np;
    }
    nu = 0;
    for (k = np - 1; k >= 0; k--) {
        i = path_i[k];
        j = path_j[k];
        st = &al[i][j];
        if (st->np == 0) {
            if (st->phantom && nu < max) {
                u[nu].s.ph = P_R;
                u[nu].s.stress = -1;
                u[nu].s.silent = 1;
                memcpy(u[nu].let, w + i - st->nl, st->nl);
                u[nu].let[st->nl] = 0;
                u[nu].at = i - st->nl;
                nu++;
            } else if (nu > 0) {
                /* a silent letter goes with the sound before */
                scat(u[nu - 1].let, "", sizeof(u[0].let));
                if (strlen(u[nu - 1].silent_after) + st->nl <
                    sizeof(u[0].silent_after))
                {
                    strncat(u[nu - 1].silent_after, w + i - st->nl,
                        st->nl);
                }
            }
            continue;
        }
        for (t = 0; t < st->np && nu < max; t++) {
            u[nu].s = s[j - st->np + t];
            u[nu].s.silent = 0;
            u[nu].silent_after[0] = 0;
            u[nu].at = i - st->nl;
            if (t == 0) {
                memcpy(u[nu].let, w + i - st->nl, st->nl);
                u[nu].let[st->nl] = 0;
            } else {
                u[nu].let[0] = 0;   /* x: k s, the x on the k */
            }
            nu++;
        }
    }
    return fix_units(u, nu);
}

/* After lining up: a y between a consonant and a u sound is part of
 * the vowel (cube KAOUB, accurate A/KU/RAT, review RE/SRAOU); a
 * near/cure vowel spelled with two vowel letters and no r is two
 * syllables (medium PHAOE/TKAOE/KWRUPL). */
static int fix_units(struct gunit *u, int n)
{
    int i, k, p;
    char l[8];

    for (i = 1; i + 1 < n; i++) {
        p = u[i + 1].s.ph;
        if (u[i].s.ph != P_Y || IS_VOWEL(u[i - 1].s.ph) ||
            !(p == V_UW || p == V_UH || p == V_UA || p == V_AX ||
            p == V_ER))
        {
            continue;
        }
        if (!strchr(u[i].let, 'u') && !strchr(u[i + 1].let, 'u') &&
            !strchr(u[i].let, 'e') && !strchr(u[i + 1].let, 'w'))
        {
            continue;
        }
        scopy(l, u[i].let, sizeof(l));
        scat(l, u[i + 1].let, sizeof(l));
        scopy(u[i + 1].let, l, sizeof(u[0].let));
        memmove(u + i, u + i + 1, (n - i - 1) * sizeof(u[0]));
        n--;
    }
    for (i = 0; i < n && n < GEN_UNITS; i++) {
        p = u[i].s.ph;
        if ((p != V_IA && p != V_UA) || vowel_letters(u[i].let) < 2 ||
            (i + 1 < n && u[i + 1].s.ph == P_R))
        {
            continue;
        }
        memmove(u + i + 1, u + i, (n - i) * sizeof(u[0]));
        n++;
        u[i].s.ph = p == V_IA ? V_IY : V_UW;
        u[i].let[1] = 0;
        u[i].silent_after[0] = 0;
        u[i + 1].s.ph = V_AX;
        u[i + 1].s.stress = 0;
        for (k = 0; u[i + 1].let[k + 1]; k++)
            u[i + 1].let[k] = u[i + 1].let[k + 1];
        u[i + 1].let[k] = 0;
        i++;
    }
    return n;
}

/* --- keys ------------------------------------------------------------ */

#define K_STAR 10
#define K_LEFT_MAX 7           /* # S T K P W H R */

static long keys_of(const char *s)
{
    long st;

    st = stroke_parse(s, strlen(s));
    return st < 0 ? 0 : st;
}

static int top_key(long st)
{
    int k;

    for (k = NUM_KEYS - 1; k >= 0; k--)
        if (st & (1L << k))
            return k;
    return -1;
}

static int low_key(long st)
{
    int k;

    for (k = 0; k < NUM_KEYS; k++)
        if (st & (1L << k))
            return k;
    return NUM_KEYS;
}

/* a after b in steno order, no key shared (the star can be shared) */
static int fits_after(long have, long add)
{
    long h, a;

    h = have & ~(1L << K_STAR);
    a = add & ~(1L << K_STAR);
    if (h & a)
        return 0;
    return !h || !a || low_key(a) > top_key(h);
}

/* --- syllables -------------------------------------------------------- */

static int onset_ok(const int *p, int n)
{
    static const char *const two[] = {
        "P R", "B R", "T R", "D R", "K R", "G R", "F R", "TH R", "SH R",
        "P L", "B L", "K L", "G L", "F L", "S L", "S P", "S T", "S K",
        "S M", "S N", "S W", "S F", "K W", "T W", "D W", "G W", "TH W",
        "V R", 0
    };
    static const char *const three[] = {
        "S P R", "S T R", "S K R", "S P L", "S K W", 0
    };
    const char *const *l;
    char b[32];
    int i;

    if (n == 0)
        return 1;
    if (n == 1)
        return p[0] != P_NG && p[0] != P_ZH;
    if (n > 3)
        return 0;
    b[0] = 0;
    for (i = 0; i < n; i++) {
        if (i)
            scat(b, " ", sizeof(b));
        scat(b, ph_name[p[i]], sizeof(b));
    }
    for (l = n == 2 ? two : three; *l; l++)
        if (!strcmp(*l, b))
            return 1;
    return 0;
}

/* Split the units into syllables: sy[k] is the first unit of syllable
 * k. Each vowel is a syllable's; the consonants between two vowels
 * go: the longest onset English allows to the second (an s with a
 * consonant after it only before a stressed vowel: astray A/STRAEU,
 * pastor PAS/TOR), ng and a said r always to the first. */
static long left_keys(const struct gunit *u, int a, int b, int at_start,
    const char *word, char *why, int wsize);

static int syllabify(const struct gunit *u, int n, int *sy, int max)
{
    int v[GEN_UNITS], nv, i, k, a, b, c, best, p[4], ok, m, stressed;
    char why[GEN_TEXT];

    nv = 0;
    for (i = 0; i < n; i++)
        if (IS_VOWEL(u[i].s.ph) && nv < GEN_UNITS)
            v[nv++] = i;
    if (!nv)
        return 0;
    sy[0] = 0;
    for (k = 1; k < nv && k < max; k++) {
        a = v[k - 1] + 1;      /* the consonants a .. b-1 */
        b = v[k];
        best = b;
        stressed = u[b].s.stress == 1;
        for (c = a; c < b; c++) {
            /* onset c .. b-1 */
            m = b - c;
            if (m > 3)
                continue;
            ok = 1;
            for (i = c; i < b; i++) {
                p[i - c] = u[i].s.ph;
                if (u[i].s.silent)
                    ok = 0;
            }
            if (!ok || !onset_ok(p, m))
                continue;
            /* two or more: an r group (betray PWE/TRAEU), qu (equal
             * AOE/KWAUL), or before a stressed vowel (astray
             * A/STRAEU); else split in the middle (pastor PAS/TOR,
             * public PUB/HREUBG) */
            if (m >= 2 && !(p[m - 1] == P_R && (m == 2 || stressed)) &&
                !(m == 2 && p[0] == P_K && p[1] == P_W) && !stressed)
            {
                continue;
            }
            /* an x is -BGS together before another consonant
             * (explicit EBGS/PHREUFT); between vowels it splits,
             * -BG and S (exam EBG/SAPL) */
            if (c > a && !strcmp(u[c - 1].let, "x") && !u[c].let[0] &&
                m >= 2)
            {
                continue;
            }
            /* it must go on the left hand in steno order */
            if (left_keys(u, c, b, 0, "", why, sizeof(why)) < 0)
                continue;
            best = c;
            break;
        }
        /* a said r right after a vowel stays with it when another
         * consonant follows (carpet KAR/PET) */
        if (best == a && b - a >= 2 && u[a].s.ph == P_R)
            best = a + 1;
        /* rr: the r stays with the vowel before, the next stroke
         * starts with KWR (carry KAR/KWREU, berry PWER/KWREU) */
        if (b - a == 1 && u[a].s.ph == P_R && !strcmp(u[a].let, "rr"))
            best = b;
        sy[k] = best;
    }
    return k;
}

/* --- one stroke from one syllable ----------------------------------- */

struct conkey {
    int ph;
    const char *left;
    const char *right;
};

static const struct conkey conkeys[] = {
    { P_P, "P", "-P" }, { P_B, "PW", "-B" }, { P_T, "T", "-T" },
    { P_D, "TK", "-D" }, { P_K, "K", "-BG" }, { P_G, "TKPW", "-G" },
    { P_F, "TP", "-F" }, { P_V, "SR", "-F" }, { P_TH, "TH", "*T" },
    { P_DH, "TH", "*T" }, { P_S, "S", "-S" }, { P_Z, "STKPW", "-Z" },
    { P_SH, "SH", "-RB" }, { P_ZH, "SH", "-RB" }, { P_HH, "H", "" },
    { P_M, "PH", "-PL" }, { P_N, "TPH", "-PB" }, { P_NG, "", "-PBG" },
    { P_L, "HR", "-L" }, { P_R, "R", "-R" }, { P_W, "W", "" },
    { P_Y, "KWR", "" }, { P_CH, "KH", "-FP" }, { P_JH, "SKWR", "-PBLG" },
    { 0, 0, 0 }
};

static const struct conkey *conkey(int ph)
{
    int i;

    for (i = 0; conkeys[i].ph; i++)
        if (conkeys[i].ph == ph)
            return &conkeys[i];
    return 0;
}

/* right-hand chords for two or more sounds at once */
struct pair {
    const char *ph;
    const char *keys;
};

static const struct pair codas[] = {
    { "N K", "*PBG" }, { "M P", "*PL" }, { "L K", "*LG" },
    { "R F", "*FRB" }, { "R V", "-FRB" }, { "N CH", "-FRPB" },
    { "R CH", "-FRPB" }, { "L JH", "-LG" }, { "L CH", "-LG" },
    { "N JH", "-PBG" }, { "S T", "-FT" }, { "S T", "*S" }, { "F T", "-FT" },
    { "NG K", "*PBG" }, { "NG K S", "*PBG" },
    { "K S", "-BGS" }, { "K S T", "-GT" }, { "S K", "-FBG" },
    { "S P", "-FP" }, { "Z T", "-FT" },
    { 0, 0 }
};

static int codas_ph[sizeof(codas) / sizeof(codas[0])][4];
static int codas_n[sizeof(codas) / sizeof(codas[0])];

static void make_codas(void)
{
    struct gsound s[4];
    int i, k, n;

    for (i = 0; codas[i].ph; i++) {
        n = parse_sounds(codas[i].ph, s, 4);
        for (k = 0; k < n; k++)
            codas_ph[i][k] = s[k].ph;
        codas_n[i] = n;
    }
}

/* the letters of a unit, lower case */
static int has_let(const struct gunit *u, const char *s)
{
    return strstr(u->let, s) != 0;
}


/* The vowel keys: long vowels by sound, short ones by spelling
 * (Lapwing 4.1-4.4, Dotterel R2-R4). r: the vowel has an r after it
 * (written, said or not). first: the word's first letters. */
static const char *vowel_keys(const struct gunit *u, int r, int last,
    char *why, int wsize)
{
    const char *l;
    int one;

    l = u->let;
    one = vowel_letters(l) <= 1 && strlen(l) <= 1;
    if (strstr(l, "oo")) {
        scopy(why, "spelled oo: AO", wsize);
        return "AO";
    }
    switch (u->s.ph) {
    case V_EY:
        scopy(why, "long a: AEU", wsize);
        return "AEU";
    case V_IY:
        if ((!strcmp(l, "i") || !strcmp(l, "y") || !strcmp(l, "ey") ||
            !strcmp(l, "ie")) && (u->s.stress == 0 || !strcmp(l, "i")))
        {
            scopy(why, "long e spelled i or y: EU", wsize);
            return "EU";
        }
        scopy(why, "long e: AOE", wsize);
        return "AOE";
    case V_AY:
        scopy(why, "long i: AOEU", wsize);
        return "AOEU";
    case V_OW:
        scopy(why, "long o: OE", wsize);
        return "OE";
    case V_UW:
        scopy(why, "long u, oo: AOU", wsize);
        return "AOU";
    case V_AW:
        scopy(why, "ow: OU", wsize);
        return "OU";
    case V_OY:
        scopy(why, "oy: OEU", wsize);
        return "OEU";
    case V_IA:
        scopy(why, "near: AOE + r", wsize);
        return "AOE";
    case V_EA:
        scopy(why, "square: AEU + r", wsize);
        return "AEU";
    case V_UA:
        scopy(why, "cure: AOU + r (R4)", wsize);
        return "AOU";
    case V_AA:
        if (strchr(l, 'a') || !l[0]) {
            scopy(why, "RP ah: A (R2)", wsize);
            return "A";
        }
        break;
    case V_AO:
        if (strchr(l, 'u') && !strchr(l, 'a') && !strchr(l, 'o')) {
            scopy(why, "oo + r spelled u: AOU (R4)", wsize);
            return "AOU";
        }
        if (r && strstr(l, "ou")) {
            scopy(why, "ou + r: OU", wsize);
            return "OU";
        }
        if (r && (strchr(l, 'o') || !l[0])) {
            scopy(why, "or: O + R", wsize);
            return "O";
        }
        if (r && strchr(l, 'a')) {
            scopy(why, "war/quar: AU (R3)", wsize);
            return "AU";
        }
        if (!strcmp(l, "o")) {
            scopy(why, "o: O", wsize);
            return "O";
        }
        scopy(why, "aw spelled au, aw, al, ough: AU", wsize);
        return "AU";
    case V_OH:
        if (!strcmp(l, "o")) {
            scopy(why, "short o spelled o: O", wsize);
            return "O";
        }
        if (one && !strcmp(l, "a")) {
            scopy(why, "short o spelled a: AU", wsize);
            return "AU";
        }
        scopy(why, "short o not spelled o: AU", wsize);
        return "AU";
    }
    (void)last;
    /* a short vowel: by its one letter, or by its sound */
    if (one && l[0]) {
        switch (l[0]) {
        case 'a': scopy(why, "short, spelled a: A", wsize); return "A";
        case 'e': scopy(why, "short, spelled e: E", wsize); return "E";
        case 'i': scopy(why, "short, spelled i: EU", wsize); return "EU";
        case 'y': scopy(why, "short, spelled y: EU", wsize); return "EU";
        case 'o': scopy(why, "short, spelled o: O", wsize); return "O";
        case 'u': scopy(why, "short, spelled u: U", wsize); return "U";
        }
    }
    switch (u->s.ph) {
    case V_AE: scopy(why, "short a: A", wsize); return "A";
    case V_EH: scopy(why, "short e: E", wsize); return "E";
    case V_IH: scopy(why, "short i: EU", wsize); return "EU";
    case V_AH: case V_UH: scopy(why, "short u: U", wsize); return "U";
    case V_ER:
        if (strchr(l, 'i')) { scopy(why, "ir: EU + R", wsize); return "EU"; }
        if (strchr(l, 'u')) { scopy(why, "ur: U + R", wsize); return "U"; }
        if (strchr(l, 'o') && !strchr(l, 'e')) {
            scopy(why, "or: O + R", wsize);
            return "O";
        }
        scopy(why, "er: E + R", wsize);
        return "E";
    }
    /* a schwa with several letters: its first vowel letter */
    for (; *l; l++) {
        switch (*l) {
        case 'a': scopy(why, "schwa, spelled a: A", wsize); return "A";
        case 'e': scopy(why, "schwa, spelled e: E", wsize); return "E";
        case 'i': case 'y':
            scopy(why, "schwa, spelled i: EU", wsize);
            return "EU";
        case 'o': scopy(why, "schwa, spelled o: O", wsize); return "O";
        case 'u': scopy(why, "schwa, spelled u: U", wsize); return "U";
        }
    }
    scopy(why, "schwa: U", wsize);
    return "U";
}

/* letters a unit was written with, for the why lines */
static void unit_letters(const struct gunit *u, int a, int b, char *out,
    int size)
{
    int i;

    out[0] = 0;
    for (i = a; i < b; i++) {
        scat(out, u[i].let, size);
        scat(out, u[i].silent_after, size);
    }
}

/* the left keys for the consonants a..b-1 before a vowel */
static long left_keys(const struct gunit *u, int a, int b, int at_start,
    const char *word, char *why, int wsize)
{
    long st, k;
    const struct conkey *c;
    int i, p;
    char note[96];

    st = 0;
    why[0] = 0;
    for (i = a; i < b; i++) {
        p = u[i].s.ph;
        c = conkey(p);
        if (!c || !c->left)
            continue;
        k = keys_of(c->left);
        note[0] = 0;
        /* spellings Lapwing writes by their letters */
        if (has_let(&u[i], "ph")) {
            k = keys_of("TP*");
            scopy(note, "ph: TP*", sizeof(note));
        } else if (p == P_R && has_let(&u[i], "wr")) {
            k = keys_of("WR");
            scopy(note, "wr: WR", sizeof(note));
        } else if (p == P_W && has_let(&u[i], "wh")) {
            k = keys_of("WH");
            scopy(note, "wh: WH", sizeof(note));
        } else if (p == P_K && i + 1 < b && u[i + 1].s.ph == P_W &&
            has_let(&u[i], "qu"))
        {
            k = keys_of("KW");
            i++;
            scopy(note, "qu: KW", sizeof(note));
        } else if (p == P_Y) {
            if (at_start && word[0] == 'y') {
                k = keys_of("KWH");
                scopy(note, "y at the start: KWH", sizeof(note));
            } else if (at_start) {
                continue;      /* use: the y is in AOU */
            } else {
                scopy(note, "y sound inside a word: KWR", sizeof(note));
            }
        } else if (p == P_Z && has_let(&u[i], "s")) {
            k = keys_of("S");
        } else if (p == P_DH || p == P_TH) {
            k = keys_of("TH");
        } else if (p == P_S && has_let(&u[i], "c") && !at_start) {
            k = keys_of("S");
        }
        if (!fits_after(st, k)) {
            /* sl: SHR; any chord whose keys spell it */
            if (p == P_L && st == keys_of("S"))
                k = keys_of("HR");
            if (!fits_after(st, k))
                return -1;
        }
        st |= k;
        if (note[0]) {
            if (why[0])
                scat(why, ", ", wsize);
            scat(why, note, wsize);
        }
    }
    return st;
}

/* the right keys for the consonants a..b-1 after a vowel; rest gets
 * the first that didn't fit (b when all did) */
static long right_keys(const struct gunit *u, int a, int b, long have,
    int *rest, char *why, int wsize)
{
    long st, k;
    const struct conkey *c;
    int i, j, t, p, n, ok;

    if (!codas_n[0])
        make_codas();
    st = 0;
    why[0] = 0;
    for (i = a; i < b; i++) {
        p = u[i].s.ph;
        k = -1;
        /* two or three sounds with a chord of their own */
        for (j = 0; codas[j].ph; j++) {
            n = codas_n[j];
            if (i + n > b)
                continue;
            ok = 1;
            for (t = 0; t < n; t++)
                if (u[i + t].s.ph != codas_ph[j][t])
                    ok = 0;
            if (ok && fits_after(have | st, keys_of(codas[j].keys))) {
                k = keys_of(codas[j].keys);
                if (why[0])
                    scat(why, ", ", wsize);
                scat(why, codas[j].ph, wsize);
                scat(why, ": ", wsize);
                scat(why, codas[j].keys, wsize);
                i += n - 1;
                break;
            }
        }
        if (k < 0) {
            c = conkey(p);
            if (!c || !c->right || !c->right[0])
                continue;      /* h, w, y at the end: not written */
            k = keys_of(c->right);
            /* any final s spelled s is -S, even said z (Lapwing 5) */
            if (p == P_Z && has_let(&u[i], "s"))
                k = keys_of("-S");
            /* s before another consonant: -F (trust TRUFT, used
             * AOUFD) */
            if ((p == P_S || (p == P_Z && has_let(&u[i], "s"))) &&
                i + 1 < b && conkey(u[i + 1].s.ph) &&
                conkey(u[i + 1].s.ph)->right[0])
            {
                k = keys_of("-F");
            }
        }
        if (!fits_after(have | st, k)) {
            *rest = i;
            return st;
        }
        st |= k;
    }
    *rest = b;
    return st;
}

/* the keys for sounds after the last one that fit: a stroke of their
 * own, no vowel; a t or f/v at its end takes the star (Lapwing 8) */
static long tail_stroke(const struct gunit *u, int a, int b)
{
    long st, k;
    const struct conkey *c;
    int i;

    st = 0;
    for (i = a; i < b; i++) {
        c = conkey(u[i].s.ph);
        if (!c || !c->right || !c->right[0])
            continue;
        k = keys_of(c->right);
        if (!fits_after(st, k))
            return -1;
        st |= k;
    }
    if (st == keys_of("-T") || st == keys_of("-F"))
        st |= 1L << K_STAR;
    return st;
}

/* --- making the outlines ----------------------------------------------- */

struct build {
    long st[STROKE_MAX];
    int n;
    char why[GEN_WHY][GEN_TEXT];
    int nwhy;
};

static void add_why(struct build *b, const char *s)
{
    if (b->nwhy < GEN_WHY) {
        scopy(b->why[b->nwhy], s, GEN_TEXT);
        b->nwhy++;
    }
}

static void stroke_str(long st, char *out)
{
    stroke_text(st, out);
}

/* The write-out: one stroke a syllable. glide: what a stroke that
 * would start with a vowel gets (KWR). Returns 0 when it can't be
 * written. */
static int write_out(const struct gunit *u, int n, const int *sy,
    int nsy, const char *word, struct build *b)
{
    int k, a, e, v, i, rest, r_after, has_v;
    long st, left, right, t;
    char why[GEN_TEXT], w1[GEN_TEXT], w2[GEN_TEXT], w3[GEN_TEXT];
    char lets[64], s[32];
    const char *vk;

    b->n = 0;
    b->nwhy = 0;
    for (k = 0; k < nsy; k++) {
        a = sy[k];
        e = k + 1 < nsy ? sy[k + 1] : n;
        v = -1;
        for (i = a; i < e; i++)
            if (IS_VOWEL(u[i].s.ph)) {
                v = i;
                break;
            }
        if (v < 0)
            return 0;
        left = left_keys(u, a, v, k == 0, word, w1, sizeof(w1));
        if (left < 0)
            return 0;
        /* a stroke after the first must start with a consonant: KWR */
        if (k > 0 && !left && a == v) {
            left = keys_of("KWR");
            scopy(w1, "starts with a vowel: KWR", sizeof(w1));
        }
        r_after = 0;
        for (i = v + 1; i < e; i++)
            if (u[i].s.ph == P_R)
                r_after = 1;
        if (u[v].s.ph == V_IA || u[v].s.ph == V_EA || u[v].s.ph == V_UA)
            r_after = 1;
        vk = vowel_keys(&u[v], r_after, k == nsy - 1, w2, sizeof(w2));
        st = left | keys_of(vk);
        right = right_keys(u, v + 1, e, st, &rest, w3, sizeof(w3));
        st |= right;
        unit_letters(u, a, e, lets, sizeof(lets));
        if (b->n == STROKE_MAX)
            return 0;
        b->st[b->n] = st;
        b->n++;
        stroke_str(st, s);
        sprintf(why, "%.20s -> %.16s  %.30s%s%.30s%s%.30s", lets, s,
            w1, w1[0] ? ", " : "", w2, w3[0] ? ", " : "", w3);
        add_why(b, why);
        if (rest < e) {
            /* what didn't fit: its own stroke (film TPEUL/-PL) */
            has_v = 0;
            t = tail_stroke(u, rest, e);
            if (t <= 0 || b->n == STROKE_MAX)
                return 0;
            b->st[b->n] = t;
            b->n++;
            stroke_str(t, s);
            sprintf(why, "  then %.16s: out of steno order, a stroke of "
                "its own%s", s, (t >> K_STAR) & 1 ?
                " (star: -T is the, -F of)" : "");
            add_why(b, why);
            (void)has_v;
        }
    }
    return 1;
}

/* --- the shorter forms ------------------------------------------------- */

/* the syllables a stroke was made of, as sounds, for the clusters */
static int syl_sounds(const struct gunit *u, int a, int e, int *p, int max)
{
    int n, i;

    n = 0;
    for (i = a; i < e && n < max; i++) {
        if (u[i].s.silent)
            continue;
        p[n] = u[i].s.ph;
        n++;
    }
    return n;
}

struct cluster {
    const char *ph;        /* a whole last syllable, said */
    const char *keys;
    const char *name;
};

/* Compound clusters (Lapwing 5, 10.4): a last unstressed syllable
 * folded into the stroke before when no key overlaps, else a stroke
 * of their own (humble HUPL/-BL). */
static const struct cluster clusters[] = {
    { "SH AX N", "-GS", "-shun: -GS" }, { "ZH AX N", "-GS", "-zhun: -GS" },
    { "K SH AX N", "-BGS", "-kshun: -BGS" }, { "SH N", "-GS", "-shun: -GS" },
    { "M AX N T", "-PLT", "-ment: -PLT" }, { "B AX L", "-BL", "-ble: -BL" },
    { "B L", "-BL", "-ble: -BL" }, { "L AX T", "-LT", "-let: -LT" },
    { "L IH T", "-LT", "-let: -LT" }, { "SH AX S", "-RBS", "-shus: -RBS" },
    { "SH AX L", "-RBL", "-shal: -RBL" }, { "SH L", "-RBL", "-shal: -RBL" },
    { 0, 0, 0 }
};

static int match_sounds(const int *p, int n, const char *spec)
{
    struct gsound s[6];
    int m, i;

    m = parse_sounds(spec, s, 6);
    if (m != n)
        return 0;
    for (i = 0; i < n; i++)
        if (s[i].ph != p[i])
            return 0;
    return 1;
}

/* --- the candidates ------------------------------------------------------ */

static void outline_of(const long *st, int n, char *out, int size)
{
    char s[32];
    int i;

    out[0] = 0;
    for (i = 0; i < n; i++) {
        stroke_text(st[i], s);
        if (i)
            scat(out, "/", size);
        scat(out, s, size);
    }
}

static void add_cand(struct gen *g, const char *outline, const char *note,
    int rank, const struct build *b)
{
    struct gcand *c;
    int i;

    if (!outline[0])
        return;
    for (i = 0; i < g->nc; i++)
        if (!strcmp(g->c[i].outline, outline)) {
            if (rank < g->c[i].rank)
                g->c[i].rank = rank;
            return;
        }
    if (g->nc == GEN_CANDS)
        return;
    c = &g->c[g->nc];
    memset(c, 0, sizeof(*c));
    scopy(c->outline, outline, sizeof(c->outline));
    scopy(c->note, note, sizeof(c->note));
    c->rank = rank;
    if (b) {
        for (i = 0; i < b->nwhy; i++)
            scopy(c->why[i], b->why[i], GEN_TEXT);
        c->nwhy = b->nwhy;
    }
    g->nc++;
}

static void add_build(struct gen *g, const struct build *b, const char *note,
    int rank)
{
    char o[GEN_TEXT];

    outline_of(b->st, b->n, o, sizeof(o));
    add_cand(g, o, note, rank, b);
}

/* The shorter forms of a write-out */
/* a compound cluster as the last syllable, folded into the stroke
 * before when it fits (motion PHOEGS): 1 when b changed */
static int cluster_fold(const struct gunit *u, int n, const int *sy,
    int nsy, struct build *b)
{
    int p[16], np, k;
    long add, prev;
    char s[32], why[GEN_TEXT];

    if (b->n < 2 || nsy < 2 || b->n != nsy)
        return 0;
    np = syl_sounds(u, sy[nsy - 1], n, p, 16);
    for (k = 0; clusters[k].ph; k++) {
        if (!match_sounds(p, np, clusters[k].ph))
            continue;
        add = keys_of(clusters[k].keys);
        prev = b->st[b->n - 2];
        if (!fits_after(prev, add))
            return 0;
        b->st[b->n - 2] = prev | add;
        b->n--;
        stroke_str(b->st[b->n - 1], s);
        sprintf(why, "%.30s folded in: %.16s", clusters[k].name, s);
        add_why(b, why);
        return 1;
    }
    return 0;
}

static void shorter(struct gen *g, const struct gunit *u, int n,
    const int *sy, int nsy, const struct build *full)
{
    struct build b;
    int p[16], np, k, a, e, i;
    long last, add, prev;
    char s[32], why[GEN_TEXT], lets[32];

    if (full->n < 2 || nsy < 2 || full->n != nsy)
        return;
    a = sy[nsy - 1];
    e = n;
    unit_letters(u, a, e, lets, sizeof(lets));
    i = strlen(lets);
    np = syl_sounds(u, a, e, p, 16);
    /* a compound cluster at the end, folded into the stroke before */
    for (k = 0; clusters[k].ph; k++) {
        if (!match_sounds(p, np, clusters[k].ph))
            continue;
        add = keys_of(clusters[k].keys);
        prev = full->st[full->n - 2];
        b = *full;
        if (fits_after(prev, add)) {
            b.st[b.n - 2] = prev | add;
            b.n--;
            stroke_str(b.st[b.n - 1], s);
            sprintf(why, "%.30s folded in: %.16s", clusters[k].name, s);
            add_why(&b, why);
            add_build(g, &b, "cluster folded", 0);
        } else {
            b.st[b.n - 1] = add;
            sprintf(why, "%.30s: its own stroke, keys overlap",
                clusters[k].name);
            add_why(&b, why);
            add_build(g, &b, "cluster", 1);
        }
        return;
    }
    /* -le after a consonant: the consonant on the stroke before,
     * then -L (saddle SAD/-L, little HREUT/-L), or -L folded in
     * (apple APL) */
    if (np == 3 && p[1] == V_AX && p[2] == P_L && i >= 2 &&
        !strcmp(lets + i - 2, "le") && conkey(p[0]) &&
        conkey(p[0])->right[0])
    {
        prev = full->st[full->n - 2];
        add = keys_of(conkey(p[0])->right);
        if (fits_after(prev, add)) {
            b = *full;
            b.st[b.n - 2] = prev | add;
            b.st[b.n - 1] = keys_of("-L");
            stroke_str(prev | add, s);
            sprintf(why, "-le: %.16s, then -L (Lapwing 8)", s);
            add_why(&b, why);
            add_build(g, &b, "-le", 0);
            if (fits_after(prev | add, keys_of("-L"))) {
                b.st[b.n - 2] = prev | add | keys_of("-L");
                b.n--;
                stroke_str(b.st[b.n - 1], s);
                sprintf(why, "-le folded: %.16s", s);
                add_why(&b, why);
                add_build(g, &b, "-le folded", 1);
            }
            return;
        }
    }
    /* r-folding and l-folding: a last syllable "-er", "-or", "-al",
     * "-le" into the stroke before (humor HAOURPL, final TPAOEUPBL) */
    last = full->st[full->n - 1];
    prev = full->st[full->n - 2];
    for (i = 0; i < 2; i++) {
        add = keys_of(i ? "-L" : "-R");
        if (!(last & add))
            continue;
        /* the syllable ends in the sound, or in a written r that
         * RP does not say (humour) */
        k = strlen(lets);
        if (!(np >= 2 && p[np - 1] == (i ? P_L : P_R)) &&
            !(!i && k >= 1 && (lets[k - 1] == 'r' ||
            (k >= 2 && !strcmp(lets + k - 2, "re")))))
        {
            continue;
        }
        /* the stroke's own consonant goes onto the one before too */
        add = last & ~keys_of(i ? "AOEU*" : "AOEU*");
        add &= 0x7fe000L;        /* right hand only */
        if (last & 0x1feL & ~keys_of("KWR")) {
            const struct conkey *c;

            /* one left consonant: its right-hand keys go onto the
             * stroke before (humor HAOU/PHOR, HAOURPL) */
            c = conkey(p[0]);
            if (np < 2 || np > 3 || !c || !c->right || !c->right[0] ||
                !IS_VOWEL(p[1]))
            {
                continue;
            }
            add = keys_of(c->right) | keys_of(i ? "-L" : "-R");
        }
        if (!fits_after(prev, add))
            continue;
        b = *full;
        b.st[b.n - 2] = prev | add;
        b.n--;
        stroke_str(b.st[b.n - 1], s);
        sprintf(why, "%s-folding: the last %s into the stroke before: %.16s",
            i ? "L" : "R", i ? "l" : "r", s);
        add_why(&b, why);
        add_build(g, &b, i ? "l folded" : "r folded", 3);
    }
    /* e-folding: a last long e (happy HAEP): E into the stroke before
     * when its consonant fits on the right */
    if (np == 2 && p[1] == V_IY) {
        const struct conkey *c;

        c = conkey(p[0]);
        if (c && c->right && c->right[0]) {
            add = keys_of(c->right);
            if (fits_after(prev, add) && !(prev & keys_of("E"))) {
                b = *full;
                b.st[b.n - 2] = prev | add | keys_of("E");
                b.n--;
                stroke_str(b.st[b.n - 1], s);
                sprintf(why, "E-folding: the last long e as E: %.16s", s);
                add_why(&b, why);
                add_build(g, &b, "e folded", 3);
            }
        }
    }
    /* t-folding: -ity, -ty (sanity SAPBT) */
    if (np == 2 && p[0] == P_T && p[1] == V_IY) {
        add = keys_of("-T");
        if (fits_after(prev, add) && full->n >= 2) {
            b = *full;
            b.st[b.n - 2] = prev | add;
            b.n--;
            stroke_str(b.st[b.n - 1], s);
            sprintf(why, "T-folding: -ty as -T: %.16s", s);
            add_why(&b, why);
            add_build(g, &b, "t folded", 3);
        }
    }
}

/* drop an unstressed vowel: two strokes as one, when the second's
 * consonants fit on the right of the first (possible POFBL,
 * definite TKEF/TPHEUT) */
static void dropped(struct gen *g, const struct gunit *u, int n,
    const int *sy, int nsy, const struct build *full)
{
    struct build b;
    int k, i, j, v, e, rest;
    long merged, right;
    char why[GEN_TEXT], s[32], w3[GEN_TEXT];

    if (full->n != nsy || nsy < 2)
        return;
    for (k = 1; k < nsy; k++) {
        e = k + 1 < nsy ? sy[k + 1] : n;
        v = -1;
        for (i = sy[k]; i < e; i++)
            if (IS_VOWEL(u[i].s.ph))
                v = i;
        if (v < 0 || u[v].s.stress != 0)
            continue;
        if (u[v].s.ph != V_AX && !(u[v].s.ph == V_IH && k < nsy - 1))
            continue;
        /* the stroke before with this syllable's consonants on its
         * right */
        merged = full->st[k - 1];
        right = 0;
        {
            /* the consonants of syllable k without its vowel */
            struct gunit cu[GEN_UNITS];
            int m;

            m = 0;
            for (j = sy[k]; j < e; j++)
                if (j != v)
                    cu[m++] = u[j];
            right = right_keys(cu, 0, m, merged, &rest, w3, sizeof(w3));
            if (rest < m || !right)
                continue;
        }
        b.n = 0;
        b.nwhy = 0;
        for (j = 0; j < full->n; j++) {
            if (j == k)
                continue;
            b.st[b.n] = j == k - 1 ? merged | right : full->st[j];
            b.n++;
        }
        stroke_str(merged | right, s);
        sprintf(why, "unstressed vowel dropped (Lapwing 14.2): %.16s", s);
        for (j = 0; j < full->nwhy && j < GEN_WHY - 1; j++)
            add_why(&b, full->why[j]);
        add_why(&b, why);
        add_build(g, &b, "vowel dropped", 4);
    }
}

/* --- syllables as written: Liang's hyphenation, British patterns ----- */

#define HY_MAX 9000
static char *hy_data;
static char *hy_pat[HY_MAX];
static int nhy = -1;

static int hy_cmp(const void *a, const void *b)
{
    const char *x, *y;
    char ka[64], kb[64];
    int i, n;

    x = *(const char *const *)a;
    y = *(const char *const *)b;
    for (i = 0, n = 0; x[i] && n < 63; i++)
        if (!(x[i] >= '0' && x[i] <= '9'))
            ka[n++] = x[i];
    ka[n] = 0;
    for (i = 0, n = 0; y[i] && n < 63; i++)
        if (!(y[i] >= '0' && y[i] <= '9'))
            kb[n++] = y[i];
    kb[n] = 0;
    return strcmp(ka, kb);
}

static void hy_load(void)
{
    long size;
    char *p, *e;

    nhy = 0;
    hy_data = read_file(dotterel_data("hyph_en_GB.dic"), &size);
    if (!hy_data)
        return;
    for (p = hy_data; *p && nhy < HY_MAX; p = e) {
        e = strchr(p, '\n');
        if (e) {
            *e = 0;
            e++;
        } else {
            e = p + strlen(p);
        }
        if (p[0] == '%' || p[0] == '#' || !p[0] || (p[0] >= 'A' &&
            p[0] <= 'Z'))
        {
            continue;          /* the charset line, comments */
        }
        p[strcspn(p, "\r ")] = 0;
        hy_pat[nhy] = p;
        nhy++;
    }
    qsort(hy_pat, nhy, sizeof(hy_pat[0]), hy_cmp);
}

static char *hy_find(const char *s, int n)
{
    int lo, hi, mid, c, i, k;
    char key[64];
    const char *x;

    lo = 0;
    hi = nhy - 1;
    while (lo <= hi) {
        mid = (lo + hi) / 2;
        x = hy_pat[mid];
        for (i = 0, k = 0; x[i] && k < 63; i++)
            if (!(x[i] >= '0' && x[i] <= '9'))
                key[k++] = x[i];
        key[k] = 0;
        c = strncmp(key, s, n);
        if (!c && (int)strlen(key) > n)
            c = 1;
        if (!c)
            return hy_pat[mid];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

/* sched|ule: where w may break, at least two letters before and
 * three after (the patterns' lefthyphenmin and righthyphenmin) */
void gen_hyphenate(const char *w, char *out, int size)
{
    char t[AL_LET + 3];
    int pts[AL_LET + 4], n, i, j, k, m;
    const char *pat;

    if (nhy < 0)
        hy_load();
    n = strlen(w);
    if (n > AL_LET || !nhy) {
        scopy(out, w, size);
        return;
    }
    t[0] = '.';
    for (i = 0; i < n; i++)
        t[i + 1] = (char)low_ascii((unsigned char)w[i]);
    t[n + 1] = '.';
    t[n + 2] = 0;
    memset(pts, 0, sizeof(pts));
    for (i = 0; i < n + 2; i++) {
        for (j = i + 1; j <= n + 2; j++) {
            pat = hy_find(t + i, j - i);
            if (!pat)
                continue;
            /* digits between the letters: the higher wins */
            for (k = 0, m = i; pat[k]; k++) {
                if (pat[k] >= '0' && pat[k] <= '9') {
                    if (pat[k] - '0' > pts[m])
                        pts[m] = pat[k] - '0';
                } else {
                    m++;
                }
            }
        }
    }
    out[0] = 0;
    for (i = 0; i < n; i++) {
        /* pts[i + 1]: before letter i of w */
        if (i >= 2 && n - i >= 3 && (pts[i + 1] & 1))
            scat(out, "|", size);
        m = strlen(out);
        if (m + 1 < size) {
            out[m] = w[i];
            out[m + 1] = 0;
        }
    }
}

/* --- the sounds shown -------------------------------------------------- */

static void show_ipa(const struct gsound *s, int n, const int *sy, int nsy,
    const char *const *ipa, char *out, int size)
{
    int i, k, st;

    out[0] = 0;
    scat(out, "/", size);
    for (k = 0; k < nsy; k++) {
        if (k)
            scat(out, " | ", size);
        st = -1;
        for (i = sy[k]; i < (k + 1 < nsy ? sy[k + 1] : n); i++)
            if (IS_VOWEL(s[i].ph))
                st = s[i].stress;
        if (nsy > 1 && st == 1)
            scat(out, "\313\210", size);
        else if (nsy > 1 && st == 2)
            scat(out, "\313\214", size);
        for (i = sy[k]; i < (k + 1 < nsy ? sy[k + 1] : n); i++) {
            if (s[i].silent)
                continue;
            if (s[i].ph == V_IY && s[i].stress == 0 && ipa == ipa_uk)
                scat(out, "i", size);
            else if (s[i].ph == V_ER && s[i].stress == 0 && ipa == ipa_us)
                scat(out, "\311\232", size);
            else
                scat(out, ipa[s[i].ph], size);
        }
    }
    scat(out, "/", size);
}

/* Russian letters, a syllable at a time (·), the stressed one in
 * capitals; British r after a vowel is not said, so not written */
static void show_ru(const struct gsound *s, int n, const int *sy, int nsy,
    int us, char *out, int size)
{
    char syl[128];
    int i, k, st, e, len, at;
    const char *r;

    out[0] = 0;
    for (k = 0; k < nsy; k++) {
        if (k)
            scat(out, "\302\267", size);
        syl[0] = 0;
        st = 0;
        e = k + 1 < nsy ? sy[k + 1] : n;
        for (i = sy[k]; i < e; i++) {
            if (s[i].silent)
                continue;
            if (IS_VOWEL(s[i].ph) && s[i].stress == 1)
                st = 1;
            if (s[i].ph == P_Y && i + 1 < n && ru_after_y[s[i + 1].ph]) {
                continue;      /* merged with the vowel */
            }
            if (i > 0 && s[i - 1].ph == P_Y && ru_after_y[s[i].ph]) {
                scat(syl, ru_after_y[s[i].ph], sizeof(syl));
                continue;
            }
            if (s[i].ph == P_R && !us && i > 0 && IS_VOWEL(s[i - 1].ph) &&
                !(i + 1 < n && IS_VOWEL(s[i + 1].ph)))
            {
                continue;
            }
            r = ru_sound[s[i].ph];
            if (s[i].ph == V_IY && s[i].stress == 0)
                r = "\320\270";
            if (us && s[i].ph == V_OW)
                r = "\320\276\321\203";
            if (us && s[i].ph == V_ER)
                r = "\321\221\321\200";
            scat(syl, r, sizeof(syl));
        }
        if (st && nsy > 1) {
            len = strlen(syl);
            for (at = 0; at < len; )
                at += case_at(syl + at, len - at, 1);
        }
        scat(out, syl, size);
    }
}

/* the spoken syllables' letters: sched|ule from the units */
static void spoken_letters(const struct gunit *u, int n, const int *sy,
    int nsy, char *out, int size)
{
    int k, i, e;

    out[0] = 0;
    for (k = 0; k < nsy; k++) {
        if (k)
            scat(out, " | ", size);
        e = k + 1 < nsy ? sy[k + 1] : n;
        for (i = sy[k]; i < e; i++) {
            scat(out, u[i].let, size);
            scat(out, u[i].silent_after, size);
        }
    }
}

/* --- a guess from the spelling, for a word no list has --------------- */

static int guess_sounds(const char *w, struct gsound *s, int max)
{
    static const struct rule g[] = {
        { "tion", "SH AX N", 0 }, { "sion", "ZH AX N", 0 },
        { "ture", "CH AX", 0 }, { "ough", "OW", 0 }, { "igh", "AY", 0 },
        { "tch", "CH", 0 }, { "dge", "JH", 0 }, { "sch", "S K", 0 },
        { "ee", "IY1", 0 }, { "ea", "IY1", 0 }, { "oo", "UW1", 0 },
        { "ai", "EY1", 0 }, { "ay", "EY1", 0 }, { "oa", "OW1", 0 },
        { "ou", "AW1", 0 }, { "ow", "OW1", 0 }, { "oi", "OY1", 0 },
        { "oy", "OY1", 0 }, { "au", "AO1", 0 }, { "aw", "AO1", 0 },
        { "ew", "Y UW1", 0 }, { "ie", "IY1", 0 }, { "ei", "EY1", 0 },
        { "ch", "CH", 0 }, { "sh", "SH", 0 }, { "th", "TH", 0 },
        { "ph", "F", 0 }, { "wh", "W", 0 }, { "ck", "K", 0 },
        { "ng", "NG", 0 }, { "qu", "K W", 0 }, { "kn", "N", 0 },
        { "wr", "R", 0 }, { "le", "AX0 L", 0 }, { "er", "AX0 R", 0 },
        { "a", "AE1", 0 }, { "e", "EH1", 0 }, { "i", "IH1", 0 },
        { "o", "OH1", 0 }, { "u", "AH1", 0 }, { "y", "IY0", 0 },
        { "b", "B", 0 }, { "c", "K", 0 }, { "d", "D", 0 }, { "f", "F", 0 },
        { "g", "G", 0 }, { "h", "HH", 0 }, { "j", "JH", 0 }, { "k", "K", 0 },
        { "l", "L", 0 }, { "m", "M", 0 }, { "n", "N", 0 }, { "p", "P", 0 },
        { "r", "R", 0 }, { "s", "S", 0 }, { "t", "T", 0 }, { "v", "V", 0 },
        { "w", "W", 0 }, { "x", "K S", 0 }, { "z", "Z", 0 },
        { 0, 0, 0 }
    };
    struct gsound t[4];
    int n, i, k, r, q, j, stressed, nv;

    n = 0;
    stressed = 0;
    nv = 0;
    for (i = 0; w[i]; ) {
        for (r = 0; g[r].let; r++) {
            k = strlen(g[r].let);
            if (!strncmp(w + i, g[r].let, k))
                break;
        }
        if (!g[r].let) {
            i++;
            continue;
        }
        /* a final e is silent; a vowel, consonant, e: long (make) */
        if (w[i] == 'e' && !w[i + 1] && n > 0) {
            i++;
            continue;
        }
        q = parse_sounds(g[r].ph, t, 4);
        for (j = 0; j < q && n < max; j++) {
            s[n] = t[j];
            if (IS_VOWEL(s[n].ph)) {
                nv++;
                if (s[n].stress == 1 && stressed)
                    s[n].stress = 0;
                if (s[n].stress == 1)
                    stressed = 1;
                if (k == 1 && w[i + 1] && !is_v((unsigned char)w[i + 1]) &&
                    w[i + 2] == 'e' && !w[i + 3])
                {
                    /* magic e */
                    switch (w[i]) {
                    case 'a': s[n].ph = V_EY; break;
                    case 'i': s[n].ph = V_AY; break;
                    case 'o': s[n].ph = V_OW; break;
                    case 'u': s[n].ph = V_UW; break;
                    case 'e': s[n].ph = V_IY; break;
                    }
                }
            }
            n++;
        }
        i += k;
    }
    (void)nv;
    return n;
}

/* --- all of it --------------------------------------------------------- */

static int rank_cmp(const void *a, const void *b)
{
    const struct gcand *x, *y;

    x = a;
    y = b;
    if (x->rank != y->rank)
        return x->rank - y->rank;
    return outline_text_cmp(x->outline, y->outline);
}

/* what an outline types now, and from which dictionary */
static void cand_now(struct gcand *c, const char *word)
{
    char out[TEXT_MAX * 2], *nl;

    outline_says(c->outline, out, sizeof(out));
    nl = strchr(out, '\n');
    if (nl)
        *nl = 0;
    scopy(c->now, out, sizeof(c->now));
    c->free = !out[0] || strstr(out, "(no entry") != 0;
    /* "word  (dict)": the same word? */
    c->same = out[0] && !strncmp(out, word, strlen(word)) &&
        (out[strlen(word)] == ' ' || !out[strlen(word)]);
}

/* sounds for a word: British, else made from the American; 0 none */
static int sounds_of(const char *w, struct gsound *uk)
{
    static char lines[2][GEN_TEXT];
    static struct gsound us[GEN_UNITS];
    int n;

    n = gen_find("pron-uk.txt", w, lines, 1);
    if (n > 0)
        return parse_sounds(lines[0], uk, GEN_UNITS);
    n = gen_find("pron-us.txt", w, lines, 1);
    if (n <= 0)
        return 0;
    n = parse_sounds(lines[0], us, GEN_UNITS);
    return n ? us_to_uk(us, n, w, uk) : 0;
}

/* a word's write-out, from its sound */
static int stem_writeout(const char *w, struct build *b)
{
    static struct gsound s[GEN_UNITS];
    static struct gunit u[GEN_UNITS];
    static int sy[GEN_UNITS];
    int n, nu, nsy;

    n = sounds_of(w, s);
    if (!n)
        return 0;
    nu = align(w, s, n, u, GEN_UNITS);
    if (!nu)
        return 0;
    nsy = syllabify(u, nu, sy, GEN_UNITS);
    if (!nsy || !write_out(u, nu, sy, nsy, w, b))
        return 0;
    cluster_fold(u, nu, sy, nsy, b);
    return 1;
}

struct inflection {
    const char *end;       /* the ending as written */
    const char *keys;      /* its stroke */
    int fold;              /* 1: folded first (Dotterel's years
                              KWHAOERZ), 0: a stroke of its own first
                              (Lapwing's going TKPWOE/-G) */
    const char *name;
};

static const struct inflection inflections[] = {
    { "ies", "-Z", 1, "-s" }, { "es", "-Z", 1, "-s" }, { "s", "-Z", 1, "-s" },
    { "ied", "-D", 1, "-ed" }, { "ed", "-D", 1, "-ed" },
    { "d", "-D", 1, "-ed" }, { "ing", "-G", 0, "-ing" },
    { "ily", "HREU", 0, "-ly" }, { "ly", "HREU", 0, "-ly" },
    { "iness", "-PBS", 0, "-ness" }, { "ness", "-PBS", 0, "-ness" },
    { "ment", "-PLT", 1, "-ment" }, { "ful", "-FL", 0, "-ful" },
    { "less", "-LS", 0, "-less" }, { "er", "*ER", 0, "-er" },
    { "est", "KWREFT", 0, "-est" }, { "able", "-BL", 1, "-able" },
    { "y", "KWREU", 0, "-y" },
    { 0, 0, 0, 0 }
};

/* The word as a stem and an ending (going: go + -ing), the stem from
 * its own sound: as Plover's spelling rules join them (make + ing,
 * get + ing, carry + ed, happy + ly) */
static void inflected(struct gen *g, const char *w)
{
    static struct build b, f, keep;
    char stem[AL_LET + 2], v[AL_LET + 2], s[32], why[GEN_TEXT];
    char best[AL_LET + 2];
    int n, k, i, t, m, r, best_rank, best_len;
    long add, last;
    const struct inflection *x, *bx;

    /* of every ending w can have, the stem Plover's rules join to w
     * that is longest, then commonest (bees: bee + s, not be + es;
     * making: make, not mak) */
    n = strlen(w);
    bx = 0;
    best[0] = 0;
    best_rank = 0;
    best_len = 0;
    for (i = 0; inflections[i].end; i++) {
        x = &inflections[i];
        k = strlen(x->end);
        if (n - k < 2 || strcmp(w + n - k, x->end))
            continue;
        memcpy(stem, w, n - k);
        stem[n - k] = 0;
        m = n - k;
        for (t = 0; t < 4; t++) {
            scopy(v, stem, sizeof(v));
            if (t == 1)
                scat(v, "e", sizeof(v));        /* make + ing */
            else if (t == 2 && m >= 3 && stem[m - 1] == stem[m - 2])
                v[m - 1] = 0;                   /* get + ing */
            else if (t == 3 && x->end[0] == 'i' && strcmp(x->end, "ing"))
                scat(v, "y", sizeof(v));        /* carry + ied */
            else if (t)
                continue;
            if (strlen(v) < 2)
                continue;
            add_suffix(v, x->end[0] == 'i' && strcmp(x->end, "ing") ?
                x->end + 1 : x->end, s, sizeof(s));
            if (strcmp(s, w))
                continue;
            r = words_rank(v);
            if (r < 0)
                continue;
            if (bx && ((int)strlen(v) < best_len ||
                ((int)strlen(v) == best_len && r >= best_rank)))
            {
                continue;
            }
            if (!stem_writeout(v, &b))
                continue;
            scopy(best, v, sizeof(best));
            best_rank = r;
            best_len = strlen(v);
            keep = b;
            bx = x;
        }
    }
    if (!bx)
        return;
    x = bx;
    b = keep;
    scopy(v, best, sizeof(v));
    add = keys_of(x->keys);
    last = b.st[b.n - 1];
    /* -s after a t folds as -S (crates KRAEUTS) */
    if (add == keys_of("-Z") && (last & keys_of("-T")))
        add = keys_of("-S");
    f = b;
    sprintf(why, "%.30s + %.8s: the word, then %.12s", v, x->name,
        x->keys);
    if (b.n < STROKE_MAX) {
        f.st[f.n] = add;
        f.n++;
        add_why(&f, why);
        add_build(g, &f, x->name, x->fold ? 1 : 0);
    }
    /* folded into the last stroke when its key is free */
    if (!(last & add & 0x7fe000L) && (add & 0x7fe000L) &&
        !(add & 0x1fffL))
    {
        f = b;
        /* s before the d: -F (based PWAEUFD) */
        if (add == keys_of("-D") && (last & keys_of("-S")) &&
            !(last & keys_of("-F")))
        {
            last = (last & ~keys_of("-S")) | keys_of("-F");
        }
        f.st[f.n - 1] = last | add;
        stroke_str(f.st[f.n - 1], s);
        sprintf(why, "%.30s + %.8s folded: %.16s", v, x->name, s);
        add_why(&f, why);
        add_build(g, &f, "folded", x->fold ? 0 : 2);
    }
}

/* Two words in one (heartbeat HART/PWAO*ET): each written out, the
 * star on the first stroke of the second (Lapwing 11). Both parts
 * must be words of three letters or more; carpet is car + pet too,
 * so it comes after the write-out. */
static void compound(struct gen *g, const char *w)
{
    static struct build a, b, c;
    char x[AL_LET + 1], why[GEN_TEXT];
    int n, k, j;

    n = strlen(w);
    for (k = 3; k <= n - 3; k++) {
        memcpy(x, w, k);
        x[k] = 0;
        /* both common words (SCOWL size 50 or less): not dic+tionary */
        if (words_rank(x) < 0 || words_rank(w + k) < 0 ||
            words_rank(x) > 50 || words_rank(w + k) > 50)
        {
            continue;
        }
        if (!stem_writeout(x, &a) || !stem_writeout(w + k, &b))
            continue;
        if (a.n + b.n > STROKE_MAX)
            continue;
        c = a;
        for (j = 0; j < b.n; j++) {
            c.st[c.n] = b.st[j];
            if (j == 0)
                c.st[c.n] |= 1L << K_STAR;
            c.n++;
        }
        sprintf(why, "%.20s + %.20s: two words, the star on the second "
            "(Lapwing 11)", x, w + k);
        add_why(&c, why);
        add_build(g, &c, "compound", 1);
    }
}

/* letter by letter: a star in every stroke */
static int fingerspelled(const char *o)
{
    int strokes, stars;

    for (strokes = 1, stars = 0; *o; o++) {
        strokes += *o == '/';
        stars += *o == '*';
    }
    return strokes > 1 && stars >= strokes;
}

static void make_outlines(struct gen *g, const struct gunit *u, int n)
{
    static int sy[GEN_UNITS];
    struct build full;
    int nsy, i, k;
    char o[TEXT_MAX], outs[8][GEN_TEXT], *p, *e;

    inflected(g, g->word);
    compound(g, g->word);
    nsy = syllabify(u, n, sy, GEN_UNITS);
    if (nsy && write_out(u, n, sy, nsy, g->word, &full)) {
        /* after a stem + ending, the word written out is second */
        add_build(g, &full, "write-out", g->nc ? 1 : 0);
        shorter(g, u, n, sy, nsy, &full);
        dropped(g, u, n, sy, nsy, &full);
    }
    /* the word's affixes, from your dictionaries: KAT/-S */
    if (word_guesses(g->word, o, sizeof(o), 8)) {
        k = 0;
        for (p = o; *p && k < 8; p = e) {
            while (*p == ' ')
                p++;
            if (!*p)
                break;
            e = p;
            while (*e && *e != ' ')
                e++;
            memcpy(outs[k], p, e - p);
            outs[k][e - p] = 0;
            k++;
        }
        for (i = 0; i < k; i++) {
            /* letter by letter only when nothing else */
            if (g->nc && fingerspelled(outs[i]))
                continue;
            if (strchr(outs[i], '/') && g->nc) {
                int slashes, j;

                for (slashes = 0, j = 0; outs[i][j]; j++)
                    slashes += outs[i][j] == '/';
                if (slashes >= 3)
                    continue;
            }
            add_cand(g, outs[i], "affix", 1, 0);
        }
    }
}

int gen_word(const char *word, struct gen *g)
{
    static char lines[4][GEN_TEXT];
    static struct gsound us[GEN_UNITS], uk[GEN_UNITS];
    static struct gunit u[GEN_UNITS];
    char w[AL_LET + 1];
    int i, n, nu, ok;

    memset(g, 0, sizeof(*g));
    scopy(g->word, word, sizeof(g->word));
    n = strlen(word);
    if (!n || n > AL_LET)
        return 0;
    for (i = 0; i <= n; i++)
        w[i] = (char)low_ascii((unsigned char)word[i]);
    gen_hyphenate(word, g->written, sizeof(g->written));
    /* the American sound */
    n = gen_find("pron-us.txt", w, lines, 2);
    g->nus = n > 0 ? parse_sounds(lines[0], g->us, GEN_UNITS) : 0;
    /* the British one: Britfone's, else made from the American */
    n = gen_find("pron-uk.txt", w, lines, 2);
    g->nuk = n > 0 ? parse_sounds(lines[0], g->uk, GEN_UNITS) : 0;
    if (!g->nuk && g->nus) {
        g->nuk = us_to_uk(g->us, g->nus, w, g->uk);
        g->uk_from_us = 1;
    }
    if (!g->nuk) {
        g->nuk = guess_sounds(w, g->uk, GEN_UNITS);
        g->guessed = 1;
    }
    (void)us;
    (void)uk;
    /* line the letters up with the British sound */
    nu = align(w, g->uk, g->nuk, u, GEN_UNITS);
    ok = nu > 0;
    if (ok) {
        g->nsyl = syllabify(u, nu, g->sy, GEN_UNITS);
        for (i = 0; i < nu; i++)
            g->ukr[i] = u[i].s;
        g->nukr = nu;
        spoken_letters(u, nu, g->sy, g->nsyl, g->spoken, sizeof(g->spoken));
        show_ipa(g->ukr, nu, g->sy, g->nsyl, ipa_uk, g->uk_ipa,
            sizeof(g->uk_ipa));
        show_ru(g->ukr, nu, g->sy, g->nsyl, 0, g->uk_ru, sizeof(g->uk_ru));
        make_outlines(g, u, nu);
    } else {
        g->nsyl = 0;
        make_outlines(g, u, 0);
    }
    if (g->nus) {
        /* the American, split the same way where it can be */
        nu = align(w, g->us, g->nus, u, GEN_UNITS);
        if (nu > 0) {
            static int sy2[GEN_UNITS];
            static struct gsound s2[GEN_UNITS];
            int ns;

            ns = syllabify(u, nu, sy2, GEN_UNITS);
            for (i = 0; i < nu; i++)
                s2[i] = u[i].s;
            show_ipa(s2, nu, sy2, ns, ipa_us, g->us_ipa,
                sizeof(g->us_ipa));
            show_ru(s2, nu, sy2, ns, 1, g->us_ru, sizeof(g->us_ru));
        } else {
            static int one[1];

            show_ipa(g->us, g->nus, one, 1, ipa_us, g->us_ipa,
                sizeof(g->us_ipa));
            show_ru(g->us, g->nus, one, 1, 1, g->us_ru, sizeof(g->us_ru));
        }
    }
    for (i = 0; i < g->nc; i++)
        cand_now(&g->c[i], word);
    /* a write-out that types something else now: AE for a long a or
     * e (Lapwing 7.2: made PHAED, team TAEPL), then the star (7)
     * (the plain one stays, to be chosen anyway) */
    n = g->nc;
    for (i = 0; i < n; i++) {
        long st[STROKE_MAX], aeu, aoe, ae;
        int k, j, t;
        char o[GEN_TEXT];
        struct build b;

        if (g->c[i].free || g->c[i].same || g->c[i].rank > 1)
            continue;
        k = outline_parse(g->c[i].outline, st, STROKE_MAX);
        if (k <= 0)
            continue;
        aeu = keys_of("AEU");
        aoe = keys_of("AOE");
        ae = keys_of("AE");
        for (t = 0; t < 2; t++) {
            for (j = 0; j < k; j++) {
                if (t == 0 && ((st[j] & 0x1b00L) == aeu ||
                    (st[j] & 0x1b00L) == aoe))
                {
                    st[j] = (st[j] & ~0x1b00L) | ae;
                    break;
                }
                if (t == 1 && !(st[j] & (1L << K_STAR))) {
                    st[j] |= 1L << K_STAR;
                    break;
                }
            }
            if (j == k)
                continue;
            outline_of(st, k, o, sizeof(o));
            b.n = 0;
            b.nwhy = 0;
            for (j = 0; j < g->c[i].nwhy; j++)
                add_why(&b, g->c[i].why[j]);
            add_why(&b, t ? "taken: the star tells it apart (Lapwing 7)" :
                "taken: AE, the low-priority long a / long e (Lapwing "
                "7.2)");
            add_cand(g, o, t ? "star" : "AE", g->c[i].rank + 1 + t, &b);
            cand_now(&g->c[g->nc - 1], word);
            if (g->c[g->nc - 1].free)
                break;         /* AE was free: no star needed */
            k = outline_parse(g->c[i].outline, st, STROKE_MAX);
        }
    }
    /* best first: a free or already-this-word outline before a taken
     * one, then the theory's order */
    for (i = 0; i < g->nc; i++)
        if (!g->c[i].free && !g->c[i].same)
            g->c[i].rank += 10;
    qsort(g->c, g->nc, sizeof(g->c[0]), rank_cmp);
    return 1;
}

/* the sound names, "SH EH1 JH UW0 L", for -G's output */
void gen_sounds_text(const struct gsound *s, int n, char *out, int size)
{
    int i;
    char t[8];

    out[0] = 0;
    for (i = 0; i < n; i++) {
        if (s[i].silent)
            continue;
        if (out[0])
            scat(out, " ", size);
        scat(out, ph_name[s[i].ph], size);
        if (IS_VOWEL(s[i].ph)) {
            sprintf(t, "%d", s[i].stress);
            scat(out, t, size);
        }
    }
}

/* steno -G WORD: all of it as text (and for testing the rules) */
int gen_print(const char *word)
{
    static struct gen g;
    char t[GEN_TEXT];
    int i, k;

    if (!gen_word(word, &g)) {
        printf("%s: can't make anything of it\n", word);
        return 1;
    }
    printf("word     %s\n", g.word);
    printf("written  %s\n", g.written);
    printf("spoken   %s\n", g.spoken);
    gen_sounds_text(g.ukr, g.nukr, t, sizeof(t));
    printf("UK       %s  %s  %s%s\n", g.uk_ipa, g.uk_ru, t,
        g.guessed ? "  (guessed from the spelling)" :
        g.uk_from_us ? "  (made from the American)" : "");
    if (g.nus)
        printf("US       %s  %s\n", g.us_ipa, g.us_ru);
    for (i = 0; i < g.nc; i++) {
        printf("%-22s %-15s %s\n", g.c[i].outline, g.c[i].note,
            g.c[i].same ? "already this word" : g.c[i].free ? "free" :
            g.c[i].now);
        for (k = 0; k < g.c[i].nwhy; k++)
            printf("    %s\n", g.c[i].why[k]);
    }
    return 0;
}

/* steno -G WORD, or -G - for words from stdin, one a line */
int gen_main(const char *word)
{
    char line[256];
    int n;

    if (strcmp(word, "-"))
        return gen_print(word);
    while (fgets(line, sizeof(line), stdin)) {
        n = strcspn(line, "\r\n");
        line[n] = 0;
        if (!n)
            continue;
        gen_print(line);
        printf("\n");
        fflush(stdout);
    }
    return 0;
}
