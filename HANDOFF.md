# Moonlander "canary" layout: handoff

This repo lets anyone (or a new Claude project) pick up the owner's
custom QMK layout for the **ZSA Moonlander rev B**. Start by reading `canary/keymap.c` from the top:
its header comment explains every layer, the keycode rules and where the
libraries come from. This file adds the background and the history.

## What is in this repo

| Path | What it is |
|---|---|
| `canary/keymap.c` | **Latest layout (version 109)** |
| `canary/config.h` | Latest config (version 109): debounce, RGB, audio, combo limits |
| `canary/rules.mk` | Latest build features (version 109): steno, mouse keys, USB endpoints |
| `knowledge/canary/` | The first working canary keymap, kept as reference |
| `knowledge/inspiration.md` | Link to the inspiration: https://github.com/optozorax/moonlander |
| `build.sh` | Sets up QMK, builds and flashes (see README.md) |
| `history/` | Every earlier numbered draft (keymap 75..86, config 71/81..86, rules 1/81..86), unchanged |

`history/` stops at version 86; versions 87 to 109 are in the version history
below, and in git: up to 97 in the dotfiles repo, where the layout
lived until 2026-11-15 (`git log -- moonlander/canary` there), and
from then on here.

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
- **Number row is symbols**: `~ + " = ! @` left, `^ ? * ' % $`
  right; `. ,` are on the bottom row under `e i`. `. ! ? , :` type just
  the mark; with Shift, `. ! ?` type the mark, a space and capitalise the
  next letter, `,` adds a space, and `:` gives a plain `;`. Chords: `" + '`
  = `` ` ``, `" + =` = `&`, `* + '` = `#`, `/ + |` = backslash,
  `+ " = !` = ellipsis ("..." and, with Shift, "... ").

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
| 10 | Function keys F1..F24 | hold left lower key next to RGB | blue |

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
  previous-tag key (Back) was removed: the owner doesn't use it. New keys:
  master count (i/d), doubledeck (r), IceCat/Chromium (w), wiremix (a),
  btop (Escape), Flameshot (Print), lock (Super+Shift+l) and suspend
  (Super+Shift+s).
- **90:** dwm layer laid out again by the owner, key by key: tags on
  the bottom row counted from the right (Tag1 right pinky, All left
  pinky), Shrk/Next/Prev/Grow on the red layer's arrow keys, Zoom left of
  them, layouts on the left top row, apps on the left home row. Kill only
  works with Shift. Shift moved to left thumb 1. Removed: last-two-layouts
  (Super+Space) and Flameshot full screen (Super+Print).
- **91:** tmux layer (9, cyan), held on the right lower key next
  to DWM. Each key types Ctrl-b and one of tmux's default keys (no custom
  tmux bindings), placed where the nearest dwm key is. On the dwm layer
  Kill works without Shift and moved to the far-left home key, Menu moved
  right into Kill's spot, and the new Tmux key (Super+x: attach to the last
  session or start one; Shift: new session) took Menu's old spot. Web moved
  up to the top-left key.
  Then the tmux layer's left half was lined up with that: Copy top-left
  (Web), Kill, Cmd (Menu), Ses: session list (Tmux), Pane> (Scr), New
  (Term), Last (Lock).
- **92:** dwm Stky (pin a window on every tag) left of TFlt.
- **93:** function key layer (10, blue), held on the left lower
  key next to RGB (also on the steno layer). F1..F10 where the red layer's
  digits are, F11..F20 on the home row, F21..F24 on the left top row
  (USB stops at F24).
- **94:** on the function key layer, the DWM key holds Ctrl+Alt
  (CTAL_0), for Ctrl+Alt+F1..F12 (console switching).
- **95:** sounds quieter: AUDIO_DAC_SAMPLE_MAX 1023 in config.h
  (QMK default 4095).
- **96:** follows dwm: Stky pins the window on its own tag
  (Super+s), Shift+Stky on every tag (Super+Shift+s). Susp sends
  Super+Ctrl+Shift+z, dwm's new suspend shortcut, so Shift+Stky can't
  suspend.
