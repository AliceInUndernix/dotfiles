/* See LICENSE file for copyright and license details. */

/* appearance */
static const unsigned int borderpx  = 1;        /* border pixel of windows */
static const unsigned int snap      = 32;       /* snap pixel */
static const unsigned int winsnap   = 16;       /* snap to other windows' edges, pixel */
static const int showbar            = 0;        /* 0 means no bar (Mod+b shows it) */
static const int topbar             = 1;        /* 0 means bottom bar */
static const char *fonts[]          = { "Liberation Mono:pixelsize=25" };
/* Habamax colours (Neovim's colorscheme, see nvim/init.lua) */
static const char col_bg[]          = "#1c1c1c";
static const char col_border[]      = "#585858";
static const char col_fg[]          = "#c7c7c7";
static const char col_selfg[]       = "#1c1c1c";
static const char *colors[][3]      = {
	/*               fg         bg       border     */
	[SchemeNorm] = { col_fg,    col_bg,   col_border },
	[SchemeSel]  = { col_selfg, col_fg,   col_fg     },
};

/* tagging */
static const char *tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

static const Rule rules[] = {
	/* xprop(1):
	 *	WM_CLASS(STRING) = instance, class
	 *	WM_NAME(STRING) = title
	 */
	/* class      instance    title       tags mask     isfloating   monitor */
	/* No rules yet. C needs at least one entry: this one matches every
	 * window and changes nothing. Example rule (IceCat on tag 2):
	 * { "Icecat",   NULL,       NULL,       1 << 1,       0,           -1 }, */
	{ NULL,       NULL,       NULL,       0,            0,           -1 },
};

/* Like rules, but only for the apps x/xinitrc starts: each one places just
 * the first window it matches in the first minute after login (not after
 * dwm restart). Plover or btop opened later open on the tag you are on.
 * btop runs in an st started with -c btop. */
static const Rule startrules[] = {
	/* class      instance    title       tags mask     isfloating   monitor */
	{ "Plover",   NULL,       NULL,       1 << 8,       0,           -1 },
	{ "btop",     NULL,       NULL,       1 << 8,       0,           -1 },
};

/* layout(s) */
static const float mfact     = 0.55; /* factor of master area size [0.05..0.95] */
static const int nmaster     = 1;    /* number of clients in master area */
static const int resizehints = 0;    /* 0: tiled windows fill their space exactly */
static const int attachbelow = 1;    /* 1 means attach after the currently active window */
static const int lockfullscreen = 1; /* 1 will force focus on the fullscreen window */
static const int refreshrate = 120;  /* refresh rate (per second) for client move/resize */

static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[]=",      tile },    /* first entry is default */
	{ "><>",      NULL },    /* no layout function means floating behavior */
	{ "[M]",      monocle },
	{ "DD",       doubledeck }, /* master and stack each show one window at a time */
};

/* key definitions */
/* combo: hold Mod and press several tag keys to view (or with Shift, tag)
 * all of them at once */
