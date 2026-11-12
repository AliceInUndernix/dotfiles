#!/bin/sh
# Install Alice's configuration on a fresh Linux system. Run it as
# yourself; only a few steps need root, and then it says so and stops
# (run those with su -c, never sudo or doas):
#   --deps   system packages          slock   must be root's to lock
#   ksh      making ksh the login shell, and root's copies of the files
#   plover   adding you to the serial port's group (dialout), once
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
# never deleted. Every program it installs goes into the user's
# ~/.system (commands in ~/.system/bin, first in PATH); nothing goes to
# /usr/local. POSIX sh, no bashisms, so it runs on any distro.
# Each run is logged, with facts about the system, to
# ~/.system/state/install/latest.log: paste it to Claude to check a run.

set -eu
# What gets installed system-wide must be readable by everyone, whatever
# umask the shell that started us has (the user's is 077).
umask 022

DOTFILES=$(cd "$(dirname "$0")" && pwd)
STAMP=$(date +%Y%m%d-%H%M%S)
PARTS="ksh xdg x nvim tmux flameshot mpv sioyek wiremix btop feh dwm st dmenu slock moonlander plover"

say()  { printf '==> %s\n' "$*"; }
warn() { printf '!!  %s\n' "$*" >&2; }
die()  { warn "$*"; exit 1; }

# The user the configs are for: you, or as root INSTALL_USER, else
# whoever owns this repo.
if [ "$(id -u)" -eq 0 ]; then
	AS_ROOT=yes
	USER_NAME=${INSTALL_USER:-$(stat -c %U "$DOTFILES" 2>/dev/null || stat -f %Su "$DOTFILES")}
	[ "$USER_NAME" != root ] ||
		die "this repo belongs to root: say whose configs these are, e.g. INSTALL_USER=alice ./install.sh"
else
	AS_ROOT=no
	USER_NAME=$(id -un)
fi
pw=$(getent passwd "$USER_NAME" 2>/dev/null || grep "^$USER_NAME:" /etc/passwd) ||
	die "no such user: $USER_NAME"
