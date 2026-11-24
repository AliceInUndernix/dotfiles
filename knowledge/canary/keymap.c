/*
 * Moonlander (rev B) keymap: Canary letters on top of optozorax's keymap code.
 *
 * WHAT'S IN THIS FILE (top to bottom)
 *   1. Includes and compatibility aliases
 *   2. Names: layers, combo keys, custom keycodes
 *   3. Combos (thumb keys, layer keys, Ctrl, Enter, Escape, one-time Shift, bracket pairs)
 *   4. The layers themselves
 *   5. LED colors (thumb clusters show the active layer)
 *   6. QMK hooks that glue the libraries together
 *
 * LAYERS
 *   0  L_EN    Canary letters                                    (white)
 *   1  L_EN_S  shifted version of layer 0                        (gray)
 *   2  L_RU    reserved by lang_shift, unused
 *   3  L_RU_S  reserved by lang_shift, unused
 *   4  L_RED   hold the middle left thumb key: brackets, symbols,
 *              arrows, Delete                                    (red)
 *   5  L_BLUE  hold the lower-row key left of Ctrl: digits       (blue)
 *   6  L_STENO steno (GeminiPR). Shift (left thumb 3) + the first right thumb
 *              key switch it on; the two innermost thumb keys switch it off
 *                                                                 (orange)
 *   7  L_DWM   hold the right lower-row key next to Alt: dwm window
 *              manager shortcuts (Super + key)                   (magenta)
 *   8  L_MOUSE hold Space + Red + Ctrl (left thumb keys) together:
 *              keyboard mouse, all on the right hand                (green)
 *
 * Layers 2 and 3 exist only because lang_shift treats 0..3 as its own
 * (English, English shifted, Russian, Russian shifted). Our layers start at 4.
 *
 * BUILD REQUIREMENTS (see rules.mk): steno needs STENO_ENABLE = yes and
 * STENO_PROTOCOL = geminipr. Steno adds a virtual serial port, so Console,
 * WebUSB, NKRO and extra keys are switched off to free USB endpoints. The mouse
 * layer needs MOUSEKEY_ENABLE = yes; with that on, the USB endpoints are
 * used up exactly (keyboard 1 + mouse 1 + Oryx raw HID 2 + steno serial 3 = 7).
 *
 * KEYCODE RULES (lang_shift)
 *   - Letters and punctuation use EN_ names (EN_A, EN_QUOT, ...), not KC_.
 *   - Shift is SFT_N (hold) or SFT_N_O (one-time). Never plain KC_LSFT.
 *   - Ctrl and Alt are CTRL_0 and ALT_0.
 *   - Keys that aren't characters (arrows, Delete, F-keys) are plain KC_ names.
 *   - _______ is transparent: the key does what it does on the layer below.
 *   - KC_NO does nothing.
 *   See keycodes.md for the full list of names.
 */

#include QMK_KEYBOARD_H

// Fail early, with a readable message, instead of "STN_S1 undeclared".
#ifndef STENO_ENABLE
#error "The steno layer needs STENO_ENABLE = yes and STENO_PROTOCOL = geminipr in rules.mk"
#endif

/* ======================================================================== *
 * 1. INCLUDES AND COMPATIBILITY
 * ======================================================================== */

// lang_shift still uses the old long keycode names; this QMK only has the
// short ones. Same aliases as in optozorax's keymap.
#define KC_LSHIFT KC_LSFT
#define KC_LCTRL  KC_LCTL

// The color library sizes its tables by this.
#ifndef DRIVER_LED_TOTAL
#define DRIVER_LED_TOTAL RGB_MATRIX_LED_COUNT
#endif

// The ORDER of these includes matters: each library hands out custom keycodes
// starting at CUSTOM_SAFE_RANGE and then moves the range forward.
#include "arbitrary_keycode/include.h"
#define CUSTOM_SAFE_RANGE ZSA_SAFE_RANGE
#include "lang_shift/include.h"
#include "combo/include.h"

#include "color/include.h"

/* ======================================================================== *
 * 2. NAMES: LAYERS, COMBO KEYS, CUSTOM KEYCODES
 * ======================================================================== */

