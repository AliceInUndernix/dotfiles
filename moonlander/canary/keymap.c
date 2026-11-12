/*
 * ==========================================================================
 *  ZSA Moonlander (rev B), keymap "canary"
 *  Canary letter layout, built on optozorax's keymap libraries.
 *  Version 91 (the latest; earlier drafts were numbered 75..90).
 * ==========================================================================
 *
 * HOW TO READ THIS FILE
 *   The file goes from top to bottom like this:
 *     1. Includes          the libraries this keymap is built on
 *     2. Names             layer names, combo key names, our own keycodes
 *     3. dwm shortcuts     short names for the window manager shortcuts
 *        tmux keys        the tmux layer's keys (end of section 3)
 *     4. Combos            what each combo key does alone and in chords
 *     5. Layers            the keys of every layer, with a picture of each
 *     6. LED colours       thumb keys light up in the colour of the layer
 *     7. Mouse movement    the accelerating mouse pointer
 *     8. Sound             clicky mode and the Jingle Bells key
 *     9. QMK hooks         the functions QMK calls; they tie it all together
 *
 * HOW KEYS ARE NAMED IN THE PICTURES BELOW
 *   The Moonlander has, per half:
 *     number row   7 keys
 *     top row      7 keys   (outer key, 5 letter keys, inner key)
 *     home row     7 keys   (outer key, 5 letter keys, inner key)
 *     bottom row   6 keys   (outer key, 5 letter keys, no inner key)
 *     lower row    5 small keys, plus one big angled thumb key
 *                  (left half: the big key is Ctrl; right half: Alt)
 *     thumbs       3 thumb keys, counted 1-2-3 from left to right
 *   "--" in a picture means the key does nothing on that layer.
 *   Keys are often named after the letter they have on layer 0, e.g.
 *   "the n key" on the mouse layer is the key that types n on layer 0.
 *
 * LAYERS  (number, name, how you reach it, thumb LED colour)
 *   0  L_EN     Canary letters, always there underneath         white
 *   1  L_EN_S   Canary with Shift (switched on by the Shift keys) dim white
 *   2  L_RU     unused, reserved by lang_shift (see below)
 *   3  L_RU_S   unused, reserved by lang_shift
 *   4  L_RED    HOLD left thumb 2: brackets, arrows, digits      red
 *   5  L_STENO  TAP left thumb 3 to switch on, tap again to leave goldenrod
 *   6  L_DWM    HOLD the right lower-row key next to Alt (Super is
 *               held too): dwm window manager shortcuts         magenta
 *   7  L_MOUSE  HOLD the left lower-row key next to Ctrl:
 *               mouse pointer, wheel and clicks                 green
 *   8  L_RGB    TAP the far-left lower-row key to switch on, tap again
 *               (or the top-left key) to leave: backlight settings yellow
 *   9  L_TMUX   HOLD the right lower-row key next to DWM: tmux,
 *               each key types Ctrl-b and then one of tmux's
 *               default keys                                    cyan
 *
 *   Layers 2 and 3 only exist because the lang_shift library always uses
 *   layers 0..3 for itself (English, English+Shift, Russian, Russian+Shift).
 *   That is why our own layers start at 4.
 *
 *   When several layers are on, the highest number wins. A key marked
 *   _______ (transparent) on a layer does what it does on the next layer
 *   down that is on.
 *
 * KEYCODE RULES (because of lang_shift)
 *   - Letters and punctuation use EN_ names (EN_A, EN_QUOT, ...), not KC_.
 *   - Shift is SFT_N (held) or SFT_N_O (one-time Shift). Not KC_LSFT,
 *     with one exception on the dwm layer (explained there).
 *   - Ctrl / Alt / Super are CTRL_0 / ALT_0 / WIN_0.
 *   - Keys that are not characters (arrows, Delete, Enter, F-keys) use
 *     the normal KC_ names.
 *   - _______ = transparent (see above).  KC_NO = does nothing.
 *   The full list of EN_ names is in lang_shift/include.h.
 *
 *   Exception to the rule: the dwm keys (section 3) are raw KC_ keys such
 *   as KC_1, because they are shortcuts for the window manager, not
 *   characters to type. Super is held by the dwm layer key itself.
 *
 * FILES THAT BELONG TOGETHER
 *   keymap.c (this file), config.h (timings and library settings) and
 *   rules.mk (which QMK features are built in). rules.mk must have
 *   STENO_ENABLE = yes, STENO_PROTOCOL = geminipr and MOUSEKEY_ENABLE = yes.
 *   config.h must have COMBO_KEYS_COUNT at least 32 (we use 32 combo keys).
 *
 * WHERE THE LIBRARIES COME FROM
 *   arbitrary_keycode, lang_shift, combo and color are optozorax's keymap
 *   libraries (https://github.com/optozorax/moonlander). Copies sit next
 *   to keymap.c in this folder, unchanged (versions in ../README.md).
 *   moonlander_leds.h (LED_1..LED_6) and mcuconf.h come from the same base.
 *   The code targets ZSA's QMK fork (the "firmware25" generation, see the
 *   audio notes in config.h), with ../qmk.patch applied.
 *
 * NOTES FOR WHOEVER EDITS THIS NEXT
 *   - After changing a layer, update the picture in the comment above it.
 *   - New combo key: give it a CMB_0xx number in enum Combos, add a one-key
 *     entry in combos[], and raise COMBO_KEYS_COUNT in config.h if needed.
 *   - New own keycode: add it to enum custom_keycodes and to the switch in
 *     process_record_user(). Keep ACC_L..ACC_R together and in order.
 *   - New layer: add it to enum Layers, to keymaps[], and give it a colour
 *     in layermap[] (section 6), or its thumb LEDs stay dark.
 *   - The design history (what was tried and changed back) is in
 *     HANDOFF.md, which travels with these files.
 */

#include QMK_KEYBOARD_H
#include <stdlib.h>   // rand(), for the clicky sound

// Stop the build early with a readable message, instead of a confusing
// "STN_S1 undeclared" error, if steno is not switched on in rules.mk.
#ifndef STENO_ENABLE
#error "The steno layer needs STENO_ENABLE = yes and STENO_PROTOCOL = geminipr in rules.mk"
#endif

/* ========================================================================== *
 * 1. INCLUDES
 * ========================================================================== */

// lang_shift still uses QMK's old long keycode names. This QMK only knows
// the short ones, so we tell it they are the same thing.
#define KC_LSHIFT KC_LSFT
#define KC_LCTRL  KC_LCTL

// A shorter name for ZSA's "turn the layer colours on/off" key (used on the
// backlight layer).
#define RGB_LYR TOGGLE_LAYER_COLOR

// The colour library still uses QMK's old name for "number of LEDs".
#ifndef DRIVER_LED_TOTAL
#define DRIVER_LED_TOTAL RGB_MATRIX_LED_COUNT
#endif

// The ORDER of these includes matters. Each library makes its own keycodes,
// starting at CUSTOM_SAFE_RANGE, and then moves CUSTOM_SAFE_RANGE past them
// so the next library (and our own keycodes) don't overlap.
#include "arbitrary_keycode/include.h"  // lets the combo library "press" any keycode
#define CUSTOM_SAFE_RANGE ZSA_SAFE_RANGE
#include "lang_shift/include.h"         // EN_ keycodes, smart Shift, CTRL_0 etc.
#include "combo/include.h"              // CMB_ keys and the chord engine
#include "color/include.h"              // layer colours on the thumb LEDs
#include "moonlander_leds.h"            // LED_1..LED_6: the small status LEDs

/* ========================================================================== *
 * 2. NAMES
 * ========================================================================== */

enum Layers {
    L_EN = 0,   // 0: Canary
    L_EN_S,     // 1: Canary with Shift (lang_shift switches to it by itself)
    L_RU,       // 2: unused (reserved by lang_shift)
    L_RU_S,     // 3: unused (reserved by lang_shift)
    L_RED,      // 4: brackets, arrows, digits
    L_STENO,    // 5: steno (GeminiPR)
    L_DWM,      // 6: dwm window manager shortcuts
    L_MOUSE,    // 7: mouse
    L_RGB,      // 8: backlight settings
    L_TMUX,     // 9: tmux (Ctrl-b + a default tmux key)
};

