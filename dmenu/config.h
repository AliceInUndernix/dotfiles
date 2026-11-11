/* See LICENSE file for copyright and license details. */
/* Default settings; can be overriden by command line. */

static int topbar = 1;                      /* -b  option; if 0, dmenu appears at bottom     */
static int centered = 1;                    /* -c  option; centers dmenu on screen */
static int min_width = 500;                 /* minimum width when centered */
static const float menu_height_ratio = 2.0f; /* 2 = middle of the screen, 4 = upper quarter */
static int fuzzy = 1;                       /* -F  option; if 0, dmenu doesn't use fuzzy matching */
/* -fn option overrides fonts[0]; default X11 font or font set */
static const char *fonts[] = {
	"Liberation Mono:pixelsize=12"
};
static const char *prompt      = NULL;      /* -p  option; prompt to the left of input field */
static const char *colors[SchemeLast][2] = {
	/*     fg         bg       */   /* Habamax colours, the same as dwm */
	[SchemeNorm] = { "#c7c7c7", "#1c1c1c" },
	[SchemeSel] = { "#1c1c1c", "#c7c7c7" },
	/* letters that match what you typed */
	[SchemeNormHighlight] = { "#d7af87", "#1c1c1c" },
	[SchemeSelHighlight] = { "#1c1c1c", "#d7af87" },
	[SchemeOut] = { "#1c1c1c", "#87afaf" },
	/* like dwm's focused window border */
	[SchemeBorder] = { "#c7c7c7", NULL },
};
/* -l option; if nonzero, dmenu uses vertical list with given number of lines */
static unsigned int lines      = 10;
/* -H option; how many entries the history file keeps */
static unsigned int maxhist    = 100;

/*
 * Characters not considered part of a word while deleting words
 * for example: " /?\"&[]"
 */
static const char worddelimiters[] = " ";

/* -bw option; border width in pixels, 1 like dwm's borderpx */
static unsigned int border_width = 1;