enum Layers {
    L_EN = 0,   // 0: Canary
    L_EN_S,     // 1: Canary, shifted (lang_shift switches to it by itself)
    L_RU,       // 2: reserved by lang_shift, unused
    L_RU_S,     // 3: reserved by lang_shift, unused
    L_RED,      // 4: brackets, symbols, arrows, Delete
    L_BLUE,     // 5: digits
    L_STENO,    // 6: steno (GeminiPR)
    L_DWM,      // 7: dwm window manager shortcuts
    L_MOUSE,    // 8: keyboard mouse
};

// Combo keys. These go into the layout in place of the real key; the
// combos[] table below says what each one (alone or together) does.
// The library allows COMBO_KEYS_COUNT of them (20 in config.h; 19 are used).
enum Combos {
    // Thumb cluster and neighbors
    CMB_SPC = CMB_000,  // left thumb 1: Space
    CMB_RED = CMB_001,  // left thumb 2: red layer
    CMB_SFT = CMB_002,  // left thumb 3: Shift
    CMB_BLU = CMB_003,  // lower row, left of Ctrl: blue layer
    CMB_TAB = CMB_004,  // right thumb 2: Tab
    CMB_BSP = CMB_005,  // right thumb 3: Backspace
    CMB_CTL = CMB_006,  // upper left thumb: Ctrl

    // Brackets on the red layer (each pair sits on neighboring keys)
    CMB_LCB = CMB_007,  // {
    CMB_RCB = CMB_008,  // }
    CMB_LAN = CMB_009,  // <
    CMB_RAN = CMB_010,  // >
    CMB_LPR = CMB_011,  // (
    CMB_RPR = CMB_012,  // )
    CMB_LBR = CMB_013,  // [
    CMB_RBR = CMB_014,  // ]

    // Steno layer switch. CMB_STN sits on layer 0; CMB_SXL / CMB_SXR sit on the
    // steno layer, on the same two thumb positions (left thumb 3, right thumb 1).
    CMB_STN = CMB_015,  // right thumb 1 (layer 0): steno ON, together with Shift
    CMB_SXL = CMB_016,  // left thumb 3 (steno layer): steno OFF, with CMB_SXR
    CMB_SXR = CMB_017,  // right thumb 1 (steno layer): steno OFF, with CMB_SXL

    // dwm layer key
    CMB_DWM = CMB_018,  // lower row, right of Alt: dwm layer
};

// Our own keycodes: each one types a whole bracket pair. They are handled in
// process_record_user() at the bottom of this file. CUSTOM_SAFE_RANGE has
// already been moved past the library keycodes by the includes above.
enum custom_keycodes {
    PAIR_PAREN = CUSTOM_SAFE_RANGE,   // ()
    PAIR_BRACKET,                     // []
    PAIR_BRACE,                       // {}
    PAIR_ANGLE,                       // <>

    // Mouse layer: pointer moves that speed up while held (see section 6).
    ACC_L,                            // left
    ACC_D,                            // down
    ACC_U,                            // up
    ACC_R,                            // right
};

/* ======================================================================== *
 * DWM SHORTCUTS
 *
 * dwm's default shortcuts are MODKEY + key. This sends them with one key.
 * Change DWM_MOD to LALT(kc) if your dwm uses Alt (Mod1) as its MODKEY
 * (the dwm default); LGUI is Super (Mod4).
 * The right layer key plus Shift (left thumb 3 in the layer) gives the
 * "Shift" versions, e.g. Super+Shift+1 moves the window to tag 1.
 * ======================================================================== */

#define DWM_MOD(kc)      LGUI(kc)
#define DWM_MOD_SFT(kc)  LGUI(LSFT(kc))

// Tags (workspaces). Hold Shift as well to move the focused window there.
#define D_TAG1  DWM_MOD(KC_1)
#define D_TAG2  DWM_MOD(KC_2)
#define D_TAG3  DWM_MOD(KC_3)
#define D_TAG4  DWM_MOD(KC_4)
#define D_TAG5  DWM_MOD(KC_5)
#define D_TAG6  DWM_MOD(KC_6)
#define D_TAG7  DWM_MOD(KC_7)
#define D_TAG8  DWM_MOD(KC_8)
#define D_TAG9  DWM_MOD(KC_9)
#define D_ALL   DWM_MOD(KC_0)            // view all tags
#define D_PREV  DWM_MOD(KC_GRV)          // back to the previous tag (Super+`)