/*
 * Combo keys.
 * A combo key is put in a layer instead of a normal key. On its own it
 * does one thing; pressed together with certain other combo keys it does
 * something else (that is a "chord"). What each one does is written in the
 * combos[] table in section 4. config.h says how many may exist
 * (COMBO_KEYS_COUNT); all 32 below are used.
 */
enum Combos {
    // Thumbs and modifiers
    CMB_SPC  = CMB_000,  // left thumb 1: Space
    CMB_RED  = CMB_001,  // left thumb 2: red layer (hold)
    CMB_SFT  = CMB_002,  // right thumb 1: Shift (hold)
    CMB_TAB  = CMB_003,  // right thumb 2: Tab
    CMB_BSP  = CMB_004,  // right thumb 3: Backspace
    CMB_CTL  = CMB_005,  // big left thumb key: Ctrl (hold)
    CMB_ALT  = CMB_031,  // big right thumb key: Alt (hold)
    CMB_DWM  = CMB_014,  // right lower row, next to Alt: dwm layer (hold)
    CMB_RGB  = CMB_015,  // left lower row, far left: backlight layer on/off

    // Brackets (red layer)
    CMB_LCB  = CMB_006,  // {
    CMB_RCB  = CMB_007,  // }
    CMB_LAN  = CMB_008,  // <
    CMB_RAN  = CMB_009,  // >
    CMB_LPR  = CMB_010,  // (
    CMB_RPR  = CMB_011,  // )
    CMB_LBR  = CMB_012,  // [
    CMB_RBR  = CMB_013,  // ]

    // Number row, layer 0
    CMB_PLS  = CMB_016,  // +
    CMB_DQ   = CMB_017,  // "
    CMB_DOT  = CMB_018,  // .  (then a space, and the next letter is a capital)
    CMB_EXC  = CMB_019,  // !  (then a space, and the next letter is a capital)
    CMB_QUE  = CMB_020,  // ?  (then a space, and the next letter is a capital)
    CMB_CMM  = CMB_021,  // ,  (then a space)
    CMB_QT   = CMB_022,  // '
    CMB_SLS  = CMB_023,  // /
    CMB_PIP  = CMB_024,  // |

    // Number row, layer 1 (Shift held): the same marks, no space after
    CMB_SPLS = CMB_025,  // +
    CMB_SDQ  = CMB_026,  // "
    CMB_SDOT = CMB_027,  // .
    CMB_SEXC = CMB_028,  // !
    CMB_SQUE = CMB_029,  // ?
    CMB_SCMM = CMB_030,  // ,
};

/*
 * Our own keycodes. Each one types a short piece of text. What they type is
 * programmed in process_record_user() in section 9. CUSTOM_SAFE_RANGE has
 * already been moved past all the library keycodes by the includes above.
 */
enum custom_keycodes {
    // Bracket pairs (both bracket keys of a pair pressed together)
    PAIR_PAREN = CUSTOM_SAFE_RANGE,   // ()
    PAIR_BRACKET,                     // []
    PAIR_BRACE,                       // {}
    PAIR_ANGLE,                       // <>

    // Punctuation followed by a space
    DOT_SP,                           // ". "  and the next letter is a capital
    EXC_SP,                           // "! "  and the next letter is a capital
    QUE_SP,                           // "? "  and the next letter is a capital
    CMM_SP,                           // ", "
    SEMI_SP,                          // "; "
    COLN_SP,                          // ": "
    ELLIPSIS,                         // "... "
    ELLIPSIS_N,                       // "..."  (no space; used with Shift held)

    // Mouse layer: pointer moves that speed up while held (section 7).
    // Keep these four together and in this order: the code relies on it.
    ACC_L,                            // left
    ACC_D,                            // down
    ACC_U,                            // up
    ACC_R,                            // right

    // dwm layer key (section 3): holds Super and the dwm layer together
    DWM_HOLD,
    D_SHOT,                           // dwm layer: plain Print (Super let go for it)

    // Sound (section 8)
    JINGLE,                           // play Jingle Bells (press again to stop)
    CLICKY,                           // clicky mode on/off: a tick on every key press

    // tmux layer (end of section 3): each one types Ctrl-b and then the
    // key in tmux_keys[]. Keep T_FIRST..T_LAST together and in the same
    // order as tmux_keys[].
    T_FIRST,
    T_WIN1 = T_FIRST, T_WIN2, T_WIN3, T_WIN4, T_WIN5,
    T_WIN6, T_WIN7, T_WIN8, T_WIN9,
    T_TREE, T_KEYS, T_PASTE,
    T_LEFT, T_DOWN, T_UP, T_RIGHT,
    T_ZOOM, T_PANES,
    T_COPY, T_KILL, T_CMD, T_NEXTP, T_NEW, T_LAST,
    T_LAYOUT, T_TILED, T_MAINV, T_EVEN, T_RENAME,
    T_BREAK, T_SPLITH, T_SESN, T_SESP, T_SPLITV, T_DETACH,
    T_LAST_KEY,
};

/* ========================================================================== *
 * 3. DWM SHORTCUTS
 *
 * dwm's shortcuts are MODKEY + key, and dwm uses Super as MODKEY.
 *
 * Holding the dwm layer key holds a real Super for as long as the key is
 * down, as if a Super key were held (DWM_HOLD, section 9). So the keys on
 * the dwm layer only send the plain key: Tag2 is just KC_2.
 *
 * Why: before version 88 every key sent its own Super (LGUI(KC_2)). QMK
 * drops that kind of Super as soon as any other key goes down, so when
 * fingers overlapped dwm sometimes saw a bare "2" and the 2 got typed.
 * A held Super can't be lost that way.
 *
 * If dwm is ever switched to Alt as MODKEY, change KC_LGUI to KC_LALT in
 * DWM_HOLD_MOD below.
 * ========================================================================== */

#define DWM_HOLD_MOD     KC_LGUI           // the modifier the layer key holds
#define DWM_MOD(kc)      (kc)              // Super (held) + key
#define DWM_MOD_SFT(kc)  LSFT(kc)          // Super (held) + Shift + key

// Tags (dwm's workspaces). Hold Shift too to MOVE the window to that tag.
#define D_TAG1  DWM_MOD(KC_1)
#define D_TAG2  DWM_MOD(KC_2)
#define D_TAG3  DWM_MOD(KC_3)
#define D_TAG4  DWM_MOD(KC_4)
#define D_TAG5  DWM_MOD(KC_5)
#define D_TAG6  DWM_MOD(KC_6)
#define D_TAG7  DWM_MOD(KC_7)
#define D_TAG8  DWM_MOD(KC_8)
#define D_TAG9  DWM_MOD(KC_9)
#define D_ALL   DWM_MOD(KC_0)            // view all tags (Shift: window on all tags)

// Launch and close
#define D_TERM  DWM_MOD_SFT(KC_ENT)      // terminal, st (Super+Shift+Enter)
#define D_ZOOM  DWM_MOD(KC_ENT)          // focused window to/from master (Super+Enter)
#define D_RUN   DWM_MOD(KC_P)            // dmenu launcher (Super+p)
#define D_SCRT  DWM_MOD(KC_GRV)          // scratchpad terminal on/off (Super+`)
#define D_KILL  DWM_MOD_SFT(KC_C)        // close the window (Super+Shift+c), no Shift needed
#define D_TMUX  DWM_MOD(KC_X)            // tmux: attach to the last session, or start one (Super+x;
                                         //   Shift: always a new session)
#define D_WEB   DWM_MOD(KC_W)            // IceCat (Super+w; Shift: Ungoogled Chromium)
#define D_MIX   DWM_MOD(KC_A)            // wiremix sound mixer (Super+a)
#define D_TOP   DWM_MOD(KC_ESC)          // btop (Super+Escape)
#define D_LOCK  DWM_MOD_SFT(KC_L)        // lock the screen, slock (Super+Shift+l)
#define D_SUSP  DWM_MOD_SFT(KC_S)        // suspend (Super+Shift+s)
// D_SHOT (plain Print, Flameshot's picker) is an own keycode in section 2,
// because it has to let go of the held Super for a moment.