- **97:** Stky and TFlt swapped on the dwm layer's top row:
  Stky is used far more than TFlt.
- **98:** fingerspelling while * or # is held; dropped, back to 97.
- **99:** dwm layer: Web, Mix and Top removed; Lock and Susp
  on the left pinky (home, bottom), Kill on the right pinky above Shot,
  Tmux under Bar, and two scratchpads side by side: STmx (tmux,
  Super+`) and Scr (plain st, Super+n, needs the dotfiles' dwm from
  2026-11-17).
- **100:** dwm layer: left-hand tags reversed to Tag6 Tag7
  Tag8 Tag9 All, so each hand counts from its pinky (Tag1 right pinky,
  Tag6 left pinky) and All sits on the hard inner index key.
- **101:** plain and Shift punctuation swapped: `. ! ? , ; :`
  now type just the mark, and with Shift they add the space (and the
  capital after `. ! ?`). The ellipsis chord swapped the same way.
  `:` moved above `a` and `;` went to the old `:` key.
- **102:** Shift + `:` (the key above `a`) types a plain `;`,
  and the `;` key on the bottom row is gone (it does nothing now).
- **103:** `/` moved from the number row to the bottom row under
  `a`, and `|` next to it on the right (under the right 1Sh). Their old
  number-row keys do nothing now; `/ + |` is still backslash.
- **104:** dwm layer: Shift on a key never does an unrelated
  thing. Shift+Shrk/Grow move the border between stacked windows. Term
  sends Super+Shift+u and Lock Super+Shift+o (dwm moved them off
  Super+Shift+Enter and Super+Shift+l, and restart to
  Super+Ctrl+Shift+r). The number row is off.
- **105:** dwm layer: Flt and Mono on the number row, Tile above
  Term, Deck above STmx, new MDck (masterdeck, Super+e) above Scr, TFlt
  on the left hand above Menu.
- **106:** number row by finger: `~ + " = ! % @ | ^ # ? * ' & $`,
  each mirrored pair on one finger (`! ?` index, `" '` ring, `= *`
  middle). `.` and `,` moved to the bottom row under `e` and `i`. Only
  chords with no key of their own are left: `` ` `` (`" + '`), backslash
  and the ellipsis; `& # $ %` are keys now. Bracket pairs put the cursor
  between the brackets. A chord belongs to its physical keys.
- **107:** `%` and `&` swapped: `&` on the left inner index key,
  `%` on the right number-row key above the pinky column.
- **108:** `&` and `#` are chords again: `&` = `" + =`, `#` =
  `* + '`. `@` moved to the old `&` key and `^` to the old `#` key, so
  the innermost number-row key on each hand does nothing.
- **109 (latest):** dwm layer: Word on the left inner home key (Super+y)
  opens the steno word adder (`steno -W`) as a dwm scratchpad.
- **config:** combo key count raised from 20 to 32 (by version 71); debounce
  raised to 12 ms against double letters (version 82). **rules:** unchanged in
  substance since the first numbered version (rules_1).

## Open notes for the next project

- The build target is `zsa/moonlander/revb:canary`: the owner's keyboard is
  revision B. Until 2026-11-11 the script built `reva` by mistake; rev A
  firmware doesn't run on rev B (other bootloader, other flash address).
- Version 90 compiles for revb with `./build.sh` (checked 2026-11-11,
  ARM gcc 13.2, no warnings). Flashed on the owner's rev B and working
  (2026-11-11).
- Build with the pinned Arm GNU Toolchain 14.2 (build.sh does). Built
  with Gentoo's arm-none-eabi-gcc 16 the firmware froze after 3-4 key
  presses on the owner's rev B; the same code built with gcc 13 works.
- If double letters come back, raise `DEBOUNCE` in `config.h` (15 or 20).
- USB endpoints are used up exactly (keyboard 1 + mouse 1 + Oryx raw HID 2 +
  steno 3 = 7). Turning on NKRO, console, WebUSB or media keys needs one freed first.