// Launch and close (the same shortcuts your Oryx layout used)
#define D_TERM  DWM_MOD(KC_ENT)          // Super+Enter
#define D_SENT  DWM_MOD_SFT(KC_ENT)      // Super+Shift+Enter
#define D_RUN   DWM_MOD(KC_SCLN)         // dmenu launcher (Super+;)
#define D_KILL  DWM_MOD_SFT(KC_C)        // close the focused window

// Focus and master area. Your dwm is bound to Super + K / N / E / I for
// these four (not the default h j k l), as in your Oryx layout.
#define D_MSTS  DWM_MOD(KC_K)            // shrink master area
#define D_FOCN  DWM_MOD(KC_N)            // focus next window
#define D_FOCP  DWM_MOD(KC_E)            // focus previous window
#define D_MSTG  DWM_MOD(KC_I)            // grow master area

// Layouts
#define D_TILE  DWM_MOD(KC_T)            // tiled
#define D_FLOT  DWM_MOD(KC_F)            // floating layout
#define D_MONO  DWM_MOD(KC_M)            // monocle (one window full screen)
#define D_LAYT  DWM_MOD(KC_SPC)          // toggle between the last two layouts
#define D_TFLT  DWM_MOD_SFT(KC_SPC)      // float / unfloat the focused window
#define D_BAR   DWM_MOD(KC_B)            // show / hide the bar

// Monitors (hold Shift to send the window to the other monitor)
#define D_MONP  DWM_MOD(KC_COMM)         // focus previous monitor
#define D_MONN  DWM_MOD(KC_DOT)          // focus next monitor

/* ======================================================================== *
 * 3. COMBOS
 *
 * CHORD(output, keys...)           sends `output` after COMBO_WAIT_TIME,
 *                                  once it is clear which keys are pressed.
 * IMMEDIATE_CHORD(output, undo, keys...)
 *                                  sends `output` right away; if another key
 *                                  joins the chord, `undo` is released again.
 *                                  Use it for layers and modifiers, where a
 *                                  delay would feel laggy.
 *
 * Every combo key needs its own one-key entry, otherwise it would do nothing
 * when pressed alone.
 * ======================================================================== */

const ComboWithKeycode combos[] PROGMEM = {
    // --- single keys: thumbs, layers, Ctrl ---
    CHORD(KC_SPC,  CMB_SPC),
    IMMEDIATE_CHORD(MO(L_RED),  MO(L_RED),  /* <-! */ CMB_RED),   // layer: hold
    IMMEDIATE_CHORD(SFT_N,      SFT_N,      /* <-! */ CMB_SFT),   // Shift: hold
    IMMEDIATE_CHORD(MO(L_BLUE), MO(L_BLUE), /* <-! */ CMB_BLU),   // layer: hold
    IMMEDIATE_CHORD(CTRL_0,     CTRL_0,     /* <-! */ CMB_CTL),   // Ctrl: hold
    IMMEDIATE_CHORD(MO(L_DWM),  MO(L_DWM),  /* <-! */ CMB_DWM),   // layer: hold
    CHORD(KC_TAB,  CMB_TAB),
    CHORD(KC_BSPC, CMB_BSP),

    // --- two-key combos (always neighboring keys) ---
    CHORD(KC_ENT,  CMB_SPC, CMB_RED),   // Space + red layer key  -> Enter
    CHORD(KC_ESC,  CMB_TAB, CMB_BSP),   // Tab + Backspace        -> Escape
    CHORD(SFT_N_O, CMB_SPC, CMB_CTL),   // Space + Ctrl          -> one-time Shift
    // Ctrl + red layer key -> Ctrl+Shift, held for as long as both are down.
    // (The red layer is switched off while this combo is active.)
    IMMEDIATE_CHORD(CTSH_0, CTSH_0, /* <-! */ CMB_CTL, CMB_RED),
    // Space + red layer key + Ctrl, all three together -> mouse layer, held for
    // as long as all three are down. (Pressing just two of them still gives
    // Enter or Ctrl+Shift as above.)
    IMMEDIATE_CHORD(MO(L_MOUSE), MO(L_MOUSE), /* <-! */ CMB_SPC, CMB_RED, CMB_CTL),

    // --- brackets, single keys (red layer) ---
    // Plain CHORDs, not immediate ones: a typed character can't be taken
    // back, so the library waits to see whether the neighbor joins in.
    CHORD(EN_LCBR, CMB_LCB),            // {
    CHORD(EN_RCBR, CMB_RCB),            // }
    CHORD(EN_LT,   CMB_LAN),            // <
    CHORD(EN_GT,   CMB_RAN),            // >
    CHORD(EN_LPRN, CMB_LPR),            // (
    CHORD(EN_RPRN, CMB_RPR),            // )
    CHORD(EN_LBRC, CMB_LBR),            // [
    CHORD(EN_RBRC, CMB_RBR),            // ]

    // --- brackets, both keys of a pair at once -> the whole pair ---
    CHORD(PAIR_BRACE,   CMB_LCB, CMB_RCB),   // { + }  -> {}
    CHORD(PAIR_ANGLE,   CMB_LAN, CMB_RAN),   // < + >  -> <>
    CHORD(PAIR_PAREN,   CMB_LPR, CMB_RPR),   // ( + )  -> ()
    CHORD(PAIR_BRACKET, CMB_LBR, CMB_RBR),   // [ + ]  -> []

    // --- steno layer on / off (TG = toggle the layer) ---
    // Alone, these three keys do nothing (KC_NO); they only exist to be combined.
    CHORD(KC_NO, CMB_STN),
    CHORD(KC_NO, CMB_SXL),
    CHORD(KC_NO, CMB_SXR),
    CHORD(TG(L_STENO), CMB_SFT, CMB_STN),   // Shift + right thumb 1: steno ON
    CHORD(TG(L_STENO), CMB_SXL, CMB_SXR),   // same two keys on the steno layer: OFF
};
const uint8_t combos_size = sizeof(combos)/sizeof(ComboWithKeycode);

