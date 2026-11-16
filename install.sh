#!/bin/sh
# Install the owner's configuration on a fresh Linux system. Run it as
# yourself; only a few steps need root, and then it says so and stops
# (run those with su -c, never sudo or doas):
#   --deps   system packages          slock   must be root's to lock
#   ksh      making ksh the login shell, and root's copies of the files
#   plover, steno   adding you to the serial port's group (dialout),
#            once
#   net      Riseup VPN, Tor and the kill switch (Gentoo with OpenRC;
#            net/README)
#   icecat, chromium   the browsers (emerge)
#
#   ./install.sh               everything that needs no root, as "all":
#                              the configs for what is there, no questions
#   ./install.sh 1 4 19        the parts with those numbers (0: --deps;
#                              ./install.sh --list shows the order)
#   ./install.sh --new-pc      a new computer, as root: --deps, all (net
#                              and slock too) and IceCat; takes hours
#   ./install.sh nvim dwm      only the parts named
#   ./install.sh all           everything that needs no root (as no argument)
#   ./install.sh all icecat    everything, and the parts named too
#   ./install.sh check [part]  only look: what is installed, missing or
#                              out of date (changes nothing)
#   ./install.sh update        the newest version: git pull, then all,
#                              then check (as root: the root parts too)
#   ./install.sh --deps        first install build dependencies with the
#                              distro's package manager (emerge -qv)
#   ./install.sh -a --deps     the same, but emerge asks first (-qva)
#   ./install.sh --list        show the parts
#   ./install.sh icecat chromium   the browsers IceCat and
#                              ungoogled-chromium (20, 21; Gentoo, as root,
#                              from source, takes hours; not in "all")
#
# The configs are for the user who owns this repo directory (or INSTALL_USER=
# name ./install.sh). Everything put in that user's home belongs to them,
# not to root. Configs are symlinked, so editing them in ~/.config edits
# this repo. Anything already in the way is moved to <name>.bak.<date>,
# never deleted. Every program it installs goes into the user's
# ~/.system (commands in ~/.system/bin, first in PATH); nothing goes to
# /usr/local (but net: the VPN is the system's, see net/README).
# POSIX sh, no bashisms, so it runs on any distro.
# Each run is logged, with facts about the system, to
# ~/.system/state/install/latest.log: paste it to Claude to check a run.

set -eu
# What gets installed system-wide must be readable by everyone, whatever
# umask the shell that started us has (the user's is 077).
umask 022

DOTFILES=$(cd "$(dirname "$0")" && pwd)
STAMP=$(date +%Y%m%d-%H%M%S)
PARTS="ksh xdg x nvim tmux flameshot mpv sioyek wiremix btop feh dwm st dmenu slock webarchiver moonlander plover net icecat chromium clean seal steno"
# only when named (or numbered): not in "all" or without arguments
ONLY_NAMED="icecat chromium"

say()  { printf '==> %s\n' "$*"; }
warn() { printf '!!  %s\n' "$*" >&2; }
die()  { warn "$*"; exit 1; }

# The user the configs are for: you, or as root INSTALL_USER, else
# whoever owns this repo.
if [ "$(id -u)" -eq 0 ]; then
	AS_ROOT=yes
	USER_NAME=${INSTALL_USER:-$(stat -c %U "$DOTFILES" 2>/dev/null || stat -f %Su "$DOTFILES")}
	[ "$USER_NAME" != root ] ||
		die "this repo belongs to root: say whose configs these are, e.g. INSTALL_USER=user ./install.sh"
else
	AS_ROOT=no
	USER_NAME=$(id -un)
fi
pw=$(getent passwd "$USER_NAME" 2>/dev/null || grep "^$USER_NAME:" /etc/passwd) ||
	die "no such user: $USER_NAME"