#define MODKEY Mod4Mask /* Super */
#define TAGKEYS(KEY,TAG) \
	{ MODKEY,                       KEY,      comboview,      {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask,           KEY,      toggleview,     {.ui = 1 << TAG} }, \
	{ MODKEY|ShiftMask,             KEY,      combotag,       {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask|ShiftMask, KEY,      toggletag,      {.ui = 1 << TAG} },

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* commands */
static char dmenumon[2] = "0"; /* component of dmenucmd, manipulated in spawn() */
/* dmenu_apps shows only the programs in dmenu/apps; font, colours, border
 * and size are in dmenu/config.h */
static const char *dmenucmd[] = { "dmenu_apps", "-m", dmenumon, NULL };
static const char *termcmd[]  = { "st", NULL };
/* scratchpad: one floating st, Mod+` shows/hides it on any tag. It runs
 * tmux session "scratchpad": attaches if the session exists, else starts it,
 * so closing the window keeps the shells. Separate from Mod+x's sessions. */
static const char scratchpadname[] = "scratchpad";
static const char *scratchpadcmd[] = { "st", "-t", scratchpadname, "-g", "120x34",
	"-e", "tmux", "new-session", "-A", "-s", "scratchpad", NULL };
static const char *lockcmd[]  = { "slock", NULL };
/* xss-lock (x/xinitrc) locks with slock before the suspend happens */
static const char *suspendcmd[] = { "loginctl", "suspend", NULL };
static const char *shotcmd[]  = { "flameshot", "gui", NULL };
static const char *fullshotcmd[] = { "flameshot", "full", "--clipboard", NULL };
static const char *icecatcmd[] = { "icecat", NULL };
static const char *chromiumcmd[] = { "chromium-browser", NULL };
static const char *mixercmd[] = { "st", "-e", "wiremix", NULL };
static const char *topcmd[]   = { "st", "-e", "btop", NULL };
/* tmux in st: Mod+x attaches to the last session (starts one if there is
 * none), Mod+Shift+x always starts a new session */
static const char *tmuxcmd[]  = { "st", "-e", "/bin/sh", "-c", "tmux attach || exec tmux", NULL };
static const char *tmuxnewcmd[] = { "st", "-e", "tmux", "new-session", NULL };

#include "movestack.c"
static const Key keys[] = {
	/* modifier                     key        function        argument */
	{ MODKEY,                       XK_p,      spawn,          {.v = dmenucmd } },
	{ MODKEY|ShiftMask,             XK_Return, spawn,          {.v = termcmd } },
	{ MODKEY,                       XK_grave,  togglescratch,  {.v = scratchpadcmd } },
	{ MODKEY,                       XK_b,      togglebar,      {0} },
	{ MODKEY,                       XK_j,      focusstack,     {.i = +1 } },
	{ MODKEY,                       XK_k,      focusstack,     {.i = -1 } },
	{ MODKEY|ShiftMask,             XK_j,      movestack,      {.i = +1 } },
	{ MODKEY|ShiftMask,             XK_k,      movestack,      {.i = -1 } },
	{ MODKEY,                       XK_i,      incnmaster,     {.i = +1 } },
	{ MODKEY,                       XK_d,      incnmaster,     {.i = -1 } },
	{ MODKEY,                       XK_h,      setmfact,       {.f = -0.05} },
	{ MODKEY,                       XK_l,      setmfact,       {.f = +0.05} },
	{ MODKEY,                       XK_Return, zoom,           {0} },
	{ MODKEY,                       XK_Tab,    view,           {0} },
	{ MODKEY|ShiftMask,             XK_c,      killclient,     {0} },
	{ MODKEY,                       XK_t,      setlayout,      {.v = &layouts[0]} },
	{ MODKEY,                       XK_f,      setlayout,      {.v = &layouts[1]} },
	{ MODKEY,                       XK_m,      setlayout,      {.v = &layouts[2]} },
	{ MODKEY,                       XK_r,      setlayout,      {.v = &layouts[3]} },
	{ MODKEY,                       XK_space,  setlayout,      {0} },
	{ MODKEY|ShiftMask,             XK_space,  togglefloating, {0} },
	/* sticky: on every tag, floating, Mod+j/k skip it (e.g. a video) */
	{ MODKEY,                       XK_s,      togglesticky,   {0} },
	{ MODKEY,                       XK_0,      view,           {.ui = ~0 } },
	{ MODKEY|ShiftMask,             XK_0,      tag,            {.ui = ~0 } },
	{ MODKEY,                       XK_comma,  focusmon,       {.i = -1 } },
	{ MODKEY,                       XK_period, focusmon,       {.i = +1 } },
	{ MODKEY|ShiftMask,             XK_comma,  tagmon,         {.i = -1 } },
	{ MODKEY|ShiftMask,             XK_period, tagmon,         {.i = +1 } },
	TAGKEYS(                        XK_1,                      0)
	TAGKEYS(                        XK_2,                      1)
	TAGKEYS(                        XK_3,                      2)
	TAGKEYS(                        XK_4,                      3)
	TAGKEYS(                        XK_5,                      4)
	TAGKEYS(                        XK_6,                      5)
	TAGKEYS(                        XK_7,                      6)
	TAGKEYS(                        XK_8,                      7)
	TAGKEYS(                        XK_9,                      8)
	{ MODKEY|ShiftMask,             XK_l,      spawn,          {.v = lockcmd } },
	{ MODKEY|ShiftMask,             XK_s,      spawn,          {.v = suspendcmd } },
	{ 0,                            XK_Print,  spawn,          {.v = shotcmd } },
	{ MODKEY,                       XK_Print,  spawn,          {.v = fullshotcmd } },
	{ MODKEY,                       XK_w,      spawn,          {.v = icecatcmd } },
	{ MODKEY|ShiftMask,             XK_w,      spawn,          {.v = chromiumcmd } },
	{ MODKEY,                       XK_a,      spawn,          {.v = mixercmd } },
	{ MODKEY,                       XK_Escape, spawn,          {.v = topcmd } },
	{ MODKEY,                       XK_x,      spawn,          {.v = tmuxcmd } },
	{ MODKEY|ShiftMask,             XK_x,      spawn,          {.v = tmuxnewcmd } },
	{ MODKEY|ShiftMask,             XK_q,      quit,           {0} },
	/* restart dwm in place (e.g. after ./install.sh dwm): windows keep their
	 * tags, order and layouts. From a terminal: dwm restart */
	{ MODKEY|ShiftMask,             XK_r,      restart,        {0} },
};

/* button definitions */
/* click can be ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle, ClkClientWin, or ClkRootWin */
static const Button buttons[] = {
	/* click                event mask      button          function        argument */
	{ ClkLtSymbol,          0,              Button1,        setlayout,      {0} },
	{ ClkLtSymbol,          0,              Button3,        setlayout,      {.v = &layouts[2]} },
	{ ClkWinTitle,          0,              Button2,        zoom,           {0} },
	{ ClkStatusText,        0,              Button2,        spawn,          {.v = termcmd } },
	{ ClkClientWin,         MODKEY,         Button1,        movemouse,      {0} },
	{ ClkClientWin,         MODKEY,         Button2,        togglefloating, {0} },
	{ ClkClientWin,         MODKEY,         Button3,        resizemouse,    {0} },
	{ ClkTagBar,            0,              Button1,        view,           {0} },
	{ ClkTagBar,            0,              Button3,        toggleview,     {0} },
	{ ClkTagBar,            MODKEY,         Button1,        tag,            {0} },
	{ ClkTagBar,            MODKEY,         Button3,        toggletag,      {0} },
};

