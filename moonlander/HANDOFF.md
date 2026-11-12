# Moonlander "canary" layout: handoff

This archive lets a new Claude project pick up Alice's custom QMK layout for
the **ZSA Moonlander rev B**. Start by reading `canary/keymap.c` from the top:
its header comment explains every layer, the keycode rules and where the
libraries come from. This file adds the background and the history.

## What is in the archive

| Path | What it is |
|---|---|
| `canary/keymap.c` | **Latest layout (version 87)**, comments refreshed for handoff |
| `canary/config.h` | Latest config (version 87): debounce, RGB, audio, combo limits |
| `canary/rules.mk` | Latest build features (version 87): steno, mouse keys, USB endpoints |
| `knowledge/canary/` | The first working canary keymap, kept as reference |
| `knowledge/Similar layout to what I want to achive.md` | Link to the inspiration: https://github.com/optozorax/moonlander |
| `history/` | Every earlier numbered draft (keymap 75..86, config 71/81..86, rules 1/81..86), unchanged |
| `UPGRADE-NOTES.md` | Notes from when the project moved from a claude.ai project |

In `canary/`, only comments differ from `keymap_87.c`, `config_87.h` and
`rules_87.mk`; the code is byte-for-byte the same once comments are removed.

## The layout in short

- **Letters:** Canary, on layer 0. Shifted letters are layer 1, which the
  `lang_shift` library switches on by itself.
- **Base:** optozorax's Moonlander keymap and its libraries
  (`lang_shift`, `combo`, `color`, `arbitrary_keycode`). Copies are in
  `canary/` next to `keymap.c`, with `moonlander_leds.h` and `mcuconf.h`
  from the same base; `README.md` lists their versions.
- **Firmware:** ZSA's QMK fork, firmware25 generation. Keyboard is the
  Moonlander rev B.
- **Thumbs (left 1-2-3 / right 1-2-3):** Space, Red layer, Steno toggle /
  Shift, Tab, Backspace. Big angled keys: Ctrl (left), Alt (right).
- **Thumb chords:** Space+Red = Enter, Tab+Bksp = Escape, Bksp+Alt = Delete,
  Ctrl+Red = Ctrl+Shift.
- **Number row is punctuation**, built from combo keys: `. ! ?` type the mark,
  a space and capitalise the next letter; `, ; :` add a space; chords give
  `& # $ % \` ` and backslash; four keys give an ellipsis. With Shift held,
  marks type without the space.

| # | Layer | How to reach it | Thumb LED colour |
|---|---|---|---|
| 0 | Canary | always underneath | white |
| 1 | Canary + Shift | Shift keys (automatic) | dim white |
| 2, 3 | unused, reserved by lang_shift | | off |
| 4 | Red: brackets, arrows, digits | hold left thumb 2 | red |
| 5 | Steno (GeminiPR, for Plover) | tap left thumb 3, tap again to leave | goldenrod |
| 6 | dwm window manager (Super + key) | hold right lower key next to Alt | magenta |
| 7 | Mouse with accelerating pointer | hold left lower key next to Ctrl | green |
| 8 | Backlight, sound, LEDs, BOOT | tap far-left lower key | yellow |
| 9 | tmux (Ctrl-b + a default tmux key) | hold right lower key next to DWM | cyan |

The steno layer keeps the RGB, Mouse and dwm layer keys in the same spots as
layer 0, so those layers can be reached while in steno.

## Things that were tried and settled

These were changed and then changed back, so a new project should not
suggest them again without asking:

- **Thumb order.** Version 76 swapped Red and Tab on the thumbs (Enter and
  Escape chords moved with them). Version 77 went back to Space-Red-Steno /
  Shift-Tab-Bksp.
- **Steno thumbs.** Version 78 put the steno toggle on the outer left thumb
  and all four vowels in a row; version 79 went back to A-O-exit / --E-U.
  Version 85 tried extra number keys (N3, N5) on the thumbs with the toggle
  on the lower row; version 86 went back.