USER_GROUP=$(id -gn "$USER_NAME")
pw=${pw%:*}
HOME=${pw##*:}
[ -d "$HOME" ] || die "home directory $HOME of $USER_NAME does not exist"
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

# userdir <dir>: mkdir -p, with every directory it creates owned by the user.
userdir() {
	[ -d "$1" ] && return
	userdir "$(dirname "$1")"
	mkdir "$1"
	chown "$USER_NAME:$USER_GROUP" "$1"
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
# the user's ~/.system (eselect-repo, 2026-11-12). Called first in a
# subshell, so the rest of the script keeps the user's.
root_env() {
	root_home=$(getent passwd root 2>/dev/null || grep '^root:' /etc/passwd)
	root_home=${root_home%:*}; HOME=${root_home##*:}
	unset XDG_CONFIG_HOME XDG_DATA_HOME XDG_STATE_HOME XDG_CACHE_HOME
}

# enable_overlay <name>: add a Gentoo overlay with eselect repository.
enable_overlay() {
	portageq get_repo_path / "$1" >/dev/null 2>&1 && return
	say "enable  overlay $1"
	emerge $EMERGE_OPTS --noreplace app-eselect/eselect-repository
	eselect repository enable "$1"
	emerge $EMERGE_OPTS --sync "$1"
}

# keyword <atom> [keyword]: allow a package from testing (~amd64, or the
# keyword given), in a file of our own.
keyword() {
	kw=/etc/portage/package.accept_keywords
	if [ -d "$kw" ] || [ ! -e "$kw" ]; then
		mkdir -p "$kw"
		kw=$kw/dotfiles
	fi
	grep -qs "^$1[[:space:]]" "$kw" && return
	k=${2:-~$(portageq envvar ARCH)}
	echo "$1 $k" >> "$kw"
	say "keyword $1 $k -> $kw"
}

deps() (
	root_env
	id=unknown
	[ -r /etc/os-release ] && id=$(. /etc/os-release; echo "${ID_LIKE:-} ${ID:-}")
	case $id in
	*gentoo*)
		# wiremix is only in testing (~amd64). Sioyek is the live ebuild
		# (9999, no keywords at all), so it needs "**".
		keyword media-sound/wiremix
		keyword app-text/sioyek '**'
		# PipeWire first, on its own: wiremix needs its libpipewire-0.3 to
		# build, but its ebuild doesn't say so, so emerge may build wiremix
		# before PipeWire is in.
		emerge $EMERGE_OPTS --noreplace media-video/pipewire \
			media-video/wireplumber
		emerge $EMERGE_OPTS --noreplace x11-libs/libX11 \
			x11-libs/libXft \
			x11-libs/libXinerama x11-libs/libXrandr x11-libs/libXext \
			media-libs/fontconfig media-libs/freetype \
			dev-vcs/git net-misc/curl net-misc/wget app-editors/neovim app-misc/tmux \
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
		[ -z "$qt" ] || emerge $EMERGE_OPTS --oneshot --update --deep \
			--changed-use $qt
		# The development branch also uses Qt Quick Widgets and Qt
		# TextToSpeech, which GURU's ebuild doesn't list yet (qmake:
		# "Unknown module(s) in QT: quickwidgets texttospeech",
		# 2026-11-12), so they go in first, on their own.
		emerge $EMERGE_OPTS --update --deep --changed-use --noreplace \
			dev-qt/qtdeclarative dev-qt/qtspeech
		had_sioyek=no
		portageq has_version / app-text/sioyek && had_sioyek=yes
		emerge $EMERGE_OPTS --update --deep --changed-use \
			app-text/sioyek
		[ "$had_sioyek" = no ] ||
			emerge $EMERGE_OPTS --oneshot app-text/sioyek ;;
	*debian*|*ubuntu*)
		apt-get install build-essential pkg-config libx11-dev \
			libxft-dev libxinerama-dev libxrandr-dev libxext-dev \
			libfontconfig-dev libfreetype-dev git curl wget neovim tmux \
			xinit x11-xserver-utils xss-lock ksh fonts-liberation xclip xsel \
			flameshot mpv yt-dlp pipewire wireplumber wiremix btop \
			python3-venv dfu-util ;;
	*arch*)
		pacman -S --needed base-devel libx11 libxft libxinerama \
			libxrandr libxext fontconfig freetype2 git curl wget neovim tmux dfu-util \
			xorg-xinit xorg-xsetroot xorg-xset xss-lock ttf-liberation xclip xsel \
			flameshot mpv yt-dlp pipewire wireplumber wiremix btop ;;
	*fedora*|*rhel*)
		dnf install gcc make pkgconf-pkg-config libX11-devel \
			libXft-devel libXinerama-devel libXrandr-devel libXext-devel \
			fontconfig-devel freetype-devel git curl wget neovim tmux dfu-util \
			xorg-x11-xinit xsetroot xset xss-lock ksh liberation-mono-fonts \
			liberation-sans-fonts xclip xsel flameshot mpv yt-dlp pipewire wireplumber btop ;;
	*void*)
		xbps-install base-devel pkg-config libX11-devel libXft-devel \
			libXinerama-devel libXrandr-devel libXext-devel \
			fontconfig-devel freetype-devel git curl wget neovim tmux dfu-util \
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
	# Plugins the config no longer uses (nvim-treesitter: Vim's built-in
	# syntax is used instead), with what they built (parsers)
	for name in nvim-treesitter; do
		[ -e "$pack/$name" ] || continue
		rm -rf "$pack/$name"
		say "delete  $pack/$name (no longer used)"
	done
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
		warn "loginctl not found: Mod+Ctrl+Shift+z (suspend) needs elogind or systemd"
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
	# make clean also after a failed build: a root run would otherwise
	# leave object files of root's in your repo
	built=yes
	( cd "$DOTFILES/$name" && make PREFIX="$PREFIX" && make PREFIX="$PREFIX" install ) ||
		built=no
	( cd "$DOTFILES/$name" && make clean >/dev/null )
	[ "$built" = yes ] || die "building $name failed"
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

# clean: takes metadata out of files (see clean/README). Our own C
# program, built and installed like the suckless tools.
install_clean() { build_suckless clean; }

# seal: turns a directory into one encrypted file, unseal gives it back
# (see seal/README). Our own C program, built like the suckless tools;
# unseal is a link to seal.
install_seal() { build_suckless seal; }

# steno: our own steno engine, in the way of Plover, in C (see
# steno/README). Built like the suckless tools. It has its own clone of
# the dotterel repo for the dictionaries, apart from Plover's:
#   ~/.system/share/steno/dotterel   updated (git pull) every run
#   ~/.system/share/steno/user.json  the strokes you add (steno -a)
# Reading the keyboard needs your user in the serial port's group, as
# for Plover; a root run adds you. Plover stays: run one at a time.
STENO_DIR=$DATA/steno

install_steno() {
	port_group=$(stat -c %G /dev/ttyACM0 2>/dev/null || :)
	case $port_group in ""|root) port_group=dialout ;; esac
	in_group=no
	id -nG "$USER_NAME" | tr ' ' '\n' | grep -qx "$port_group" && in_group=yes
	if [ "$AS_ROOT" = yes ]; then
		if [ "$in_group" = no ] && getent group "$port_group" >/dev/null; then
			usermod -aG "$port_group" "$USER_NAME"
			say "group   $USER_NAME in $port_group (the Moonlander's serial port; log in again)"
		fi
		su -s /bin/sh "$USER_NAME" -c "INSTALL_LOG='$INSTALL_LOG' '$DOTFILES/install.sh' steno"
		return
	fi
	[ "$in_group" = yes ] ||
		warn "steno can't read the Moonlander until you are in group $port_group: su -c '$DOTFILES/install.sh steno', then log in again"
	build_suckless steno
	userdir "$CONFIG/steno"
	link "$DOTFILES/steno/config" "$CONFIG/steno/config"
	userdir "$STENO_DIR"
	command -v git >/dev/null 2>&1 || die "git not found (try ./install.sh --deps)"
	src=$STENO_DIR/dotterel
	if [ -d "$src/.git" ]; then
		git -C "$src" pull --quiet --ff-only ||
			warn "could not update $src; steno uses the dictionaries that are there"
		say "ok      $src ($(git -C "$src" log -1 --format=%h))"
	else
		say "clone   $PLOVER_REPO -> $src"
		git clone --quiet --depth 1 "$PLOVER_REPO" "$src"
	fi
}

# WebArchiver: archives whole websites (see webarchiver/README). Built
# and installed like the suckless tools; it runs wget for every download.
install_webarchiver() {
	build_suckless webarchiver
	command -v wget >/dev/null 2>&1 ||
		warn "wget is not installed; webarchiver needs it (su -c '$DOTFILES/install.sh --deps')"
}
# st's font2 falls back to Symbols Nerd Font Mono for icons and symbols
# Liberation Mono lacks. It goes into your home directory, nothing needs root;
# delete the directory and run ./install.sh st again to get the newest release.
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

