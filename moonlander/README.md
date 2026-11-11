# Moonlander: "canary" layout

QMK keymap for the ZSA Moonlander rev B: Canary letters, a steno layer for
Plover, a dwm layer, a mouse layer and number-row punctuation combos.
`HANDOFF.md` has the full background, the version history and the things
that were tried and settled. Start with the comment at the top of
`canary/keymap.c`.

| Path | What it is |
|---|---|
| `canary/` | Current layout (version 88): `keymap.c`, `config.h`, `rules.mk` |
| `knowledge/canary/` | The first working canary keymap, kept as reference |
| `history/` | Earlier numbered drafts (75..86) |
| `HANDOFF.md`, `UPGRADE-NOTES.md` | Notes from the earlier project |

## Building (not done yet)

This is firmware for the keyboard, so `install.sh` skips it. Nothing here
has been compiled since the handoff. The outline is:

1. Clone ZSA's QMK fork (https://github.com/zsa/qmk_firmware), firmware25
   branch, and set it up as QMK describes.
2. Copy `canary/` into the fork as a Moonlander keymap folder.
3. Put optozorax's libraries next to `keymap.c`: `lang_shift`, `combo`,
   `color`, `arbitrary_keycode` and `moonlander_leds.h`, from
   https://github.com/optozorax/moonlander. They are not in this repo.
4. Compile the keymap and flash it with Keymapp.

The exact keyboard name and compile command for rev B on firmware25 still
need checking on a real build.