USER_GROUP=$(id -gn "$USER_NAME")
pw=${pw%:*}
HOME=${pw##*:}
[ -d "$HOME" ] || die "home folder $HOME of $USER_NAME does not exist"
SYSTEM=$HOME/.system          # all installed programs live here
SYSBIN=$SYSTEM/bin            # ...their commands here (first in PATH)
# Where programs keep their files, the same as ~/.profile sets for the
# login: settings in ~/.config, everything else in ~/.system (not ~/.local
# or ~/.cache). Exported, so what this script runs (nvim, pip, fc-cache)
# uses them too, and root's own settings don't leak in.
CONFIG=$HOME/.config
DATA=$SYSTEM/share
STATE=$SYSTEM/state
CACHE=$SYSTEM/cache
export XDG_CONFIG_HOME="$CONFIG" XDG_DATA_HOME="$DATA" \
	XDG_STATE_HOME="$STATE" XDG_CACHE_HOME="$CACHE"
PREFIX=${PREFIX:-$SYSTEM}     # suckless tools: PREFIX/bin = ~/.system/bin
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

# The package managers run as root with root's own home and settings,
# not the user's: otherwise eselect, emerge and pip leave root's caches in
# the user's ~/.system (eselect-repo, 2026-11-12). A subshell, so the
# rest of the script keeps the user's.
deps() (
	root_home=$(getent passwd root 2>/dev/null || grep '^root:' /etc/passwd)
	root_home=${root_home%:*}; HOME=${root_home##*:}
	unset XDG_CONFIG_HOME XDG_DATA_HOME XDG_STATE_HOME XDG_CACHE_HOME
	id=unknown
	[ -r /etc/os-release ] && id=$(. /etc/os-release; echo "${ID_LIKE:-} ${ID:-}")
	case $id in
	*gentoo*)
		# enable_overlay <name>: add an overlay with eselect repository.
		enable_overlay() {
			portageq get_repo_path / "$1" >/dev/null 2>&1 && return
			say "enable  overlay $1"
			emerge -qav --noreplace app-eselect/eselect-repository
			eselect repository enable "$1"
			emerge --sync "$1"
		}
		# wiremix and the two browsers are only in testing (~amd64): allow
		# just those three, in a file of our own. Sioyek is the live
		# ebuild (9999, no keywords at all), so it needs "**".
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
		if ! grep -qs "^app-text/sioyek[[:space:]]" "$kw"; then
			echo "app-text/sioyek **" >> "$kw"
			say "keyword app-text/sioyek ** -> $kw"
		fi
		# PipeWire first, on its own: wiremix needs its libpipewire-0.3 to
		# build, but its ebuild doesn't say so, so emerge may build wiremix
		# before PipeWire is in.
		emerge -qav --noreplace media-video/pipewire media-video/wireplumber
		emerge -qav --noreplace x11-libs/libX11 x11-libs/libXft \
			x11-libs/libXinerama x11-libs/libXrandr x11-libs/libXext \
			media-libs/fontconfig media-libs/freetype \
			dev-vcs/git net-misc/curl app-editors/neovim app-misc/tmux \
			x11-apps/xinit x11-apps/xsetroot x11-apps/xset x11-misc/xss-lock \
			app-shells/ksh media-fonts/liberation-fonts \
			x11-misc/xclip x11-misc/xsel \
			media-gfx/flameshot media-video/mpv net-misc/yt-dlp \
			media-sound/wiremix \
			sys-process/btop media-gfx/feh \
			app-mobilephone/dfu-util
		# Sioyek: the development version (GURU's sioyek-9999, built from
		# the newest commit of its development branch), which TypeTrack
		# needs (select_current_search_match). --update --deep also updates
		# its Qt, so a newer Qt doesn't clash with the installed one. A
		# live ebuild is never "out of date", so when it is already there
		# it is built again (--oneshot: world already has it), to get the
		# newest commit every time --deps runs.
		#
		# Its qt3d needs Qt built with OpenGL (qtbase[opengl]) and the same
		# Qt version as qtbase. Updating only Sioyek's own dependencies
		# gave a slot conflict (2026-11-12): qtbase 6.11.3 for qt3d, while
		# the installed qtsvg, qttranslations, ... kept 6.11.2. So every
		# installed Qt package is updated with it, in one go (--oneshot:
		# they don't go into your world file).
		enable_overlay guru
		# Our patch for it (sioyek/gentoo-patches): its default key
		# bindings from /etc/sioyek, where the ebuild puts them (without it
		# no key works, only the mouse), and its data in ~/.system/share.
		pd=/etc/portage/patches/app-text/sioyek
		mkdir -p "$pd"
		for f in "$DOTFILES"/sioyek/gentoo-patches/*.patch; do
			[ "$(stat -c %a "$pd/dotfiles-${f##*/}" 2>/dev/null)" = 644 ] &&
				cmp -s "$f" "$pd/dotfiles-${f##*/}" && continue
			# readable by everyone: emerge reads it as user portage, and
			# your repo's files are yours alone (600)
			cp "$f" "$pd/dotfiles-${f##*/}"
			chmod 644 "$pd/dotfiles-${f##*/}"
			say "patch   ${f##*/} -> $pd (applied when Sioyek is built)"
		done
		# Portage reads package.use/ files in name order and the last one
		# wins, so ours is zz-dotfiles: an older "-opengl" elsewhere (one
		# emerge's autounmask had written into package.use/qmkfirmware,
		# 2026-11-12) can't turn it off again. Qt Quick and Qt Multimedia
		# (for TextToSpeech) must match qtbase, so they get it too.
		pu=/etc/portage/package.use
		if [ -d "$pu" ] || [ ! -e "$pu" ]; then
			mkdir -p "$pu"
			# the first version of this file was package.use/dotfiles
			if [ "$(cat "$pu/dotfiles" 2>/dev/null)" = "dev-qt/qtbase opengl" ]; then
				rm "$pu/dotfiles"
			fi
			pu=$pu/zz-dotfiles
		fi
		for atom in dev-qt/qtbase dev-qt/qtdeclarative dev-qt/qtmultimedia; do
			grep -qs "^$atom[[:space:]].*opengl" "$pu" && continue
			echo "$atom opengl" >> "$pu"
			say "use     $atom opengl -> $pu (Sioyek needs Qt with OpenGL)"
		done
		if grep -rqs -- "dev-qt/qtbase.*[[:space:]]-opengl" /etc/portage/package.use; then
			warn "another package.use file turns OpenGL off for qtbase:"
			grep -rn -- "dev-qt/qtbase.*[[:space:]]-opengl" /etc/portage/package.use >&2 || :
			warn "if emerge still wants qtbase without opengl, delete that line"
		fi
		vdb=$(portageq vdb_path)
		qt=
		for d in "$vdb"/dev-qt/*/; do
			[ -d "$d" ] || continue
			b=${d%/}; b=${b##*/}
			qt="$qt dev-qt/${b%%-[0-9]*}"
		done
		[ -z "$qt" ] || emerge -qav --oneshot --update --deep --changed-use $qt
		# The development branch also uses Qt Quick Widgets and Qt
		# TextToSpeech, which GURU's ebuild doesn't list yet (qmake:
		# "Unknown module(s) in QT: quickwidgets texttospeech",
		# 2026-11-12), so they go in first, on their own.
		emerge -qav --update --deep --changed-use --noreplace \
			dev-qt/qtdeclarative dev-qt/qtspeech
		had_sioyek=no
		portageq has_version / app-text/sioyek && had_sioyek=yes
		emerge -qav --update --deep --changed-use app-text/sioyek
		[ "$had_sioyek" = no ] || emerge -qav --oneshot app-text/sioyek
		# Browsers only with BROWSERS=yes: both build from source and take
		# hours. They are not in the main tree: IceCat comes from
		# parona-overlay, ungoogled-chromium from pf4public.
		if [ "${BROWSERS:-no}" = yes ]; then
			enable_overlay parona-overlay
			enable_overlay pf4public
			emerge -qav --noreplace www-client/icecat \
				www-client/ungoogled-chromium
		else
			say "skip    IceCat and Chromium (BROWSERS=yes ./install.sh --deps builds them)"
		fi ;;
	*debian*|*ubuntu*)
		apt-get install build-essential pkg-config libx11-dev \
			libxft-dev libxinerama-dev libxrandr-dev libxext-dev \
			libfontconfig-dev libfreetype-dev git curl neovim tmux \
			xinit x11-xserver-utils xss-lock ksh fonts-liberation xclip xsel \
			flameshot mpv yt-dlp pipewire wireplumber wiremix btop \
			python3-venv dfu-util ;;
	*arch*)
		pacman -S --needed base-devel libx11 libxft libxinerama \
			libxrandr libxext fontconfig freetype2 git curl neovim tmux dfu-util \
			xorg-xinit xorg-xsetroot xorg-xset xss-lock ttf-liberation xclip xsel \
			flameshot mpv yt-dlp pipewire wireplumber wiremix btop ;;
	*fedora*|*rhel*)
		dnf install gcc make pkgconf-pkg-config libX11-devel \
			libXft-devel libXinerama-devel libXrandr-devel libXext-devel \
			fontconfig-devel freetype-devel git curl neovim tmux dfu-util \
			xorg-x11-xinit xsetroot xset xss-lock ksh liberation-mono-fonts \
			liberation-sans-fonts xclip xsel flameshot mpv yt-dlp pipewire wireplumber btop ;;
	*void*)
		xbps-install base-devel pkg-config libX11-devel libXft-devel \
			libXinerama-devel libXrandr-devel libXext-devel \
			fontconfig-devel freetype-devel git curl neovim tmux dfu-util \
			xinit xsetroot xset xss-lock mksh liberation-fonts-ttf xclip xsel \
			flameshot mpv yt-dlp pipewire wireplumber btop ;;
	*)
		warn "unknown distro ($id): install a C compiler, make, pkg-config,"
		warn "and headers for X11, Xft, Xinerama, Xrandr, Xext, fontconfig, freetype." ;;
	esac
)

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
			nvim --headless -u NONE -i NONE -n -c "helptags $pack/$name/doc" -c q >/dev/null 2>&1 || :
		fi
		own "$pack/$name"
		say "plugin  $name (from archive)"
	done
	# vim runs Neovim: ~/.system/bin/vim -> nvim. ~/.profile puts
	# ~/.system/bin first in PATH, ahead of everything, even /usr/bin/vim.
	if nvim=$(command -v nvim); then
		link "$nvim" "$SYSBIN/vim"
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
	if [ "$AS_ROOT" = no ]; then
		now=$(getent passwd "$USER_NAME" 2>/dev/null || grep "^$USER_NAME:" /etc/passwd)
		case ${now##*:} in
		*ksh) say "ok      shell ${now##*:} for $USER_NAME" ;;
		*) warn "your login shell is not ksh yet: su -c '$DOTFILES/install.sh ksh'" ;;
		esac
		# root's copies (made by a root run) are only refreshed by root
		root_home=$(getent passwd root 2>/dev/null || grep '^root:' /etc/passwd)
		root_home=${root_home%:*}; root_home=${root_home##*:}
		for f in profile kshrc; do
			[ ! -r "$root_home/.$f" ] || cmp -s "$DOTFILES/ksh/$f" "$root_home/.$f" ||
				warn "root's .$f is older than ksh/$f: su -c '$DOTFILES/install.sh ksh'"
		done
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

# Sioyek itself comes from the distro (on Gentoo: --deps builds the
# development version, sioyek-9999 from GURU). An old sioyek put in
# ~/.system/bin by hand (2.0.0) would win over it in PATH, so once the
# system has one, the old one is moved aside, and its data
# (~/.system/share/sioyek: bookmarks, highlights) copied to a backup
# first, in case the new version changes it.
install_sioyek() {
	link "$DOTFILES/sioyek/prefs_user.config" "$CONFIG/sioyek/prefs_user.config"
	sys=
	for d in /usr/bin /usr/local/bin; do [ -x "$d/sioyek" ] && sys=$d/sioyek && break; done
	if [ -n "$sys" ] && [ -e "$SYSBIN/sioyek" ] && [ ! -L "$SYSBIN/sioyek" ]; then
		if [ -d "$DATA/sioyek" ]; then
			cp -Rp "$DATA/sioyek" "$DATA/sioyek.bak.$STAMP"
			own "$DATA/sioyek.bak.$STAMP"
			say "backup  $DATA/sioyek -> $DATA/sioyek.bak.$STAMP"
		fi
		mv "$SYSBIN/sioyek" "$SYSBIN/sioyek.old.$STAMP"
		say "aside   $SYSBIN/sioyek -> sioyek.old.$STAMP ($sys is used now)"
	fi
	if command -v sioyek >/dev/null 2>&1 || [ -n "$sys" ]; then
		say "ok      sioyek ${sys:-$(command -v sioyek)}"
	else
		warn "sioyek is not installed (on Gentoo: su -c '$DOTFILES/install.sh --deps')"
	fi
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

# suckless: build and install into ~/.system; make clean leaves nothing behind.
build_suckless() {
	name=$1
	say "build   $name"
	command -v make >/dev/null 2>&1 || die "make not found (try ./install.sh --deps)"
	case $PREFIX in "$HOME"/*) userdir "$PREFIX/bin"; userdir "$PREFIX/share/man/man1" ;; esac
	( cd "$DOTFILES/$name" && make PREFIX="$PREFIX" && make PREFIX="$PREFIX" install )
	( cd "$DOTFILES/$name" && make clean >/dev/null )
	# In the user's home, the installed files belong to the user. Except
	# slock: it must stay root's and setuid to lock the screen (owning it
	# would drop the setuid bit). This needs a home not mounted nosuid.
	case $PREFIX in "$HOME"/*)
		find "$PREFIX/bin" "$PREFIX/share" ! -path "$PREFIX/bin/slock" \
			-exec chown -h "$USER_NAME:$USER_GROUP" {} + ;;
	esac
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
#   ~/.system/share/qmk/zsa_qmk   ZSA's QMK source (firmware25), with qmk.patch
#   ~/.system/share/qmk/venv      Python environment for the qmk command
#   ~/.system/share/qmk/arm-gnu-toolchain   ARM compiler
#   ~/.system/bin/qmk, arm-none-eabi-*     symlinks to the two above
# Each run copies moonlander/canary into QMK fresh (like update.sh did) and
# builds and flashes. The built .bin is also copied to moonlander/.
QMK_DIR=$DATA/qmk
QMK_BIN=$SYSBIN
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
	own_qmk() { own "$QMK_DIR"; [ ! -e "$CONFIG/qmk" ] || own "$CONFIG/qmk"; }
	trap own_qmk EXIT
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
	# A venv can't be moved (its scripts name their old path), so one that
	# no longer runs, e.g. after moving from ~/.local/share, is made again.
	if [ -d "$QMK_DIR/venv" ] && ! "$QMK_DIR/venv/bin/qmk" --version >/dev/null 2>&1; then
		rm -rf "$QMK_DIR/venv"
	fi
	if [ ! -x "$QMK_DIR/venv/bin/qmk" ]; then
		say "python  qmk -> $QMK_DIR/venv"
		python3 -m venv "$QMK_DIR/venv"
		PIP_NO_CACHE_DIR=1 "$QMK_DIR/venv/bin/python" -m pip install --quiet qmk -r "$fw/requirements.txt"
	fi
	link "$QMK_DIR/venv/bin/qmk" "$QMK_BIN/qmk"
	"$QMK_DIR/venv/bin/qmk" config user.qmk_home="$fw" >/dev/null 2>&1

	# 4. ARM compiler: always our own pinned Arm GNU Toolchain, even when the
	# system has one. Gentoo's gcc 16 built firmware that froze the
	# keyboard after a few key presses (2026-11-11); gcc 13 built it fine.
	# It is linked into ~/.system/bin, which comes first in the build's PATH.
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
	# The build runs as the user. Files that git wrote under the user's
	# umask 077 are 600, and root's copy keeps that mode but is root's,
	# so the user's QMK could not read keymap.c and reported "No rule to
	# make target 'canary'". The copy belongs to the user.
	own "$km"
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
	# What root cloned and unpacked above must be the user's first, or
	# the build can't write its .build folder ("Permission denied").
	if [ "$AS_ROOT" = yes ]; then
		own_qmk
		su -s /bin/sh "$USER_NAME" -c "cd '$fw' && PATH='$QMK_DIR/venv/bin:$QMK_BIN:$PATH' make '$QMK_TARGET' $nosuffix"
	else
		( cd "$fw" && PATH=$QMK_DIR/venv/bin:$QMK_BIN:$PATH make "$QMK_TARGET" $nosuffix )
	fi
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

# Plover, the steno engine for the Moonlander's steno layer. Everything is
# in ~/.system/plover, nothing needs root:
#   app/          the newest Plover release from GitHub (its AppImage,
#                 unpacked, so it runs without FUSE)
#   plover.cfg    settings (from plover/plover.cfg the first time); Plover
#                 keeps all its files next to it: plugins/, strokes.log, ...
#   moonlander/   AliceInUndernix/moonlander, where Dotterel comes from
#   ~/.system/bin/plover   starts it (plover/plover)
# Each run updates Plover to its newest release and Dotterel (with its dark
# theme) to the newest commit. Reading the keyboard's serial port needs
# your user in the port's group (dialout); a root run adds you to it.
PLOVER_DIR=$SYSTEM/plover
PLOVER_API=https://api.github.com/repos/openstenoproject/plover/releases/latest
PLOVER_REPO=https://github.com/AliceInUndernix/moonlander

install_plover() {
	# The serial port: the Moonlander is /dev/ttyACM0, group dialout (uucp
	# on Arch). Only root can add you to it; the rest runs as you.
	port_group=$(stat -c %G /dev/ttyACM0 2>/dev/null || :)
	case $port_group in ""|root) port_group=dialout ;; esac
	in_group=no
	id -nG "$USER_NAME" | tr ' ' '\n' | grep -qx "$port_group" && in_group=yes
	if [ "$AS_ROOT" = yes ]; then
		if [ "$in_group" = no ] && getent group "$port_group" >/dev/null; then
			usermod -aG "$port_group" "$USER_NAME"
			say "group   $USER_NAME in $port_group (the Moonlander's serial port; log in again)"
		fi
		su -s /bin/sh "$USER_NAME" -c "INSTALL_LOG='$INSTALL_LOG' DOTTEREL_CLEAN='${DOTTEREL_CLEAN:-no}' '$DOTFILES/install.sh' plover"
		return
	fi
	[ "$in_group" = yes ] ||
		warn "Plover can't read the Moonlander until you are in group $port_group: su -c '$DOTFILES/install.sh plover', then log in again"
	command -v curl >/dev/null 2>&1 || die "curl not found"
	command -v git >/dev/null 2>&1 || die "git not found (try ./install.sh --deps)"
	mkdir -p "$PLOVER_DIR" "$SYSBIN"

	# 1. Plover: the newest release's AppImage for this machine.
	url=$(curl -fsSL "$PLOVER_API" |
		grep -o '"browser_download_url": *"[^"]*-'"$(uname -m)"'\.AppImage"' |
		sed 's/.*"\(http[^"]*\)"$/\1/' | head -n 1) || :
	app=$PLOVER_DIR/app
	if [ -z "$url" ]; then
		[ -x "$app/AppRun" ] || die "could not find the newest Plover release ($PLOVER_API)"
		warn "could not check for a newer Plover; keeping the one in $app"
	elif [ "$(cat "$app/.release" 2>/dev/null)" = "$url" ]; then
		say "ok      Plover ${url##*/}"
	else
		say "fetch   ${url##*/}"
		rm -rf "$app.tmp" && mkdir "$app.tmp"
		curl -fL -o "$app.tmp/plover.AppImage" "$url"
		chmod 755 "$app.tmp/plover.AppImage"
		( cd "$app.tmp" && ./plover.AppImage --appimage-extract >/dev/null )
		echo "$url" > "$app.tmp/squashfs-root/.release"
		rm -rf "$app" && mv "$app.tmp/squashfs-root" "$app" && rm -rf "$app.tmp"
		say "install Plover -> $app"
	fi
	link "$DOTFILES/plover/plover" "$SYSBIN/plover"

	# 2. Settings: a copy, since Plover rewrites the file (and would replace
	# a link with a file). Yours is never overwritten.
	if [ -e "$PLOVER_DIR/plover.cfg" ]; then
		say "ok      $PLOVER_DIR/plover.cfg (kept yours)"
	else
		cp "$DOTFILES/plover/plover.cfg" "$PLOVER_DIR/plover.cfg"
		say "copy    plover/plover.cfg -> $PLOVER_DIR/plover.cfg"
	fi

	# 3. Dotterel and its dark theme, from the newest commit of the
	# moonlander repo, into Plover's own plugins folder. pip's cache is
	# off, so nothing lands in ~/.cache.
	src=$PLOVER_DIR/moonlander
	if [ -d "$src/.git" ]; then
		git -C "$src" pull --quiet --ff-only ||
			warn "could not update $src; installing Dotterel from what is there"
	else
		say "clone   $PLOVER_REPO -> $src"
		git clone --quiet --depth 1 "$PLOVER_REPO" "$src"
	fi
	# A clean install when the commit is new, the installed dictionaries
	# differ from the repo's, or DOTTEREL_CLEAN=yes: Plover is stopped,
	# Dotterel is uninstalled, every Dotterel dictionary file anywhere in
	# ~/.system/plover is deleted (no backup), the dictionary list saved in
	# plover.cfg is dropped so Plover uses the plugin's own list, and the
	# plugin is installed fresh. (Plover copies the plugin's dictionaries
	# into ~/.system/plover the first time it starts and never updates the
	# copies, so they have to go for a new Dotterel to be used.)
	commit=$(git -C "$src" rev-parse HEAD)
	new=$src/plover-dotterel/plover_dotterel/dictionaries
	old=$(dotterel_dicts)
	if [ "${DOTTEREL_CLEAN:-no}" != yes ] &&
		[ "$(cat "$PLOVER_DIR/plugins/.dotterel" 2>/dev/null)" = "$commit" ] &&
		[ -n "$old" ] && diff -rq -x __pycache__ "$new" "$old" >/dev/null 2>&1; then
		say "ok      Dotterel ($(git -C "$src" log -1 --format=%h))"
	else
		if pkill -u "$USER_NAME" -f "python.* -m plover\.scripts" 2>/dev/null; then
			sleep 2
			say "stop    Plover (start it again when this is done)"
		fi
		PIP_NO_CACHE_DIR=1 "$SYSBIN/plover" -s plover_plugins uninstall -y \
			plover-dotterel plover-dotterel-dark-theme >/dev/null 2>&1 || :
		find "$PLOVER_DIR/plugins" -depth -name 'plover_dotterel*' -exec rm -rf {} + 2>/dev/null || :
		find "$PLOVER_DIR" -path "$src" -prune -o -type f \
			\( -name 'dotterel-*.json' -o -name 'dotterel-*.py' -o -name 'dotterel-*.modal' \) \
			-print -exec rm -f {} + 2>/dev/null | sed 's/^/==> delete  /'
		rm -rf "$STATE/plover"/dictionaries-*
		cfg=$PLOVER_DIR/plover.cfg
		if [ -f "$cfg" ] && grep -q '^dictionaries *=' "$cfg"; then
			awk '/^\[/ { sec = $0 } !(sec == "[System: Dotterel]" && /^dictionaries *=/)' \
				"$cfg" > "$cfg.tmp" && mv "$cfg.tmp" "$cfg"
			say "reset   Dotterel's dictionary list in plover.cfg (Plover uses the plugin's)"
		fi
		say "plugin  Dotterel and its dark theme, fresh ($(git -C "$src" log -1 --format=%h))"
		PIP_NO_CACHE_DIR=1 "$SYSBIN/plover" -s plover_plugins install \
			"$src/plover-dotterel" "$src/plover-dotterel/dark-theme"
		old=$(dotterel_dicts)
		if [ -n "$old" ] && diff -rq -x __pycache__ "$new" "$old" >/dev/null 2>&1; then
			echo "$commit" > "$PLOVER_DIR/plugins/.dotterel"
			say "ok      Dotterel dictionaries match the repo; start Plover again"
		else
			warn "Dotterel's installed dictionaries differ from $new"
		fi
	fi

	# Any other dictionary list in plover.cfg that still names Dotterel
	# files outside the plugin (another system's section, say).
	if grep '^dictionaries *=' "$PLOVER_DIR/plover.cfg" 2>/dev/null |
		grep -o '"path": *"[^"]*"' | grep -v 'asset:plover_dotterel:' | grep -q dotterel; then
		warn "plover.cfg still lists Dotterel dictionaries outside the plugin"
	fi
}

# The installed plugin's dictionaries folder, or nothing.
dotterel_dicts() {
	find "$PLOVER_DIR/plugins" -type d -path '*/plover_dotterel/dictionaries' 2>/dev/null | head -n 1
}

# --- main -----------------------------------------------------------------

want_deps=no
parts=
for arg in "$@"; do
	case $arg in
	--deps) want_deps=yes ;;
	--list) echo "$PARTS"; exit 0 ;;
	-h|--help) sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
	-*) die "unknown option: $arg" ;;
	*)
		case " $PARTS " in *" $arg "*) ;; *) die "unknown part: $arg (see --list)" ;; esac
		parts="$parts $arg" ;;
	esac
