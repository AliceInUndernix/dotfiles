#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include "kspkan.h"

/* Finding game folders without being told: the places Steam, GOG and
   Heroic put the game, every Steam library, and the home directory a
   few levels down.  Only directories are read; nothing is written but
   the list of remembered games. */

#define GM_DEPTH 3
#define GM_VISITS 20000
#define GM_MAX 32

static const char *gm_where[] = {
    ".steam/steam/steamapps/common/Kerbal Space Program",
    ".local/share/Steam/steamapps/common/Kerbal Space Program",
    ".var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/"
        "common/Kerbal Space Program",
    "GOG Games/Kerbal Space Program/game",
    "GOG Games/Kerbal Space Program",
    "Games/Heroic/Kerbal Space Program/game",
    "Games/Heroic/Kerbal Space Program",
    "Games/kerbal-space-program/drive_c/GOG Games/Kerbal Space Program",
    NULL
};

static const char *gm_steam[] = {
    ".steam/steam/steamapps/libraryfolders.vdf",
    ".local/share/Steam/steamapps/libraryfolders.vdf",
    ".var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/"
        "libraryfolders.vdf",
    NULL
};

static char *cat3(const char *a, const char *b, const char *c)
{
    char *p;

    p = (char *)xalloc((long)(strlen(a) + strlen(b) + strlen(c)) + 1);
    strcpy(p, a);
    strcat(p, b);
    strcat(p, c);
    return p;
}

static int is_dir(const char *p)
{
    struct stat st;

    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int has(const char *dir, const char *rel)
{
    struct stat st;
    char *p;
    int rc;

    p = cat3(dir, "/", rel);
    rc = stat(p, &st);
    free(p);
    return rc == 0;
}

/* a real game folder: GameData with the stock Squad folder, or
   GameData next to the game's own program */
static int is_ksp(const char *dir)
{
    if (!has(dir, "GameData")) {
        return 0;
    }
    return has(dir, "GameData/Squad") || has(dir, "KSP.x86_64") ||
        has(dir, "KSP_x64.exe") || has(dir, "buildID64.txt");
}

/* the same directory, whatever the path (symlinks, //, trailing /) */
int same_dir(const char *a, const char *b)
{
    struct stat sa, sb;

    if (stat(a, &sa) != 0 || stat(b, &sb) != 0) {
        return strcmp(a, b) == 0;
    }
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

static void gm_add(struct strs *out, const char *dir)
{
    int i;

    if (out->n >= GM_MAX || !is_ksp(dir)) {
        return;
    }
    for (i = 0; i < out->n; i++) {
        if (same_dir(out->v[i], dir)) {
            return;
        }
    }
    strs_add(out, xstrdup(dir));
}

/* "path"  "/mnt/games/SteamLibrary" lines of libraryfolders.vdf */
static void gm_libraries(const char *vdf, struct strs *out)
{
    char *text, *p, *q, *dir;

    text = read_file(vdf, NULL, 1L << 20);
    if (text == NULL) {
        return;
    }
    p = text;
    while ((p = strstr(p, "\"path\"")) != NULL) {
        p += 6;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p != '"') {
            continue;
        }
        p++;
        q = strchr(p, '"');
        if (q == NULL) {
            break;
        }
        *q = 0;
        dir = cat3(p, "/steamapps/common/", "Kerbal Space Program");
        gm_add(out, dir);
        free(dir);
        p = q + 1;
    }
    free(text);
}

/* the home directory, a few levels down, hidden directories left out */
static void gm_walk(const char *dir, int depth, int *visits,
    struct strs *out)
{
    DIR *d;
    struct dirent *de;
    struct strs subs;
    char *p;
    int i;

    if (depth > GM_DEPTH || *visits > GM_VISITS) {
        return;
    }
    (*visits)++;
    if (is_ksp(dir)) {
        gm_add(out, dir);
        return;
    }
    d = opendir(dir);
    if (d == NULL) {
        return;
    }
    subs.v = NULL;
    subs.n = 0;
    subs.cap = 0;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') {
            continue;
        }
        p = cat3(dir, "/", de->d_name);
        if (is_dir(p)) {
            strs_add(&subs, p);
        } else {
            free(p);
        }
    }
    closedir(d);
    for (i = 0; i < subs.n; i++) {
        gm_walk(subs.v[i], depth + 1, visits, out);
    }
    strs_free(&subs);
}

/* the game folders found; their count */
int games_search(struct strs *out)
{
    const char *h;
    char *p;
    int i, visits;

    h = getenv("HOME");
    if (h == NULL || h[0] != '/') {
        return 0;
    }
    for (i = 0; gm_where[i] != NULL; i++) {
        p = cat3(h, "/", gm_where[i]);
        gm_add(out, p);
        free(p);
    }
    for (i = 0; gm_steam[i] != NULL; i++) {
        p = cat3(h, "/", gm_steam[i]);
        gm_libraries(p, out);
        free(p);
    }
    visits = 0;
    gm_walk(h, 0, &visits, out);
    return out->n;
}

