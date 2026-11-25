#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kspkan.h"

/* Reading the mod lists that CKAN writes, into a kspkan modlist.  Only
   reading: kspkan never writes these formats.  It understands

     - a .ckan modpack (File, Export modpack): a metapackage whose
       "depends" and "recommends" name the mods ("suggests" is left
       out, as CKAN leaves it unticked)
     - CKAN's own CKAN/registry.json from a game folder: the mods it
       installed, without the ones it installed as dependencies
     - the lists CKAN saves (Save installed mod list): plain text
       "Name (id version)", Markdown "- **Name** `id version`", BBCode
       "[*][B]Name[/B] (id version)", CSV and TSV with an identifier
       and a version column
     - the output of "ckan list": "- id version" lines

   Anything else on a line is passed over.  The JSON is read into the
   small depth-limited tree of json.c and thrown away; what is kept is
   the flat "id version" modlist. */

#define CK_MAX (1L << 25)
#define CK_IDMAX 100

static int id_ok(const char *s)
{
    int i;

    if (s == NULL || s[0] == 0 || strlen(s) > CK_IDMAX) {
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
    return s[0] != '.' && s[0] != '-';
}

static int ver_ok(const char *s)
{
    int i;

    if (strlen(s) > CK_IDMAX) {
        return 0;
    }
    for (i = 0; s[i]; i++) {
        if ((unsigned char)s[i] <= 32 || (unsigned char)s[i] >= 127) {
            return 0;
        }
    }
    return 1;
}

/* add one mod; 1 if taken (or already there), 0 if it is no mod */
static int take(struct modlist *ml, const char *id, const char *ver)
{
    int i;

    if (ver == NULL) {
        ver = "";
    }
    if (!id_ok(id) || !ver_ok(ver)) {
        return 0;
    }
    for (i = 0; i < ml->n; i++) {
        if (strcmp(ml->v[i].id, id) == 0) {
            return 1;
        }
    }
    modlist_add(ml, id, ver);
    return 1;
}

/* one relation: {"name": ..., "version": ...}, or {"any_of": [...]},
   of which the first choice is taken */
static void from_rel(struct jnode *r, struct modlist *ml, int *skip)
{
    struct jnode *alt;

    alt = json_get(r, "any_of");
    if (alt != NULL && alt->type == J_ARR && alt->child != NULL) {
        r = alt->child;
    }
    if (!take(ml, json_str(r, "name"), json_str(r, "version"))) {
        (*skip)++;
    }
}

static void from_rels(struct jnode *arr, struct modlist *ml, int *skip)
{
    struct jnode *r;

    if (arr == NULL || arr->type != J_ARR) {
        return;
    }
    for (r = arr->child; r != NULL; r = r->next) {
        from_rel(r, ml, skip);
    }
}

/* CKAN/registry.json: installed_modules is an object of
   "id": {"source_module": {"identifier", "version"}, "auto_installed"} */
static void from_registry(struct jnode *im, struct modlist *ml, int *skip,
    int *deps)
{
    struct jnode *m, *src, *ai;
    const char *id;

    for (m = im->child; m != NULL; m = m->next) {
        ai = json_get(m, "auto_installed");
        if (ai != NULL && ai->type == J_BOOL &&
            strcmp(ai->str, "true") == 0)
        {
            (*deps)++;
            continue;
        }
        src = json_get(m, "source_module");
        id = json_str(src, "identifier");
        if (id == NULL) {
            id = m->key;
        }
        if (!take(ml, id, json_str(src, "version"))) {
            (*skip)++;
        }
    }
}

static int from_json(const char *text, long len, struct ckanimp *r)
{
    struct jnode *root, *im;

    root = json_parse(text, len);
    if (root == NULL || root->type != J_OBJ) {
        json_free(root);
        r->kind = "file that is not valid JSON";
        return -1;
    }
    im = json_get(root, "installed_modules");
    if (im != NULL && im->type == J_OBJ) {
        r->kind = "CKAN registry";
        from_registry(im, &r->ml, &r->skipped, &r->deps);
    } else {
        r->kind = "CKAN modpack";
        from_rels(json_get(root, "depends"), &r->ml, &r->skipped);
        from_rels(json_get(root, "recommends"), &r->ml, &r->skipped);
        im = json_get(root, "suggests");
        if (im != NULL && im->type == J_ARR) {
            for (im = im->child; im != NULL; im = im->next) {
                r->suggests++;
            }
        }
    }
    json_free(root);
    return 0;
}

/* cut up to max words out of s, in place; the count */
static int words(char *s, char **w, int max)
{
    int n;

    n = 0;
    for (;;) {
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        if (*s == 0) {
            break;
        }
        if (n == max) {
            return max + 1;
        }
        w[n] = s;
        n++;
        while (*s && *s != ' ' && *s != '\t') {
            s++;
        }
        if (*s == 0) {
            break;
        }
        *s = 0;
        s++;
    }
    return n;
}

/* one CSV or TSV field, unquoted, in place; where the next one starts */
static char *field(char *s, int delim, char **out)
{
    char *d;

    if (*s == '"') {
        s++;
        *out = s;
        d = s;
        while (*s) {
            if (*s == '"' && s[1] == '"') {
                *d = '"';
                d++;
                s += 2;
            } else if (*s == '"') {
                s++;
                break;
            } else {
                *d = *s;
                d++;
                s++;
            }
        }
        *d = 0;
        while (*s && *s != delim) {
            s++;
        }
    } else {
        *out = s;
        while (*s && *s != delim) {
            s++;
        }
    }
    if (*s == delim) {
        *s = 0;
        s++;
    }
    return s;
}

/* "id version" (or just "id") inside (...) or `...` */
static int pair(char *s, struct modlist *ml)
{
    char *w[2];
    int n;

    n = words(s, w, 2);
    if (n < 1 || n > 2) {
        return 0;
    }
    return take(ml, w[0], n == 2 ? w[1] : "");
}

static void text_line(char *s, struct ckanimp *r, int *delim)
{
    char *w[3], *a, *b, *p, *id, *ver;
    long len;
    int n, got;

    while (*s == ' ' || *s == '\t') {
        s++;
    }
    len = (long)strlen(s);
    while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == ' ' ||
        s[len - 1] == '\t'))
    {
        len--;
        s[len] = 0;
    }
    if (s[0] == 0 || s[0] == '#') {
        return;
    }
    if (*delim == 0 && strncmp(s, "identifier", 10) == 0 &&
        (s[10] == ',' || s[10] == '\t' || s[10] == ';'))
    {
        *delim = s[10];
        r->kind = s[10] == '\t' ? "CKAN TSV list" : "CKAN CSV list";
        return;
    }
    got = 0;
    a = strchr(s, '`');
    b = a != NULL ? strchr(a + 1, '`') : NULL;
    p = strrchr(s, '(');
    if (*delim != 0) {
        a = field(s, *delim, &id);
        field(a, *delim, &ver);
        got = take(&r->ml, id, ver);
    } else if (b != NULL) {
        *b = 0;
        got = pair(a + 1, &r->ml);
        r->kind = "CKAN Markdown list";
    } else if (s[len - 1] == ')' && p != NULL) {
        s[len - 1] = 0;
        got = pair(p + 1, &r->ml);
        r->kind = strncmp(s, "[*]", 3) == 0 ? "CKAN BBCode list" :
            "CKAN text list";
    } else {
        n = words(s, w, 3);
        if (n == 3 && w[0][1] == 0 &&
            strchr("-^~?X*+A!", w[0][0]) != NULL)
        {
            got = take(&r->ml, w[1], w[2]);
            r->kind = "ckan list output";
        } else if (n == 1 || n == 2) {
            got = take(&r->ml, w[0], n == 2 ? w[1] : "");
        }
    }
    if (!got) {
        r->skipped++;
    }
}