/* ======================================================================== *
 * 4. LAYERS
 *
 * Each LAYOUT_moonlander(...) lists 72 keys in this order:
 *
 *   number row   7 keys  |  7 keys
 *   top row      7 keys  |  7 keys   (outer, 5 letters, inner)
 *   home row     7 keys  |  7 keys   (outer, 5 letters, inner)
 *   bottom row   6 keys  |  6 keys   (outer, 5 letters)
 *   lower row    6 keys  |  6 keys   (5 keys + the upper thumb key)
 *   thumbs       3 keys  |  3 keys
 * ======================================================================== */

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {

    /*
     * Layer 0: Canary.
     *
     *   ;  w l y p b  .   .  z f o u '  :
     *   ^  c r s t g  .   .  m n e i a  ^        ^ = one-time shift (SFT_N_O)
     *      q j v d k         x h / , .
     *               Blue Ctrl  Alt DWM
     *   thumbs:  Space  Red  Shift  |  Steno  Tab  Backspace
     *
     * The first right thumb key (CMB_STN) does nothing on its own. Press it
     * together with Shift (the third left thumb key) to switch steno on.
     *
     * The dwm layer key sits right of Alt (first free key of the right lower row).
     * The mouse layer has no key of its own: press Space + Red + Ctrl (the left
     * thumb keys plus the upper left thumb key) together and hold them.
     *
     * Ctrl and Alt are the upper thumb keys (last key of the left lower row,
     * first key of the right lower row). The blue layer key sits just left of
     * Ctrl, in the same lower row. Ctrl is a combo key (CMB_CTL): Ctrl alone
     * is CTRL_0, Ctrl + the red layer key is Ctrl+Shift (CTSH_0).
     */
    [L_EN] = LAYOUT_moonlander(
        KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,        KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,
        EN_SCLN, EN_W,    EN_L,    EN_Y,    EN_P,    EN_B,    KC_NO,        KC_NO,   EN_Z,    EN_F,    EN_O,    EN_U,    EN_QUOT, EN_COLN,
        SFT_N_O, EN_C,    EN_R,    EN_S,    EN_T,    EN_G,    KC_NO,        KC_NO,   EN_M,    EN_N,    EN_E,    EN_I,    EN_A,    SFT_N_O,
        KC_NO,   EN_Q,    EN_J,    EN_V,    EN_D,    EN_K,                  EN_X,    EN_H,    EN_SLSH, EN_COMM, EN_DOT,  KC_NO,
        KC_NO,   KC_NO,   KC_NO,   KC_NO,   CMB_BLU, CMB_CTL,               ALT_0,   CMB_DWM, KC_NO,   KC_NO,   KC_NO,   KC_NO,
                                            CMB_SPC, CMB_RED, CMB_SFT,      CMB_STN, CMB_TAB, CMB_BSP
    ),

    /*
     * Layer 1: shifted Canary. lang_shift switches to it while Shift is held
     * or after a one-time shift. Every letter is written out as a capital.
     * Only ' / , . change into other characters (" ? < >).
     */
    [L_EN_S] = LAYOUT_moonlander(
        _______, _______, _______, _______, _______, _______, _______,      _______, _______, _______, _______, _______, _______, _______,
        _______, EN_S_W,  EN_S_L,  EN_S_Y,  EN_S_P,  EN_S_B,  _______,      _______, EN_S_Z,  EN_S_F,  EN_S_O,  EN_S_U,  EN_DQUO, _______,
        _______, EN_S_C,  EN_S_R,  EN_S_S,  EN_S_T,  EN_S_G,  _______,      _______, EN_S_M,  EN_S_N,  EN_S_E,  EN_S_I,  EN_S_A,  _______,
        _______, EN_S_Q,  EN_S_J,  EN_S_V,  EN_S_D,  EN_S_K,                EN_S_X,  EN_S_H,  EN_QUES, EN_COMM, EN_DOT,   _______,
        _______, _______, _______, _______, _______, _______,               _______, _______, _______, _______, _______, _______,
                                            _______, _______, _______,      _______, _______, _______
    ),

    // Layers 2 and 3 (L_RU, L_RU_S) are lang_shift's Russian layers.
    // Not used, so they are left out and stay empty.

    /*
     * Layer 4 (red): hold the middle left thumb key.
     *
     * Every symbol is a single key here, so none of them needs Shift.
     * The keys are named after the letter they sit on in layer 0:
     *
     *            w  l  y  p  b           z  f  o  u  '
     *   top row  @  #  {  }  &           !  =  +  -  _
     *            c  r  s  t  g           m  n  e  i  a
     *   home row <  >  (  )  |           *  Lt Dn Up Rt    (arrow keys on n e i a)
     *            q  j  v  d  k           x  h  /  ,  .
     *   bottom   `  ~  [  ]  \           $  %  ?  ^  "
     *
     * Right thumb, third key: Delete.
     * Not repeated here, because layer 0 already has them: ; : ' / , .
     * Digits are on the blue layer.
     *
     * BRACKET PAIRS: press both keys of a pair at the same time and the whole
     * pair is typed:  { + }  gives {}   < + >  gives <>
     *                 ( + )  gives ()   [ + ]  gives []
     * The bracket keys are combo keys (CMB_...), see the combos table above.
     *
     * Everything else is transparent; fill in more as you like.
     */
    [L_RED] = LAYOUT_moonlander(
        _______, _______, _______, _______, _______, _______, _______,      _______, _______, _______, _______, _______, _______, _______,
        _______, EN_AT,   EN_HASH, CMB_LCB, CMB_RCB, EN_AMPR, _______,      _______, EN_EXCL, EN_EQL,  EN_PLUS, EN_MINS, EN_UNDS, _______,
        _______, CMB_LAN, CMB_RAN, CMB_LPR, CMB_RPR, EN_PIPE, _______,      _______, EN_ASTR, KC_LEFT, KC_DOWN, KC_UP,   KC_RGHT, _______,
        _______, EN_GRV,  EN_TILD, CMB_LBR, CMB_RBR, EN_BSLS,               EN_DLR,  EN_PERC, EN_QUES, EN_CIRC, EN_DQUO, _______,
        _______, _______, _______, _______, _______, _______,               _______, _______, _______, _______, _______, _______,
                                            _______, _______, _______,      _______, _______, KC_DEL
    ),

    /*
     * Layer 5 (blue): hold the lower-row key left of Ctrl.
     *   Home row: digits, 1 2 3 4 5 on c r s t g and 6 7 8 9 0 on m n e i a.
     * Everything else is transparent; fill in more as you like.
     */
    [L_BLUE] = LAYOUT_moonlander(
        _______, _______, _______, _______, _______, _______, _______,      _______, _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______,      _______, _______, _______, _______, _______, _______, _______,
        _______, EN_1,    EN_2,    EN_3,    EN_4,    EN_5,    _______,      _______, EN_6,    EN_7,    EN_8,    EN_9,    EN_0,    _______,
        _______, _______, _______, _______, _______, _______,               _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,               _______, _______, _______, _______, _______, _______,
                                            _______, _______, _______,      _______, _______, _______
    ),

    /*
     * Layer 6 (steno, GeminiPR): switched on and off with the two innermost
     * thumb keys pressed together (Shift + first right thumb key on layer 0).
     *
     * The steno keys sit on the home row and the bottom row, which are the
     * steno top and bottom rows. The right hand is shifted one key outward
     * (to the right), which frees the inner key of the home row for the
     * right-hand star.
     *
     *   (letters on layer 0)  c    r    s    t    g     m    n    e    i    a    ;
     *   home row              S1   T    P    H    *1    *3   F    P    L    T    D
     *   (letters on layer 0)  q    j    v    d    k     x    h    /    ,    .    -
     *   bottom row            S2   K    W    R    *2    --   R    B    G    S    Z
     *
     *   thumbs      A    O   (exit)     (exit)     E    U
     *
     * *1 and *2 (STN_ST1, STN_ST2) are the left stars. *3 (STN_ST3) is the
     * right star. The free key below it (STN_ST4) is left empty; add it if you
     * want a second right star.
     * Every key that isn't steno is KC_NO (not transparent) on purpose, so
     * nothing from layer 0 can leak through while you steno.
     * Not mapped (add them if you need them): the number bar (STN_N1..).
     */
    [L_STENO] = LAYOUT_moonlander(
        KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,        KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,
        KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,        KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,
        KC_NO,   STN_S1,  STN_TL,  STN_PL,  STN_HL,  STN_ST1, KC_NO,        KC_NO,   STN_ST3, STN_FR,  STN_PR,  STN_LR,  STN_TR,  STN_DR,
        KC_NO,   STN_S2,  STN_KL,  STN_WL,  STN_RL,  STN_ST2,               STN_ST4, STN_RR,  STN_BR,  STN_GR,  STN_SR,  STN_ZR,
        KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,                 KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,   KC_NO,
                                            STN_A,   STN_O,   CMB_SXL,      CMB_SXR, STN_E,   STN_U
    ),

    /*
     * Layer 7 (dwm): hold the key right of Alt (lower row). Every key sends
     * Super + key (see DWM SHORTCUTS above), so dwm gets a real shortcut with
     * one finger. Hold Shift (left thumb 3) too for the Shift versions.
     *
     *            w     l     y     p     b            z     f     o     u     '
     *   top row  --    --    mon<  mon>  --           layout tile  float mono  tglflt
     *            c     r     s     t     g            m     n     e     i     a
     *   home row tag1  tag2  tag3  tag4  tag5         --    shrnk next  prev  grow
     *            q     j     v     d     k            x     h     /     ,     .
     *   bottom   tag6  tag7  tag8  tag9  prevtag      bar   S+Ent S+Sht dmenu close
     *
     *   left hand:  tags 1-5 on c r s t g and 6-9 on q j v d (hold Shift as
     *               well to move the window there); k = previous tag;
     *               top row = focus previous / next monitor.
     *               View all tags (Super+0) is on the lower-row key left of
     *               Ctrl (where the blue layer key is on layer 0): the mirror
     *               image of the dwm layer key.
     *   right hand: home row on n e i a: shrink master, next window,
     *               previous window, grow master (m is empty).
     *               Top row: layouts. Bottom row: x = bar, h = Super+Enter,
     *               / = Super+Shift+Enter, , = dmenu launcher (Super+;),
     *               . = close window.
     *   thumbs:     the left Shift (plain Shift, so Super+Shift+key works);
     *               everything else on the thumbs is transparent (Ctrl works).
     */
    [L_DWM] = LAYOUT_moonlander(
        _______, _______, _______, _______, _______, _______, _______,      _______, _______, _______, _______, _______, _______, _______,
        KC_NO,   KC_NO,   KC_NO,   D_MONP,  D_MONN,  KC_NO,   KC_NO,        KC_NO,   D_LAYT,  D_TILE,  D_FLOT,  D_MONO,  D_TFLT,  KC_NO,
        KC_NO,   D_TAG1,  D_TAG2,  D_TAG3,  D_TAG4,  D_TAG5,  KC_NO,        KC_NO,   KC_NO,   D_MSTS,  D_FOCN,  D_FOCP,  D_MSTG,  KC_NO,
        KC_NO,   D_TAG6,  D_TAG7,  D_TAG8,  D_TAG9,  D_PREV,                D_BAR,   D_TERM,  D_SENT,  D_RUN,   D_KILL,  KC_NO,
        _______, _______, _______, _______, D_ALL,   _______,               _______, _______, _______, _______, _______, _______,
                                            _______, _______, KC_LSFT,      _______, _______, _______
    ),

    /*
     * Layer 8 (mouse): hold Space + Red + Ctrl together (left hand). The whole
     * mouse is on the RIGHT hand; the left hand holds the layer and has the
     * modifiers. Keys not listed here are transparent.
     *
     *   left hand, home row:    c = Ctrl    r = Alt    s = Super
     *
     *   right hand, home row:   n = left    e = down   i = up     a = right
     *   right hand, bottom row: h = wheel left   / = wheel down
     *                           , = wheel up     . = wheel right
     *
     *   The left hand modifiers are for clicking with a modifier held. Super
     *   is the dwm modifier.
     *
     *   right thumbs:  Backspace key = left click,
     *                  Tab key       = right click,
     *                  first key     = middle click.
     *
     * Pointer speed: a tap moves exactly 1 pixel. Hold the key and after a
     * short pause it starts moving, getting faster the longer you hold (see
     * ACCEL_* in section 6). Two keys at once move diagonally.
     */
    [L_MOUSE] = LAYOUT_moonlander(
        _______, _______, _______, _______, _______, _______, _______,      _______, _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______, _______,      _______, _______, _______, _______, _______, _______, _______,
        _______, CTRL_0,  ALT_0,   WIN_0,   _______, _______, _______,      _______, _______, ACC_L,   ACC_D,   ACC_U,   ACC_R,   _______,
        _______, _______, _______, _______, _______, _______,               _______, KC_WH_L, KC_WH_D, KC_WH_U, KC_WH_R, _______,
        _______, _______, _______, _______, _______, _______,               _______, _______, _______, _______, _______, _______,
                                            _______, _______, _______,      KC_BTN3, KC_BTN2, KC_BTN1
    ),
};