## Version history (one line each)

- **knowledge/canary:** first working canary keymap. Had a blue digits layer,
  steno on chords, mouse via a three-thumb chord.
- **75:** Blue layer removed (digits moved to the Red layer bottom row);
  backlight layer (8) added; number-row punctuation combos with auto-space
  and auto-capital; Alt as a combo key; status LEDs.
- **76-77:** thumb swap tried and reverted (see above).
- **78:** steno colour made deeper (saturation 120 to 218, "goldenrod");
  steno thumb layout tried, reverted in 79.
- **80:** `tion`, `ing`, `ed` suffix keys removed; code reorganised into the
  current 9-section structure.
- **81:** number-row chords (& # ` $ %) also work with Shift held.
- **83:** Jingle Bells key and clicky mode added to the backlight layer, plus
  an audio on/off key.
- **84:** fix for sound getting stuck silent (`audio_unstick`); clicks use a
  plain timed note.
- **85-86:** steno number key `#` (STN_N1) replaces the upper S; thumb
  number-key experiment reverted.
- **87:** RGB, Mouse and dwm layer keys added to the steno layer.
- **88:** the dwm layer key holds a real Super while held, and the
  dwm keys send plain keys (KC_2, not LGUI(KC_2)). Before, Super was lost
  when keys overlapped and a bare "2" got typed.
- **89:** dwm layer brought up to date with `dwm/config.h`: dmenu is
  Super+p, focus/master use h j k l (Shift+j/k moves the window), Super+`
  is the scratchpad, Enter is zoom and Shift+Enter the terminal. The
  previous-tag key (Back) was removed: Alice doesn't use it. New keys:
  master count (i/d), doubledeck (r), IceCat/Chromium (w), wiremix (a),
  btop (Escape), Flameshot (Print), lock (Super+Shift+l) and suspend
  (Super+Shift+s).
- **90:** dwm layer laid out again by Alice, key by key: tags on
  the bottom row counted from the right (Tag1 right pinky, All left
  pinky), Shrk/Next/Prev/Grow on the red layer's arrow keys, Zoom left of
  them, layouts on the left top row, apps on the left home row. Kill only
  works with Shift. Shift moved to left thumb 1. Removed: last-two-layouts
  (Super+Space) and Flameshot full screen (Super+Print).
- **91 (latest):** tmux layer (9, cyan), held on the right lower key next
  to DWM. Each key types Ctrl-b and one of tmux's default keys (no custom
  tmux bindings), placed where the nearest dwm key is. On the dwm layer
  Kill works without Shift and moved to the far-left home key, Menu moved
  right into Kill's spot, and the new Tmux key (Super+x: attach to the last
  session or start one; Shift: new session) took Menu's old spot. Web moved
  up to the top-left key.
- **config:** combo key count raised from 20 to 32 (by version 71); debounce
  raised to 12 ms against double letters (version 82). **rules:** unchanged in
  substance since the first numbered version (rules_1).

## Open notes for the next project

- The build target is `zsa/moonlander/revb:canary`: Alice's keyboard is
  revision B. Until 2026-11-11 the script built `reva` by mistake; rev A
  firmware doesn't run on rev B (other bootloader, other flash address).
- Version 90 compiles for revb with `./install.sh moonlander` (checked 2026-11-11,
  ARM gcc 13.2, no warnings). Flashed on Alice's rev B and working
  (2026-11-11).
- Build with the pinned Arm GNU Toolchain 14.2 (install.sh does). Built
  with Gentoo's arm-none-eabi-gcc 16 the firmware froze after 3-4 key
  presses on Alice's rev B; the same code built with gcc 13 works.
- If double letters come back, raise `DEBOUNCE` in `config.h` (15 or 20).
- USB endpoints are used up exactly (keyboard 1 + mouse 1 + Oryx raw HID 2 +
  steno 3 = 7). Turning on NKRO, console, WebUSB or media keys needs one freed first.