# Moonlander: the layout and its build script live in their own repo,
# AliceInUndernix/moonlander (see its README.md), so anyone can build
# it. It is cloned into ~/.system/share/moonlander and its build.sh runs
# as the user, with everything in the user's home:
#   ~/.system/share/qmk/zsa_qmk   ZSA's QMK source (firmware25), patched
#   ~/.system/share/qmk/venv      Python environment for the qmk command
#   ~/.system/share/qmk/arm-gnu-toolchain   ARM compiler
#   ~/.system/bin/qmk, arm-none-eabi-*     links to the two above
#   ~/.system/share/moonlander/zsa_moonlander_revb_canary.bin   firmware
# Each run updates the clone, builds and flashes (as root: the user
# builds, root flashes, since root reaches the keyboard without udev
# rules). MOONLANDER_FLASH=no ./install.sh moonlander only builds.
ML_REPO=https://github.com/AliceInUndernix/moonlander
ML_SRC=$DATA/moonlander
QMK_DIR=$DATA/qmk
ML_BIN=zsa_moonlander_revb_canary.bin

# as_user <command>: run a shell command as the user (su from root).
as_user() {
	if [ "$AS_ROOT" = yes ]; then
		su -s /bin/sh "$USER_NAME" -c "$1"
	else
		sh -c "$1"
	fi
}

install_moonlander() {
	command -v git >/dev/null 2>&1 || die "git not found (try ./install.sh --deps)"
	# what a root run of the old install_moonlander made belongs to the user
	[ "$AS_ROOT" = no ] || [ ! -d "$QMK_DIR" ] || own "$QMK_DIR"
	# The layout used to be in this repo (moonlander/): only its
	# firmware, which git ignored, can be left there.
	if [ ! -e "$DOTFILES/moonlander/canary" ] && [ -d "$DOTFILES/moonlander" ]; then
		rm -f "$DOTFILES/moonlander/$ML_BIN"
		rmdir "$DOTFILES/moonlander" 2>/dev/null &&
			say "delete  $DOTFILES/moonlander (the layout is in $ML_SRC now)" || :
	fi
	if [ -d "$ML_SRC/.git" ]; then
		as_user "git -C '$ML_SRC' pull --quiet --ff-only" ||
			warn "could not update $ML_SRC; building what is there"
	else
		userdir "${ML_SRC%/*}"
		say "clone   $ML_REPO -> $ML_SRC"
		as_user "git clone --quiet '$ML_REPO' '$ML_SRC'"
	fi
	say "build   the Moonlander layout ($ML_SRC/build.sh)"
	if [ "$AS_ROOT" = yes ]; then
		as_user "QMK_DIR='$QMK_DIR' LINK_DIR='$SYSBIN' MOONLANDER_FLASH=no sh '$ML_SRC/build.sh'"
		[ "${MOONLANDER_FLASH:-yes}" = no ] || sh "$ML_SRC/build.sh" flash
	else
		QMK_DIR=$QMK_DIR LINK_DIR=$SYSBIN \
			MOONLANDER_FLASH=${MOONLANDER_FLASH:-yes} sh "$ML_SRC/build.sh"
	fi
}

# Plover, the steno engine for the Moonlander's steno layer, and
# Dotterel come from AliceInUndernix/dotterel, which installs both with
# its own install.sh (see its README.md). Here it is cloned into
# ~/.system/plover/dotterel and run with:
#   ~/.system/plover          Plover (app/), plover.cfg, its plugins and
#                             logs: it runs in portable mode there
#   ~/.system/bin/plover      starts it
# Each run updates Plover to its newest release and Dotterel to the
# newest commit. Reading the keyboard's serial port needs your user in
# the port's group (dialout); a root run adds you to it.
PLOVER_DIR=$SYSTEM/plover
PLOVER_REPO=https://github.com/AliceInUndernix/dotterel

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
	command -v git >/dev/null 2>&1 || die "git not found (try ./install.sh --deps)"
	mkdir -p "$PLOVER_DIR" "$SYSBIN"
	src=$PLOVER_DIR/dotterel
	# Dotterel used to come from the moonlander repo: that old clone goes.
	if [ -d "$PLOVER_DIR/moonlander/.git" ]; then
		rm -rf "$PLOVER_DIR/moonlander"
		say "delete  $PLOVER_DIR/moonlander (Dotterel now comes from $PLOVER_REPO)"
	fi
	if [ -d "$src/.git" ]; then
		git -C "$src" pull --quiet --ff-only ||
			warn "could not update $src; installing Dotterel from what is there"
	else
		say "clone   $PLOVER_REPO -> $src"
		git clone --quiet --depth 1 "$PLOVER_REPO" "$src"
	fi
	# Plover outside portable mode (no plover.cfg in ~/.system/plover)
	# writes to ~/.config/plover and ~/.system/share/plover: removed.
	for d in "$CONFIG/plover" "$DATA/plover"; do
		[ -e "$d" ] || continue
		rm -rf "$d"
		say "delete  $d (Plover only uses ~/.system/plover)"
	done
	rm -rf "$STATE/plover"/dictionaries-*
	PLOVER_DIR=$PLOVER_DIR BIN_DIR=$SYSBIN \
		DOTTEREL_CLEAN=${DOTTEREL_CLEAN:-no} sh "$src/install.sh"
}