/* ======================================================================== *
 * 5. LED COLORS
 *
 * The thumb clusters show the color of the active layer (the highest layer
 * that is switched on). Colors come from his `color` library.
 * ======================================================================== */

// Color of each layer, HSV: { hue, saturation, value }, each 0..255.
// Layers 2 and 3 are unused and stay { 0, 0, 0 } (LEDs off).
const uint8_t PROGMEM layermap[][3] = {
    [L_EN]    = {   0,   0, 255 },  // white
    [L_EN_S]  = {   0,   0, 192 },  // gray: dimmer white
    [L_RED]   = {   0, 255, 210 },  // red
    [L_BLUE]  = { 164, 255, 255 },  // blue
    [L_STENO] = {  35, 255, 255 },  // orange
    [L_DWM]   = { 213, 255, 255 },  // magenta
    [L_MOUSE] = {  76, 255, 255 },  // green
};
const uint8_t layermap_size = sizeof(layermap)/(sizeof(uint8_t) * 3);

// A "picture" gives every LED a color index. COLOR_LAYER means "use the
// active layer's color from layermap"; COLOR_TRANS (0) leaves the LED alone,
// so the normal backlight shows through.
//
// The Moonlander has 72 LEDs, 36 per half. The last four of each half are the
// thumb cluster: left 32..35, right 68..71.
const uint8_t PROGMEM ledmap[][DRIVER_LED_TOTAL] = {
    [0] = {
        [32] = COLOR_LAYER, [33] = COLOR_LAYER, [34] = COLOR_LAYER, [35] = COLOR_LAYER,
        [68] = COLOR_LAYER, [69] = COLOR_LAYER, [70] = COLOR_LAYER, [71] = COLOR_LAYER,
    },
};
const uint8_t ledmap_size = sizeof(ledmap)/(sizeof(uint8_t) * DRIVER_LED_TOTAL);

