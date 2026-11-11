#!/bin/sh
# Install Alice's configuration on a fresh Linux system.
#
#   ./install.sh               link configs and build/install everything
#   ./install.sh nvim dwm      only the parts named
#   ./install.sh --deps        first install build dependencies with the
#                              distro's package manager (asks before it does)
#   ./install.sh --list        show the parts
#
# Configs are symlinked, so editing them in ~/.config edits this repo.
# Anything already in the way is moved to <name>.bak.<date>, never deleted.
# suckless tools are built as you and installed with sudo (or doas) into
# PREFIX (default /usr/local; PREFIX=~/.local ./install.sh needs no root).
# POSIX sh, no bashisms, so it runs on any distro.

set -eu

DOTFILES=$(cd "$(dirname "$0")" && pwd)
PREFIX=${PREFIX:-/usr/local}
CONFIG=${XDG_CONFIG_HOME:-$HOME/.config}
STAMP=$(date +%Y%m%d-%H%M%S)
PARTS="nvim dwm st dmenu slock moonlander"

say()  { printf '==> %s\n' "$*"; }
warn() { printf '!!  %s\n' "$*" >&2; }
die()  { warn "$*"; exit 1; }

# Root helper for installs outside $HOME.
asroot() {
	if [ "$(id -u)" -eq 0 ]; then "$@"
	elif command -v sudo >/dev/null 2>&1; then sudo "$@"
	elif command -v doas >/dev/null 2>&1; then doas "$@"
	else die "need root for: $* (install sudo or doas, or set PREFIX=~/.local)"
	fi
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
	mkdir -p "$(dirname "$dst")"
	ln -s "$src" "$dst"
	say "link    $dst -> $src"
}

# --- build dependencies ---------------------------------------------------

deps() {
	id=unknown
	[ -r /etc/os-release ] && id=$(. /etc/os-release; echo "${ID_LIKE:-} ${ID:-}")
	case $id in
	*gentoo*)
		asroot emerge --ask --noreplace x11-libs/libX11 x11-libs/libXft \
			x11-libs/libXinerama x11-libs/libXrandr x11-libs/libXext \
			media-libs/fontconfig media-libs/freetype \
			dev-vcs/git app-editors/neovim ;;
	*debian*|*ubuntu*)
		asroot apt-get install build-essential pkg-config libx11-dev \
			libxft-dev libxinerama-dev libxrandr-dev libxext-dev \
			libfontconfig-dev libfreetype-dev git neovim ;;
	*arch*)
		asroot pacman -S --needed base-devel libx11 libxft libxinerama \
			libxrandr libxext fontconfig freetype2 git neovim ;;
	*fedora*|*rhel*)
		asroot dnf install gcc make pkgconf-pkg-config libX11-devel \
			libXft-devel libXinerama-devel libXrandr-devel libXext-devel \
			fontconfig-devel freetype-devel git neovim ;;
	*void*)
		asroot xbps-install base-devel pkg-config libX11-devel libXft-devel \
			libXinerama-devel libXrandr-devel libXext-devel \
			fontconfig-devel freetype-devel git neovim ;;
	*)
		warn "unknown distro ($id): install a C compiler, make, pkg-config,"
		warn "and headers for X11, Xft, Xinerama, Xrandr, Xext, fontconfig, freetype." ;;
	esac
}

# --- parts ----------------------------------------------------------------

install_nvim() {
	link "$DOTFILES/nvim" "$CONFIG/nvim"
	command -v nvim >/dev/null 2>&1 || warn "nvim is not installed (needs 0.11+)"
}

# suckless: build as the user, install as root (unless PREFIX is writable).
build_suckless() {
	name=$1
	say "build   $name"
	command -v make >/dev/null 2>&1 || die "make not found (try ./install.sh --deps)"
	( cd "$DOTFILES/$name" && make PREFIX="$PREFIX" )
	if mkdir -p "$PREFIX" 2>/dev/null && [ -w "$PREFIX" ]; then
		( cd "$DOTFILES/$name" && make PREFIX="$PREFIX" install )
	else
		( cd "$DOTFILES/$name" && asroot make PREFIX="$PREFIX" install )
	fi
	( cd "$DOTFILES/$name" && make clean >/dev/null )
	say "install $name -> $PREFIX/bin/$name"
}

install_dwm()   { build_suckless dwm; }
install_st()    { build_suckless st; }
install_dmenu() { build_suckless dmenu; }

install_slock() {
	# slock drops privileges to the group in slock/config.h ("nobody").
	if command -v getent >/dev/null 2>&1 && ! getent group nobody >/dev/null; then
		warn "group 'nobody' does not exist here; edit 'group' in slock/config.h"
		warn "(Debian and Ubuntu call it 'nogroup')"
	fi
	build_suckless slock
}

install_moonlander() {
	# Firmware is flashed onto the keyboard, not installed into the system.
	say "skip    moonlander (firmware: see moonlander/README.md to build and flash)"
}

# --- main -----------------------------------------------------------------

want_deps=no
parts=
for arg in "$@"; do
	case $arg in
	--deps) want_deps=yes ;;
	--list) echo "$PARTS"; exit 0 ;;
	-h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
	-*) die "unknown option: $arg" ;;
	*)
		case " $PARTS " in *" $arg "*) ;; *) die "unknown part: $arg (see --list)" ;; esac
		parts="$parts $arg" ;;
	esac
done
[ -n "$parts" ] || parts=$PARTS

[ "$want_deps" = yes ] && deps
for p in $parts; do "install_$p"; done
say "done"
