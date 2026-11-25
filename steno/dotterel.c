/* Dotterel's own dictionaries that are more program than words, built
 * into steno as C data, so no file is read or parsed for them:
 *
 *   dotterel-movement   dotterel-movement.modal (plover-modal-
 *                       dictionary): arrows, Home, End, pages
 *   dotterel-modifiers  dotterel-modifiers.py: KHR, SH, THRA, KPW ...
 *   dotterel-raw        dotterel-raw.json: RA*U, raw mode
 *
 * A dict line names them without an extension. The code that runs them
 * is in dict.c. A changed .modal still works: steno -C makes a .mode
 * file of it, and a dict line can name that file instead. */

#include "steno.h"

/* --- dotterel-movement ------------------------------------------------
 * Its options: exclude_entry no, exit_on_mismatch yes, exit_on_match
 * no, ignore_folding yes (set in dict.c, load_movement). */

/* 'e' an entry stroke (it turns the mode on and gives the text), 'd' a
 * stroke while in the mode; in the .modal file's order */
const struct builtin_line dotterel_movement[] = {
    { 'e', "{#Left}{^}", "#TPH-F STPH-F" },
    { 'e', "{#Shift_l(Left)}{^}", "#TPH*F STPH*F" },
    { 'e', "{#Down}{^}", "#TPH-P STPH-P" },
    { 'e', "{#Shift_l(Down)}{^}", "#TPH*P STPH*P" },
    { 'e', "{#Up}{^}", "#TPH-L STPH-L" },
    { 'e', "{#Shift_l(Up)}{^}", "#TPH*L STPH*L" },
    { 'e', "{#Right}{^}", "#TPH-T" },
    { 'e', "{#Shift_l(Right)}{^}", "#TPH*T STPH*T" },
    { 'e', "{#Control_l(Left)}{^}", "#TPH-R STPH-R" },
    { 'e', "{#Control_l(Shift_l(Left))}{^}", "#TPH*R STPH*R" },
    { 'e', "{#Page_Down}{^}", "#TPH-B STPH-B" },
    { 'e', "{#Shift_l(Page_Down)}{^}", "#TPH*B STPH*B" },
    { 'e', "{#Page_Up}{^}", "#TPH-G STPH-G" },
    { 'e', "{#Shift_l(Page_Up)}{^}", "#TPH*G STPH*G" },
    { 'e', "{#Control_l(Right)}{^}", "#TPH-S" },
    { 'e', "{#Control_l(Shift_l(Right))}{^}", "#TPH*S STPH*S" },
    { 'e', "{#Home}{^}", "#TPH-FR STPH-FR" },
    { 'e', "{#Shift_l(Home)}{^}", "#TPH*FR STPH*FR" },
    { 'e', "{#End}{^}", "#TPH-TS" },
    { 'e', "{#Shift_l(End)}{^}", "#TPH*TS STPH*TS" },
    { 'e', "{#Control_l(Home)}{^}", "#TPH-LG STPH-LG" },
    { 'e', "{#Control_l(Shift_l(Home))}{^}", "#TPH*LG STPH*LG" },
    { 'e', "{#Control_l(End)}{^}", "#TPH-PB STPH-PB" },
    { 'e', "{#Control_l(Shift_l(End))}{^}", "#TPH*PB STPH*PB" },
    { 'd', "{#Left}{^}", "-F" },
    { 'd', "{#Shift_l(Left)}{^}", "*F" },
    { 'd', "{#Down}{^}", "-P" },
    { 'd', "{#Shift_l(Down)}{^}", "*P" },
    { 'd', "{#Up}{^}", "-L" },
    { 'd', "{#Shift_l(Up)}{^}", "*L" },
    { 'd', "{#Right}{^}", "-T" },
    { 'd', "{#Shift_l(Right)}{^}", "*T" },
    { 'd', "{#Control_l(Left)}{^}", "-R" },
    { 'd', "{#Control_l(Shift_l(Left))}{^}", "*R" },
    { 'd', "{#Page_Down}{^}", "-B" },
    { 'd', "{#Shift_l(Page_Down)}{^}", "*B" },
    { 'd', "{#Page_Up}{^}", "-G" },
    { 'd', "{#Shift_l(Page_Up)}{^}", "*G" },
    { 'd', "{#Control_l(Right)}{^}", "-S" },
    { 'd', "{#Control_l(Shift_l(Right))}{^}", "*S" },
    { 'd', "{#Home}{^}", "-FR" },
    { 'd', "{#Shift_l(Home)}{^}", "*FR" },
    { 'd', "{#End}{^}", "-TS" },
    { 'd', "{#Shift_l(End)}{^}", "*TS" },
    { 'd', "{#Control_l(Home)}{^}", "-LG" },
    { 'd', "{#Control_l(Shift_l(Home))}{^}", "*LG" },
    { 'd', "{#Control_l(End)}{^}", "-PB" },
    { 'd', "{#Control_l(Shift_l(End))}{^}", "*PB" },
    { 0, 0, 0 }
};

/* --- dotterel-modifiers -----------------------------------------------
 * A modifier stroke, then a stroke that presses one key or types one
 * character: that key with the modifiers. bits: 1 control, 2 alt,
 * 4 shift, 8 super */
const char *const dotterel_mod_strokes[] = {
    "KHR", "SH", "THRA", "KPW", "SKHR", "TKHRA", "STHRA", "STKHRA",
    "KPWHR", "SKPWHR", 0
};
const int dotterel_mod_bits[] = { 1, 4, 2, 8, 5, 3, 6, 7, 9, 12 };

/* Where the keys come from, as dotterel-modifiers.py reads them: the
 * first key of a stroke stays. These dictionaries are taken from the
 * list when they are in it, or else read from the dictionaries
 * directory (.dict); dotterel-movement is the one above. Your new
 * punctuation comes before the old (dotterel-old-punctuation), so a
 * key you give a stroke wins and the rest still work. */
const char *const dotterel_mod_keys[] = {
    "dotterel-punctuation", "dotterel-old-punctuation",
    "dotterel-commands", "dotterel-fingerspelling", "dotterel-numbers",
    "dotterel-movement", 0
};

/* --- dotterel-raw ----------------------------------------------------- */
const char dotterel_raw_text[] = "{PLOVER:DOTTEREL_RAW}";
const char dotterel_raw_outline[] = "RA*U";
