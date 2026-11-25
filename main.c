#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "kspkan.h"

static void usage(void)
{
    fputs("usage: kspkan [-g GAMEDIR] [-k KSPVERSION] [-a] [-t] [-f] [-y]\n"
        "              [-p] [-r URL] [-i NAME] [COMMAND [ARGS]]\n\n"
        "  no command            full screen mode (? shows the keys)\n"
        "  update                fetch the module list\n"
        "  search [QUERY]        list modules (author:x dl>5k sort:new ...)\n"
        "  show ID               describe a module\n"
        "  list                  installed modules\n"
        "  version               print the version\n", stderr);
    fputs("  install ID[=VER]...   install modules and what they need\n"
        "  remove ID...          remove modules\n"
        "  upgrade [ID...]       upgrade installed modules\n"
        "  autoremove            remove dependencies nothing needs\n"
        "  keep ID... | auto ID... mark as chosen by you, or as a "
        "dependency\n"
        "  clean                 delete downloaded archives\n",
        stderr);
    fputs("  modlist [list]        the saved modlists\n"
        "  modlist save NAME     save the modules you chose\n"
        "  modlist show NAME     what it has, and how the game differs\n"
        "  modlist install NAME  install what it has (newest that fits)\n"
        "  modlist pin NAME      install the very versions it names\n"
        "  modlist sync NAME     install it, remove chosen ones not in it\n"
        "  modlist delete NAME   (NAME with a / is a file anywhere)\n",
        stderr);
    fputs("  modlist import FILE [NAME]  read a .ckan modpack, registry.json"
        "\n"
        "                        or a mod list CKAN saved\n",
        stderr);
    fputs("  add NAME [GAMEDIR]    remember a game folder as an instance\n"
        "  instances | use NAME | forget NAME\n\n", stderr);
    fputs("  -g DIR  game directory (default: current directory)\n"
        "  -k VER  game version, if it cannot be found (as 1.12.5)\n"
        "  -a      ignore game version compatibility\n"
        "  -t      allow testing and development releases\n"
        "  -f      overwrite files that kspkan did not install\n"
        "  -y      do not ask for confirmation\n", stderr);
    fputs("  -p      list most downloaded first\n"
        "  -R      also install recommended modules\n"
        "  -S      also install recommended and suggested modules\n"
        "  -m      no colours on the screen (also when NO_COLOR is set)\n"
        "  -r URL  module list archive (default: CKAN-meta tarball)\n"
        "  -i NAME use that instance (default: the last one used)\n"
        "  -s DIR  keep all kspkan files in DIR (for tests)\n",
        stderr);
}

static int debug_cmds(int argc, char **argv)
{
    struct archive a;
    char hex[65];
    unsigned char *data, *t;
    long len, tlen;
    int rc, i;

    if (argc == 4 && strcmp(argv[1], "x-hash") == 0) {
        rc = hash_file(argv[3], argv[2], hex);
        if (rc < 0) {
            die("cannot hash %s", argv[3]);
        }
        printf("%s\n", hex);
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "x-vercmp") == 0) {
        printf("%d\n", version_compare(argv[2], argv[3]));
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "x-rx") == 0) {
        printf("%d\n", rx_match(argv[2], argv[3]));
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "x-zipls") == 0) {
        data = (unsigned char *)read_file(argv[2], &len, 1L << 30);
        if (data == NULL || zip_open(&a, data, len) < 0) {
            die("bad zip %s", argv[2]);
        }
        for (i = 0; i < a.n; i++) {
            printf("%ld %s\n", a.e[i].usize, a.e[i].name);
        }
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "x-zipcat") == 0) {
        data = (unsigned char *)read_file(argv[2], &len, 1L << 30);
        if (data == NULL || zip_open(&a, data, len) < 0) {
            die("bad zip %s", argv[2]);
        }
        t = archive_read(&a, (int)parse_count(argv[3]), 1L << 28, &tlen);
        if (t == NULL) {
            die("cannot read entry");
        }
        rc = (int)write(1, t, (unsigned long)tlen);
        return rc < 0;
    }
    return -1;
}

/* the remaining arguments as one string */
static const char *joined(int n, char **v)
{
    struct buf b;
    int i;

    b.p = NULL;
    b.n = 0;
    b.cap = 0;
    buf_add(&b, "", 0);
    for (i = 0; i < n; i++) {
        if (i > 0) {
            buf_adds(&b, " ");
        }
        buf_adds(&b, v[i]);
    }
    return b.p;
}

