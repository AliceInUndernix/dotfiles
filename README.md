# dotfiles

My Linux setup: Gentoo, plain X, dwm, st, dmenu, slock, tmux, Neovim and a
ZSA Moonlander keyboard. The aim is a small, fast system with as little
as possible running. Every application has its own directory, and one
script puts the whole configuration back after a fresh install.

## Layout

| Directory | What it is | Installed to |
|---|---|---|
| `dwm/` | dwm 6.8 source (window manager) | `PREFIX/bin/dwm` |
| `st/` | st 0.9.3 source (terminal) | `PREFIX/bin/st` |
| `dmenu/` | dmenu 5.4 source (launcher) | `PREFIX/bin/dmenu`, `dmenu_run` |
| `slock/` | slock 1.7 source (screen locker) | `PREFIX/bin/slock` |
| `nvim/` | Neovim config (0.11+, no plugin manager) | `~/.config/nvim` (symlink) |
| `tmux/` | tmux config | `~/.config/tmux/tmux.conf` (symlink) |
| `moonlander/` | QMK "canary" keymap for the keyboard | flashed to the keyboard |
| `archive/` | Untouched copies of everything downloaded from elsewhere | |
| `install.sh` | Sets everything up | |

`PREFIX` is `/usr/local` unless you set it.

The suckless tools are the upstream sources from https://git.suckless.org,
copied at the release tag. Each directory has an `UPSTREAM` file with the
tag and commit, and any local change.

## Fresh install

```sh
git clone https://github.com/AliceInUndernix/dotfiles ~/dotfiles
cd ~/dotfiles
./install.sh --deps     # build dependencies + git + neovim, then everything
```

`--deps` uses the system's package manager and asks first (emerge --ask on
Gentoo; apt, pacman, dnf and xbps are also known). Leave it out when the
dependencies are already there:

```sh
./install.sh            # everything
./install.sh nvim       # only some parts: nvim tmux dwm st dmenu slock moonlander
./install.sh --list     # the part names
PREFIX=~/.local ./install.sh dwm   # install without root
```

What the script does:

- **Configs** are symlinked, so `~/.config/nvim` *is* `~/dotfiles/nvim` and
  edits land in the repo. Anything already there is moved to
  `<name>.bak.<date>`, never deleted. Running it again is safe.
- **suckless tools** are built as you, installed with `sudo` (or `doas`),
  and the build files are cleaned up afterwards.
- **Moonlander** is skipped: it is keyboard firmware. See
  `moonlander/README.md`.

On Gentoo the build needs `x11-libs/libX11`, `libXft`, `libXinerama`,
`libXrandr`, `libXext`, `media-libs/fontconfig` and `freetype`; `--deps`
installs them.

## Changing a suckless tool

The suckless way: settings are C code in `config.h`, and you rebuild.

```sh
cd ~/dotfiles/dwm
$EDITOR config.h
../install.sh dwm       # or: make && sudo make install && make clean
```

Then restart dwm (or log out and in). Rules for these directories:

- Edit `config.h`. Leave `config.def.h` as upstream ships it, so a new
  release can be compared against it.
- Patches from suckless.org: `patch -p1 < file.diff` inside the
  directory, then note the patch in that directory's `UPSTREAM` file.
- Updating to a new release: copy the new upstream files over, keep
  `config.h`, compare the old and new `config.def.h`, update `UPSTREAM`.

Local changes so far: `slock/config.h` uses group `nobody` instead of
`nogroup`, because Gentoo has no `nogroup` group (Debian and Ubuntu do;
the script warns if the group is missing).

## Archive

`archive/` keeps release tarballs of dwm, st, dmenu, slock and tmux, the
three Neovim plugins and optozorax's Moonlander libraries, with source,
version and checksum for each in `archive/README.md`. If an upstream
disappears, everything can still be rebuilt from this repo. The install
script also unpacks the Neovim plugins from here, so Neovim works on
first start without internet.

## tmux

`tmux/tmux.conf` stays close to defaults (prefix is still Ctrl-b): no Esc
delay for Neovim, true colour from st, windows numbered from 1, vi keys
in copy mode, a plain grey status bar.

## Neovim

`nvim/README.md` is the user guide (keys, prose mode, grammar check,
Claude proofreading, Sioyek tracker) and `nvim/HANDOFF.md` records
earlier decisions and open issues. Its rules: stay close to Neovim
defaults, no plugin manager, keep working on 0.11. Plugins download
themselves on first start if missing; `install.sh` puts the archived
copies in place first, so no internet is needed.

## Moonlander

`moonlander/canary/` is the current layout (version 87);
`moonlander/HANDOFF.md` explains it. Building needs ZSA's QMK fork and
optozorax's keymap libraries, which are not in this repo.