/* a name for a found game: steam, gog, heroic, else the folder's name;
   -2, -3 ... when taken */
static void gm_name(struct ctx *c, const char *dir, char *out)
{
    const char *b, *e;
    char base[48];
    int i, n;

    if (strstr(dir, "/steamapps/") != NULL) {
        strcpy(base, "steam");
    } else if (strstr(dir, "GOG Games") != NULL) {
        strcpy(base, "gog");
    } else if (strstr(dir, "/Heroic/") != NULL) {
        strcpy(base, "heroic");
    } else {
        e = dir + strlen(dir);
        b = e;
        while (b > dir && b[-1] != '/') {
            b--;
        }
        n = 0;
        for (; b < e && n < 30; b++) {
            base[n] = (char)((*b >= 'a' && *b <= 'z') || (*b >= 'A' &&
                *b <= 'Z') || is_digit(*b) || *b == '_' || *b == '.' ?
                *b : '-');
            n++;
        }
        base[n] = 0;
        if (n == 0 || base[0] == '.' || strcmp(base, "game") == 0) {
            strcpy(base, "ksp");
        }
    }
    strcpy(out, base);
    for (i = 2; inst_index(c, out) >= 0 && i < 100; i++) {
        sprintf(out, "%s-%d", base, i);
    }
}

/* remember every game folder found that is not known (or forgotten)
   yet; the count added */
int games_autoadd(struct ctx *c, int quiet)
{
    struct strs found;
    char name[64];
    int i, k, known, added;

    if (c->datadir == NULL) {
        return 0;
    }
    found.v = NULL;
    found.n = 0;
    found.cap = 0;
    games_search(&found);
    added = 0;
    for (i = 0; i < found.n; i++) {
        known = 0;
        for (k = 0; k < c->igames.n && !known; k++) {
            known = same_dir(c->igames.v[k], found.v[i]);
        }
        for (k = 0; k < c->iskip.n && !known; k++) {
            known = same_dir(c->iskip.v[k], found.v[i]);
        }
        if (known) {
            continue;
        }
        gm_name(c, found.v[i], name);
        strs_add(&c->inames, xstrdup(name));
        strs_add(&c->igames, xstrdup(found.v[i]));
        if (c->idefault == NULL) {
            c->idefault = xstrdup(name);
        }
        if (!quiet) {
            printf("found a game: %s  %s\n", name, found.v[i]);
        }
        added++;
    }
    strs_free(&found);
    if (added > 0) {
        inst_save(c);
    }
    return added;
}

/* what is in a remembered game, read without switching to it */
void game_info(struct ctx *c, int idx, struct gameinfo *g)
{
    static const char *dlc[] = {
        "MakingHistory", "Making History", "Serenity", "Breaking Ground",
        NULL
    };
    DIR *d;
    struct dirent *de;
    char *p, *text, *line, *nl, *t;
    const char *dir;
    int i;

    memset(g, 0, sizeof(*g));
    dir = c->igames.v[idx];
    g->version = game_version(dir);
    p = cat3(c->datadir, "/inst/", c->inames.v[idx]);
    t = cat3(p, "/", "installed");
    free(p);
    text = read_file(t, NULL, 1L << 26);
    free(t);
    line = text;
    while (line != NULL && *line) {
        nl = strchr(line, '\n');
        if (nl != NULL) {
            *nl = 0;
        }
        t = strrchr(line, '\t');
        if (line[0] == 'M' && t != NULL) {
            if (strlen(line) > 4 && strstr(line, "-DLC\t") != NULL) {
                /* counted below, from the folder */
            } else if (t[1] != 0 && t[2] == 'f') {
                g->found++;
            } else if (t[1] == '1') {
                g->deps++;
            } else {
                g->chosen++;
            }
        }
        if (nl == NULL) {
            break;
        }
        line = nl + 1;
    }
    free(text);
    g->mods = g->chosen + g->deps + g->found;
    p = cat3(dir, "/", "GameData");
    d = opendir(p);
    free(p);
    while (d != NULL && (de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.' || strcmp(de->d_name, "Squad") == 0 ||
            strcmp(de->d_name, "SquadExpansion") == 0)
        {
            continue;
        }
        p = cat3(dir, "/GameData/", de->d_name);
        if (is_dir(p)) {
            g->folders++;
        }
        free(p);
    }
    if (d != NULL) {
        closedir(d);
    }
    for (i = 0; dlc[i] != NULL; i += 2) {
        p = cat3(dir, "/GameData/SquadExpansion/", dlc[i]);
        if (is_dir(p)) {
            if (g->dlcs[0] != 0) {
                strcat(g->dlcs, ", ");
            }
            strcat(g->dlcs, dlc[i + 1]);
        }
        free(p);
    }
}

void game_info_free(struct gameinfo *g)
{
    free(g->version);
    g->version = NULL;
}