# Riseup VPN, Tor and the kill switch: net/README. Gentoo with OpenRC,
# root only. What it puts or changes outside this repo:
#   /usr/local/bin/vpn and vpnauth (its password helper, setuid root for
#   group vpnctl); /etc/init.d/vpnfw (boot) and vpn (default);
#   /etc/vpn/ (settings, Riseup's list and certificate); /etc/tor/torrc,
#   /etc/proxychains.conf and .d/, /usr/local/bin/proxychains,
#   /etc/sysctl.d/vpn.conf; /etc/dhcpcd.conf (no
#   hostname, random MAC); hostname "host"; time zone UTC.
# The system's own files are backed up (<name>.bak.<date>) first.
install_net() {
	if [ ! -x /sbin/openrc-run ] || ! command -v emerge >/dev/null 2>&1; then
		warn "net is only for Gentoo with OpenRC: skipped"
		return
	fi
	# tor with seccomp, for "Sandbox 1" in torrc (zz-dotfiles: the last
	# package.use file wins); --changed-use builds it again when the
	# installed tor lacks it
	pu=/etc/portage/package.use
	if [ -d "$pu" ] || [ ! -e "$pu" ]; then
		mkdir -p "$pu"
		pu=$pu/zz-dotfiles
	fi
	if ! grep -qs '^net-vpn/tor[[:space:]].*seccomp' "$pu"; then
		echo "net-vpn/tor seccomp" >> "$pu"
		chmod 644 "$pu"
		say "use     net-vpn/tor seccomp -> $pu (Tor in its sandbox)"
	fi
	emerge $EMERGE_OPTS --noreplace --changed-use net-vpn/openvpn \
		net-vpn/tor net-firewall/nftables net-misc/proxychains
	net=$DOTFILES/net
	install -d -m 755 /usr/local/bin /etc/vpn
	# The system's files as they were before net first changed them, for
	# vpn off (level -1). An older backup of ours (.bak.<date>) is the
	# truer original, so it wins.
	install -d -m 700 /etc/vpn/orig
	for f in /etc/dhcpcd.conf /etc/conf.d/hostname /etc/hostname \
		/etc/hosts /etc/timezone /etc/conf.d/hwclock /etc/tor/torrc \
		/etc/proxychains.conf
	do
		o=/etc/vpn/orig$f
		if [ -e "$o" ] || [ -e "$o.absent" ]; then continue; fi
		mkdir -p "${o%/*}"
		old=$(ls "$f".bak.* 2>/dev/null | head -n 1) || :
		if [ -n "$old" ]; then cp -p "$old" "$o"
		elif [ -e "$f" ]; then cp -p "$f" "$o"
		else : > "$o.absent"; fi
	done
	[ -e /etc/vpn/orig/localtime.link ] ||
		readlink /etc/localtime > /etc/vpn/orig/localtime.link || :
	echo "$DOTFILES" > /etc/vpn/repo
	rm -f /etc/vpn/off
	install -m 755 "$net/vpn" /usr/local/bin/vpn
	say "install /usr/local/bin/vpn"
	# vpnctl: may run vpn's commands with their own password (vpnauth);
	# vpnallow: what "vpn run" starts, the only programs online in the
	# sandbox
	for g in vpnctl vpnallow; do
		getent group "$g" >/dev/null || { groupadd "$g"; say "group   $g"; }
		case " $(id -nG "$USER_NAME") " in *" $g "*) ;; *)
			usermod -aG "$g" "$USER_NAME"
			say "group   $USER_NAME in $g (log in again)" ;;
		esac
	done
	if ! make -s -C "$net" vpnauth >/dev/null 2>&1; then
		warn "no static libcrypt: vpnauth is built dynamically"
		warn "(for a static one: sys-libs/libxcrypt with USE=static-libs)"
		make -s -C "$net" vpnauth LDFLAGS= ||
			{ warn "vpnauth did not build; vpn will ask for root's password"; }
	fi
	if [ -f "$net/vpnauth" ]; then
		install -o root -g vpnctl -m 4750 "$net/vpnauth" /usr/local/bin/vpnauth
		say "install /usr/local/bin/vpnauth (setuid root, group vpnctl)"
	fi
	# vpntop: the screen of "vpn" (plain C, static, no libraries)
	if make -s -C "$net" vpntop >/dev/null 2>&1 ||
		make -s -C "$net" vpntop LDFLAGS= >/dev/null 2>&1
	then
		install -m 755 "$net/vpntop" /usr/local/bin/vpntop
		say "install /usr/local/bin/vpntop (the screen of vpn)"
	else
		warn "vpntop did not build; vpn uses its plain screen"
	fi
	make -s -C "$net" clean
	install -m 755 "$net/vpnfw.initd" /etc/init.d/vpnfw
	install -m 755 "$net/vpn.initd" /etc/init.d/vpn
	for f in openvpn.conf countries mirrors; do
		install -m 644 "$net/$f" "/etc/vpn/$f"
	done
	install -d -m 755 /etc/sysctl.d
	install -m 644 "$net/sysctl.conf" /etc/sysctl.d/vpn.conf
	say "install /etc/init.d/vpnfw, vpn; /etc/vpn; /etc/sysctl.d/vpn.conf"
	rootcopy "$net/proxychains.conf" /etc/proxychains.conf
	# proxychains PROGRAM: each program on a Tor port of its own
	# (9200-9299 in torrc), one config per port
	install -d -m 755 /etc/proxychains.d
	rm -f /etc/proxychains.d/[0-9]*.conf
	p=9200
	while [ $p -le 9299 ]; do
		sed "s/^socks5 127\.0\.0\.1 9050\$/socks5 127.0.0.1 $p/" \
			"$net/proxychains.conf" > "/etc/proxychains.d/$p.conf"
		chmod 644 "/etc/proxychains.d/$p.conf"
		p=$((p + 1))
	done
	[ -L /usr/local/bin/proxychains ] && rm -f /usr/local/bin/proxychains
	install -m 755 "$net/proxychains" /usr/local/bin/proxychains
	say "install /usr/local/bin/proxychains (own Tor port per program)"
	# torrc: ours, with the User and DataDirectory lines of the old one
	# (Gentoo's), or Gentoo's defaults
	keep=$(grep -E '^[[:space:]]*(User|DataDirectory)[[:space:]]' \
		/etc/tor/torrc 2>/dev/null) || :
	if [ -z "$keep" ] && [ ! -f /etc/tor/torrc ]; then
		keep=$(printf 'User tor\nDataDirectory /var/lib/tor/data')
	fi
	{
		cat "$net/torrc"
		echo
		echo "# kept from the torrc that was here before"
		echo "$keep"
	} > "$net/.torrc"
	# Sandbox 1 stops a tor built without seccomp from starting at all
	if command -v tor >/dev/null 2>&1 &&
		! tor --verify-config -f "$net/.torrc" >/dev/null 2>&1 &&
		sed '/^Sandbox 1$/d' "$net/.torrc" > "$net/.torrc2" &&
		tor --verify-config -f "$net/.torrc2" >/dev/null 2>&1
	then
		mv "$net/.torrc2" "$net/.torrc"
		warn "tor did not build with seccomp: Sandbox 1 left out for now"
		warn "(emerge -qv --changed-use net-vpn/tor, then install.sh net)"
	fi
	rm -f "$net/.torrc2"
	install -d -m 755 /etc/tor
	rootcopy "$net/.torrc" /etc/tor/torrc
	rm -f "$net/.torrc"
	d=$(echo "$keep" | sed -n 's/^[[:space:]]*DataDirectory[[:space:]]*//p')
	[ -z "$d" ] || [ -d "$d" ] || install -d -o tor -g tor -m 700 "$d"
	# dhcpcd: no hostname, no fixed client ID, and resolv.conf left alone
	# (vpn writes and locks it). The random MAC address is vpnfw's, once
	# at boot: dhcpcd's randomise_hwaddr changed it whenever the link
	# dropped, which broke Wi-Fi (2026-11-15), so it is taken out.
	f=/etc/dhcpcd.conf
	if grep -q '^randomise_hwaddr$' "$f" 2>/dev/null; then
		sed -i '/^randomise_hwaddr$/d; s/^# each time, leave/# leave/' "$f"
		say "dhcpcd  randomise_hwaddr out (it broke Wi-Fi)"
	fi
	if [ -f "$f" ] && ! grep -q '^# vpn (dotfiles' "$f"; then
		cp -p "$f" "$f.bak.$STAMP"
		sed -i 's/^hostname[[:space:]]*$/#hostname  # off: net (vpn)/' "$f"
		cat >> "$f" <<-'EOF'

		# vpn (dotfiles, net/): send no hostname or fixed ID, and leave
		# /etc/resolv.conf alone (vpn sets and locks it). Keep these last:
		# anonymous hides what comes before it.
		nohook resolv.conf
		anonymous
		EOF
		say "dhcpcd  no hostname (backup $f.bak.$STAMP)"
	fi
	v=$(dhcpcd --version 2>/dev/null | sed -n '1s/[^0-9]*\([0-9]*\).*/\1/p')
	[ "${v:-0}" -ge 9 ] 2>/dev/null ||
		warn "dhcpcd ${v:-?} is older than 9: it may send your hostname (emerge -qv dhcpcd)"
	# a hostname that says nothing
	f=/etc/conf.d/hostname
	if [ -f "$f" ] && ! grep -q '^hostname="host"' "$f"; then
		cp -p "$f" "$f.bak.$STAMP"
		if grep -q '^hostname=' "$f"; then
			sed -i 's/^hostname=.*/hostname="host"/' "$f"
		else
			echo 'hostname="host"' >> "$f"
		fi
		say "host    hostname \"host\" at next boot (backup $f.bak.$STAMP)"
	fi
	if [ -f /etc/hostname ] && [ "$(cat /etc/hostname)" != host ]; then
		cp -p /etc/hostname "/etc/hostname.bak.$STAMP"
		echo host > /etc/hostname
	fi
	if ! grep -q '^127\.0\.0\.1[[:space:]].*[[:space:]]host\([[:space:]]\|$\)' /etc/hosts; then
		cp -p /etc/hosts "/etc/hosts.bak.$STAMP"
		printf '127.0.0.1\thost\n' >> /etc/hosts
	fi
	# the clock in UTC, so programs can't tell your time zone
	if [ -f /usr/share/zoneinfo/UTC ] &&
		[ "$(readlink /etc/localtime)" != ../usr/share/zoneinfo/UTC ]
	then
		ln -sfn ../usr/share/zoneinfo/UTC /etc/localtime
		echo UTC > /etc/timezone
		say "time    zone UTC"
	fi
	f=/etc/conf.d/hwclock
	if [ -f "$f" ] && ! grep -q '^clock="UTC"' "$f"; then
		cp -p "$f" "$f.bak.$STAMP"
		sed -i 's/^clock=.*/clock="UTC"/' "$f"
		grep -q '^clock=' "$f" || echo 'clock="UTC"' >> "$f"
		say "time    hardware clock in UTC (vpn sets the time after boot)"
	fi
	# Riseup's list and a certificate, straight out while the firewall
	# isn't there yet (later vpn renews them through the VPN)
	if [ ! -s /etc/vpn/gateways ] && ! nft list table inet vpn >/dev/null 2>&1; then
		/usr/local/bin/vpn refresh --install ||
			warn "no server list yet: run vpn refresh before you reboot"
	fi
	/usr/local/bin/vpn firewall closed --check ||
		warn "this kernel can't load the kill switch: it needs nftables (NF_TABLES, NFT_NAT, NFT_REDIR, NFT_CT, NF_TABLES_INET) and TUN"
	# runlevels: the kill switch at boot, the VPN in default. Tor is
	# started by vpn after OpenVPN, so it isn't in a runlevel itself.
	for s in vpnfw:boot vpn:default; do
		rc-update show "${s#*:}" 2>/dev/null | grep -q "^ *${s%:*} |" ||
			rc-update add "${s%:*}" "${s#*:}"
	done
	for l in boot default; do
		for s in tor nftables iptables ip6tables; do
			rc-update show "$l" 2>/dev/null | grep -q "^ *$s |" || continue
			rc-update del "$s" "$l"
			say "runlevel $s out of $l (vpn handles it now)"
		done
		rc-update show "$l" 2>/dev/null | grep -o 'openvpn[^ ]*' |
			while read -r s; do
				warn "$s is in runlevel $l: vpn runs OpenVPN itself (rc-update del $s $l)"
			done
	done
	say "net     ready: reboot, or start it now with"
	say "        rc-service vpnfw start && rc-service vpn start"
}

