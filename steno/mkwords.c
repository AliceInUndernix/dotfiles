/* mkwords: words.txt (Plover's word list, "word rank" a line) ->
 * words.c, sorted, so the list is part of the steno binary. Run by
 * make; not installed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char **line;
static long nline;

static int before(const char *a, const char *b)
{
    while (*a != ' ' && *a == *b) {
        a++;
        b++;
    }
    if (*a == ' ' && *b == ' ')
        return 0;
    if (*a == ' ')
        return 1;
    if (*b == ' ')
        return 0;
    return (unsigned char)*a < (unsigned char)*b;
}

static void sift(long i, long n)
{
    long c;
    char *t;

    for (;;) {
        c = 2 * i + 1;
        if (c >= n)
            return;
        if (c + 1 < n && before(line[c], line[c + 1]))
            c++;
        if (!before(line[i], line[c]))
            return;
        t = line[i];
        line[i] = line[c];
        line[c] = t;
        i = c;
    }
}

int main(int argc, char **argv)
{
    FILE *in, *out;
    char buf[1024], *p;
    long cap, i, n, total;
    char *t;
    int col, len;

    if (argc != 3) {
        fprintf(stderr, "usage: mkwords words.txt words.c\n");
        return 1;
    }
    in = fopen(argv[1], "r");
    if (!in) {
        fprintf(stderr, "mkwords: can't read %s\n", argv[1]);
        return 1;
    }
    cap = 1024;
    line = malloc(cap * sizeof(*line));
    while (line && fgets(buf, sizeof(buf), in)) {
        len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
            len--;
        buf[len] = 0;
        p = strchr(buf, ' ');
        if (!p || p == buf)
            continue;
        if (nline == cap) {
            char **q;
            q = malloc(cap * 2 * sizeof(*q));
            if (!q)
                return 1;
            memcpy(q, line, cap * sizeof(*q));
            free(line);
            line = q;
            cap *= 2;
        }
        line[nline] = malloc(len + 1);
        if (!line[nline])
            return 1;
        memcpy(line[nline], buf, len + 1);
        nline++;
    }
    fclose(in);
    if (!line)
        return 1;
    /* heap sort */
    for (i = nline / 2 - 1; i >= 0; i--)
        sift(i, nline);
    for (n = nline - 1; n > 0; n--) {
        t = line[0];
        line[0] = line[n];
        line[n] = t;
        sift(0, n);
    }
    out = fopen(argv[2], "w");
    if (!out) {
        fprintf(stderr, "mkwords: can't write %s\n", argv[2]);
        return 1;
    }
    fprintf(out, "/* made by mkwords from words.txt: do not edit */\n");
    fprintf(out, "const char words_txt[] =\n\"");
    total = 0;
    col = 1;
    for (i = 0; i < nline; i++) {
        for (p = line[i]; ; p++) {
            int c;
            c = *p ? (unsigned char)*p : '\n';
            if (col > 70) {
                fprintf(out, "\"\n\"");
                col = 1;
            }
            if (c == '\n') {
                fprintf(out, "\\n");
                col += 2;
            } else if (c < 32 || c > 126 || c == '"' || c == '\\' ||
                c == '?')
            {
                fprintf(out, "\\%03o", c);
                col += 4;
            } else {
                fputc(c, out);
                col++;
            }
            total++;
            if (!*p)
                break;
        }
    }
    fprintf(out, "\";\nconst long words_len = %ld;\n", total);
    fclose(out);
    return 0;
}
