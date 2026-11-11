#!/bin/sh
# Install Alice's configuration on a fresh Linux system. Run it as root
# (log in as root, or su -c ./install.sh); it never calls sudo or doas.
#
#   ./install.sh               link configs and build/install everything
#   ./install.sh nvim dwm      only the parts named
#   ./install.sh --deps        first install build dependencies with the
#                              distro's package manager (asks before it does)
#   ./install.sh --list        show the parts
#   BROWSERS=yes ./install.sh --deps   also build IceCat and Chromium
#                              (Gentoo; from source, takes hours)
#
# The configs are for the user who owns this repo folder (or INSTALL_USER=
# name ./install.sh). Everything put in that user's home belongs to them,
# not to root. Configs are symlinked, so editing them in ~/.config edits
# this repo. Anything already in the way is moved to <name>.bak.<date>,
# never deleted. suckless tools are installed into PREFIX (default
# /usr/local). POSIX sh, no bashisms, so it runs on any distro.

set -eu
# What gets installed system-wide must be readable by everyone, whatever
# umask the shell that started us has (the user's is 077).
umask 022

DOTFILES=$(cd "$(dirname "$0")" && pwd)
PREFIX=${PREFIX:-/usr/local}
STAMP=$(date +%Y%m%d-%H%M%S)
PARTS="ksh xdg x nvim tmux flameshot mpv sioyek wiremix btop feh dwm st dmenu slock moonlander"

say()  { printf '==> %s\n' "$*"; }
warn() { printf '!!  %s\n' "$*" >&2; }
die()  { warn "$*"; exit 1; }

[ "$(id -u)" -eq 0 ] || die "run this as root (log in as root, or: su -c ./install.sh)"

# The user the configs are for: INSTALL_USER, else whoever owns this repo.
USER_NAME=${INSTALL_USER:-$(stat -c %U "$DOTFILES" 2>/dev/null || stat -f %Su "$DOTFILES")}
[ "$USER_NAME" != root ] ||
	die "this repo belongs to root: say whose configs these are, e.g. INSTALL_USER=alice ./install.sh"
pw=$(getent passwd "$USER_NAME" 2>/dev/null || grep "^$USER_NAME:" /etc/passwd) ||
	die "no such user: $USER_NAME"