done
named=$parts
[ -n "$parts" ] || parts=$PARTS

# The log: the script runs itself again with everything it prints also
# copied (by tee) to ~/.system/state/install/<date>.log, after a header of
# facts about the system, and ends it with the exit status. latest.log is
# the newest. A run started by this one (plover as the user) adds to it.
LOG_DIR=$STATE/install
if [ -z "${INSTALL_LOG:-}" ]; then
	userdir "$LOG_DIR"
	chmod 700 "$LOG_DIR"
	INSTALL_LOG=$LOG_DIR/$STAMP.log
	( umask 077; : > "$INSTALL_LOG" )
	ln -sfn "$STAMP.log" "$LOG_DIR/latest.log"
	own "$INSTALL_LOG" "$LOG_DIR/latest.log"
	export INSTALL_LOG
	start=$(date +%s)
	{
		echo "install.sh $* ($(date '+%Y-%m-%d %H:%M:%S %z'))"
		echo "repo     $DOTFILES $(git -c safe.directory="$DOTFILES" -C "$DOTFILES" log -1 --format='%h %cd' --date=short 2>/dev/null)" \
			"$(git --no-optional-locks -c safe.directory="$DOTFILES" -C "$DOTFILES" status --porcelain 2>/dev/null | grep -q . && echo '(uncommitted changes)')"
		echo "system   $( (. /etc/os-release; echo "$PRETTY_NAME") 2>/dev/null), $(uname -srm)"
		echo "run by   $(id -un) (uid $(id -u)), for $USER_NAME ($(id -nG "$USER_NAME")), home $HOME"
		echo "shell    $(getent passwd "$USER_NAME" 2>/dev/null | cut -d: -f7), sh is $(readlink -f /bin/sh)"
		echo "PATH     $PATH"
		found= missing=
		for t in ksh mksh git make cc pkg-config curl python3 nvim tmux xclip xsel \
			mpv flameshot sioyek wiremix btop feh dfu-util fc-cache loginctl; do
			if command -v "$t" >/dev/null 2>&1; then found="$found $t"; else missing="$missing $t"; fi
		done
		echo "found   $found"
		echo "missing $missing"
		! command -v nvim >/dev/null 2>&1 || echo "nvim     $(nvim --version | head -n 1)"
		! command -v cc >/dev/null 2>&1 || echo "cc       $(cc --version | head -n 1)"
		echo "------------------------------------------------------------------"
	} >> "$INSTALL_LOG" 2>&1
	exec 3>&1
	rc=$( { { /bin/sh "$DOTFILES/install.sh" "$@" 2>&1 || echo $? >&4; } |
		tee -a "$INSTALL_LOG" >&3; } 4>&1 )
	rc=${rc:-0}
	echo "------------------------------------------------------------------" >> "$INSTALL_LOG"
	echo "exit status $rc after $(( $(date +%s) - start ))s" >> "$INSTALL_LOG"
	say "log     $LOG_DIR/latest.log"
	exit "$rc"