// Palette for pictures. We only use COLOR_LAYER, but the library needs the
// array to exist.
const uint8_t PROGMEM colormap[][3] = {
    { 0, 0, 0 },
};
const uint8_t colormap_size = sizeof(colormap)/(sizeof(uint8_t) * 3);

/* ======================================================================== *
 * 6. QMK HOOKS
 * ======================================================================== */

// Type an opening and a closing character, one after the other.
// lang_shift_tap_key() presses the key in the right language and shift state,
// so it works whatever layer or shift is active.
static void tap_pair(uint16_t open, uint16_t close) {
    lang_shift_tap_key(open);
    lang_shift_tap_key(close);
}

/* --- Mouse acceleration --------------------------------------------------
 * Tap = 1 pixel. Hold: nothing for ACCEL_DELAY ms, then the pointer moves
 * every ACCEL_PERIOD ms, starting at 1 pixel per step and gaining one more
 * pixel per step every ACCEL_RAMP ms, up to ACCEL_MAX pixels per step.
 * Full speed comes after ACCEL_DELAY + (ACCEL_MAX - 1) * ACCEL_RAMP ms,
 * which is about 1.4 s with the numbers below.
 * Change them to taste: bigger ACCEL_RAMP = slower build-up, bigger ACCEL_MAX
 * = higher top speed. */