USER_GROUP=$(id -gn "$USER_NAME")
pw=${pw%:*}
HOME=${pw##*:}
[ -d "$HOME" ] || die "home folder $HOME of $USER_NAME does not exist"
# Root's own XDG settings must not leak in; use the user's defaults.
unset XDG_CONFIG_HOME XDG_DATA_HOME XDG_CACHE_HOME
CONFIG=$HOME/.config
DATA=$HOME/.local/share
export HOME

# own <path>...: give files root made in the user's home to the user.
own() { chown -hR "$USER_NAME:$USER_GROUP" "$@"; }

# userdir <dir>: mkdir -p, with every folder it creates owned by the user.
userdir() {
	d=$1 new=
	while [ ! -e "$d" ]; do new="$new $d"; d=$(dirname "$d"); done
	[ -z "$new" ] && return
	mkdir -p "$1"
	for d in $new; do chown "$USER_NAME:$USER_GROUP" "$d"; done
}

# link <file or dir in repo> <target>: symlink, backing up what is there.
link() {
	src=$1 dst=$2
	if [ -L "$dst" ] && [ "$(readlink "$dst")" = "$src" ]; then
		say "ok      $dst"
		return
	fi
	if [ -e "$dst" ] || [ -L "$dst" ]; then
		mv "$dst" "$dst.bak.$STAMP"
		say "backup  $dst -> $dst.bak.$STAMP"
	fi
	userdir "$(dirname "$dst")"
	ln -s "$src" "$dst"
	own "$dst"
	say "link    $dst -> $src"
}

# --- build dependencies ---------------------------------------------------

deps() {
	id=unknown
	[ -r /etc/os-release ] && id=$(. /etc/os-release; echo "${ID_LIKE:-} ${ID:-}")
	case $id in
	*gentoo*)
		# wiremix and the two browsers are only in testing (~amd64): allow
		# just those three, in a file of our own.
		kw=/etc/portage/package.accept_keywords
		if [ -d "$kw" ] || [ ! -e "$kw" ]; then
			mkdir -p "$kw"
			kw=$kw/dotfiles
		fi
		arch=$(portageq envvar ARCH)
		for atom in media-sound/wiremix www-client/icecat www-client/ungoogled-chromium; do
			grep -qs "^$atom[[:space:]]" "$kw" && continue
			echo "$atom ~$arch" >> "$kw"
			say "keyword $atom ~$arch -> $kw"
		done
		# PipeWire first, on its own: wiremix needs its libpipewire-0.3 to
		# build, but its ebuild doesn't say so, so emerge may build wiremix
		# before PipeWire is in.
		emerge --ask --noreplace media-video/pipewire media-video/wireplumber
		emerge --ask --noreplace x11-libs/libX11 x11-libs/libXft \
			x11-libs/libXinerama x11-libs/libXrandr x11-libs/libXext \
			media-libs/fontconfig media-libs/freetype \
			dev-vcs/git app-editors/neovim app-misc/tmux \
			x11-apps/xinit x11-apps/xsetroot x11-apps/xset x11-misc/xss-lock \
			app-shells/ksh media-fonts/liberation-fonts \
			x11-misc/xclip x11-misc/xsel \
			media-gfx/flameshot media-video/mpv net-misc/yt-dlp \
			media-sound/wiremix \
			sys-process/btop media-gfx/feh \
			app-mobilephone/dfu-util
		# Browsers only with BROWSERS=yes: both build from source and take
		# hours. They are not in the main tree: IceCat comes from
		# parona-overlay, ungoogled-chromium from pf4public.
		if [ "${BROWSERS:-no}" = yes ]; then
			for repo in parona-overlay pf4public; do
				portageq get_repo_path / "$repo" >/dev/null 2>&1 && continue
				say "enable  overlay $repo"
				emerge --ask --noreplace app-eselect/eselect-repository
				eselect repository enable "$repo"
				emerge --sync "$repo"
			done
			emerge --ask --noreplace www-client/icecat \
				www-client/ungoogled-chromium
		else
			say "skip    IceCat and Chromium (BROWSERS=yes ./install.sh --deps builds them)"
		fi ;;
	*debian*|*ubuntu*)
		apt-get install build-essential pkg-config libx11-dev \
			libxft-dev libxinerama-dev libxrandr-dev libxext-dev \
			libfontconfig-dev libfreetype-dev git neovim tmux \
			xinit x11-xserver-utils xss-lock ksh fonts-liberation xclip xsel \
			flameshot mpv yt-dlp pipewire wireplumber wiremix btop \
			python3-venv dfu-util ;;
	*arch*)
		pacman -S --needed base-devel libx11 libxft libxinerama \
			libxrandr libxext fontconfig freetype2 git neovim tmux dfu-util \
			xorg-xinit xorg-xsetroot xorg-xset xss-lock ttf-liberation xclip xsel \
			flameshot mpv yt-dlp pipewire wireplumber wiremix btop ;;
	*fedora*|*rhel*)
		dnf install gcc make pkgconf-pkg-config libX11-devel \
			libXft-devel libXinerama-devel libXrandr-devel libXext-devel \
			fontconfig-devel freetype-devel git neovim tmux dfu-util \
			xorg-x11-xinit xsetroot xset xss-lock ksh liberation-mono-fonts \
			liberation-sans-fonts xclip xsel flameshot mpv yt-dlp pipewire wireplumber btop ;;
	*void*)
		xbps-install base-devel pkg-config libX11-devel libXft-devel \
			libXinerama-devel libXrandr-devel libXext-devel \
			fontconfig-devel freetype-devel git neovim tmux dfu-util \
			xinit xsetroot xset xss-lock mksh liberation-fonts-ttf xclip xsel \
			flameshot mpv yt-dlp pipewire wireplumber btop ;;
	*)
		warn "unknown distro ($id): install a C compiler, make, pkg-config,"
		warn "and headers for X11, Xft, Xinerama, Xrandr, Xext, fontconfig, freetype." ;;
	esac
}

# --- parts ----------------------------------------------------------------

