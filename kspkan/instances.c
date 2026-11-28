#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "kspkan.h"

/* datadir/instances holds lines
     I<TAB>name<TAB>game folder
     D<TAB>name            (the one used when none is named)
     X<TAB>game folder     (forgotten: not added again when found) */

char *data_dir(void)
{
    const char *x, *h;
    char *p;

    x = getenv("XDG_DATA_HOME");
    if (x != NULL && x[0] == '/') {
        p = (char *)xalloc((long)strlen(x) + 16);
        strcpy(p, x);
        strcat(p, "/kspkan");
        return p;
    }
    h = getenv("HOME");
    if (h == NULL || h[0] != '/') {
        return NULL;
    }
    p = (char *)xalloc((long)strlen(h) + 32);
    strcpy(p, h);
    strcat(p, "/.local/share/kspkan");
    return p;
}

int name_valid(const char *s)
{
    int i;

    if (s[0] == 0 || strlen(s) > 40) {
        return 0;
    }
    for (i = 0; s[i]; i++) {
        if (!((s[i] >= 'a' && s[i] <= 'z') ||
            (s[i] >= 'A' && s[i] <= 'Z') || is_digit(s[i]) ||
            s[i] == '_' || s[i] == '-' || s[i] == '.'))
        {
            return 0;
        }
    }
    return s[0] != '.';
}

