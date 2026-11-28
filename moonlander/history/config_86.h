/*
 * ==========================================================================
 *  ZSA Moonlander (rev B), keymap "canary": config.h
 * ==========================================================================
 *
 * config.h holds numbers and switches that QMK and the libraries read while
 * the firmware is being built. It goes next to keymap.c and rules.mk.
 * Reference: https://docs.qmk.fm/config_options
 */

#pragma once

/* --- ZSA / Oryx ----------------------------------------------------------
 * Lets ZSA's tools (Oryx, Keymapp) recognise and talk to the keyboard. */
#define ORYX_CONFIGURATOR
#define FIRMWARE_VERSION u8"xbJpL/wy5xo"

/* --- Key debounce (fixes "double taps") ---------------------------------
 * When a switch closes or opens, its metal contacts bounce for a few
 * milliseconds. Debounce means: only accept a key change once the switch
 * has been steady for DEBOUNCE ms. QMK's default is 5 ms. Worn or dusty
 * switches can bounce for longer, so one press shows up as two letters.
 * 12 ms hides that, and is still far too short to notice while typing
 * (a very fast typist presses a key about every 60-80 ms).
 * Still getting doubles? Try 15 or 20. If one key keeps doing it at any
 * value, that switch is probably worn and is worth replacing.
 * (Every key press arrives DEBOUNCE ms later, so don't go much above 25.) */
#undef  DEBOUNCE
#define DEBOUNCE 12

/* --- Backlight (RGB matrix) ---------------------------------------------- */
#define RGB_MATRIX_STARTUP_SPD 60            // animation speed at first start, 0..255
#undef  RGB_MATRIX_MAXIMUM_BRIGHTNESS
#define RGB_MATRIX_MAXIMUM_BRIGHTNESS 255    // allow full brightness

// Which LED "picture" from keymap.c is shown at start-up. Picture 1 is
// ledmap[0]: the thumb keys in the active layer's colour.
// 0 would mean "no picture" (no layer colours).
#define COLOR_PICTURE_DEFAULT 1

// Use the LED code in keymap.c instead of ZSA's built-in LED behaviour, and
// let keymap.c control the six small status LEDs (LED_1..LED_6).
#define DISABLE_DEFAULT_ML_LEDS_LOGIC
#define MOONLANDER_USER_LEDS

/* --- Audio ---------------------------------------------------------------
 * Kept from optozorax's keymap. The layout has no music keys, but the
 * start-up sound and the audio on/off sounds still use these. */
#define TEMPO_DEFAULT 200                    // same timing as older QMK
// Run the audio timer at 1000 Hz so songs play with 2 ms accuracy
// (ZSA's firmware25 default of 200 ms is far too coarse).
#define AUDIO_STATE_TIMER_FREQUENCY 1000
// Start-up and audio on/off sounds. (Older ZSA firmware, firmware20,
// played these one octave lower than written; firmware25 plays them right.)
#define STARTUP_SONG   SONG(E__NOTE(_E5), E__NOTE(_A5), ED_NOTE(_E6))
#define AUDIO_ON_SONG  SONG(E__NOTE(_A4), E__NOTE(_A5))
#define AUDIO_OFF_SONG SONG(E__NOTE(_A5), E__NOTE(_A4))

/* --- lang_shift (EN_ keys and smart Shift) -------------------------------
 * The library can also switch the computer's language; this layout only
 * uses English, so these just need sensible values. */
#define LANG_CHANGE_DEFAULT    LANG_CHANGE_WIN_SPACE   // OS shortcut for switching language
#define SYSTEM_CURRENT_DEFAULT SYSTEM_WINDOWS
// Pause (ms) after switching the OS language, so the OS is ready before the
// next character arrives. 0 = no pause.
#define LANG_SWITCH_DELAY_MS 0

/* --- Combos (chords) ----------------------------------------------------- */
#define COMBO_KEYS_COUNT     32   // number of combo keys (CMB_000..CMB_031); keymap.c uses all 32
#define COMBO_MAX_SIZE       4    // most keys in one chord (the ellipsis chord has 4)
#define COMBO_STACK_MAX_SIZE 5    // how many chords can be in progress at the same time
#define COMBO_WAIT_TIME      50   // ms to wait for the other keys of a chord