// Focus, stack and master area: dwm's default h j k l.
// Hold Shift with Next / Prev to MOVE the window down / up the stack.
#define D_MSTS  DWM_MOD(KC_H)            // shrink master area
#define D_FOCN  DWM_MOD(KC_J)            // focus next window (Shift: move it down)
#define D_FOCP  DWM_MOD(KC_K)            // focus previous window (Shift: move it up)
#define D_MSTG  DWM_MOD(KC_L)            // grow master area
#define D_NMAI  DWM_MOD(KC_I)            // one more window in the master area
#define D_NMAD  DWM_MOD(KC_D)            // one fewer window in the master area

// Layouts
#define D_TILE  DWM_MOD(KC_T)            // tiled
#define D_FLOT  DWM_MOD(KC_F)            // floating
#define D_MONO  DWM_MOD(KC_M)            // monocle (one window fills the screen)
#define D_DECK  DWM_MOD(KC_R)            // doubledeck
#define D_TFLT  DWM_MOD_SFT(KC_SPC)      // float / unfloat the focused window
#define D_BAR   DWM_MOD(KC_B)            // show / hide the bar

// Monitors (hold Shift too to send the window to the other monitor)
#define D_MONP  DWM_MOD(KC_COMM)         // focus previous monitor
#define D_MONN  DWM_MOD(KC_DOT)          // focus next monitor

/*
 * TMUX KEYS
 *
 * tmux's prefix stays Ctrl-b (tmux.conf doesn't change it). Each key on
 * the tmux layer types Ctrl-b and then one of tmux's own default keys,
 * so nothing has to be set up in tmux for them: "New" is Ctrl-b c, "Zoom"
 * is Ctrl-b z. Run `tmux list-keys -N` (or the Keys key) to see them all.
 *
 * Ctrl or Alt held while pressing a key (the big thumb keys) are let go
 * for Ctrl-b and held again for the second key. That gives tmux's default
 * resize keys: Ctrl + arrow = resize by 1, Alt + arrow = resize by 5.
 *
 * The table is in the same order as T_FIRST..T_LAST_KEY in section 2.
 */
#define TMUX_PREFIX  LCTL(KC_B)

static const uint16_t PROGMEM tmux_keys[] = {
    KC_1, KC_2, KC_3, KC_4, KC_5,          // Win1..5  window 1..5
    KC_6, KC_7, KC_8, KC_9,                // Win6..9  window 6..9
    KC_W,                                  // Tree   choose a window from a list
    LSFT(KC_SLSH),                         // Keys   ?  list key bindings
    KC_RBRC,                               // Paste  ]  paste the last copied text
    KC_LEFT, KC_DOWN, KC_UP, KC_RGHT,      // pane to the left / below / above / right
    KC_Z,                                  // Zoom   pane fills the window, and back
    KC_Q,                                  // Panes  show pane numbers
    KC_LBRC,                               // Copy   [  copy mode
    KC_X,                                  // Kill   close pane (asks y/n)
    LSFT(KC_SCLN),                         // Cmd    :  command prompt
    KC_O,                                  // Pane>  next pane
    KC_C,                                  // New    new window
    KC_L,                                  // Last   previous window
    KC_SPC,                                // Lay    next layout
    LALT(KC_5),                            // Tile   tiled layout
    LALT(KC_4),                            // Main   main-vertical layout
    LALT(KC_1),                            // Even   even-horizontal layout
    KC_COMM,                               // Ren    ,  rename window
    LSFT(KC_1),                            // Brk    !  pane into its own window
    LSFT(KC_5),                            // Spl|   %  split side by side
    LSFT(KC_0),                            // Ses>   )  next session
    LSFT(KC_9),                            // Ses<   (  previous session
    LSFT(KC_QUOT),                         // Spl-   "  split one above the other
    KC_D,                                  // Det    detach
};
_Static_assert(sizeof(tmux_keys) / sizeof(tmux_keys[0]) == T_LAST_KEY - T_FIRST,
               "tmux_keys[] and T_FIRST..T_LAST_KEY must have the same length");

// Type Ctrl-b, then the key. Held Ctrl/Alt/Shift are let go for Ctrl-b
// (so tmux sees a plain Ctrl-b) and pressed again for the second key.
static void tmux_send(uint16_t keycode) {
    uint8_t mods = get_mods();
    clear_mods();
    tap_code16(TMUX_PREFIX);
    set_mods(mods);
    tap_code16(pgm_read_word(&tmux_keys[keycode - T_FIRST]));
}

/* ========================================================================== *
 * 4. COMBOS
 *
 * Two kinds of entry:
 *
 *   CHORD(output, keys...)
 *       Waits until it is clear which keys you are pressing together
 *       (at most COMBO_WAIT_TIME ms, set in config.h), then sends `output`.
 *       Used for anything that types a character, because a typed
 *       character can't be taken back.
 *
 *   IMMEDIATE_CHORD(output, undo, keys...)
 *       Sends `output` the moment the key goes down, so there is no delay.
 *       If you then press a key that makes a bigger chord, `undo` is
 *       released and the bigger chord takes over. Used for modifiers and
 *       layer keys, where a delay would feel laggy.
 *
 * Every combo key needs its own one-key entry, or it would do nothing when
 * pressed alone. Chords only work between combo keys (CMB_ names).
 * ========================================================================== */