fi

# Now that we know what is wanted: does any of it need root?
skipped=
if [ "$AS_ROOT" = no ]; then
	[ "$want_deps" = no ] ||
		die "--deps installs system packages, which needs root: su -c '$DOTFILES/install.sh --deps'"
	case " $parts " in *" slock "*)
		[ -z "$named" ] ||
			die "slock needs root (it must be root's to lock the screen): su -c '$DOTFILES/install.sh slock'"
		parts=$(echo " $parts " | sed 's/ slock / /')
		skipped=slock ;;
	esac
elif [ "$want_deps" = no ] && [ -n "$named" ]; then
	case " $named " in *" slock "*|*" ksh "*|*" plover "*) ;; *)
		say "note    these parts don't need root; next time just run ./install.sh$named" ;;
	esac
fi

if [ "$want_deps" = yes ]; then say "part    deps ($(date +%H:%M:%S))"; deps; fi

# ~/.local/bin is no longer used: everything lives in ~/.system/bin.
# Links this script made there are dropped (they are made again in
# ~/.system/bin); anything else is moved over, never deleted. If a name
# is already taken in ~/.system/bin, the old one stays and is reported.
old_bin=$HOME/.local/bin
if [ -d "$old_bin" ] && [ ! -L "$old_bin" ]; then
	userdir "$SYSBIN"
	for f in "$old_bin"/* "$old_bin"/.[!.]*; do
		[ -e "$f" ] || [ -L "$f" ] || continue
		b=$(basename "$f")
		case $(readlink "$f" 2>/dev/null) in
		*/nvim|"$HOME"/.local/share/qmk/*) rm "$f"; continue ;;
		esac
		if [ -e "$SYSBIN/$b" ] || [ -L "$SYSBIN/$b" ]; then
			warn "kept    $f ($SYSBIN/$b already exists)"
		else
			mv "$f" "$SYSBIN/$b"
			say "move    $f -> $SYSBIN/$b"
		fi
	done
	rmdir "$old_bin" 2>/dev/null && say "remove  $old_bin (now empty)" || :
fi

# ~/.local/share, ~/.local/state and ~/.cache are no longer used: their
# contents move into ~/.system/share, state and cache (the XDG folders
# ~/.profile sets), and so does ~/.ksh_history. Nothing is deleted; a
# name already taken in the new place is kept where it was and reported.
# moveinto <old dir> <new dir>: move every entry, merging folders. Each
# file is listed in ~/.system/state/install/<date>-moved.txt; the screen
# and the log only get how many (a browser's cache is thousands of files).
MOVED=$LOG_DIR/$STAMP-moved.txt
moveinto_r() {
	userdir "$2"
	for f in "$1"/* "$1"/.[!.]*; do
		[ -e "$f" ] || [ -L "$f" ] || continue
		t=$2/$(basename "$f")
		if [ ! -e "$t" ] && [ ! -L "$t" ]; then
			mv "$f" "$t"
			echo "move  $f -> $t" >> "$MOVED"
		elif [ -d "$f" ] && [ ! -L "$f" ] && [ -d "$t" ] && [ ! -L "$t" ]; then
			( moveinto_r "$f" "$t" )
		else
			echo "kept  $f ($t already exists)" >> "$MOVED"
		fi
	done
	rmdir "$1" 2>/dev/null || :
}
moveinto() {
	[ -d "$1" ] && [ ! -L "$1" ] || return 0
	before=$(cat "$MOVED" 2>/dev/null | wc -l)
	moveinto_r "$1" "$2"
	[ -e "$MOVED" ] || return 0
	own "$MOVED"
	m=$(tail -n +$((before + 1)) "$MOVED" | grep -c '^move') || :
	k=$(tail -n +$((before + 1)) "$MOVED" | grep -c '^kept') || :
	[ "$m" -eq 0 ] || say "move    $m entries $1 -> $2 (list: $MOVED)"
	[ "$k" -eq 0 ] ||
		warn "kept    $k entries in $1: already in $2 (list: $MOVED)"
}
moveinto "$HOME/.local/share" "$DATA"
moveinto "$HOME/.local/state" "$STATE"
moveinto "$HOME/.cache" "$CACHE"
rmdir "$HOME/.local" 2>/dev/null && say "remove  $HOME/.local (now empty)" || :
if [ -f "$HOME/.ksh_history" ] && [ ! -e "$STATE/ksh/history" ]; then
	userdir "$STATE/ksh"
	mv "$HOME/.ksh_history" "$STATE/ksh/history"
	say "move    $HOME/.ksh_history -> $STATE/ksh/history"
fi
# Links in ~/.system/bin into the old ~/.local/share now point at the
# new place (QMK's toolchain).
for f in "$SYSBIN"/*; do
	[ -L "$f" ] || continue
	case $(readlink "$f") in "$HOME"/.local/share/*)
		ln -sfn "$DATA/$(readlink "$f" | sed "s|^$HOME/.local/share/||")" "$f"
		own "$f" ;;
	esac
done

for p in $parts; do say "part    $p ($(date +%H:%M:%S))"; "install_$p"; done
# A root run leaves files of root's behind where programs it ran wrote
# their caches and state (fc-cache, pip, nvim): they belong to the user,
# or the user's programs can't update them. slock stays root's.
if [ "$AS_ROOT" = yes ]; then
	n=$(find "$SYSTEM" "$CONFIG" -xdev ! -path "$SYSBIN/slock" ! -user "$USER_NAME" \
		-print -exec chown -h "$USER_NAME:$USER_GROUP" {} + 2>/dev/null | wc -l)
	[ "$n" -eq 0 ] || say "owner   $n files root made in $SYSTEM and $CONFIG -> $USER_NAME"
fi
# The user's files are theirs alone, like their umask 077 makes new ones:
# take group and other permissions off everything in their home.
chmod 700 "$HOME"
# slock is left alone: it is root's, so you could no longer run it.
n=$(find "$HOME" ! -type l ! -path "$SYSBIN/slock" -user "$USER_NAME" \
	-perm /077 -print -exec chmod go= {} + | wc -l)
[ "$n" -eq 0 ] || say "private $n files and folders in $HOME (only $USER_NAME and root)"
[ -z "$skipped" ] ||
	say "skipped $skipped (needs root): su -c '$DOTFILES/install.sh $skipped'"
say "done"
