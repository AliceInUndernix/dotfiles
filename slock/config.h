/* user and group to drop privileges to */
static const char *user  = "nobody";
static const char *group = "nobody";  /* Gentoo: the group is "nobody", not "nogroup" */

static const char *colorname[NUMCOLS] = {
	/* Habamax colours */
	[INIT] =   "#1c1c1c",   /* after initialization */
	[INPUT] =  "#5f87af",   /* during input */
	[FAILED] = "#af5f5f",   /* wrong password */
};

/* treat a cleared input like a wrong password (color) */
static const int failonclear = 1;