const ComboWithKeycode combos[] PROGMEM = {
    // --- Thumbs, modifiers and layer keys, pressed alone ---
    CHORD(KC_SPC,  CMB_SPC),                                       // Space
    CHORD(KC_TAB,  CMB_TAB),                                       // Tab
    CHORD(KC_BSPC, CMB_BSP),                                       // Backspace
    IMMEDIATE_CHORD(MO(L_RED), MO(L_RED), /* <-! */ CMB_RED),      // hold: red layer
    IMMEDIATE_CHORD(SFT_N,     SFT_N,     /* <-! */ CMB_SFT),      // hold: Shift
    IMMEDIATE_CHORD(CTRL_0,    CTRL_0,    /* <-! */ CMB_CTL),      // hold: Ctrl
    IMMEDIATE_CHORD(ALT_0,     ALT_0,     /* <-! */ CMB_ALT),      // hold: Alt
    IMMEDIATE_CHORD(DWM_HOLD,  DWM_HOLD,  /* <-! */ CMB_DWM),      // hold: Super + dwm layer
    CHORD(TG(L_RGB), CMB_RGB),                                     // tap: backlight layer on/off

    // --- Two neighbouring keys together ---
    CHORD(KC_ENT,  CMB_SPC, CMB_RED),   // Space + red layer key    -> Enter
    CHORD(KC_ESC,  CMB_TAB, CMB_BSP),   // Tab + Backspace          -> Escape
    CHORD(KC_DEL,  CMB_BSP, CMB_ALT),   // Backspace + Alt          -> Delete
    // Ctrl + red layer key -> Ctrl+Shift, held for as long as both are down.
    // While this is held the red layer is OFF (so letters give Ctrl+Shift+letter).
    IMMEDIATE_CHORD(CTSH_0, CTSH_0, /* <-! */ CMB_CTL, CMB_RED),

    // --- Brackets on the red layer, one key ---
    CHORD(EN_LCBR, CMB_LCB),            // {
    CHORD(EN_RCBR, CMB_RCB),            // }
    CHORD(EN_LT,   CMB_LAN),            // <
    CHORD(EN_GT,   CMB_RAN),            // >
    CHORD(EN_LPRN, CMB_LPR),            // (
    CHORD(EN_RPRN, CMB_RPR),            // )
    CHORD(EN_LBRC, CMB_LBR),            // [
    CHORD(EN_RBRC, CMB_RBR),            // ]

    // --- Brackets: both keys of a pair together type the whole pair ---
    CHORD(PAIR_BRACE,   CMB_LCB, CMB_RCB),   // { + }  -> {}
    CHORD(PAIR_ANGLE,   CMB_LAN, CMB_RAN),   // < + >  -> <>
    CHORD(PAIR_PAREN,   CMB_LPR, CMB_RPR),   // ( + )  -> ()
    CHORD(PAIR_BRACKET, CMB_LBR, CMB_RBR),   // [ + ]  -> []

    // --- Number row on layer 0, one key ---
    // (~ = @ ^ * are normal keys, not combo keys, so they are not listed here.)
    CHORD(EN_PLUS, CMB_PLS),            // +
    CHORD(EN_DQUO, CMB_DQ),             // "
    CHORD(DOT_SP,  CMB_DOT),            // ". " and a capital next
    CHORD(EXC_SP,  CMB_EXC),            // "! " and a capital next
    CHORD(QUE_SP,  CMB_QUE),            // "? " and a capital next
    CHORD(CMM_SP,  CMB_CMM),            // ", "
    CHORD(EN_QUOT, CMB_QT),             // '
    CHORD(EN_SLSH, CMB_SLS),            // /
    CHORD(EN_PIPE, CMB_PIP),            // |

    // --- Number row on layer 0, two keys ---
    CHORD(EN_AMPR, CMB_DQ,  CMB_DOT),   // " + .   -> &
    CHORD(EN_HASH, CMB_CMM, CMB_QT),    // , + '   -> #
    CHORD(EN_GRV,  CMB_DQ,  CMB_QT),    // " + '   -> `   (one key on each hand)
    CHORD(EN_BSLS, CMB_SLS, CMB_PIP),   // / + |   -> \   (backslash)

    // --- Number row on layer 0, three and four keys ---
    CHORD(EN_DLR,   CMB_DQ,  CMB_DOT, CMB_EXC),           // " + . + !      -> $
    CHORD(EN_PERC,  CMB_QUE, CMB_CMM, CMB_QT),            // ? + , + '      -> %
    CHORD(ELLIPSIS, CMB_PLS, CMB_DQ,  CMB_DOT, CMB_EXC),  // + + " + . + !  -> "... "

    // --- Number row on layer 1 (Shift held): plain marks, no space ---
    CHORD(EN_PLUS, CMB_SPLS),           // +
    CHORD(EN_DQUO, CMB_SDQ),            // "
    CHORD(EN_DOT,  CMB_SDOT),           // .
    CHORD(EN_EXCL, CMB_SEXC),           // !
    CHORD(EN_QUES, CMB_SQUE),           // ?
    CHORD(EN_COMM, CMB_SCMM),           // ,
    // The same chords as on layer 0. They are needed again because with Shift
    // held (or right after ". ", "! ", "? ") the number-row keys are the
    // layer 1 combo keys. ' / | are the same keys on both layers.
    CHORD(EN_AMPR, CMB_SDQ,  CMB_SDOT),                   // " + .      -> &
    CHORD(EN_HASH, CMB_SCMM, CMB_QT),                     // , + '      -> #
    CHORD(EN_GRV,  CMB_SDQ,  CMB_QT),                     // " + '      -> `
    CHORD(EN_DLR,  CMB_SDQ,  CMB_SDOT, CMB_SEXC),         // " + . + !  -> $
    CHORD(EN_PERC, CMB_SQUE, CMB_SCMM, CMB_QT),           // ? + , + '  -> %
    CHORD(ELLIPSIS_N, CMB_SPLS, CMB_SDQ, CMB_SDOT, CMB_SEXC),   // + + " + . + ! -> "..." (no space)
};
const uint8_t combos_size = sizeof(combos) / sizeof(ComboWithKeycode);

