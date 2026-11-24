/* user and group to drop privileges to */
static const char *user  = "nobody";
static const char *group = "nobody";  /* Gentoo: the group is "nobody", not "nogroup" */

static const char *colorname[NUMCOLS] = {
	/* Two greys only: Habamax's background, a bit lighter while typing */
	[INIT] =   "#1c1c1c",   /* nothing typed */
	[INPUT] =  "#262626",   /* during input */
	[FAILED] = "#1c1c1c",   /* wrong password: back to the empty grey */
};

/* treat a cleared input like a wrong password (color) */
static const int failonclear = 1;