#define ACCEL_DELAY   20    // ms of holding before it starts moving
#define ACCEL_PERIOD  5     // ms between steps
#define ACCEL_RAMP    100   // ms of holding per extra pixel per step
#define ACCEL_MAX     15    // top speed, pixels per step

static uint8_t  accel_held = 0;         // bit n set while direction n is held
static uint32_t accel_start[4];         // when each direction was pressed
static uint32_t accel_last = 0;         // when the last step was sent
static const int8_t accel_dx[4] = { -1,  0,  0,  1 };   // L D U R
static const int8_t accel_dy[4] = {  0,  1, -1,  0 };

static void accel_send(int16_t x, int16_t y) {
    if (x >  127) x =  127;
    if (x < -127) x = -127;
    if (y >  127) y =  127;
    if (y < -127) y = -127;
    // Start from the current mouse state, so the buttons that are held down
    // (a click you are dragging with) stay held while the pointer moves.
    report_mouse_t report = mousekey_get_report();
    report.x = x;
    report.y = y;
    report.h = 0;
    report.v = 0;
    host_mouse_send(&report);
}

// ACC_L / ACC_D / ACC_U / ACC_R. Returns false for these keys (handled here).
static bool accel_process(uint16_t keycode, keyrecord_t *record) {
    if (keycode < ACC_L || keycode > ACC_R) return true;
    uint8_t dir = keycode - ACC_L;
    if (record->event.pressed) {
        accel_held |= (1 << dir);
        accel_start[dir] = timer_read32();
        accel_last = accel_start[dir];
        accel_send(accel_dx[dir], accel_dy[dir]);   // the 1 pixel tap
    } else {
        accel_held &= ~(1 << dir);
    }
    return false;
}