/* ========================================================================== *
 * 5. LAYERS
 *
 * Each LAYOUT_moonlander(...) lists the 72 keys in this order:
 *
 *   number row   7 left  |  7 right
 *   top row      7 left  |  7 right
 *   home row     7 left  |  7 right
 *   bottom row   6 left  |  6 right
 *   lower row    5 small keys + big Ctrl key  |  big Alt key + 5 small keys
 *   thumbs       3 left  |  3 right
 * ========================================================================== */

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {

    /*
     * LAYER 0: Canary
     *
     *          left half                              right half
     * number   ~    +    "    .    !    =    @        ^    *    ?    ,    '    /    |
     * top      _    w    l    y    p    b    --       --   z    f    o    u    ;    -
     * home     1Sh  c    r    s    t    g    --       --   m    n    e    i    a    1Sh
     * bottom   --   q    j    v    d    k                 x    h    --   --   :    --
     * lower    RGB  --   --   --   Mse  [Ctrl]      [Alt] DWM  Tmux --   --   --
     * thumbs                  Spc  Red  Steno       Shift Tab  Bksp
     *
     *   1Sh   one-time Shift: the next key you press is shifted
     *   RGB   tap: backlight layer on/off           Mse  hold: mouse layer
     *   DWM   hold: Super + dwm layer               Red  hold: red layer
 *   Tmux  hold: tmux layer
     *   Steno tap: steno layer on (tap again on the steno layer to leave)
     *
     * Punctuation that adds a space:
     *   .  !  ?   type the mark + a space, and the next letter is a capital
     *   ,  ;  :   type the mark + a space
     *   With Shift held (layer 1) they type just the mark, no space.
     *
     * Chords (press the keys together):
     *   Space + Red        Enter          Tab + Bksp        Escape
     *   Bksp + Alt         Delete         Ctrl + Red        Ctrl+Shift
     *   " + .              &              , + '             #
     *   " + . + !          $              ? + , + '         %
     *   " + '              `              / + |             \  (backslash)
     *   + + " + . + !      "... "
     */
    [L_EN] = LAYOUT_moonlander(
        EN_TILD, CMB_PLS, CMB_DQ,  CMB_DOT, CMB_EXC, EN_EQL,  EN_AT,        EN_CIRC, EN_ASTR, CMB_QUE, CMB_CMM, CMB_QT,  CMB_SLS, CMB_PIP,
        EN_UNDS, EN_W,    EN_L,    EN_Y,    EN_P,    EN_B,    KC_NO,        KC_NO,   EN_Z,    EN_F,    EN_O,    EN_U,    SEMI_SP, EN_MINS,
        SFT_N_O, EN_C,    EN_R,    EN_S,    EN_T,    EN_G,    KC_NO,        KC_NO,   EN_M,    EN_N,    EN_E,    EN_I,    EN_A,    SFT_N_O,
        KC_NO,   EN_Q,    EN_J,    EN_V,    EN_D,    EN_K,                  EN_X,    EN_H,    KC_NO,   KC_NO,   COLN_SP, KC_NO,
        CMB_RGB, KC_NO,   KC_NO,   KC_NO,   MO(L_MOUSE), CMB_CTL,           CMB_ALT, CMB_DWM, MO(L_TMUX), KC_NO, KC_NO,   KC_NO,
                                            CMB_SPC, CMB_RED, TG(L_STENO), CMB_SFT, CMB_TAB, CMB_BSP
    ),

    /*
     * LAYER 1: Canary with Shift
     *
     * lang_shift switches this layer on by itself while Shift is held, or
     * for one key after a one-time Shift. Every letter is written out as a
     * capital (EN_S_ names). The number-row marks and ; : type without a
     * space here. The number-row chords (& # ` $ % and backslash) work here too; the
     * four-key ellipsis chord types "..." with no space.
     * Everything marked _______ works as on layer 0.
     *
     * number   __   +    "    .    !    __   __       __   __   ?    ,    __   __   __
     * top      __   W    L    Y    P    B    __       __   Z    F    O    U    ;    __
     * home     __   C    R    S    T    G    __       __   M    N    E    I    A    __
     * bottom   __   Q    J    V    D    K                 X    H    __   __   :    __
     *   (__ = same as layer 0)
     */
    [L_EN_S] = LAYOUT_moonlander(
        _______, CMB_SPLS, CMB_SDQ, CMB_SDOT, CMB_SEXC, _______, _______,    _______, _______, CMB_SQUE, CMB_SCMM, _______, _______, _______,
        _______, EN_S_W,  EN_S_L,  EN_S_Y,  EN_S_P,  EN_S_B,  _______,      _______, EN_S_Z,  EN_S_F,  EN_S_O,  EN_S_U,  EN_SCLN, _______,
        _______, EN_S_C,  EN_S_R,  EN_S_S,  EN_S_T,  EN_S_G,  _______,      _______, EN_S_M,  EN_S_N,  EN_S_E,  EN_S_I,  EN_S_A,  _______,
        _______, EN_S_Q,  EN_S_J,  EN_S_V,  EN_S_D,  EN_S_K,                EN_S_X,  EN_S_H,  _______, _______, EN_COLN, _______,
        _______, _______, _______, _______, _______, _______,               _______, _______, _______, _______, _______, _______,
                                            _______, _______, _______,      _______, _______, _______
    ),

    // Layers 2 and 3 (L_RU, L_RU_S) are lang_shift's Russian layers.
    // They are not used, so they are left out here and stay empty.

    /*
     * LAYER 4: red  (hold left thumb 2)
     *
     * Brackets, arrows and digits. Every other symbol is on layer 0.
     *
     * top      __   __   {    [    (    <    __       __   >    )    ]    }    __   __
     * home     __   __   __   __   __   __   __       __   __   Left Down Up   Right __
     * bottom   __   1    2    3    4    5                 6    7    8    9    0    __
     *   (__ = same as layer 0, so letters still type)
     *
     * The brackets are mirrored: the l and u keys are { and }, y and o are
     * [ and ], p and f are ( and ), b and z are < and >.
     *
     * Bracket pairs: press both keys of a pair together and the whole pair
     * is typed:   { + } -> {}    [ + ] -> []    ( + ) -> ()    < + > -> <>
     * (That is why the bracket keys are combo keys.)
     */
    [L_RED] = LAYOUT_moonlander(
        _______, _______, _______, _______, _______, _______, _______,      _______, _______, _______, _______, _______, _______, _______,
        _______, _______, CMB_LCB, CMB_LBR, CMB_LPR, CMB_LAN, _______,      _______, CMB_RAN, CMB_RPR, CMB_RBR, CMB_RCB, _______, _______,
        _______, _______, _______, _______, _______, _______, _______,      _______, _______, KC_LEFT, KC_DOWN, KC_UP,   KC_RGHT, _______,
        _______, EN_1,    EN_2,    EN_3,    EN_4,    EN_5,                  EN_6,    EN_7,    EN_8,    EN_9,    EN_0,    _______,
        _______, _______, _______, _______, _______, _______,               _______, _______, _______, _______, _______, _______,
                                            _______, _______, _______,      _______, _______, _______
    ),

    /*
     * LAYER 5: steno (GeminiPR)  (tap left thumb 3 to switch on and off)
     *
     * Use it with Plover (or another steno program) set to the Gemini PR
     * protocol. The steno top row is on the home row and the
     * steno bottom row is on the bottom row.
     *
     * home     --   #    T    P    H    *        --       --   *    F    P    L    T    D
     * bottom   --   S    K    W    R    *                     *    R    B    G    S    Z
     * lower    RGB  --   --   --   Mse [--]            [--]  DWM  Tmux --   --   --
     * thumbs                  A    O   exit               --   E    U
     *
     * 26 steno keys: 10 on the left rows, 12 on the right rows, 4 vowels.
     *   #    = number key (STN_N1), on the left pinky home key (the c key).
     *          There is a single S, on the left pinky bottom key (q key).
     *          (Earlier versions had a second S here instead of #; Plover
     *          treats both S keys as the same S, so nothing was lost.)
     *   exit = left thumb 3, the same key that turns steno on from layer 0
     *   RGB, Mse, DWM, Tmux = the same layer keys as on layer 0, in the same spots:
     *          tap RGB for the backlight layer (yellow), hold Mse for the
     *          mouse layer (green), hold DWM for the dwm layer (magenta).
     *          Those layers sit above steno, so they win while they are on.
     *
     * Both hands stay on their usual typing keys. Left: pinky # / S, ring
     * T / K, middle P / W, index H / R, and the star (*) on the index
     * stretch keys g and k. Right: the star on the index stretch keys m
     * and x, then index F / R (n, h), middle P / B, ring L / G, pinky T / S
     * (a and the : key), and D / Z on the outer pinky column.
     * All four * keys count as the same star.
     *
     * Every other key is KC_NO (not transparent) on purpose, so nothing from
     * layer 0 can sneak in while you write steno. The steno number bar is
     * not mapped.
     */
    [L_STENO] = LAYOUT_moonlander(
        KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,        KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,
        KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,        KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,
        KC_NO,   STN_N1,  STN_TL,  STN_PL,  STN_HL,  STN_ST1, KC_NO,        KC_NO,   STN_ST3, STN_FR,  STN_PR,  STN_LR,  STN_TR,  STN_DR,
        KC_NO,   STN_S2,  STN_KL,  STN_WL,  STN_RL,  STN_ST2,               STN_ST4, STN_RR,  STN_BR,  STN_GR,  STN_SR,  STN_ZR,
        CMB_RGB, KC_NO,   KC_NO,   KC_NO,   MO(L_MOUSE), KC_NO,             KC_NO,   CMB_DWM, MO(L_TMUX), KC_NO, KC_NO,   KC_NO,
                                            STN_A,   STN_O,   TG(L_STENO), KC_NO,   STN_E,   STN_U
    ),

    /*
     * LAYER 6: dwm  (hold the right lower-row key next to Alt)
     *
     * Holding the layer key also holds Super (see section 3), so every key
     * here is Super + that key, and each dwm shortcut is one key.
     * Matches dwm/config.h in the dotfiles (checked 2026-11-12).
     * Layout chosen by Alice key by key (version 90). Version 91: Kill,
     * Menu and the new Tmux key on the home row, Web moved up to the top row.
     *
     * top      Web  Flt  Tile Deck Mono Bar  --       --   TFlt Mst+ Mon> Mon< Mst- Susp
     * home     Kill Menu Tmux Scr  Term Lock --       --   Zoom Shrk Next Prev Grow Top
     * bottom   Mix  All  Tag9 Tag8 Tag7 Tag6               Tag5 Tag4 Tag3 Tag2 Tag1 Shot
     * lower    __   __   __   __   __   __          __   (held)
     * thumbs                  Shift __   --          __   __   __
     *   (__ = same as layer 0, so Ctrl and Alt still work; -- = nothing)
     *
     * Shrk Next Prev Grow sit where the red layer has its arrows (Left Down
     * Up Right), like h j k l. The tags are on the bottom row like the red
     * layer's digits, but counted from the right: Tag1 is the right pinky,
     * All (0) the left pinky.
     *
     *   Tag1..9  go to tag 1..9 (Super+1..9)    All   view all tags (Super+0)
     *   Shrk Grow  master area smaller / bigger (Super+h / l)
     *   Next Prev  focus next / previous window (Super+j / k)
     *   Zoom     window to/from master (Super+Enter)
     *   Mst+ Mst-  one more / fewer window in master (Super+i / d)
     *   Mon< Mon>  focus previous / next monitor (Super+, .)
     *   TFlt     float / unfloat the window (Super+Shift+Space)
     *   Flt Tile Deck Mono  floating / tiled / doubledeck / monocle (Super+f t r m)
     *   Bar      show/hide bar (Super+b)
     *   Kill     close the window (sends Super+Shift+c itself, no Shift
     *            needed; version 91)
     *   Tmux     st with tmux: attaches to the last session, or starts one
     *            if there is none (Super+x). With Shift: always a new
     *            session (Super+Shift+x)
     *   Menu     dmenu (Super+p)                Scr   scratchpad terminal (Super+`)
     *   Term     st (Super+Shift+Enter)         Web   IceCat (Super+w)
     *   Mix      wiremix (Super+a)              Top   btop (Super+Escape)
     *   Shot     Flameshot picker (plain Print; Ctrl+A in it takes the whole screen)
     *   Lock     slock (Super+Shift+l)          Susp  suspend (Super+Shift+s)
     *
     * Shift versions: hold Shift as well. Either Shift works: right thumb 1
     * (normal Shift), or left thumb 1 (Space on layer 0), which is a plain
     * Shift (KC_LSFT) here, so you can hold it with the left thumb while the
     * right hand holds the dwm key. This is the one place where plain
     * KC_LSFT is used on purpose. Left thumb 3 is KC_NO here, so it can't
     * switch steno on by accident. With Shift:
     *   Tag1..9  move the window to that tag    All   window on all tags
     *   Next Prev  move the window down / up the stack
     *   Mon< Mon>  send the window to that monitor
     *   Web      Ungoogled Chromium instead of IceCat
     *   Tmux     a new tmux session
     * Ctrl versions of the tags (Super+Ctrl+1..9: show that tag as well)
     * work by holding the big Ctrl key too.
     */
    [L_DWM] = LAYOUT_moonlander(
        _______, _______, _______, _______, _______, _______, _______,      _______, _______, _______, _______, _______, _______, _______,
        D_WEB,   D_FLOT,  D_TILE,  D_DECK,  D_MONO,  D_BAR,   KC_NO,        KC_NO,   D_TFLT,  D_NMAI,  D_MONN,  D_MONP,  D_NMAD,  D_SUSP,
        D_KILL,  D_RUN,   D_TMUX,  D_SCRT,  D_TERM,  D_LOCK,  KC_NO,        KC_NO,   D_ZOOM,  D_MSTS,  D_FOCN,  D_FOCP,  D_MSTG,  D_TOP,
        D_MIX,   D_ALL,   D_TAG9,  D_TAG8,  D_TAG7,  D_TAG6,                D_TAG5,  D_TAG4,  D_TAG3,  D_TAG2,  D_TAG1,  D_SHOT,
        _______, _______, _______, _______, _______, _______,               _______, _______, _______, _______, _______, _______,
                                            KC_LSFT, _______, KC_NO,        _______, _______, _______
    ),

    /*
     * LAYER 7: mouse  (hold the left lower-row key next to Ctrl)
     *
     * The left hand holds the layer key and has modifiers for Ctrl-click
     * etc. The mouse itself is on the right hand.
     *
     * home     __   Ctrl Alt  Supr __   __   __       __   __   Left Down Up   Right __
     * bottom   __   __   __   __   __   __                 __   WhL  WhDn WhUp WhR  __
     * thumbs                  __   __   __          Middle Right Left    (mouse clicks)
     *   (__ = same as layer 0)
     *
     *   Left Down Up Right  move the pointer (two at once = diagonal)
     *   WhL WhDn WhUp WhR   scroll wheel: left, down, up, right
     *
     * Pointer speed: a quick tap moves exactly 1 pixel. Hold the key and the
     * pointer starts moving and gets faster the longer you hold. You can
     * tune this with the ACCEL_ numbers in section 7.
     */
    [L_MOUSE] = LAYOUT_moonlander(
        _______, _______, _______, _______, _______, _______, _______,      _______, _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______,      _______, _______, _______, _______, _______, _______, _______,
        _______, CTRL_0,  ALT_0,   WIN_0,   _______, _______, _______,      _______, _______, ACC_L,   ACC_D,   ACC_U,   ACC_R,   _______,
        _______, _______, _______, _______, _______, _______,               _______, KC_WH_L, KC_WH_D, KC_WH_U, KC_WH_R, _______,
        _______, _______, _______, _______, _______, _______,               _______, _______, _______, _______, _______, _______,
                                            _______, _______, _______,      KC_BTN3, KC_BTN2, KC_BTN1
    ),

    /*
     * LAYER 8: backlight  (tap the far-left lower-row key to switch on; tap
     *                      it again, or the top-left key, to leave)
     *
     * Based on optozorax's "yellow" layer, without the music keys.
     *
     * number   Exit Hue+ Sat+ Bri+ Spd+ Mod+ On/Off
     * top      __   Hue- Sat- Bri- Spd- Mod- LyrCol   __   Jingl Click Audio __ __  __
     * home     Fx0  Fx1  Fx2  Fx25 Fx28 Fx36 Fx27     __   L1   L2   L3   L4   L5   L6
     * bottom   Fx7  Fx13 Fx15 Fx16 Fx17 __
     * lower    __   __   __   __   BOOT __
     *   (__ = same as layer 0; the rest of the right half is __)
     *
     *   Hue Sat Bri Spd   colour, saturation, brightness, speed of the effect
     *   Mod+ Mod-         next / previous backlight effect
     *   On/Off            backlight on or off
     *   LyrCol            thumb layer colours on or off
     *   FxN               jump straight to backlight effect number N
     *   L1..L6            light one of the six small status LEDs while held
     *   BOOT              restart into flashing mode (on the mouse-layer key)
     *   Jingl             play Jingle Bells; press again to stop  (z key)
     *   Click             clicky mode on/off: a tick on every key press  (f key)
     *   Audio             all sound on/off, including clicky and the
     *                     start-up song; remembered after unplugging  (o key)
     *
     * Note: changes made here are saved on the keyboard, but at power-up the
     * backlight is always switched on (see keyboard_post_init_user).
     */
    [L_RGB] = LAYOUT_moonlander(
        CMB_RGB, RGB_HUI, RGB_SAI, RGB_VAI, RGB_SPI, RGB_MOD, RGB_TOG,      _______, _______, _______, _______, _______, _______, _______,
        _______, RGB_HUD, RGB_SAD, RGB_VAD, RGB_SPD, RGB_RMOD, RGB_LYR,     _______, JINGLE,  CLICKY,  AU_TOGG, _______, _______, _______,
        RGB__0,  RGB__1,  RGB__2,  RGB__25, RGB__28, RGB__36, RGB__27,      _______, LED_1,   LED_2,   LED_3,   LED_4,   LED_5,   LED_6,
        RGB__7,  RGB__13, RGB__15, RGB__16, RGB__17, _______,               _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, QK_BOOT, _______,               _______, _______, _______, _______, _______, _______,
                                            _______, _______, _______,      _______, _______, _______
    ),

    /*
     * LAYER 9: tmux  (hold the right lower-row key next to DWM)
     *
     * Every key types Ctrl-b and then one of tmux's default keys (section
     * 3, "TMUX KEYS"), so tmux needs no setup for them. Each key sits in
     * the same spot as the dwm key that does the nearest thing.
     *
     * top      --   Lay  Tile Main Even Ren  --       --   Brk  Spl| Ses> Ses< Spl- Det
     * home     Copy Kill Cmd  Pne> New  Last --       --   Zoom Left Down Up   Rght Pnes
     * bottom   Keys Tree Win9 Win8 Win7 Win6               Win5 Win4 Win3 Win2 Win1 Pste
     * lower    __   __   __   __   __   __          __   __   (held) --   --   --
     * thumbs                  __   __   --          __   __   __
     *   (__ = same as layer 0, so Ctrl and Alt still work; -- = nothing)
     *
     *   Win1..9  window 1..9 (Ctrl-b 1..9)      Tree  pick a window from a list (w)
     *   Left Down Up Rght  go to the pane in that direction (arrows; on the
     *            red layer's arrow keys). Hold Ctrl: resize by 1; hold
     *            Alt: resize by 5
     *   Zoom     pane fills the window, and back (z)
     *   Pnes     show pane numbers (q)          Pne>  next pane (o)
     *   New      new window (c)                 Last  previous window (l)
     *   Kill     close the pane, asks y/n (x)   Det   detach (d)
     *   Cmd      command prompt (:)             Copy  copy mode ([)
     *   Pste     paste the last copied text (]) Keys  list all keys (?)
     *   Spl| Spl-  split side by side (%) / one above the other (")
     *   Brk      move the pane to its own window (!)
     *   Ses> Ses<  next / previous session ( ) and ( )
     *   Lay      next layout (Space)            Ren   rename the window (,)
     *   Tile Main Even  tiled / main-vertical / even-horizontal (Alt+5 4 1)
     */
    [L_TMUX] = LAYOUT_moonlander(
        _______, _______, _______, _______, _______, _______, _______,      _______, _______, _______, _______, _______, _______, _______,
        KC_NO,   T_LAYOUT, T_TILED, T_MAINV, T_EVEN, T_RENAME, KC_NO,       KC_NO,   T_BREAK, T_SPLITH, T_SESN, T_SESP,  T_SPLITV, T_DETACH,
        T_COPY,  T_KILL,  T_CMD,   T_NEXTP, T_NEW,   T_LAST,  KC_NO,        KC_NO,   T_ZOOM,  T_LEFT,  T_DOWN,  T_UP,    T_RIGHT, T_PANES,
        T_KEYS,  T_TREE,  T_WIN9,  T_WIN8,  T_WIN7,  T_WIN6,                T_WIN5,  T_WIN4,  T_WIN3,  T_WIN2,  T_WIN1,  T_PASTE,
        _______, _______, _______, _______, _______, _______,               _______, _______, _______, _______, _______, _______,
                                            _______, _______, KC_NO,        _______, _______, _______
    ),
};

