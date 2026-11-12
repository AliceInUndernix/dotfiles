# dotfiles

My Linux setup: Gentoo, plain X, dwm, st, dmenu, slock, tmux, Neovim and a
ZSA Moonlander keyboard. The aim is a small, fast system with as little
as possible running. Every application has its own directory, and one
script puts the whole configuration back after a fresh install.

## Layout

| Directory | What it is | Installed to |
|---|---|---|
| `dwm/` | dwm 6.8 source (window manager) | `~/.system/bin/dwm` |
| `st/` | st 0.9.3 source (terminal): scrollback like Alacritty (Shift+PageUp/PageDown/Home/End, mouse wheel), Symbols Nerd Font for icons | `~/.system/bin/st`, font in `~/.system/share/fonts` |
| `dmenu/` | dmenu 5.4 source (launcher), `dmenu_apps` and its list `apps` | `~/.system/bin/dmenu`, `dmenu_apps`, `~/.config/dmenu/apps` |
| `slock/` | slock 1.7 source (screen locker; Mod+Shift+l locks, Mod+Shift+s suspends) | `~/.system/bin/slock` |
| `nvim/` | Neovim config (0.11+, no plugin manager) | `~/.config/nvim` (symlink), `~/.system/bin/vim` -> nvim (first in PATH) |
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
| `plover/` | Plover (steno) launcher and first settings: Dotterel, Gemini PR on the Moonlander | `~/.system/plover` (newest release, Dotterel from the moonlander repo), `~/.system/bin/plover` |
| `archive/` | Untouched copies of everything downloaded from elsewhere | |
| `TODO.md` | Things decided but left for later | |
| `install.sh` | Sets everything up | |

Every program goes into your own `~/.system` (commands in `~/.system/bin`,
first in PATH, manuals in `~/.system/share/man`); nothing goes to
`/usr/local`, and `~/.local/bin` is not used (install.sh moves what was
there into `~/.system/bin`). `PREFIX=...` overrides this for the
suckless tools.

What programs keep for you goes there too: `~/.profile` points the XDG
folders at `~/.system/share` (data: fonts, Neovim plugins, QMK),
`~/.system/state` (ksh history, Neovim undo, the X cookie) and
`~/.system/cache`, so `~/.local` and `~/.cache` are not used; install.sh
moves what was in them, and `~/.ksh_history`, over. Settings stay in
`~/.config`, and `~/.profile`, `~/.kshrc` and `~/.xinitrc` stay in `~`,
all as links into this repo. Your own files (books, downloads) stay in
`~`. Log in again after the first run so programs see the new places.

The suckless tools are the upstream sources from https://git.suckless.org,
copied at the release tag. Each directory has an `UPSTREAM` file with the
tag and commit, and any local change.

## Fresh install

Clone and run it as yourself. Only three things need root, and for those
the script stops and tells you the `su -c` command (it never uses sudo
or doas): `--deps` (system packages), `slock` (must be root's to lock
the screen) and making ksh the login shell (part `ksh`). Run as root, it
sets up the configs for the user who owns the repo folder, and
everything it puts in that home belongs to that user, not to root.

```sh
git clone https://github.com/AliceInUndernix/dotfiles ~/dotfiles
su -c '~alice/dotfiles/install.sh --deps'   # first time: dependencies, then everything
~/dotfiles/install.sh                       # later: everything that doesn't need root
```

`--deps` uses the system's package manager and asks first (emerge --ask on
Gentoo; apt, pacman, dnf and xbps are also known). Leave it out when the
dependencies are already there:

```sh
./install.sh            # everything
./install.sh nvim       # only some parts: ksh xdg x nvim tmux flameshot mpv sioyek wiremix btop feh dwm st dmenu slock moonlander plover
./install.sh --list     # the part names
INSTALL_USER=alice ./install.sh    # as root, if the repo folder belongs to root
```

What the script does:

- **Configs** are symlinked, so `~/.config/nvim` *is* `~/dotfiles/nvim` and
  edits land in the repo. Anything already there is moved to
  `<name>.bak.<date>`, never deleted. Running it again is safe.
- **suckless tools** are built and installed into `~/.system`, and the build
  files are cleaned up afterwards.
- **Log**: each run writes what it did, any errors and facts about the system (distro, kernel, which tools are installed) to `~/.system/state/install/latest.log`, yours even when root ran it. Paste it to Claude to check a run.
- **Private home**: new files are yours alone (umask 077 in `~/.profile`, not for root), and each run of `install.sh` takes group and other permissions off everything already in your home.
- **Login shell**: ksh becomes the shell of both the user and root (`chsh`, no password asked; root gets copies of `profile` and `kshrc`, owned by root).
- **Moonlander**: QMK, its Python environment, the ARM compiler and ZSA's
  QMK source go into `~/.system/share/qmk`, then the firmware is built into
  `moonlander/` for Keymapp. See `moonlander/README.md`.
- **Plover**: the newest release (its AppImage, unpacked) goes into
  `~/.system/plover`, with Dotterel and its dark theme from
  AliceInUndernix/moonlander. Plover runs in portable mode there, so its
  settings, plugins and logs stay in that folder too. Each run updates both.
  To read the keyboard you must be in group `dialout`:
  `su -c './install.sh plover'` adds you once (then log in again).

On Gentoo the build needs `x11-libs/libX11`, `libXft`, `libXinerama`,
`libXrandr`, `libXext`, `media-libs/fontconfig` and `freetype`; `--deps`
installs them.

## Changing a suckless tool

The suckless way: settings are C code in `config.h`, and you rebuild.

```sh
cd ~/dotfiles/dwm
$EDITOR config.h
../install.sh dwm           # no root needed (slock: su -c '../install.sh slock')
```

For dwm, then type `dwm restart` in a terminal (or press Super+Shift+r):
dwm starts again in place with the new build, and every window stays on
its tag, in the same order, with each tag's layout. For the other tools,
start them again. Rules for these directories:

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
in copy mode, a plain grey status bar. No extra key bindings: the
Moonlander's tmux layer (hold the key right of DWM) types Ctrl-b plus
tmux's own default keys. In dwm, Super+x opens st with tmux attached to
the last session (or a new one if there is none), Super+Shift+x always
starts a new session.

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
