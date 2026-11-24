# Moonlander (rev B), keymap "canary": rules.mk overrides.
# See https://docs.qmk.fm/#/config_options?id=the-rulesmk-file

# --- Features ---
COMMAND_ENABLE = no
RGBLIGHT_ENABLE = no
RGB_MATRIX_ENABLE = yes
AUDIO_DRIVER = dac_basic
POINTING_DEVICE_ENABLE = no

# --- Steno (GeminiPR) ---
STENO_ENABLE = yes
STENO_PROTOCOL = geminipr

# --- Mouse layer (wheel, clicks) ---
# Needs one USB endpoint for the mouse.
MOUSEKEY_ENABLE = yes

# --- USB endpoints ---
# The board has 7 USB endpoints. They are used exactly:
#   keyboard 1 + mouse 1 + Oryx raw HID 2 + steno serial port 3 = 7
# so everything else that takes an endpoint stays off.
CONSOLE_ENABLE = no
WEBUSB_ENABLE = no
NKRO_ENABLE = no
EXTRAKEY_ENABLE = no   # media/volume keys; not used in this layout

# The Navigator module needs a pointing driver name; "custom" means none.
POINTING_DEVICE_DRIVER = custom
DYNAMIC_MACRO_ENABLE = no
