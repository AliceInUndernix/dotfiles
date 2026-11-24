/* ui.h: the screen (ui.c) and the settings it uses (main.c) */

#define VIEWERS_MAX 8
#define ARGS_MAX 8

struct config {
    char *editor[ARGS_MAX];       /* nvim, else vim, else vi */
    int neditor;
    int private_ed;               /* -i NONE: no shada/viminfo */
    char *viewer[VIEWERS_MAX][ARGS_MAX];
    int nviewer_args[VIEWERS_MAX];
    char viewer_ext[VIEWERS_MAX][200];
    int nviewers;
    char *clean;                  /* the clean program */
    char shell[256];
    int utf8;
};

extern struct config cfg;

int ui_run(void);