/* choose the game folder and the place for kspkan's files; 0 if usable */
static int pick_context(struct ctx *c, const char *inst, int gset, int sset)
{
    int rc;

    c->kspfixed = (c->kspver != NULL);
    if (sset) {
        c->shared = c->state;
        return 0;
    }
    if (inst != NULL) {
        rc = inst_select(c, inst);
        if (rc < 0) {
            fprintf(stderr, "kspkan: no such instance: %s (see: kspkan "
                "instances)\n", inst);
        }
        return rc;
    }
    if (!gset && c->idefault != NULL) {
        rc = inst_select(c, c->idefault);
        if (rc == 0) {
            return 0;
        }
    }
    if (!gset && !is_game_dir(".")) {
        return inst_first_run(c);
    }
    c->state = (char *)xalloc((long)strlen(c->game) + 16);
    strcpy(c->state, c->game);
    strcat(c->state, "/kspkan");
    c->shared = c->state;
    return 0;
}

int main(int argc, char **argv)
{
    struct ctx c;
    const char *cmd, *inst;
    int i, rc, k, gset, sset;

    rc = debug_cmds(argc, argv);
    if (rc >= 0) {
        return rc;
    }
    memset(&c, 0, sizeof(c));
    c.game = ".";
    c.repo = DEFAULT_REPO;
    inst = NULL;
    gset = 0;
    sset = 0;
    i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1] != 0 &&
        argv[i][1] != 'V')
    {
        k = argv[i][1];
        if (argv[i][2] != 0) {
            usage();
            return 2;
        }
        if (k == 'a') {
            c.anyver = 1;
        } else if (k == 't') {
            c.testing = 1;
        } else if (k == 'f') {
            c.force = 1;
        } else if (k == 'y') {
            c.assumeyes = 1;
        } else if (k == 'p') {
            c.bypop = 1;
        } else if (k == 'm') {
            c.mono = 1;
        } else if (k == 'R') {
            c.withrec = 1;
        } else if (k == 'S') {
            c.withsug = 1;
            c.withrec = 1;
        } else if (k == 'g' || k == 'k' || k == 'r' || k == 's' ||
            k == 'i')
        {
            i++;
            if (i >= argc) {
                usage();
                return 2;
            }
            if (k == 'g') {
                c.game = argv[i];
                gset = 1;
            } else if (k == 's') {
                c.state = argv[i];
                sset = 1;
            } else if (k == 'k') {
                c.kspver = argv[i];
            } else if (k == 'i') {
                inst = argv[i];
            } else {
                c.repo = argv[i];
            }
        } else {
            usage();
            return 2;
        }
        i++;
    }
    cmd = i < argc ? argv[i] : "";
    i++;
    c.datadir = data_dir();
    inst_load(&c);
    if (strcmp(cmd, "version") == 0 || strcmp(cmd, "-V") == 0) {
        printf("kspkan %s\n", KSPKAN_VERSION);
        return 0;
    }
    if (strcmp(cmd, "help") == 0) {
        usage();
        return 2;
    }
    if (strcmp(cmd, "instances") == 0) {
        return cmd_instances(&c);
    }
    if (strcmp(cmd, "add") == 0 && i < argc) {
        return cmd_add(&c, argv[i], i + 1 < argc ? argv[i + 1] : ".");
    }
    if (strcmp(cmd, "forget") == 0 && i < argc) {
        return cmd_forget(&c, argv[i]);
    }
    if (strcmp(cmd, "use") == 0 && i < argc) {
        return cmd_use(&c, argv[i]);
    }
    rc = pick_context(&c, inst, gset, sset);
    if (rc < 0) {
        return 1;
    }
    if (strcmp(cmd, "update") == 0) {
        return cmd_update(&c);
    }
    if (cmd[0] == 0) {
        return cmd_tui(&c);
    }
    if (strcmp(cmd, "list") == 0) {
        registry_clear(&c.reg);
        registry_load(&c);
        return cmd_list(&c);
    }
    rc = ctx_ready(&c);
    if (rc < 0) {
        return 1;
    }
    if (strcmp(cmd, "search") == 0) {
        return cmd_search(&c, joined(argc - i, argv + i));
    }
    if (strcmp(cmd, "show") == 0 && i < argc) {
        return cmd_show(&c, argv[i]);
    }
    if (strcmp(cmd, "install") == 0 && i < argc) {
        return cmd_install(&c, argv + i, argc - i);
    }
    if (strcmp(cmd, "remove") == 0 && i < argc) {
        return cmd_remove(&c, argv + i, argc - i);
    }
    if (strcmp(cmd, "clean") == 0) {
        return cmd_clean(&c);
    }
    if (strcmp(cmd, "autoremove") == 0) {
        return cmd_remove(&c, NULL, 0);
    }
    if (strcmp(cmd, "keep") == 0 && i < argc) {
        return cmd_keep(&c, argv + i, argc - i, 0);
    }
    if (strcmp(cmd, "auto") == 0 && i < argc) {
        return cmd_keep(&c, argv + i, argc - i, 1);
    }
    if (strcmp(cmd, "modlist") == 0) {
        return cmd_modlist(&c, argv + i, argc - i);
    }
    if (strcmp(cmd, "upgrade") == 0) {
        return cmd_upgrade(&c, argv + i, argc - i);
    }
    usage();
    return 2;
}
