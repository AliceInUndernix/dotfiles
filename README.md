# dotfiles

**Read this first.** Everything here was written with an AI (vibecoded).
It was made for one person's computer and works there; it is not
tested anywhere else. Read what a script does before you run it, and
use it with care. Improvements are still welcome.

A complete, small Linux desktop: Gentoo, plain X, dwm, st, dmenu,
slock, tmux and Neovim, typed on a ZSA Moonlander with steno. Every
program has its own directory here, and one POSIX shell script,
`install.sh`, puts the whole setup back on a fresh install.

Besides the configs there are a few programs of our own, all plain
C89, statically linked, with no dependency but the kernel at run time:

| Program | What it does |
|---|---|
| `steno/` | a steno engine (a port of Plover's) for a Gemini PR keyboard, with Dotterel's dictionaries; paper tape, suggestions, adding and looking up strokes |
| `diary/` | a private diary in one encrypted file, opened into RAM only: days, people and places, notes, plans, calendars, attachments, search |
| `seal/` | `seal DIR` turns a directory into one encrypted file (Argon2id, XChaCha20-Poly1305); `unseal FILE` gives it back, into RAM by default |
| `clean/` | takes metadata out of pictures, documents, video and audio, in place, with a backup |
| `webarchiver/` | archives whole websites with wget for offline reading and keeps them up to date, with version history |
| `ballfix/` | a filter for the Kensington SlimBlade Pro trackball: no false double clicks, buttons changed |
| `net/vpntop`, `net/vpnauth` | the screen and the password helper of the VPN and kill switch |

Each has a `README` with every option, and `make test` where there
are tests.

## What is where

| Directory | What it is | Installed to |
|---|---|---|
| `dwm/` | dwm 6.8 (window manager), patched | `~/.system/bin/dwm` |
| `st/` | st 0.9.3 (terminal): scrollback, Symbols Nerd Font for icons | `~/.system/bin/st` |
| `dmenu/` | dmenu 5.4 (launcher), `dmenu_apps` and its list `apps` | `~/.system/bin/dmenu`, `~/.system/config/dmenu/apps` |
| `slock/` | slock 1.7 (screen locker): Mod+Shift+o locks, Mod+Ctrl+Shift+z suspends | `~/.system/bin/slock` (root's, setuid) |
| `nvim/` | Neovim 0.11+ config, no plugin manager (`nvim/README.md`) | `~/.system/config/nvim`, `~/.system/bin/vim` -> nvim |
| `tmux/` | tmux, close to its defaults | `~/.system/config/tmux/tmux.conf` |
| `ksh/` | Korn shell: `profile`, `kshrc`, and `termcheck` (why a terminal shows no prompt) | `~/.profile`, `~/.system/config/ksh/kshrc`; ksh is the login shell of the user and root |
| `x/` | `xinitrc`: wallpaper, PipeWire, xss-lock, then dwm (`startx`) | `~/.system/config/x/xinitrc` |
| `xdg/` | Downloads, Pictures, Music and the rest all set to `~` | `~/.system/config/user-dirs.*` |
| `mpv/`, `sioyek/`, `feh/`, `flameshot/`, `wiremix/`, `btop/` | video, PDF and EPUB, pictures, screenshots, mixer, system monitor | `~/.system/config/<name>` |
| `net/` | Riseup VPN, Tor and a kill switch for Gentoo with OpenRC (`net/README`) | `/usr/local/bin/vpn`, `/etc/init.d/`, `/etc/vpn` |
| `icecatrc/` | IceCat: its settings, three add-ons and our extension "switch" (JavaScript, Tor per site, fingerprints, wipes, HTTPS only; `icecatrc/README`) | the IceCat profile in `~/.mozilla/icecat` |
| `scripts/` | small helpers (`reset-usb`) | |
| `archive/` | untouched copies of everything taken from elsewhere, with checksums | |
| `install.sh`, `install/` | the installer | |

Configs are symlinks into this repo, so editing `~/.system/config/nvim`
edits `~/dotfiles/nvim`. Programs go into `~/.system` (commands in
`~/.system/bin`, first in PATH; data and state in
`~/.system/share` and `state`; caches are not ours and stay in `~/.cache`; see "Where everything goes" below).

## Install

Clone it and run the script as yourself. It never uses sudo or doas:
the few steps that need root stop and print the `su -c` command to
run instead.

```sh
git clone https://github.com/AliceInUndernix/dotfiles ~/dotfiles
su -c '~user/dotfiles/install.sh --new-pc'   # root: a new computer, everything
~/dotfiles/install.sh                        # later: all parts needing no root
~/dotfiles/install.sh pick                   # or choose from a menu
```

### The parts, in groups

| Group | Parts |
|---|---|
| `shell` | `ksh xdg nvim tmux` |
| `desktop` | `x dwm st dmenu slock feh flameshot claude` |
| `media` | `mpv sioyek wiremix btop` |
| `tools` | `webarchiver clean seal ballfix diary` |
| `keyboard` | `steno moonlander` |
| `network` | `net icecatrc` |
| `games` | `ksp` (only when named) |
| `browsers` | `icecat chromium` (only when named) |

A part, a group or a number (`./install.sh --list` shows them) can be
named anywhere a part can:

```sh
./install.sh                 # every part that needs no root (= all)
./install.sh nvim dwm        # only these parts
./install.sh desktop media   # whole groups
./install.sh 4 12            # by number (0 is --deps)
./install.sh pick            # a menu: type numbers or group names to
                             # choose, a all, n none, Enter installs
./install.sh all icecat      # every part, and IceCat too
./install.sh check           # only look: installed, missing, out of date
./install.sh update          # git pull, every part, then check
./install.sh update steno    # the same for only those parts
su -c '~user/dotfiles/install.sh update --deps'   # with the packages first
```

The work is in three steps, each a file in `install/`:

1. `packages.sh`, `./install.sh --deps` (root): the system's packages:
   X and the libraries the suckless tools need, ksh, git, curl, wget,
   Neovim, tmux, xclip, btop, mpv, yt-dlp, feh, Flameshot, wiremix,
   Sioyek, zip, unzip, 7zip, OpenVPN, Tor and dfu-util. Gentoo uses
   `emerge -qv`; Debian and Ubuntu `apt-get`, Arch `pacman`, each
   installing only the names its repositories have (wiremix, for one,
   is missing from some). dnf and xbps are known but not tested.
2. `apps.sh`, `./install.sh icecat chromium` (root, Gentoo): the
   browsers IceCat and ungoogled-chromium, built from source for hours,
   so only when named. IceCat then gets the part `icecatrc` at once.
3. `configs.sh`, `./install.sh` (no root): the configs and our own
   programs.

Add `-a` and the package manager asks before it installs anything
(`emerge -qva`, apt and pacman without `-y`/`--noconfirm`):
`su -c '~user/dotfiles/install.sh -a --deps'`.

Not everything works everywhere: `net` is for Gentoo with OpenRC
only, the browsers are Gentoo only, and `nvim/` needs Neovim 0.11 or
newer (Debian 13 has 0.10).

These need root (`su -c '~user/dotfiles/install.sh PART'`):

- `--deps`: the system's packages.
- `slock`: it must be root's and setuid to lock the screen.
- `ksh`: making ksh the login shell, and root's copies of its files.
- `steno`: adding you to the keyboard's serial port group, once.
- `net`: the VPN, Tor and the kill switch.
- `ksp`: the libraries Principia needs (below).
- `icecat`, `chromium`: the browsers.

Run as root, the script sets up the configs for the user who owns the
repo directory (or `INSTALL_USER=name`), and everything it puts in
that home belongs to that user.

### Where everything goes: `~/.system`

Everything lives in one directory, so it is easy to find:

| Where | What |
|---|---|
| `~/.system/config` | the settings, each a link into `~/dotfiles` |
| `~/.system/bin` | the commands (first in PATH) |
| `~/.system/share` | data: the steno dictionaries, kspkan, QMK |
| `~/.system/state` | install logs, backups, the steno words you added |

In `~` there are only two links: `~/.profile` and `~/.config` (which
points to `~/.system/config`, for programs that ignore
`XDG_CONFIG_HOME`). Only settings are ever moved into `~/.system` (from `~/.config`);
install.sh never moves or copies data or caches such as
`~/.local/share` (Steam, games) or `~/.cache`.

Root works the same way: root's real files are in `/root/.system`
(`config/ksh`, and `net/` with the VPN's scripts, services and
settings), and `/root/.profile`, `/usr/local/bin/vpn`,
`/etc/init.d/vpn`, `/etc/vpn`, `/etc/tor/torrc` and the rest are links
into it. Installing `net` sets `/root` and `/root/.system` to mode 711,
so the links work, but nobody else can list what is in them. Apart
from the system's packages, nothing outside the two `.system`
directories is a real file of ours.

What it does, and what it never does:

- Anything already where a config goes is moved to
  `~/.system/state/backup/<path>.<date>` (root's to
  `/root/.system/state/backup`), never deleted. Running it again is
  safe.
- The suckless tools and our programs are built, installed into
  `~/.system/bin` and cleaned up again.
- Each run is logged to `~/.system/state/install/latest.log`, which
  stays on your computer.
- New files are yours alone (umask 077), and each run takes group and
  other permissions off everything in your home.
- Browsers get only the program; their settings and profiles are not
  touched.
- `moonlander` clones the layout into `~/.system/share/moonlander` and
  runs its `build.sh` (QMK and the ARM compiler go into
  `~/.system/share/qmk`), then builds and flashes the firmware.
- `steno` builds steno and clones dotterel into
  `~/.system/share/steno/dotterel`; each run updates it and keeps the
  words you added.

### Uninstall

```sh
./uninstall.sh               # everything, but the system's packages
./uninstall.sh nvim media    # only these parts or groups
./uninstall.sh --purge       # and steno's words you didn't push
su -c '~user/dotfiles/uninstall.sh net slock'   # the root parts
```

It takes out only what the installer put in: the links, the programs
in `~/.system/bin`, and root's side when run as root. The login shell
goes back to what it was, `net` is switched off first (and kept, if it
won't switch off), the original `torrc` and `proxychains.conf` come
back, and Principia leaves the game (an older copy you had comes
back). Your own data stays: the diary, kspkan's mod lists, the steno
words you added. A full uninstall moves `~/.system/config` back to `~/.config`, so
your other programs keep their settings. `share` and `state`
(data your programs wrote there, and the backups) are left where they
are.
`--deps` also takes the packages out of Gentoo's world file (`emerge
--depclean` is then up to you); the browsers go only when named.

### Kerbal Space Program: `./install.sh ksp`

Only when named. It installs:

- **kspkan**, our KSP mod manager, cloned from
  [kspkan](https://github.com/AliceInUndernix/kspkan) into
  `~/.system/share/kspkan-src` and built into `~/.system/bin`
  (kspkan's own data, the game folders and mod lists, is in
  `~/.system/share/kspkan`).
- **[Principia](https://github.com/mockingbirdnest/Principia)**, the
  n-body gravity mod. Its binary is linked from its README and is not
  on GitHub, so download the zip yourself, anywhere in `~`. The
  installer finds every `Principia*.zip` in your home, newest first,
  and asks which one to install from (or name it:
  `PRINCIPIA_ZIP=FILE`; with no terminal to ask on, it installs none).
  The zip is checked first (nothing outside its folder, no links, at
  most 2 GB unpacked). Its layout may change from release to release,
  so the installer does not expect one: it finds Principia's folder
  inside (the one with `ksp_plugin_adapter.dll`, else the one above
  `principia.so`), leaves out anything else in the zip, and puts it
  into `GameData/Principia` of `KSP_DIR`, or of the first KSP found in
  Steam's, GOG's or `~/KSP`. An old one is moved
  out of the game to `~/.system/state/backup/ksp/`. Start a new save
  after installing it.

Principia on Linux needs LLVM's libc++, libc++abi and libunwind (20
or newer). Those are system packages, so run it once as root:
`su -c '~user/dotfiles/install.sh ksp'` installs them (Gentoo:
`llvm-runtimes/libcxx`, `libcxxabi` and `libunwind`; Debian: the
newest `libc++1-N`; Arch: `libc++` and `libc++abi`), then does the
rest as you. `./install.sh check ksp` shows
what is missing.

## The other repositories

Each can be used without the rest; `./install.sh keyboard games`
sets them up here.

- [moonlander](https://github.com/AliceInUndernix/moonlander):
  the QMK layout for the ZSA Moonlander
- [dotterel](https://github.com/AliceInUndernix/dotterel):
  the British English steno theory and its dictionaries
- [kspkan](https://github.com/AliceInUndernix/kspkan):
  a small mod manager for Kerbal Space Program

## Colours: Habamax everywhere

Every program here uses Habamax, Neovim's dark colour scheme, and so
must anything new: the same dark grey background, grey text and calm
accents, no other palette. A terminal UI also looks like btop
(`btop/btop.conf`): boxes with the title in the top border, keys shown
in the borders with the key in blue, the chosen item on the selected
grey, meters and graphs in btop's Habamax gradients
(`btop/themes/habamax.theme`), square corners, ASCII and DEC line
drawing only.

| Use | Colour |
|---|---|
| background | `#1c1c1c` |
| text | `#c7c7c7` |
| titles, bold | `#eeeeee` |
| labels, graph text | `#9e9e9e` |
| inactive, dim | `#767676` |
| box lines, selected background | `#585858` |
| meter background | `#303030` |
| keys, blue | `#5f87af` |
| cyan | `#5fafd7` |
| teal | `#87afaf` |
| green: fine | `#87af87` |
| brown: worth knowing | `#af875f` |
| red: a problem | `#af5f5f` |
| magenta | `#d787d7` |

## Changing a suckless tool

Settings are C code in `config.h`; edit it and rebuild:

```sh
cd ~/dotfiles/dwm
$EDITOR config.h
../install.sh dwm     # slock: su -c '../install.sh slock'
```

For dwm, then run `dwm restart` (or Super+Ctrl+Shift+r): dwm starts
again in place, and every window keeps its tag, order and layout.

- Leave `config.def.h` as upstream ships it, so a new release can be
  compared against it.
- A patch from suckless.org: `patch -p1 < file.diff` in the directory,
  then note it in that directory's `UPSTREAM` file, with any local
  change.
- A new release: copy the new upstream files over, keep `config.h`,
  compare the old and new `config.def.h`, update `UPSTREAM`.

The sources are the releases from https://git.suckless.org; `UPSTREAM`
names the tag and commit. One local change: `slock/config.h` uses group
`nobody`, because Gentoo has no `nogroup`.

## No Caps Lock, no stuck keys

There is no Caps Lock on any keyboard: `x/nocaps.conf` (X's
`caps:none`) is linked into `/etc/X11/xorg.conf.d` by a root run of
`./install.sh x`, and X reads it when it starts. If a Shift, Ctrl, Alt
or Super ever stays down with no key pressed (everything typed comes
out in capitals), Super+BackSpace (with or without Shift) runs
`steno -K`: it lets go of the key and says on the bar (Super+b) what
it found. A key that another keyboard holds can't be let go of from
the computer: press and let go of that key there, or unplug that
keyboard. The Moonlander (version 111 and later) lets go of every
modifier by itself 0.3 s after its last key is let go.

## tmux and Neovim

tmux stays close to its defaults (prefix Ctrl-b): no Esc delay, true
colour, windows from 1, vi keys in copy mode, a grey status bar with
the time. The Moonlander's tmux layer types Ctrl-b and tmux's own
keys. Super+x opens st with tmux on the last session, Super+Shift+x a
new session.

Neovim: `nvim/README.md` is the guide (keys, prose mode, the book
typing check, the Sioyek tracker). No plugin manager; the plugins come
from `archive/`, so Neovim never downloads code.

## Network: VPN and Tor

`net/README` explains it. At boot the kill switch lets nothing out but
Riseup's servers; the fastest of three random servers in the best zone
of `net/countries` is used, Tor starts after it, and DNS goes through
Tor. `vpn status` tells you how it is, `vpn` gives a menu, and your
prompt shows `VPN` when something is wrong. `vpn selftest` checks
every level (about 15 minutes); with no network, `vpn rescue`.

## IceCat: icecatrc

`./install.sh` (the part `icecatrc`, in "all") puts IceCat's settings
(`icecatrc/icecat.prefs`, into `user.js`), Habamax colours for its window
(`icecatrc/userChrome.css`), the add-ons in `icecatrc/addons.txt` (uBlock
Origin, Dark Reader, I don't care about cookies; fetched through Tor)
and our extension `switch` into your IceCat profile. When IceCat has
never been started, it makes the profile; when IceCat isn't installed
yet, `su -c '~user/dotfiles/install.sh icecat'` builds it and runs the
part right after. Close IceCat first. Then, once, in IceCat: enable the
new add-ons it asks about, and in `about:addons` let switch run in
private windows. Alt+Shift+s opens its popup; every key is in
`icecatrc/README`. By default pages are read, not run: JavaScript off,
Tor on, no web fonts and none of your own fonts shown to pages; both
JavaScript (key s) and Tor (key v, then through the VPN) can be
switched per site. The `icecat` command (`~/.system/bin/icecat`) runs
the real IceCat and then `icecatrc/tidy.sh`, so that no file left in the
profile names a site you did not keep.

The extension and its pages are JavaScript and its manifest is JSON,
which these rules otherwise avoid: an IceCat extension can be made of
nothing else. It runs only in your own browser.

## Archive

`archive/` keeps the release tarballs of dwm, st, dmenu, slock, tmux,
Monocypher and the Neovim plugins, with source, version and checksum
in `archive/README.md`, so everything can be rebuilt if an upstream
disappears.

## License

Copyright (c) 2026 AliceInUndernix. What is ours here is available
under the terms of Croco's Individualistic Free Software License,
version 0.251225, by Andrey V. Stolyarov (`LICENSE`; its home is
http://thalassa.croco.net/license.html). The parts taken from others
keep their own licenses: the suckless tools (`*/LICENSE`, MIT/X) and
their patches, the parts of Monocypher in `seal/crypto.c` (CC0, or
2-clause BSD) and the Neovim plugins. `COPYRIGHT` lists them.