/* ========================================================================== *
 * 6. LED COLOURS
 *
 * The four LEDs of each thumb cluster show the colour of the active layer
 * (the highest layer that is on). The rest of the board shows the normal
 * backlight effect. This uses optozorax's `color` library.
 * ========================================================================== */

// Colour of each layer, as HSV: { hue, saturation, brightness }, each 0..255.
// Layers 2 and 3 are not listed, so they are { 0, 0, 0 } (LEDs off).
const uint8_t PROGMEM layermap[][3] = {
    [L_EN]    = {   0,   0, 255 },  // white
    [L_EN_S]  = {   0,   0, 192 },  // dim white
    [L_RED]   = {   0, 255, 210 },  // red
    [L_STENO] = {  30, 218, 255 },  // goldenrod (deep warm gold)
    [L_DWM]   = { 213, 255, 255 },  // magenta
    [L_MOUSE] = {  76, 255, 255 },  // green
    [L_RGB]   = {  43, 255, 255 },  // yellow
    [L_TMUX]  = { 128, 255, 255 },  // cyan
};
const uint8_t layermap_size = sizeof(layermap) / (sizeof(uint8_t) * 3);

// A "picture" gives every LED a colour:
//   COLOR_LAYER = use the active layer's colour from layermap above
//   COLOR_TRANS = (0, the default) leave the LED to the normal backlight
// The Moonlander has 72 LEDs, 36 per half. The last four of each half are
// the thumb cluster (the big angled key plus the three thumb keys):
// left 32..35, right 68..71.
// We have one picture, number 1 (it is [0] here). config.h makes it the
// default with COLOR_PICTURE_DEFAULT 1.
const uint8_t PROGMEM ledmap[][DRIVER_LED_TOTAL] = {
    [0] = {
        [32] = COLOR_LAYER, [33] = COLOR_LAYER, [34] = COLOR_LAYER, [35] = COLOR_LAYER,
        [68] = COLOR_LAYER, [69] = COLOR_LAYER, [70] = COLOR_LAYER, [71] = COLOR_LAYER,
    },
};
const uint8_t ledmap_size = sizeof(ledmap) / (sizeof(uint8_t) * DRIVER_LED_TOTAL);