int inst_index(struct ctx *c, const char *name)
{
    int i;

    for (i = 0; i < c->inames.n; i++) {
        if (strcmp(c->inames.v[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

int inst_load(struct ctx *c)
{
    char *p, *text, *line, *nl, *tab;
    long len;

    c->inames.v = c->igames.v = c->iskip.v = NULL;
    c->inames.n = c->igames.n = c->inames.cap = c->igames.cap = 0;
    c->iskip.n = c->iskip.cap = 0;
    c->idefault = NULL;
    if (c->datadir == NULL) {
        return -1;
    }
    p = (char *)xalloc((long)strlen(c->datadir) + 16);
    strcpy(p, c->datadir);
    strcat(p, "/instances");
    text = read_file(p, &len, 1L << 20);
    free(p);
    if (text == NULL) {
        return 0;
    }
    line = text;
    while (*line) {
        nl = strchr(line, '\n');
        if (nl != NULL) {
            *nl = 0;
        }
        if (line[0] == 'I' && line[1] == '\t') {
            tab = strchr(line + 2, '\t');
            if (tab != NULL) {
                *tab = 0;
                if (name_valid(line + 2) && tab[1] == '/') {
                    strs_add(&c->inames, xstrdup(line + 2));
                    strs_add(&c->igames, xstrdup(tab + 1));
                }
            }
        } else if (line[0] == 'X' && line[1] == '\t' && line[2] == '/') {
            strs_add(&c->iskip, xstrdup(line + 2));
        } else if (line[0] == 'D' && line[1] == '\t') {
            free(c->idefault);
            c->idefault = xstrdup(line + 2);
        }
        if (nl == NULL) {
            break;
        }
        line = nl + 1;
    }
    free(text);
    return 0;
}

int inst_save(struct ctx *c)
{
    struct buf b;
    char *p;
    int i, rc;

    b.p = NULL;
    b.n = 0;
    b.cap = 0;
    buf_add(&b, "", 0);
    for (i = 0; i < c->inames.n; i++) {
        buf_adds(&b, "I\t");
        buf_adds(&b, c->inames.v[i]);
        buf_adds(&b, "\t");
        buf_adds(&b, c->igames.v[i]);
        buf_adds(&b, "\n");
    }
    for (i = 0; i < c->iskip.n; i++) {
        buf_adds(&b, "X\t");
        buf_adds(&b, c->iskip.v[i]);
        buf_adds(&b, "\n");
    }
    if (c->idefault != NULL) {
        buf_adds(&b, "D\t");
        buf_adds(&b, c->idefault);
        buf_adds(&b, "\n");
    }
    make_dirs(c->datadir);
    p = (char *)xalloc((long)strlen(c->datadir) + 16);
    strcpy(p, c->datadir);
    strcat(p, "/instances");
    rc = write_file(p, b.p, b.n);
    free(p);
    buf_free(&b);
    return rc;
}

/* make the named instance the current one */
int inst_select(struct ctx *c, const char *name)
{
    int i;
    char *p;

    i = inst_index(c, name);
    if (i < 0) {
        return -1;
    }
    c->game = c->igames.v[i];
    c->instname = c->inames.v[i];
    c->shared = c->datadir;
    p = (char *)xalloc((long)(strlen(c->datadir) + strlen(name)) + 16);
    strcpy(p, c->datadir);
    strcat(p, "/inst/");
    strcat(p, name);
    c->state = p;
    make_dirs(c->state);
    if (!c->kspfixed) {
        c->kspver = NULL;
    }
    registry_clear(&c->reg);
    registry_load(c);
    ksp_detect(c);
    if (c->idefault == NULL || strcmp(c->idefault, name) != 0) {
        free(c->idefault);
        c->idefault = xstrdup(name);
        inst_save(c);
    }
    return 0;
}

int is_game_dir(const char *dir)
{
    struct stat st;
    char *p;
    int rc;

    p = (char *)xalloc((long)strlen(dir) + 16);
    strcpy(p, dir);
    strcat(p, "/GameData");
    rc = stat(p, &st);
    free(p);
    return rc == 0 && S_ISDIR(st.st_mode);
}

int cmd_add(struct ctx *c, const char *name, const char *dir)
{
    char cwd[1024];
    char *full, *p;
    int i, rc;

    if (c->datadir == NULL) {
        fprintf(stderr, "kspkan: HOME is not set\n");
        return 1;
    }
    if (!name_valid(name)) {
        fprintf(stderr, "kspkan: a name uses letters, digits, - _ . only\n");
        return 1;
    }
    if (dir[0] == '/') {
        full = xstrdup(dir);
    } else {
        p = getcwd(cwd, sizeof(cwd) - 2);
        if (p == NULL) {
            fprintf(stderr, "kspkan: cannot find the current folder\n");
            return 1;
        }
        full = (char *)xalloc((long)(strlen(cwd) + strlen(dir)) + 4);
        strcpy(full, cwd);
        if (strcmp(dir, ".") != 0) {
            strcat(full, "/");
            strcat(full, dir);
        }
    }
    i = (int)strlen(full);
    while (i > 1 && full[i - 1] == '/') {
        full[i - 1] = 0;
        i--;
    }
    if (!is_game_dir(full)) {
        fprintf(stderr, "kspkan: %s has no GameData folder; give the folder "
            "that holds GameData\n", full);
        free(full);
        return 1;
    }
    for (i = 0; i < c->iskip.n; i++) {
        if (same_dir(c->iskip.v[i], full)) {
            free(c->iskip.v[i]);
            c->iskip.n--;
            c->iskip.v[i] = c->iskip.v[c->iskip.n];
            i--;
        }
    }
    i = inst_index(c, name);
    if (i >= 0) {
        free(c->igames.v[i]);
        c->igames.v[i] = full;
    } else {
        strs_add(&c->inames, xstrdup(name));
        strs_add(&c->igames, full);
    }
    if (c->idefault == NULL) {
        c->idefault = xstrdup(name);
    }
    rc = inst_save(c);
    if (rc < 0) {
        fprintf(stderr, "kspkan: cannot save %s\n", c->datadir);
        return 1;
    }
    printf("added %s: %s\n", name, full);
    return 0;
}

int cmd_forget(struct ctx *c, const char *name)
{
    int i;

    i = inst_index(c, name);
    if (i < 0) {
        fprintf(stderr, "kspkan: no such instance: %s\n", name);
        return 1;
    }
    free(c->inames.v[i]);
    strs_add(&c->iskip, c->igames.v[i]);
    for (; i + 1 < c->inames.n; i++) {
        c->inames.v[i] = c->inames.v[i + 1];
        c->igames.v[i] = c->igames.v[i + 1];
    }
    c->inames.n--;
    c->igames.n--;
    if (c->idefault != NULL && strcmp(c->idefault, name) == 0) {
        free(c->idefault);
        c->idefault = c->inames.n > 0 ? xstrdup(c->inames.v[0]) : NULL;
    }
    inst_save(c);
    printf("forgot %s (the game folder and its mods are untouched; "
        "kspkan add brings it back)\n", name);
    return 0;
}

int cmd_use(struct ctx *c, const char *name)
{
    if (inst_index(c, name) < 0) {
        fprintf(stderr, "kspkan: no such instance: %s\n", name);
        return 1;
    }
    free(c->idefault);
    c->idefault = xstrdup(name);
    inst_save(c);
    printf("now using %s\n", name);
    return 0;
}

/* the remembered games, with what is in each; games found on the
   disk that are not remembered yet are added first */
int cmd_instances(struct ctx *c)
{
    struct gameinfo g;
    int i;

    games_autoadd(c, 0);
    if (c->inames.n == 0) {
        printf("no game found; add one: kspkan add NAME GAMEDIR\n");
        return 0;
    }
    for (i = 0; i < c->inames.n; i++) {
        game_info(c, i, &g);
        printf("%c %-14s %s\n", c->idefault != NULL &&
            strcmp(c->idefault, c->inames.v[i]) == 0 ? '*' : ' ',
            c->inames.v[i], c->igames.v[i]);
        printf("  game %s, %d mods by kspkan (%d chosen, %d dependencies),"
            " %d found, %d folders in GameData; DLC: %s\n",
            g.version != NULL ? g.version : "?", g.chosen + g.deps,
            g.chosen, g.deps, g.found, g.folders,
            g.dlcs[0] ? g.dlcs : "none");
        game_info_free(&g);
    }
    return 0;
}

static void ask(const char *prompt, char *out, int size)
{
    char *p;
    int n;

    printf("%s", prompt);
    fflush(stdout);
    out[0] = 0;
    p = fgets(out, size, stdin);
    if (p == NULL) {
        out[0] = 0;
        return;
    }
    n = (int)strlen(out);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r')) {
        out[n - 1] = 0;
        n--;
    }
}

/* first start: find the games on the disk and remember them all; ask
   only when none is found.  Then offer to fetch the list. */
int inst_first_run(struct ctx *c)
{
    char line[512];
    char *p;
    int rc;

    games_autoadd(c, 0);
    if (c->inames.n == 0) {
        if (!isatty(0)) {
            fprintf(stderr, "kspkan: no game folder found; run: "
                "kspkan add NAME GAMEDIR\n");
            return -1;
        }
        printf("No Kerbal Space Program found in the usual places.\n");
        ask("The folder that holds GameData: ", line, sizeof(line));
        rc = cmd_add(c, "ksp", line);
        if (rc != 0) {
            return -1;
        }
    }
    rc = inst_select(c, c->idefault != NULL ? c->idefault :
        c->inames.v[0]);
    if (rc < 0) {
        return -1;
    }
    if (c->inames.n > 1) {
        printf("using %s; switch with: kspkan use NAME, or Tab in the "
            "full screen\n", c->instname);
    }
    p = shared_path(c, "index");
    rc = access(p, R_OK);
    free(p);
    if (rc != 0 && !isatty(0)) {
        printf("no module list yet: kspkan update fetches it\n");
    } else if (rc != 0) {
        ask("Fetch the module list now? [Y/n] ", line, sizeof(line));
        if (line[0] != 'n' && line[0] != 'N') {
            cmd_update(c, NULL);
        }
    }
    return 0;
}