// Called from user_timer(): send the next step for the held directions.
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

// Draw the layer colors on top of the normal backlight.
bool rgb_matrix_indicators_user(void) {
    color_rgb_matrix_indicators();
    return true;
}

// Make sure the RGB matrix is on after power-up.
void keyboard_post_init_user(void) {
    rgb_matrix_enable();
}

// Every key press goes through here. The order matters: combos first, so a
// chord is recognised before lang_shift sees the keys.
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (!combo_process_record(keycode, record)) return false;
    if (!lang_shift_process_record(keycode, record)) return false;
    if (!color_process_record(keycode, record)) return false;
    if (!accel_process(keycode, record)) return false;               // accelerating moves

    // Our own keycodes: bracket pairs. Typed on press; nothing on release.
    switch (keycode) {
        case PAIR_PAREN:
            if (record->event.pressed) tap_pair(EN_LPRN, EN_RPRN);   // ()
            return false;
        case PAIR_BRACKET:
            if (record->event.pressed) tap_pair(EN_LBRC, EN_RBRC);   // []
            return false;
        case PAIR_BRACE:
            if (record->event.pressed) tap_pair(EN_LCBR, EN_RCBR);   // {}
            return false;
        case PAIR_ANGLE:
            if (record->event.pressed) tap_pair(EN_LT, EN_GT);       // <>
            return false;
    }
    return true;
}

// Called about every 2 ms; the libraries use it for their timeouts.
void user_timer(void) {
    combo_user_timer();
    lang_shift_user_timer();
    accel_tick();
}

void matrix_scan_user(void) {
    user_timer();
}

// Called by the combo library when a limit from config.h is exceeded. If one
// ever fires, raise COMBO_STACK_MAX_SIZE / COMBO_MAX_SIZE there.
void combo_max_count_error(void) {}
void combo_max_size_error(void) {}