static void from_text(char *text, struct ckanimp *r)
{
    char *line, *nl;
    int delim;

    r->kind = "mod list";
    delim = 0;
    line = text;
    if ((unsigned char)line[0] == 0xef && (unsigned char)line[1] == 0xbb &&
        (unsigned char)line[2] == 0xbf)
    {
        line += 3;
    }
    while (*line) {
        nl = strchr(line, '\n');
        if (nl != NULL) {
            *nl = 0;
        }
        text_line(line, r, &delim);
        if (nl == NULL) {
            break;
        }
        line = nl + 1;
    }
}

/* read a file CKAN wrote; 0 if read (r->ml may still be empty) */
int ckan_import(const char *file, struct ckanimp *r)
{
    char *text, *s;
    long len;
    int rc;

    r->ml.v = NULL;
    r->ml.n = 0;
    r->ml.cap = 0;
    r->kind = "file";
    r->skipped = 0;
    r->deps = 0;
    r->suggests = 0;
    text = read_file(file, &len, CK_MAX);
    if (text == NULL) {
        return -1;
    }
    s = text;
    if ((unsigned char)s[0] == 0xef && (unsigned char)s[1] == 0xbb &&
        (unsigned char)s[2] == 0xbf)
    {
        s += 3;
    }
    while (is_space(*s)) {
        s++;
    }
    rc = 0;
    if (*s == '{') {
        rc = from_json(s, len - (long)(s - text), r);
    } else {
        from_text(text, r);
    }
    free(text);
    return rc;
}

/* a modlist name made from a file name: the last part, without the
   extension, with anything but letters, digits, _ - . made a _ */
char *ckan_list_name(const char *file)
{
    const char *b;
    char *n, *dot;
    int i;

    b = strrchr(file, '/');
    b = b != NULL ? b + 1 : file;
    n = (char *)xalloc(41);
    n[0] = 0;
    strncat(n, b, 40);
    dot = strrchr(n, '.');
    if (dot != NULL && dot != n) {
        *dot = 0;
    }
    for (i = 0; n[i]; i++) {
        if (!((n[i] >= 'a' && n[i] <= 'z') || (n[i] >= 'A' && n[i] <= 'Z')
            || is_digit(n[i]) || n[i] == '_' || n[i] == '-' || n[i] == '.'))
        {
            n[i] = '_';
        }
    }
    if (n[0] == '.') {
        n[0] = '_';
    }
    if (n[0] == 0) {
        strcpy(n, "ckan");
    }
    return n;
}