# --- browsers --------------------------------------------------------------

# The two browsers are parts of their own, only when asked for by name or
# number (not in "all"): both build from source and take hours. Gentoo
# only, and as root. Neither is in the main tree, and both are only in
# testing (~amd64). Only the program is installed: no settings or
# profile of ours.
# browser <overlay> <atom>
browser() (
	if ! command -v emerge >/dev/null 2>&1; then
		warn "${2#*/} is only for Gentoo: skipped"
		return 0
	fi
	root_env
	enable_overlay "$1"
	keyword "$2"
	emerge $EMERGE_OPTS --noreplace "$2"
)
install_icecat()   { browser parona-overlay www-client/icecat; }
install_chromium() { browser pf4public www-client/ungoogled-chromium; }

# --- check (./install.sh check) --------------------------------------------
# Only looks, changes nothing: for each part what is installed, missing,
# out of date or not as the repo has it. Asks git whether this repo and
# Dotterel have newer commits (that needs the network). Exit status 1
# when anything needs a look; each such line says what to run.

CK_BAD=0
ck_ok() { printf '  ok   %s\n' "$*"; }
ck_bad() { printf '  !!   %s\n' "$*"; CK_BAD=$((CK_BAD + 1)); }
ck_info() { printf '  --   %s\n' "$*"; }

# ck_links <part>: the links install_<part> makes from this repo
ck_links() {
	lines=$(sed -n "/^install_$1() {/,/^}/p" "$DOTFILES/install.sh" |
		grep '^[[:space:]]*link "$DOTFILES/') || :
	[ -n "$lines" ] || return 0
	while IFS= read -r line; do
		eval "set -- ${line#*link }"
		if [ -L "$2" ] && [ "$(readlink "$2")" = "$1" ]; then
			ck_ok "$2"
		elif [ -e "$2" ] || [ -L "$2" ]; then
			ck_bad "$2 is not the repo's: ./install.sh $p"
		else
			ck_bad "$2 is missing: ./install.sh $p"
		fi
	done <<EOF
$lines
EOF
}