// Extra colours for pictures. We only use COLOR_LAYER, but the library
// needs this list to exist.
const uint8_t PROGMEM colormap[][3] = {
    { 0, 0, 0 },
};
const uint8_t colormap_size = sizeof(colormap) / (sizeof(uint8_t) * 3);

/* ========================================================================== *
 * 7. MOUSE MOVEMENT (ACC_L / ACC_D / ACC_U / ACC_R)
 *
 * A tap moves the pointer exactly 1 pixel. While held: nothing happens for
 * ACCEL_DELAY ms, then the pointer moves every ACCEL_PERIOD ms. It starts
 * at 1 pixel per step and gains 1 pixel per step every ACCEL_RAMP ms, up
 * to ACCEL_MAX pixels per step.
 * Full speed is reached after ACCEL_DELAY + (ACCEL_MAX - 1) * ACCEL_RAMP ms,
 * about 1.4 seconds with the numbers below.
 *
 * To tune: bigger ACCEL_RAMP = slower build-up, bigger ACCEL_MAX = higher
 * top speed.
 * ========================================================================== */

#define ACCEL_DELAY   20    // ms of holding before it starts moving
#define ACCEL_PERIOD  5     // ms between steps
#define ACCEL_RAMP    100   // ms of holding per extra pixel per step
#define ACCEL_MAX     15    // top speed, in pixels per step

// Direction number = keycode - ACC_L:  0 left, 1 down, 2 up, 3 right
static const int8_t accel_dx[4] = { -1,  0,  0,  1 };
static const int8_t accel_dy[4] = {  0,  1, -1,  0 };

static uint8_t  accel_held = 0;     // bit n is set while direction n is held
static uint32_t accel_start[4];     // when each direction was pressed
static uint32_t accel_last = 0;     // when the last step was sent

// Send one pointer movement to the computer.
static void accel_send(int16_t x, int16_t y) {
    // A mouse report can only carry -127..127 per step.
    if (x >  127) x =  127;
    if (x < -127) x = -127;
    if (y >  127) y =  127;
    if (y < -127) y = -127;
    // Start from the current mouse state, so a held click (when you drag)
    // stays held while the pointer moves.
    report_mouse_t report = mousekey_get_report();
    report.x = x;
    report.y = y;
    report.h = 0;
    report.v = 0;
    host_mouse_send(&report);
}

// Handles presses and releases of ACC_L..ACC_R. Returns false for those
// keys (meaning "handled, QMK doesn't need to do anything else") and true
// for every other key.
static bool accel_process(uint16_t keycode, keyrecord_t *record) {
    if (keycode < ACC_L || keycode > ACC_R) return true;
    uint8_t dir = keycode - ACC_L;
    if (record->event.pressed) {
        accel_held |= (1 << dir);
        accel_start[dir] = timer_read32();
        accel_last = accel_start[dir];
        accel_send(accel_dx[dir], accel_dy[dir]);   // the 1-pixel tap
    } else {
        accel_held &= ~(1 << dir);
    }
    return false;
}

// Called all the time (from user_timer). Sends the next step for every
// direction that is held.
static void accel_tick(void) {
    if (!accel_held) return;
    uint32_t now = timer_read32();
    if (now - accel_last < ACCEL_PERIOD) return;
    accel_last = now;

    int16_t x = 0, y = 0;
    for (uint8_t dir = 0; dir < 4; dir++) {
        if (!(accel_held & (1 << dir))) continue;
        uint32_t held = now - accel_start[dir];
        if (held < ACCEL_DELAY) continue;
        uint32_t step = 1 + (held - ACCEL_DELAY) / ACCEL_RAMP;
        if (step > ACCEL_MAX) step = ACCEL_MAX;
        x += accel_dx[dir] * (int16_t)step;
        y += accel_dy[dir] * (int16_t)step;
    }
    if (x || y) accel_send(x, y);
}

/* ========================================================================== *
 * 8. SOUND
 *
 * The Moonlander has a small speaker. Songs are lists of notes:
 *   Q__NOTE(_E5)  = a quarter note E in octave 5,  H__NOTE = half,
 *   W__NOTE = whole, E__NOTE = eighth, QD_NOTE = dotted quarter.
 * The speed comes from TEMPO_DEFAULT in config.h (beats per minute).
 * A song must be stored outside any function, because it keeps playing
 * after the function that started it has finished.
 * ========================================================================== */

// Jingle Bells (the chorus).
static float jingle_bells[][2] = SONG(
    // Jingle bells, jingle bells, jingle all the way
    Q__NOTE(_E5), Q__NOTE(_E5), H__NOTE(_E5),
    Q__NOTE(_E5), Q__NOTE(_E5), H__NOTE(_E5),
    Q__NOTE(_E5), Q__NOTE(_G5), QD_NOTE(_C5), E__NOTE(_D5),
    W__NOTE(_E5),
    // Oh what fun it is to ride in a one-horse open sleigh, hey!
    Q__NOTE(_F5), Q__NOTE(_F5), QD_NOTE(_F5), E__NOTE(_F5),
    Q__NOTE(_F5), Q__NOTE(_E5), Q__NOTE(_E5), E__NOTE(_E5), E__NOTE(_E5),
    Q__NOTE(_E5), Q__NOTE(_D5), Q__NOTE(_D5), Q__NOTE(_E5),
    H__NOTE(_D5), H__NOTE(_G5),
    // Jingle bells, jingle bells, jingle all the way
    Q__NOTE(_E5), Q__NOTE(_E5), H__NOTE(_E5),
    Q__NOTE(_E5), Q__NOTE(_E5), H__NOTE(_E5),
    Q__NOTE(_E5), Q__NOTE(_G5), QD_NOTE(_C5), E__NOTE(_D5),
    W__NOTE(_E5),
    // Oh what fun it is to ride in a one-horse open sleigh
    Q__NOTE(_F5), Q__NOTE(_F5), Q__NOTE(_F5), Q__NOTE(_F5),
    Q__NOTE(_F5), Q__NOTE(_E5), Q__NOTE(_E5), E__NOTE(_E5), E__NOTE(_E5),
    Q__NOTE(_G5), Q__NOTE(_G5), Q__NOTE(_F5), Q__NOTE(_D5),
    W__NOTE(_C5)
);

