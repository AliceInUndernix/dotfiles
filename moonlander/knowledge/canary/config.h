/*
 * Moonlander (rev B), keymap "canary": config.h overrides.
 * See https://docs.qmk.fm/#/config_options?id=the-configh-file
 */

#define ORYX_CONFIGURATOR
#define FIRMWARE_VERSION u8"xbJpL/wy5xo"

/* --- RGB --- */
#define RGB_MATRIX_STARTUP_SPD 60
#undef  RGB_MATRIX_MAXIMUM_BRIGHTNESS
#define RGB_MATRIX_MAXIMUM_BRIGHTNESS 255

// Picture used by the color library for the thumb-cluster layer colors.
// We only have one (ledmap[0] in keymap.c).
#define COLOR_PICTURE_DEFAULT 1

// Keep our own LED logic (keymap.c) instead of the ZSA default one.
#define DISABLE_DEFAULT_ML_LEDS_LOGIC
#define MOONLANDER_USER_LEDS

/* --- Audio (kept from the base keymap; not used by the layout) --- */
// Match the timing used by QMK's pre-audio-overhaul implementation.
#define TEMPO_DEFAULT 200
// dac_basic advances playback every two timer ticks. 1000 Hz gives songs a
// 2 ms scheduling resolution instead of firmware25's unusable 200 ms default.
#define AUDIO_STATE_TIMER_FREQUENCY 1000
// firmware20's DAC timer made these sound one octave below their definitions.
#define STARTUP_SONG  SONG(E__NOTE(_E5), E__NOTE(_A5), ED_NOTE(_E6))
#define AUDIO_ON_SONG SONG(E__NOTE(_A4), E__NOTE(_A5))
#define AUDIO_OFF_SONG SONG(E__NOTE(_A5), E__NOTE(_A4))

/* --- Language / shift library (lang_shift) --- */
#define LANG_CHANGE_DEFAULT LANG_CHANGE_WIN_SPACE
#define SYSTEM_CURRENT_DEFAULT SYSTEM_WINDOWS
// Wait this many ms after sending the OS language-switch shortcut so the OS
// can finish switching before the next character is typed. 0 = no wait.
#define LANG_SWITCH_DELAY_MS 0

/* --- Combo library --- */
#define COMBO_KEYS_COUNT 20       // combo keys in the layout (19 used)
#define COMBO_MAX_SIZE 4          // most keys in one combo
#define COMBO_STACK_MAX_SIZE 5
#define COMBO_WAIT_TIME 50        // ms to decide which keys form a combo