ck_cmd() {
	if c=$(PATH=$PATH:/usr/sbin:/sbin command -v "$1"); then
		ck_ok "$1 installed ($c)"
	else
		ck_bad "$1 is not installed: su -c '$DOTFILES/install.sh --deps'"
	fi
}

# ck_built <name>: ~/.system/bin/<name> is there and newer than its source
ck_built() {
	b=$SYSBIN/$1
	if [ ! -x "$b" ]; then
		ck_bad "$1 is not built: ./install.sh $1"
		return
	fi
	n=$(find "$DOTFILES/$1" -type f \( -name '*.[ch]' -o -name config.mk \
		-o -name Makefile \) -newer "$b" | wc -l)
	if [ "$n" -gt 0 ]; then
		ck_bad "$1 is older than its source ($n files changed): ./install.sh $1"
	else
		ck_ok "$1 built from the current source"
	fi
}

# ck_git <dir> <name>: uncommitted changes, newer commits upstream
ck_git() {
	g="git -c safe.directory=$1 -C $1"
	if ! $g rev-parse >/dev/null 2>&1; then
		ck_bad "$2 is not a git checkout ($1)"
		return
	fi
	if $g status --porcelain 2>/dev/null | grep -q .; then
		ck_info "$2 has changes not committed"
	fi
	if ! $g fetch -q 2>/dev/null; then
		ck_info "$2: could not ask upstream for newer commits (network?)"
		return
	fi
	n=$($g rev-list --count 'HEAD..@{upstream}' 2>/dev/null || echo 0)
	if [ "$n" -gt 0 ]; then
		ck_bad "$2 is $n commits behind: git -C $1 pull"
	else
		ck_ok "$2 is up to date ($($g log -1 --format=%h))"
	fi
}

ck_part() {
	case $1 in
	ksh)
		ck_links ksh
		sh_now=$(getent passwd "$USER_NAME" 2>/dev/null | cut -d: -f7)
		case $sh_now in
		*ksh) ck_ok "login shell $sh_now" ;;
		*) ck_bad "login shell is $sh_now: su -c '$DOTFILES/install.sh ksh'" ;;
		esac
		rh=$(getent passwd root 2>/dev/null | cut -d: -f6)
		rh=${rh:-/root}
		for f in profile kshrc; do
			if [ ! -r "$rh/.$f" ]; then
				ck_info "root's .$f: only root can read it (check as root)"
			elif cmp -s "$DOTFILES/ksh/$f" "$rh/.$f"; then
				ck_ok "root's .$f"
			else
				ck_bad "root's .$f is older: su -c '$DOTFILES/install.sh ksh'"
			fi
		done ;;
	nvim)
		ck_links nvim
		if command -v nvim >/dev/null 2>&1; then
			v=$(nvim --version | sed -n '1s/^NVIM v\{0,1\}//p')
			case $v in
			0.[0-9].*|0.10.*) ck_bad "Neovim $v is older than 0.11" ;;
			*) ck_ok "Neovim $v" ;;
			esac
		else
			ck_bad "nvim is not installed: su -c '$DOTFILES/install.sh --deps'"
		fi
		pack=$DATA/nvim/site/pack/plugins/opt
		for t in "$DOTFILES"/archive/nvim-plugins/*.tar.gz; do
			n=$(basename "$t" .tar.gz)
			if [ -d "$pack/$n" ]; then ck_ok "plugin $n"
			else ck_bad "plugin $n is missing: ./install.sh nvim"; fi
		done
		if command -v nvim >/dev/null 2>&1 &&
			[ "$(readlink "$SYSBIN/vim" 2>/dev/null)" = "$(command -v nvim)" ]
		then
			ck_ok "vim runs Neovim"
		else
			ck_bad "$SYSBIN/vim is not Neovim: ./install.sh nvim"
		fi ;;
	xdg|x|feh) ck_links "$1" ;;
	tmux|flameshot|mpv|sioyek|wiremix|btop)
		ck_links "$1"
		ck_cmd "$1" ;;
	dwm|st|webarchiver|clean|seal) ck_built "$1" ;;
	steno)
		ck_built steno
		ck_links steno
		if [ -d "$STENO_DIR/dotterel" ]; then ck_git "$STENO_DIR/dotterel" "steno's Dotterel"
		else ck_bad "steno has no dictionaries: ./install.sh steno"; fi ;;
	dmenu)
		ck_built dmenu
		ck_links dmenu ;;
	slock)
		ck_built slock
		if [ -u "$SYSBIN/slock" ] && [ "$(stat -c %U "$SYSBIN/slock" 2>/dev/null)" = root ]; then
			ck_ok "slock is root's and setuid (it can lock)"
		elif [ -e "$SYSBIN/slock" ]; then
			ck_bad "slock is not root's setuid: su -c '$DOTFILES/install.sh slock'"
		fi ;;
	moonlander)
		if [ -x "$SYSBIN/qmk" ]; then ck_ok "qmk"; else ck_bad "qmk is missing: ./install.sh moonlander"; fi
		if [ ! -d "$ML_SRC/.git" ]; then
			ck_bad "the layout is not cloned: ./install.sh moonlander"
		else
			ck_git "$ML_SRC" "the Moonlander layout"
			b=$ML_SRC/$ML_BIN
			if [ ! -f "$b" ]; then
				ck_bad "no firmware built: ./install.sh moonlander"
			elif [ -n "$(find "$ML_SRC/canary" -type f -newer "$b" 2>/dev/null)" ]; then
				ck_bad "the firmware is older than the layout: ./install.sh moonlander"
			else
				ck_ok "firmware $ML_BIN is from the current layout"
			fi
		fi ;;
	plover)
		if [ -d "$PLOVER_DIR/app" ]; then ck_ok "Plover in $PLOVER_DIR"
		else ck_bad "Plover is not installed: ./install.sh plover"; fi
		if [ -x "$SYSBIN/plover" ]; then ck_ok "$SYSBIN/plover"
		else ck_bad "$SYSBIN/plover is missing: ./install.sh plover"; fi
		# the same group install_plover adds you to
		g=$(stat -c %G /dev/ttyACM0 2>/dev/null || :)
		case $g in ""|root) g=dialout ;; esac
		case " $(id -nG "$USER_NAME") " in
		*" $g "*) ck_ok "$USER_NAME is in group $g (the keyboard's port)" ;;
		*) ck_bad "$USER_NAME is not in group $g: su -c '$DOTFILES/install.sh plover'" ;;
		esac
		[ ! -d "$PLOVER_DIR/dotterel" ] || ck_git "$PLOVER_DIR/dotterel" Dotterel
		ck_info "a newer Plover release: ./install.sh plover always takes the newest" ;;
	net) ck_net ;;
	icecat) ck_browser www-client/icecat icecat ;;
	chromium) ck_browser www-client/ungoogled-chromium chromium ;;
	esac
}

ck_browser() {    # ck_browser <atom>: optional, so missing is no fault
	if ! command -v portageq >/dev/null 2>&1; then
		ck_info "${1#*/}: only for Gentoo"
	elif portageq has_version / "$1"; then
		ck_ok "${1#*/} $(portageq best_version / "$1" | sed 's/.*-\([0-9]\)/\1/')"
	else
		ck_info "${1#*/} is not installed: su -c '$DOTFILES/install.sh ${2:-}'"
	fi
}

