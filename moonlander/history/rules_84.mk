# ==========================================================================
#  ZSA Moonlander (rev B), keymap "canary": rules.mk
# ==========================================================================
#
# rules.mk says which QMK features are built into the firmware.
# "yes" builds a feature in, "no" leaves it out.
# Reference: https://docs.qmk.fm/config_options#the-rulesmk-file
#
# Comments are kept on their own lines on purpose: in a .mk file, a comment
# after a value can leave invisible spaces in that value.

# --- Basic features ---
# QMK's "magic" debug key combos: not used
COMMAND_ENABLE = no
# The Moonlander uses RGB matrix (per-key backlight), not RGB light
RGBLIGHT_ENABLE = no
RGB_MATRIX_ENABLE = yes
# How the Moonlander's speaker is driven
AUDIO_DRIVER = dac_basic
# Record-and-replay macros: not used
DYNAMIC_MACRO_ENABLE = no

# --- Steno layer (layer 5) ---
# Plover talks to the keyboard over a virtual serial port using GeminiPR.
STENO_ENABLE = yes
STENO_PROTOCOL = geminipr

# --- Mouse layer (layer 7) ---
# Mouse movement, wheel and clicks.
MOUSEKEY_ENABLE = yes

# --- Features left off to save USB endpoints ---
# The keyboard's chip has only a few USB endpoints (connections to the
# computer). The steno serial port needs several of them, so features that
# would take another one stay off.
# Debug text output:
CONSOLE_ENABLE = no
# Browser connection (Oryx live training):
WEBUSB_ENABLE = no
# Unlimited simultaneous keys (without it: 6 keys + modifiers):
NKRO_ENABLE = no
# Media and volume keys (not in this layout):
EXTRAKEY_ENABLE = no

# --- Navigator trackpad ---
# No ZSA Navigator is used. The ZSA Navigator module still expects a pointing
# driver name, and "custom" with POINTING_DEVICE_ENABLE = no means "none".
POINTING_DEVICE_ENABLE = no
POINTING_DEVICE_DRIVER = custom