/* --- Keeping the sound from getting stuck ----------------------------------
 * QMK's audio code has a timing bug: when a note ends, the "note is over"
 * check and the "song is over" check can land 1 ms apart. If the note check
 * wins, the speaker is switched off but QMK still thinks a song is playing,
 * and nothing switches the speaker on again: the keyboard stays silent until
 * it is unplugged. Short sounds (like clicks) hit this very often.
 * audio_unstick() spots that state (a song "playing" with no notes left)
 * and clears it. It runs all the time from user_timer(). */
extern bool playing_melody;   // QMK's own "a song is playing" flag (quantum/audio/audio.c)

static void audio_unstick(void) {
    if (playing_melody && audio_get_number_of_active_tones() == 0) {
        playing_melody = false;
    }
}

// JINGLE key: start the song, or stop it if something is already playing.
static void jingle_press(void) {
    if (audio_is_playing_melody()) {
        audio_stop_all();
    } else {
        PLAY_SONG(jingle_bells);
    }
}

/* --- Clicky mode ------------------------------------------------------------
 * A short tick on every key you physically press. This is our own version
 * of QMK's clicky feature: QMK's version would also tick for the keys the
 * chord code presses for you, so chords would tick twice.
 * Clicky starts switched off every time the keyboard is plugged in.
 * Tune the sound with these: */
#define CLICK_PITCH     880.0f  // Hz. Higher = brighter tick (try 440..1500)
#define CLICK_LENGTH_MS 8       // ms. Longer = louder, more "beep" than "tick"
#define CLICK_WOBBLE    0.05f   // random pitch change per click (0.05 = up to 5%),
                                // so fast typing sounds less robotic

static bool clicky_enabled = false;

static void clicky_tick(void) {
    if (!clicky_enabled || !audio_is_on()) return;
    if (audio_is_playing_melody()) return;    // don't cut into a song
    float pitch = CLICK_PITCH * (1.0f + CLICK_WOBBLE * ((float)rand() / (float)RAND_MAX));
    // A single timed note, not a one-note "song", so a click never touches
    // the song machinery at all.
    audio_play_note(pitch, CLICK_LENGTH_MS);
}

// QMK calls this for every REAL key press and release (before anything
// else, and not for keys the chord code presses on our behalf), which is
// exactly when clicky mode should tick. The Jingle Bells key doesn't tick,
// so its tick can't be mistaken for "a song is playing, stop it".
bool pre_process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed && keycode != JINGLE) clicky_tick();
    return true;
}

/* ========================================================================== *
 * 9. QMK HOOKS
 *
 * QMK calls these functions by name. They pass every key press and every
 * timer tick on to the libraries, and type our own keycodes.
 * ========================================================================== */

// Type a mark (like . or ,) followed by a space. If capitalize_next is true,
// the next key is shifted, so a new sentence starts with a capital.
// lang_shift_tap_key() types the mark correctly whatever Shift state or
// layer is active.
static void type_mark_space(uint16_t mark, bool capitalize_next) {
    lang_shift_tap_key(mark);
    tap_code(KC_SPC);
    if (capitalize_next) {
        shift_once_use_to_next_key(lang_get_shift_layer_number());
    }
}

// Type an opening and a closing bracket, one after the other.
static void type_pair(uint16_t open, uint16_t close) {
    lang_shift_tap_key(open);
    lang_shift_tap_key(close);
}

// Type "..." and, if add_space is true, a space after it.
static void type_ellipsis(bool add_space) {
    lang_shift_tap_key(EN_DOT);
    lang_shift_tap_key(EN_DOT);
    lang_shift_tap_key(EN_DOT);
    if (add_space) tap_code(KC_SPC);
}

// Draw the layer colours on top of the normal backlight.
bool rgb_matrix_indicators_user(void) {
    color_rgb_matrix_indicators();
    return true;
}

// Runs once at power-up. Switches the backlight on, and if the saved
// brightness is so low that the board would look dark, raises it to 160.
// Nothing is saved, so your saved settings stay as they are.
void keyboard_post_init_user(void) {
    rgb_matrix_enable_noeeprom();
    if (rgb_matrix_get_val() < 100) {
        rgb_matrix_sethsv_noeeprom(rgb_matrix_get_hue(), rgb_matrix_get_sat(), 160);
    }
}

// Every key press and release goes through here.
// The order matters: combos first (so a chord is recognised before the
// other code sees its keys), then lang_shift, then the rest. Each step
// returns false when it has handled the key completely.
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (!combo_process_record(keycode, record))      return false;  // chords
    if (!lang_shift_process_record(keycode, record)) return false;  // EN_ keys, Shift, CTRL_0...
    if (!color_process_record(keycode, record))      return false;  // backlight effect keys
    if (!process_moonlander_leds(keycode, record))   return false;  // LED_1..LED_6
    if (!accel_process(keycode, record))             return false;  // mouse movement

    // Our own keycodes. Most type when pressed and do nothing on release;
    // DWM_HOLD is the exception, it is held until released.
    bool down = record->event.pressed;
    switch (keycode) {
        case PAIR_PAREN:   if (down) type_pair(EN_LPRN, EN_RPRN);     return false;  // ()
        case PAIR_BRACKET: if (down) type_pair(EN_LBRC, EN_RBRC);     return false;  // []
        case PAIR_BRACE:   if (down) type_pair(EN_LCBR, EN_RCBR);     return false;  // {}
        case PAIR_ANGLE:   if (down) type_pair(EN_LT,   EN_GT);       return false;  // <>

        case DOT_SP:       if (down) type_mark_space(EN_DOT,  true);  return false;  // ". " + capital
        case EXC_SP:       if (down) type_mark_space(EN_EXCL, true);  return false;  // "! " + capital
        case QUE_SP:       if (down) type_mark_space(EN_QUES, true);  return false;  // "? " + capital
        case CMM_SP:       if (down) type_mark_space(EN_COMM, false); return false;  // ", "
        case SEMI_SP:      if (down) type_mark_space(EN_SCLN, false); return false;  // "; "
        case COLN_SP:      if (down) type_mark_space(EN_COLN, false); return false;  // ": "

        case ELLIPSIS:     if (down) type_ellipsis(true);             return false;  // "... "
        case ELLIPSIS_N:   if (down) type_ellipsis(false);            return false;  // "..."

        // dwm layer key: hold Super and the dwm layer, release both together
        case DWM_HOLD:
            if (down) {
                layer_on(L_DWM);
                register_code(DWM_HOLD_MOD);
            } else {
                unregister_code(DWM_HOLD_MOD);
                layer_off(L_DWM);
            }
            return false;

        // Plain Print from the dwm layer: let go of the held Super, tap
        // Print, then hold Super again (the dwm key is still down).
        case D_SHOT:
            if (down) {
                unregister_code(DWM_HOLD_MOD);
                tap_code(KC_PSCR);
                register_code(DWM_HOLD_MOD);
            }
            return false;

        case T_FIRST ... T_LAST_KEY - 1:                                         // tmux layer
            if (down) tmux_send(keycode);
            return false;

        case JINGLE:       if (down) jingle_press();                  return false;  // Jingle Bells
        case CLICKY:       if (down) clicky_enabled = !clicky_enabled; return false; // clicky on/off
    }
    return true;
}

// Timer work for the libraries (chord timeouts, Shift release), the mouse
// acceleration, and the stuck-sound check.
void user_timer(void) {
    combo_user_timer();
    lang_shift_user_timer();
    accel_tick();
    audio_unstick();
}

// QMK calls this on every scan of the keys, many times per millisecond.
void matrix_scan_user(void) {
    user_timer();
}

// The combo library calls these if a limit from config.h is too small
// (too many chords at once, or a chord with too many keys). They do
// nothing here; if chords ever act strangely, raise COMBO_STACK_MAX_SIZE
// or COMBO_MAX_SIZE in config.h.
void combo_max_count_error(void) {}
void combo_max_size_error(void) {}
