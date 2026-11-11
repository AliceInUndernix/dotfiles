# Moonlander: "canary" layout

QMK keymap for the ZSA Moonlander rev B: Canary letters, a steno layer for
Plover, a dwm layer, a mouse layer and number-row punctuation combos.
`HANDOFF.md` has the full background, the version history and the things
that were tried and settled. Start with the comment at the top of
`canary/keymap.c`.

| Path | What it is |
|---|---|
| `canary/` | Current layout (version 89): `keymap.c`, `config.h`, `rules.mk`, `keymap.json`, `mcuconf.h`, and the libraries it needs |
| `qmk.patch` | Small patch to ZSA's QMK that the libraries need |
| `knowledge/canary/` | The first working canary keymap, kept as reference |
| `history/` | Earlier numbered drafts (75..86) |
| `HANDOFF.md`, `UPGRADE-NOTES.md` | Notes from the earlier project |

## Building

```sh
./install.sh moonlander
```

Everything goes into your home folder; nothing needs root:

| What | Where |
|---|---|
| ZSA's QMK source, `firmware25` branch, with `qmk.patch` applied | `~/.local/share/qmk/zsa_qmk` |
| `qmk` command, in its own Python environment | `~/.local/share/qmk/venv`, linked as `~/.local/bin/qmk` |
| ARM compiler, only if `arm-none-eabi-gcc` isn't installed already (Arm GNU Toolchain 14.2.rel1) | `~/.local/share/qmk/arm-gnu-toolchain`, linked into `~/.local/bin` |

Each run copies `canary/` into QMK as `keyboards/zsa/moonlander/keymaps/canary`,
builds `zsa/moonlander/reva:canary` and puts
`moonlander/zsa_moonlander_reva_canary.bin` here (git ignores it). Flash
that file with Keymapp. Run it again after every change to the keymap; the
download steps are skipped once done.

`dfu-suffix` (package `dfu-util`, installed by `./install.sh --deps`) adds a
small DFU trailer to the .bin. Without it the build still works and says so.

### Libraries and the QMK patch

The libraries in `canary/` are copies of optozorax's, unchanged:

| Folder | From | Commit |
|---|---|---|
| `lang_shift/` | https://github.com/klavarog/lang_shift | `2a6f814` (2026-06-21) |
| `combo/` (without the diagram images) | https://github.com/klavarog/combo | `c882759` (2026-06-21) |
| `arbitrary_keycode/` | https://github.com/klavarog/arbitrary_keycode | `a3eb347` (2026-06-20) |
| `color/`, `moonlander_leds.h`, `mcuconf.h`, `../qmk.patch` | https://github.com/optozorax/moonlander | `8b725d0` (2026-07-30) |

`qmk.patch` changes ZSA's QMK in three places: key events can carry any
keycode (needed by `arbitrary_keycode`, which the combo library uses to
press keys), the RGB code ignores those events, and the audio state timer
frequency can be set from `config.h`. `mcuconf.h` turns on the two extra
timers the audio driver uses.