install_nvim() {
	link "$DOTFILES/nvim" "$CONFIG/nvim"
	# Unpack archived plugins that are not there yet, so the first start
	# works offline. init.lua only downloads plugins that are missing.
	pack=$DATA/nvim/site/pack/plugins/opt
	for tarball in "$DOTFILES"/archive/nvim-plugins/*.tar.gz; do
		[ -e "$tarball" ] || continue
		name=$(basename "$tarball" .tar.gz)
		[ -e "$pack/$name" ] && continue
		userdir "$pack"
		tar -xzf "$tarball" -C "$pack"
		if [ -d "$pack/$name/doc" ] && command -v nvim >/dev/null 2>&1; then
			nvim --headless -u NONE -c "helptags $pack/$name/doc" -c q >/dev/null 2>&1 || :
		fi
		own "$pack/$name"
		say "plugin  $name (from archive)"
	done
	# vim runs Neovim: ~/.local/bin/vim -> nvim (~/.local/bin is in PATH
	# through ~/.profile, ahead of any real vim).
	if nvim=$(command -v nvim); then
		link "$nvim" "$HOME/.local/bin/vim"
	else
		warn "nvim is not installed (needs 0.11+)"
	fi
	command -v xclip >/dev/null 2>&1 || command -v xsel >/dev/null 2>&1 ||
		warn "xclip/xsel not installed: Neovim can't use the system clipboard (try ./install.sh --deps)"
}

install_ksh() {
	link "$DOTFILES/ksh/profile" "$HOME/.profile"
	link "$DOTFILES/ksh/kshrc" "$HOME/.kshrc"
	ksh=$(command -v ksh || command -v mksh || :)
	if [ -z "$ksh" ]; then
		warn "ksh is not installed (try ./install.sh --deps)"
		return
	fi
	# Make ksh the login shell of both the user and root.
	grep -qx "$ksh" /etc/shells 2>/dev/null || echo "$ksh" >> /etc/shells
	login_shell "$USER_NAME" "$ksh"
	login_shell root "$ksh"
	# root gets the same settings as copies it owns, not links: a link to
	# the user's file would let anything running as the user change what
	# root runs. Run ./install.sh ksh again after changing them.
	root_home=$(getent passwd root 2>/dev/null || grep '^root:' /etc/passwd)
	root_home=${root_home%:*}
	root_home=${root_home##*:}
	rootcopy "$DOTFILES/ksh/profile" "$root_home/.profile"
	rootcopy "$DOTFILES/ksh/kshrc" "$root_home/.kshrc"
	# History moved from ~/.local/state/ksh/history to ~/.ksh_history.
	old=$HOME/.local/state/ksh/history
	if [ -f "$old" ] && [ ! -e "$HOME/.ksh_history" ]; then
		mv "$old" "$HOME/.ksh_history"
		rmdir "${old%/*}" 2>/dev/null || :
		say "move    $old -> $HOME/.ksh_history"
	fi
}

# rootcopy <file in repo> <target>: copy owned by root, backing up what is there.
rootcopy() {
	if [ -f "$2" ] && [ ! -L "$2" ] && cmp -s "$1" "$2"; then
		say "ok      $2"
		return
	fi
	if [ -e "$2" ] || [ -L "$2" ]; then
		mv "$2" "$2.bak.$STAMP"
		say "backup  $2 -> $2.bak.$STAMP"
	fi
	cp "$1" "$2"
	chown root:"$(id -gn root)" "$2"
	chmod 644 "$2"
	say "copy    $1 -> $2"
}

# login_shell <user> <shell>: make <shell> the user's login shell.
login_shell() {
	now=$(getent passwd "$1" 2>/dev/null || grep "^$1:" /etc/passwd)
	now=${now##*:}
	if [ "$(basename "$now")" = "$(basename "$2")" ]; then
		say "ok      shell $now for $1"
		return
	fi
	chsh -s "$2" "$1" >/dev/null 2>&1 || usermod -s "$2" "$1" ||
		{ warn "could not make $2 the shell of $1"; return; }
	say "shell   $2 for $1 (was $now; takes effect at next login)"
}

install_xdg() {
	link "$DOTFILES/xdg/user-dirs.dirs" "$CONFIG/user-dirs.dirs"
	link "$DOTFILES/xdg/user-dirs.conf" "$CONFIG/user-dirs.conf"
}

install_feh() {
	link "$DOTFILES/feh/themes" "$CONFIG/feh/themes"
	command -v feh >/dev/null 2>&1 || warn "feh is not installed"
}

install_x() {
	link "$DOTFILES/x/xinitrc" "$HOME/.xinitrc"
	for tool in xsetroot xset xss-lock; do
		command -v $tool >/dev/null 2>&1 || warn "$tool is not installed (try ./install.sh --deps)"
	done
	command -v loginctl >/dev/null 2>&1 ||
		warn "loginctl not found: Mod+Shift+s (suspend) needs elogind or systemd"
}

install_tmux() {
	link "$DOTFILES/tmux/tmux.conf" "$CONFIG/tmux/tmux.conf"
	command -v tmux >/dev/null 2>&1 || warn "tmux is not installed"
}

install_flameshot() {
	link "$DOTFILES/flameshot/flameshot.ini" "$CONFIG/flameshot/flameshot.ini"
	command -v flameshot >/dev/null 2>&1 || warn "flameshot is not installed"
}

install_mpv() {
	link "$DOTFILES/mpv" "$CONFIG/mpv"
	command -v mpv >/dev/null 2>&1 || warn "mpv is not installed"
}

install_sioyek() {
	link "$DOTFILES/sioyek/prefs_user.config" "$CONFIG/sioyek/prefs_user.config"
	command -v sioyek >/dev/null 2>&1 || warn "sioyek is not installed"
}

install_wiremix() {
	link "$DOTFILES/wiremix/wiremix.toml" "$CONFIG/wiremix/wiremix.toml"
	command -v wiremix >/dev/null 2>&1 || warn "wiremix is not installed"
}

install_btop() {
	link "$DOTFILES/btop/btop.conf" "$CONFIG/btop/btop.conf"
	link "$DOTFILES/btop/themes/habamax.theme" "$CONFIG/btop/themes/habamax.theme"
	command -v btop >/dev/null 2>&1 || warn "btop is not installed"
}

# suckless: build and install as root; make clean leaves nothing behind.
build_suckless() {
	name=$1
	say "build   $name"
	command -v make >/dev/null 2>&1 || die "make not found (try ./install.sh --deps)"
	( cd "$DOTFILES/$name" && make PREFIX="$PREFIX" && make PREFIX="$PREFIX" install )
	( cd "$DOTFILES/$name" && make clean >/dev/null )
	# PREFIX=~/.local and the like: the installed files belong to the user
	case $PREFIX in "$HOME"/*) own "$PREFIX/bin/$name" ;; esac
	say "install $name -> $PREFIX/bin/$name"
}

install_dwm()   { build_suckless dwm; }
# st's font2 falls back to Symbols Nerd Font Mono for icons and symbols
# Liberation Mono lacks. It goes into your home folder, nothing needs root;
# delete the folder and run ./install.sh st again to get the newest release.
NERD_FONTS=$DATA/fonts/NerdFontsSymbolsOnly

install_st() {
	build_suckless st
	if [ -f "$NERD_FONTS/SymbolsNerdFontMono-Regular.ttf" ]; then
		say "ok      $NERD_FONTS"
	else
		url=https://github.com/ryanoasis/nerd-fonts/releases/latest/download/NerdFontsSymbolsOnly.tar.xz
		say "fetch   Symbols Nerd Font -> $NERD_FONTS"
		userdir "${NERD_FONTS%/*}"
		rm -rf "$NERD_FONTS.tmp" && mkdir -p "$NERD_FONTS.tmp"
		if curl -fL "$url" | tar -xJ -C "$NERD_FONTS.tmp"; then
			rm -rf "$NERD_FONTS" && mv "$NERD_FONTS.tmp" "$NERD_FONTS"
			own "$NERD_FONTS"
		else
			rm -rf "$NERD_FONTS.tmp"
			warn "could not download Symbols Nerd Font; st still works without it"
		fi
	fi
	if command -v fc-cache >/dev/null 2>&1; then
		fc-cache "${NERD_FONTS%/*}"
	else
		warn "fc-cache not found (fontconfig): log in again before st sees the font"
	fi
}
install_dmenu() {
	build_suckless dmenu
	link "$DOTFILES/dmenu/apps" "$CONFIG/dmenu/apps"
}

install_slock() {
	# slock drops privileges to the group in slock/config.h ("nobody").
	if command -v getent >/dev/null 2>&1 && ! getent group nobody >/dev/null; then
		warn "group 'nobody' does not exist here; edit 'group' in slock/config.h"
		warn "(Debian and Ubuntu call it 'nogroup')"
	fi
	build_suckless slock
}

# Moonlander: everything goes into your home folder, nothing needs root.
#   ~/.local/share/qmk/zsa_qmk   ZSA's QMK source (firmware25), with qmk.patch
#   ~/.local/share/qmk/venv      Python environment for the qmk command
#   ~/.local/share/qmk/arm-gnu-toolchain   ARM compiler, if none is installed
#   ~/.local/bin/qmk, arm-none-eabi-*      symlinks to the two above
# Each run copies moonlander/canary into QMK fresh (like update.sh did) and
# builds and flashes. The built .bin is also copied to moonlander/.
QMK_DIR=$DATA/qmk
QMK_BIN=$HOME/.local/bin
QMK_BRANCH=firmware25
# Alice's Moonlander is revision B (shipped since December 2025). Rev B has
# its own bootloader ("Moonlander Bootloader", USB 3297:2003) that starts
# the firmware at 0x08002000; rev A's is ST's (0483:df11, at 0x08000000).
# Flashing one revision's firmware on the other doesn't work.
QMK_TARGET=zsa/moonlander/revb:canary
ML_BIN=zsa_moonlander_revb_canary.bin
ML_DFU_ID=3297:2003
ML_DFU_ADDR=0x08002000
ARM_VERSION=14.2.rel1

install_moonlander() {
	fw=$QMK_DIR/zsa_qmk
	userdir "$QMK_DIR"
	userdir "$QMK_BIN"
	# whatever happens below, everything made here belongs to the user
	trap 'own "$QMK_DIR"; [ ! -e "$CONFIG/qmk" ] || own "$CONFIG/qmk"' EXIT
	command -v git >/dev/null 2>&1 || die "git not found (try ./install.sh --deps)"
	command -v python3 >/dev/null 2>&1 || die "python3 not found"

	# 1. ZSA's QMK source, only the latest commit to keep it small.
	if [ ! -d "$fw/.git" ]; then
		say "clone   zsa/qmk_firmware ($QMK_BRANCH) -> $fw"
		git clone --depth 1 --branch "$QMK_BRANCH" --recurse-submodules \
			--shallow-submodules https://github.com/zsa/qmk_firmware "$fw"
	fi

	# 2. QMK patch for arbitrary_keycode and the audio timer (applied once).
	# root works in the user's clone (git apply, QMK's make): tell git,
	# and every git QMK runs, that this is fine
	export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0="$fw"
	if git -C "$fw" apply --reverse --check "$DOTFILES/moonlander/qmk.patch" 2>/dev/null; then
		say "ok      qmk.patch already applied"
	else
		git -C "$fw" apply "$DOTFILES/moonlander/qmk.patch"
		say "patch   qmk.patch -> $fw"
	fi

	# 3. qmk command in its own Python environment.
	if [ ! -x "$QMK_DIR/venv/bin/qmk" ]; then
		say "python  qmk -> $QMK_DIR/venv"
		python3 -m venv "$QMK_DIR/venv"
		"$QMK_DIR/venv/bin/python" -m pip install --quiet qmk -r "$fw/requirements.txt"
	fi
	link "$QMK_DIR/venv/bin/qmk" "$QMK_BIN/qmk"
	"$QMK_DIR/venv/bin/qmk" config user.qmk_home="$fw" >/dev/null 2>&1

	# 4. ARM compiler: always our own pinned Arm GNU Toolchain, even when the
	# system has one. Gentoo's gcc 16 built firmware that froze the
	# keyboard after a few key presses (2026-11-11); gcc 13 built it fine.
	# It is linked into ~/.local/bin, which comes first in the build's PATH.
	{
		arm=$QMK_DIR/arm-gnu-toolchain
		if [ ! -x "$arm/bin/arm-none-eabi-gcc" ]; then
			name=arm-gnu-toolchain-$ARM_VERSION-$(uname -m)-arm-none-eabi
			url=https://developer.arm.com/-/media/Files/downloads/gnu/$ARM_VERSION/binrel/$name.tar.xz
			say "fetch   $name"
			rm -rf "$arm" "$arm.tmp" && mkdir -p "$arm.tmp"
			curl -fL "$url" | tar -xJ -C "$arm.tmp" --strip-components=1
			mv "$arm.tmp" "$arm"
		fi
		for tool in "$arm"/bin/arm-none-eabi-*; do
			link "$tool" "$QMK_BIN/$(basename "$tool")"
		done
	}

	# 4b. ZSA's rev B code moves the stack pointer with an asm "g"
	# operand; gcc 16 (Gentoo's arm-none-eabi-gcc) then picks a memory
	# operand that msr can't take ("Thumb encoding does not support an
	# immediate here"). "r" (a register) is what msr needs, on every gcc.
	ml=$fw/keyboards/zsa/moonlander/moonlander.c
	if grep -q '"msr msp, %0" ::"g"' "$ml"; then
		sed -i 's/"msr msp, %0" ::"g"/"msr msp, %0" ::"r"/' "$ml"
		say "fix     msr operand in $ml (for gcc 16)"
	fi

	# 5. Keymap: a fresh copy of moonlander/canary.
	km=$fw/keyboards/zsa/moonlander/keymaps/canary
	rm -rf "$km"
	cp -R "$DOTFILES/moonlander/canary" "$km"
	say "copy    moonlander/canary -> $km"

	# 6. Build.
	# dfu-suffix (from dfu-util) only adds a small DFU trailer to the .bin.
	nosuffix=
	if ! command -v dfu-suffix >/dev/null 2>&1; then
		warn "dfu-suffix not found (package dfu-util): building without the DFU trailer"
		nosuffix=DFU_SUFFIX=true
	fi
	say "build   $QMK_TARGET"
	# Build as the user, not root: the QMK folder is theirs, and git (which
	# QMK asks for its version string) refuses repositories owned by someone
	# else when run as root ("dubious ownership"), which breaks the build.
	su -s /bin/sh "$USER_NAME" -c "cd '$fw' && PATH='$QMK_DIR/venv/bin:$QMK_BIN:$PATH' make '$QMK_TARGET' $nosuffix"
	cp "$fw/$ML_BIN" "$DOTFILES/moonlander/"
	own "$DOTFILES/moonlander/$ML_BIN"
	say "built   moonlander/$ML_BIN"

	# 7. Flash, with dfu-util (root can reach the keyboard without udev
	# rules). Waits up to 2 minutes for the keyboard to be in flashing mode;
	# if it never shows up, the .bin is still there for Keymapp.
	# MOONLANDER_FLASH=no ./install.sh moonlander  builds without flashing.
	[ "${MOONLANDER_FLASH:-yes}" = no ] && return 0
	if ! command -v dfu-util >/dev/null 2>&1; then
		warn "dfu-util not found (try ./install.sh --deps): flash the .bin with Keymapp"
		return 0
	fi
	say "flash   put the Moonlander in flashing mode now:"
	say "        tap the far-left lower key (backlight layer), then BOOT"
	say "        (the key next to Ctrl), or press the reset pin hole"
	i=0
	until dfu-util -l 2>/dev/null | grep -q "$ML_DFU_ID"; do
		i=$((i + 1))
		if [ "$i" -gt 120 ]; then
			warn "no keyboard in flashing mode after 2 minutes: flash the .bin with Keymapp, or run ./install.sh moonlander again"
			return 0
		fi
		sleep 1
	done
	if dfu-util -a 0 -d "$ML_DFU_ID" -s "$ML_DFU_ADDR:leave" -D "$DOTFILES/moonlander/$ML_BIN"; then
		say "flashed the Moonlander"
	else
		warn "dfu-util reported an error; if the keyboard doesn't restart, flash the .bin with Keymapp"
	fi

	case ":$PATH:" in *":$QMK_BIN:"*) ;; *) warn "add $QMK_BIN to PATH to run qmk yourself" ;; esac
}

# --- main -----------------------------------------------------------------

want_deps=no
parts=
for arg in "$@"; do
	case $arg in
	--deps) want_deps=yes ;;
	--list) echo "$PARTS"; exit 0 ;;
	-h|--help) sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
	-*) die "unknown option: $arg" ;;
	*)
		case " $PARTS " in *" $arg "*) ;; *) die "unknown part: $arg (see --list)" ;; esac
		parts="$parts $arg" ;;
	esac
done
[ -n "$parts" ] || parts=$PARTS

[ "$want_deps" = yes ] && deps
for p in $parts; do "install_$p"; done
# The user's files are theirs alone, like their umask 077 makes new ones:
# take group and other permissions off everything in their home.
chmod 700 "$HOME"
n=$(find "$HOME" ! -type l -perm /077 -print -exec chmod go= {} + | wc -l)
[ "$n" -eq 0 ] || say "private $n files and folders in $HOME (only $USER_NAME and root)"
say "done"
