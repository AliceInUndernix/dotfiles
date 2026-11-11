# dotfiles

My Linux setup: Gentoo, plain X, dwm, st, dmenu, slock, tmux, Neovim and a
ZSA Moonlander keyboard. The aim is a small, fast system with as little
as possible running. Every application has its own directory, and one
script puts the whole configuration back after a fresh install.

## Layout

| Directory | What it is | Installed to |
|---|---|---|
| `dwm/` | dwm 6.8 source (window manager) | `PREFIX/bin/dwm` |
| `st/` | st 0.9.3 source (terminal): scrollback like Alacritty (Shift+PageUp/PageDown/Home/End, mouse wheel), Symbols Nerd Font for icons | `PREFIX/bin/st`, font in `~/.local/share/fonts` |
| `dmenu/` | dmenu 5.4 source (launcher), `dmenu_apps` and its list `apps` | `PREFIX/bin/dmenu`, `dmenu_apps`, `~/.config/dmenu/apps` |
| `slock/` | slock 1.7 source (screen locker; Mod+Shift+l locks, Mod+Shift+s suspends) | `PREFIX/bin/slock` |
| `nvim/` | Neovim config (0.11+, no plugin manager) | `~/.config/nvim` (symlink), `~/.local/bin/vim` -> nvim |
| `tmux/` | tmux config | `~/.config/tmux/tmux.conf` (symlink) |
| `ksh/` | Korn shell: `profile` (PATH, EDITOR) and `kshrc` (prompt, emacs keys, aliases) | `~/.profile`, `~/.kshrc` (symlinks); ksh becomes the login shell of the user and root |
| `flameshot/` | Screenshots: Print selects, Mod+Print copies the screen | `~/.config/flameshot/flameshot.ini` (symlink) |
| `mpv/` | Video player: resume, Habamax OSD, vim seek keys | `~/.config/mpv` (symlink) |
| `sioyek/` | Book/PDF reader in Habamax colours (TypeTrack's green/red too) | `~/.config/sioyek/prefs_user.config` (symlink) |
| `wiremix/` | PipeWire mixer in the terminal (like pavucontrol), Habamax theme | `~/.config/wiremix/wiremix.toml` (symlink) |
| `btop/` | btop with a Habamax theme and vim keys | `~/.config/btop/` files (symlinks) |
| `xdg/` | Download, Pictures, Music etc. all set to `~` (no capitalised folders) | `~/.config/user-dirs.dirs`, `user-dirs.conf` (symlinks) |
| `feh/` | feh shows pictures on Habamax grey | `~/.config/feh/themes` (symlink) |
| `x/` | `xinitrc`: Habamax-grey wallpaper, PipeWire, idle lock off by default (`IDLE_LOCK_MINUTES`), xss-lock (slock before suspend), then dwm (run `startx`) | `~/.xinitrc` (symlink) |
| `moonlander/` | QMK "canary" keymap for the keyboard | built into `moonlander/*.bin`, flashed with Keymapp |
| `archive/` | Untouched copies of everything downloaded from elsewhere | |
| `TODO.md` | Things decided but left for later | |
| `install.sh` | Sets everything up | |

`PREFIX` is `/usr/local` unless you set it.

The suckless tools are the upstream sources from https://git.suckless.org,
copied at the release tag. Each directory has an `UPSTREAM` file with the
tag and commit, and any local change.

## Fresh install

Clone as yourself, then run the script as root (it never uses sudo or
doas). It sets up the configs for the user who owns the repo folder, and
everything it puts in that home belongs to that user, not to root.

```sh
git clone https://github.com/AliceInUndernix/dotfiles ~/dotfiles
su -c '~alice/dotfiles/install.sh --deps'   # dependencies, then everything
```

`--deps` uses the system's package manager and asks first (emerge --ask on
Gentoo; apt, pacman, dnf and xbps are also known). Leave it out when the
dependencies are already there:

```sh
./install.sh            # everything
./install.sh nvim       # only some parts: ksh xdg x nvim tmux flameshot mpv sioyek wiremix btop feh dwm st dmenu slock moonlander
./install.sh --list     # the part names
INSTALL_USER=alice ./install.sh    # if the repo folder belongs to root
```

What the script does:

- **Configs** are symlinked, so `~/.config/nvim` *is* `~/dotfiles/nvim` and
  edits land in the repo. Anything already there is moved to
  `<name>.bak.<date>`, never deleted. Running it again is safe.
- **suckless tools** are built and installed into `PREFIX`, and the build
  files are cleaned up afterwards.
- **Login shell**: ksh becomes the shell of both the user and root (`chsh`, no password asked; root keeps its own `/root/.profile`).
- **Moonlander**: QMK, its Python environment, the ARM compiler and ZSA's
  QMK source go into the user's `~/.local`, then the firmware is built into
  `moonlander/` for Keymapp. See `moonlander/README.md`.

On Gentoo the build needs `x11-libs/libX11`, `libXft`, `libXinerama`,
`libXrandr`, `libXext`, `media-libs/fontconfig` and `freetype`; `--deps`
installs them.

## Changing a suckless tool

The suckless way: settings are C code in `config.h`, and you rebuild.

```sh
cd ~/dotfiles/dwm
$EDITOR config.h
su -c '../install.sh dwm'   # or, as root: make install && make clean
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

`moonlander/canary/` is the current layout (version 88);
`moonlander/HANDOFF.md` explains it. `./install.sh moonlander` sets up QMK
in your home folder and builds it (see `moonlander/README.md`).
