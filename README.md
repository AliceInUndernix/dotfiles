# Moonlander: "canary" layout

**Read this first.** Everything here was written with an AI (vibecoded).
It was made for one person's computer and works there; it is not
tested anywhere else. Read what a script does before you run it, and
use it with care. Improvements are still welcome.

A QMK layout for the ZSA Moonlander rev B: Canary letters, a steno
layer (Gemini PR), a dwm layer, a tmux layer, a mouse layer, a
function key layer and number-row punctuation combos. The comment at
the top of `canary/keymap.c` explains every layer, and `layout.html`
shows each one as key squares (plain HTML, no scripts; open it in a
browser or print it). `HANDOFF.md` has the background, the version
history and what was tried and settled.

The steno layer is made for steno, the steno engine in
[dotfiles](https://github.com/AliceInUndernix/dotfiles), with the
[Dotterel](https://github.com/AliceInUndernix/dotterel) dictionaries;
any program that reads Gemini PR (Plover too) works with it.

| Path | What it is |
|---|---|
| `canary/` | the layout (version 111): `keymap.c`, `config.h`, `rules.mk`, `keymap.json`, `mcuconf.h`, and the libraries it needs |
| `build.sh` | sets up QMK, builds the firmware and flashes it |
| `layout.html` | every layer as key squares, updated with every layout change |
| `qmk.patch` | a small patch to ZSA's QMK that the libraries need |
| `knowledge/canary/` | the first working canary keymap, kept for reference |
| `history/` | earlier numbered drafts (75..86) |
| `archive/` | optozorax's keymap and libraries as they were taken (below) |

## Building and flashing

```sh
git clone https://github.com/AliceInUndernix/moonlander
cd moonlander
./build.sh
```

Run it as yourself; it needs `git`, `curl`, `python3` (with venv),
`make`, `tar` with xz, and `dfu-util` to flash. Everything goes into
your home directory, in `QMK_DIR` (`${XDG_DATA_HOME:-~/.local/share}/qmk`
unless you set it):

| What | Where |
|---|---|
| ZSA's QMK source, `firmware25` branch, with `qmk.patch` applied | `$QMK_DIR/zsa_qmk` |
| `qmk` command, in its own Python environment | `$QMK_DIR/venv` |
| ARM compiler, Arm GNU Toolchain 14.2.rel1, always (Gentoo's gcc 16 built firmware that froze) | `$QMK_DIR/arm-gnu-toolchain` |

Each run copies `canary/` into QMK as
`keyboards/zsa/moonlander/keymaps/canary`, builds
`zsa/moonlander/revb:canary` and puts `zsa_moonlander_revb_canary.bin`
next to `build.sh` (git ignores it). The download steps are skipped once
done. Then it flashes with `dfu-util`: when it says so, tap the far-left
lower key (backlight layer) and then BOOT (the key next to Ctrl), or
press the reset pin hole. It waits up to 2 minutes; if the keyboard
doesn't show up, flash the .bin with Keymapp instead.

```sh
MOONLANDER_FLASH=no ./build.sh    # build only
./build.sh flash                  # flash the last build (root is fine)
LINK_DIR=~/bin ./build.sh         # also link qmk and arm-none-eabi-* there
```

Without a udev rule for the keyboard only root can reach it: then build
as yourself with `MOONLANDER_FLASH=no` and run `./build.sh flash` as
root. `dfu-suffix` (from `dfu-util`) adds a small DFU trailer to the
.bin; without it the build still works and says so.

### Libraries and the QMK patch

The libraries in `canary/` are copies of optozorax's, unchanged
(`archive/optozorax-moonlander.tar.gz` is his whole keymap at the commit
below, sha256 `8aefffb90e10fbd63b660c9fa68fef881851e7c8d0491b83f4454030998de326`;
its library directories are git submodules and come out empty):

| Directory | From | Commit |
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

## The other repositories

Each can be used on its own; the dotfiles installer sets them all up
(`./install.sh keyboard games`).

- [dotfiles](https://github.com/AliceInUndernix/dotfiles):
  the whole Linux desktop: configs, the installer, steno, the diary
  and the other programs
- [dotterel](https://github.com/AliceInUndernix/dotterel):
  the British English steno theory and its dictionaries
- [kspkan](https://github.com/AliceInUndernix/kspkan):
  a small mod manager for Kerbal Space Program

## License

Copyright (c) 2026 AliceInUndernix. The firmware is built with QMK,
which is GPL-2.0-or-later, so the keymap (`canary/keymap.c`,
`config.h`, `rules.mk`, `keymap.json`) is under the GNU General Public
License, version 2 or later, too (`COPYING`). The rest of what is ours
(`build.sh`, `layout.html`, the docs) is under Croco's Individualistic
Free Software License, version 0.251225 (`LICENSE`); `COPYRIGHT` has
the details. The libraries keep
their authors' terms: `combo/map.h` is William Swanson's (MIT), and
the rest of the libraries are optozorax's and klavarog's (their
repositories, linked above, say how they may be used).
