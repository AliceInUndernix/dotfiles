#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "kspkan.h"

/* datadir/instances holds lines
     I<TAB>name<TAB>game folder
     D<TAB>name            (the one used when none is named)  */

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

static int name_valid(const char *s)
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

static int inst_index(struct ctx *c, const char *name)
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

    c->inames.v = c->igames.v = NULL;
    c->inames.n = c->igames.n = c->inames.cap = c->igames.cap = 0;
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

static int inst_save(struct ctx *c)
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
    free(c->igames.v[i]);
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
    printf("forgot %s (the game folder and its mods are untouched)\n", name);
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

int cmd_instances(struct ctx *c)
{
    int i;

    if (c->inames.n == 0) {
        printf("no instances yet; add one: kspkan add NAME GAMEDIR\n");
        return 0;
    }
    for (i = 0; i < c->inames.n; i++) {
        printf("%c %-14s %s\n", c->idefault != NULL &&
            strcmp(c->idefault, c->inames.v[i]) == 0 ? '*' : ' ',
            c->inames.v[i], c->igames.v[i]);
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

/* first start: find the game, remember it, offer to fetch the list */
int inst_first_run(struct ctx *c)
{
    static const char *where[] = {
        "GOG Games/Kerbal Space Program/game",
        ".steam/steam/steamapps/common/Kerbal Space Program",
        ".local/share/Steam/steamapps/common/Kerbal Space Program",
        "KSP", "Games/KSP", "ksp", NULL
    };
    char found[8][512];
    char line[512], name[64];
    const char *h;
    char *p;
    int i, nf, pick, rc;

    if (!isatty(0)) {
        fprintf(stderr, "kspkan: no game folder known; run: "
            "kspkan add NAME GAMEDIR\n");
        return -1;
    }
    printf("Welcome.  kspkan needs to know where Kerbal Space Program is.\n");
    h = getenv("HOME");
    nf = 0;
    for (i = 0; h != NULL && where[i] != NULL && nf < 8; i++) {
        if (strlen(h) + strlen(where[i]) + 2 >= 512) {
            continue;
        }
        strcpy(found[nf], h);
        strcat(found[nf], "/");
        strcat(found[nf], where[i]);
        if (is_game_dir(found[nf])) {
            nf++;
        }
    }
    for (i = 0; i < nf; i++) {
        printf("  %d) %s\n", i + 1, found[i]);
    }
    ask(nf > 0 ? "Number, or the folder that holds GameData [1]: " :
        "The folder that holds GameData: ", line, sizeof(line));
    pick = 0;
    if (line[0] == 0 && nf > 0) {
        pick = 1;
    } else if (is_digit(line[0]) && line[1] == 0) {
        pick = line[0] - '0';
    }
    if (pick >= 1 && pick <= nf) {
        strcpy(line, found[pick - 1]);
    }
    ask("A short name for it [ksp]: ", name, sizeof(name));
    if (name[0] == 0) {
        strcpy(name, "ksp");
    }
    rc = cmd_add(c, name, line);
    if (rc != 0) {
        return -1;
    }
    rc = inst_select(c, name);
    if (rc < 0) {
        return -1;
    }
    p = shared_path(c, "index");
    rc = access(p, R_OK);
    free(p);
    if (rc != 0) {
        ask("Fetch the module list now? [Y/n] ", line, sizeof(line));
        if (line[0] != 'n' && line[0] != 'N') {
            cmd_update(c);
        }
    }
    return 0;
}