ck_same() {    # ck_same <repo file> <installed file>
	if [ ! -e "$2" ]; then
		ck_bad "$2 is missing: su -c '$DOTFILES/install.sh net'"
	elif cmp -s "$1" "$2"; then
		ck_ok "$2"
	else
		ck_bad "$2 is older than the repo's: su -c '$DOTFILES/install.sh net'"
	fi
}

ck_net() {
	if [ ! -x /sbin/openrc-run ]; then
		ck_info "not OpenRC: net is not for this system"
		return
	fi
	if [ -e /etc/vpn/off ]; then
		ck_info "net is off (vpn level -1): vpn on turns it back on"
		return
	fi
	n=$DOTFILES/net
	ck_same "$n/vpn" /usr/local/bin/vpn
	ck_same "$n/vpnfw.initd" /etc/init.d/vpnfw
	ck_same "$n/vpn.initd" /etc/init.d/vpn
	for f in openvpn.conf countries mirrors; do ck_same "$n/$f" "/etc/vpn/$f"; done
	ck_same "$n/sysctl.conf" /etc/sysctl.d/vpn.conf
	ck_same "$n/proxychains.conf" /etc/proxychains.conf
	ck_same "$n/proxychains" /usr/local/bin/proxychains
	if [ -u /usr/local/bin/vpnauth ]; then
		ck_ok "vpnauth (your password for vpn commands)"
		[ ! -f /usr/local/bin/vpnauth ] ||
			[ -z "$(find "$n/vpnauth.c" -newer /usr/local/bin/vpnauth)" ] ||
			ck_bad "vpnauth is older than vpnauth.c: su -c '$DOTFILES/install.sh net'"
	else
		ck_bad "vpnauth missing: vpn asks root's password (install.sh net)"
	fi
	if [ -x /usr/local/bin/vpntop ]; then
		ck_ok "vpntop (the screen of vpn)"
		[ -z "$(find "$n/vpntop.c" -newer /usr/local/bin/vpntop)" ] ||
			ck_bad "vpntop is older than vpntop.c: su -c '$DOTFILES/install.sh net'"
	else
		ck_bad "vpntop missing: vpn shows its plain screen (install.sh net)"
	fi
	for t in openvpn tor nft proxychains; do ck_cmd "$t"; done
	for g in vpnctl vpnallow; do
		case " $(id -nG "$USER_NAME") " in
		*" $g "*) ck_ok "$USER_NAME is in group $g" ;;
		*) ck_bad "$USER_NAME is not in group $g: su -c '$DOTFILES/install.sh net', log in again" ;;
		esac
	done
	for s in boot/vpnfw default/vpn; do
		if [ -e "/etc/runlevels/$s" ]; then ck_ok "${s#*/} in runlevel ${s%/*}"
		else ck_bad "${s#*/} is not in runlevel ${s%/*}: su -c '$DOTFILES/install.sh net'"; fi
	done
	for l in boot default; do
		[ ! -e "/etc/runlevels/$l/tor" ] ||
			ck_bad "tor is in runlevel $l, so it starts before the VPN: rc-update del tor $l"
	done
	if [ -s /etc/vpn/gateways ] && [ -s /etc/vpn/client.crt ]; then
		ck_ok "$(wc -l < /etc/vpn/gateways) Riseup servers known"
		if openssl x509 -checkend 0 -noout -in /etc/vpn/client.crt >/dev/null 2>&1; then
			ck_ok "certificate valid until $(openssl x509 -enddate -noout -in /etc/vpn/client.crt | sed 's/notAfter=//')"
		else
			ck_bad "the certificate has expired: vpn refresh"
		fi
	else
		ck_bad "no Riseup server list or certificate: vpn refresh"
	fi
	if [ -e /run/openrc/started/vpn ]; then
		read -r st lv 2>/dev/null < /run/vpn/state || st=?
		if [ "$st" = ok ]; then ck_ok "vpn running, level $lv"
		else ck_bad "vpn is $st: vpn status"; fi
	else
		ck_bad "the vpn service is not running (reboot, or rc-service vpn start)"
	fi
	# Tor's DNS, 1.1.1.1 (level 0, sandbox) or at level 1 Riseup's (the
	# one OpenVPN was given)
	rdns=$(sed -n 's/^dns=//p' /run/vpn/up 2>/dev/null)
	if [ "$(cat /etc/resolv.conf 2>/dev/null)" = "nameserver 127.0.0.1" ] ||
		[ "$(cat /etc/resolv.conf 2>/dev/null)" = "nameserver 1.1.1.1" ] ||
		{ [ -n "$rdns" ] &&
		[ "$(cat /etc/resolv.conf 2>/dev/null)" = "nameserver $rdns" ]; }
	then
		case $(lsattr -d /etc/resolv.conf 2>/dev/null) in
		*i*' '*) ck_ok "/etc/resolv.conf locked ($(cat /etc/resolv.conf))" ;;
		*) ck_bad "/etc/resolv.conf is not locked (vpn locks it when it starts)" ;;
		esac
	else
		ck_bad "/etc/resolv.conf is not vpn's (vpn sets it when it starts)"
	fi
	if grep -q '^randomise_hwaddr' /etc/dhcpcd.conf 2>/dev/null; then
		ck_bad "dhcpcd has randomise_hwaddr, which breaks Wi-Fi: su -c '$DOTFILES/install.sh net'"
	fi
	if grep -q '^anonymous' /etc/dhcpcd.conf 2>/dev/null; then ck_ok "dhcpcd: sends no hostname"
	else ck_bad "dhcpcd still sends your hostname and MAC: su -c '$DOTFILES/install.sh net'"; fi
	h=$(cat /proc/sys/kernel/hostname 2>/dev/null) || :
	if [ "$h" = host ]; then ck_ok "hostname host"
	else ck_bad "the hostname is not \"host\" yet (after the next boot)"; fi
	if [ "$(readlink /etc/localtime)" = ../usr/share/zoneinfo/UTC ]; then ck_ok "time zone UTC"
	else ck_bad "the time zone is not UTC: su -c '$DOTFILES/install.sh net'"; fi
	if [ "$(cat /proc/sys/net/ipv6/conf/all/disable_ipv6 2>/dev/null)" = 1 ]; then ck_ok "IPv6 off"
	else ck_bad "IPv6 is on (vpnfw turns it off at boot)"; fi
}

do_check() {
	echo "== repo"
	ck_git "$DOTFILES" "this repo"
	echo "== deps"
	missing=
	for t in git make cc pkg-config curl wget nvim tmux xclip xsel mpv flameshot \
		sioyek wiremix btop feh dfu-util fc-cache; do
		command -v "$t" >/dev/null 2>&1 || missing="$missing $t"
	done
	if [ -z "$missing" ]; then ck_ok "all the tools are installed"
	else ck_bad "missing:$missing: su -c '$DOTFILES/install.sh --deps'"; fi
	for p in ${1:-$PARTS}; do
		echo "== $p"
		ck_part "$p"
	done
	echo
	if [ "$CK_BAD" -eq 0 ]; then
		echo "everything is as the repo has it"
		return 0
	fi
	echo "$CK_BAD things need a look (the lines with !!)"
	return 1
}

# --- main -----------------------------------------------------------------

want_deps=no
want_all=no
parts=
EMERGE_OPTS=-qv  # -a: -qva, so emerge asks before it builds anything
# check: only look, and before anything else is done
case ${1:-} in check|--check)
	shift
	named=
	for x in "$@"; do
		case " $PARTS " in *" $x "*) named="$named $x" ;;
		*) die "unknown part: $x (see --list)" ;; esac
	done
	rc=0
	do_check "$named" || rc=$?
	exit $rc ;;
esac
# update: the newest version from git, then everything, then check. The
# pulled install.sh does the work (this one only pulls), so a new part or
# fix is used at once. git runs as the repo's owner, even from root.
case ${1:-} in update|--update)
	[ $# -eq 1 ] || die "update takes no parts: ./install.sh update"
	say "pull    $DOTFILES"
	if [ "$AS_ROOT" = yes ]; then
		su -s /bin/sh "$USER_NAME" -c "git -C '$DOTFILES' pull --ff-only" ||
			die "git pull failed (changes not committed? no network?)"
	else
		git -C "$DOTFILES" pull --ff-only ||
			die "git pull failed (changes not committed? no network?)"
	fi
	rc=0
	/bin/sh "$DOTFILES/install.sh" all || rc=$?
	echo
	say "check"
	/bin/sh "$DOTFILES/install.sh" check || rc=$?
	exit $rc ;;
esac
for arg in "$@"; do
	case $arg in
	all) want_all=yes ;;
	--deps|0) want_deps=yes ;;
	--new-pc) want_deps=yes; want_all=yes; parts="$parts icecat" ;;
	[1-9]|[1-9][0-9])
		p=$(echo $PARTS | awk -v n="$arg" '{ print $n }')
		[ -n "$p" ] || die "there is no part $arg (see ./install.sh --list)"
		parts="$parts $p" ;;
	-a) EMERGE_OPTS=-qva ;;
	--list) echo "$PARTS"; exit 0 ;;
	-h|--help) sed -n '2,/^$/p' "$0" | sed '/^$/d; s/^# \{0,1\}//'; exit 0 ;;
	-*) die "unknown option: $arg" ;;
	*)
		case " $PARTS " in *" $arg "*) ;; *) die "unknown part: $arg (see --list)" ;; esac
		parts="$parts $arg" ;;
	esac
done
named=$parts
# all (or nothing named): every part but the browsers, in the usual
# order, with any named ones after it (all icecat: everything and IceCat)
if [ -z "$parts" ] || [ "$want_all" = yes ]; then
	parts=
	for p in $PARTS; do
		case " $ONLY_NAMED " in *" $p "*) ;; *) parts="$parts $p" ;; esac
	done
	for p in $named; do
		case " $parts " in *" $p "*) ;; *) parts="$parts $p" ;; esac
	done
	[ "$want_all" = no ] || named=
fi

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
		for t in ksh mksh git make cc pkg-config curl wget python3 nvim tmux xclip xsel \
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
	case " $parts " in *" net "*)
		[ -z "$named" ] ||
			die "net sets up the system's network, which needs root: su -c '$DOTFILES/install.sh net'"
		parts=$(echo " $parts " | sed 's/ net / /')
		skipped="${skipped:+$skipped }net" ;;
	esac
	for p in $ONLY_NAMED; do
		case " $parts " in *" $p "*)
			die "$p is built with emerge, which needs root: su -c '$DOTFILES/install.sh $p'" ;;
		esac
	done
elif [ "$want_deps" = no ] && [ -n "$named" ]; then
	case " $named " in *" slock "*|*" ksh "*|*" plover "*|*" steno "*|*" net "*|*" icecat "*|*" chromium "*) ;; *)
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
# contents move into ~/.system/share, state and cache (the XDG directories
# ~/.profile sets), and so does ~/.ksh_history. Nothing is deleted; a
# name already taken in the new place is kept where it was and reported.
# moveinto <old dir> <new dir>: move every entry, merging directories. Each
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

# Each part on its own: one that fails (no internet for Plover, say)
# doesn't stop the others; they are listed at the end.
failed=
for p in $parts; do
	say "part    $p ($(date +%H:%M:%S))"
	set +e
	( set -e; "install_$p" )
	rc=$?
	set -e
	if [ "$rc" -ne 0 ]; then
		warn "part $p did not finish (error $rc); going on with the rest"
		failed="$failed $p"
	fi
done
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
[ "$n" -eq 0 ] || say "private $n files and directories in $HOME (only $USER_NAME and root)"
[ -z "$skipped" ] ||
	say "skipped $skipped (needs root): su -c '$DOTFILES/install.sh $skipped'"
if [ -n "$failed" ]; then
	warn "these parts did not finish:$failed (the messages above say why;"
	warn "no internet? parts that download: plover, moonlander, st's font, net)"
	exit 1
fi
say "done"
