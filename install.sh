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
PARTS="ksh xdg x nvim tmux flameshot mpv sioyek wiremix btop feh dwm st dmenu slock moonlander"

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
			dev-vcs/git app-editors/neovim app-misc/tmux \
			x11-apps/xinit x11-apps/xsetroot x11-misc/xss-lock \
			app-shells/ksh media-fonts/liberation-fonts \
			media-gfx/flameshot media-video/mpv net-misc/yt-dlp \
			media-video/pipewire media-video/wireplumber media-sound/wiremix \
			sys-process/btop media-gfx/feh www-client/chromium \
			app-mobilephone/dfu-util
		# IceCat is not in the main tree: it comes from src_prepare-overlay.
		if ! portageq get_repo_path / src_prepare-overlay >/dev/null 2>&1; then
			say "enable  src_prepare-overlay (for www-client/icecat)"
			asroot emerge --ask --noreplace app-eselect/eselect-repository
			asroot eselect repository enable src_prepare-overlay
			asroot emerge --sync src_prepare-overlay
		fi
		asroot emerge --ask --noreplace www-client/icecat ;;
	*debian*|*ubuntu*)
		asroot apt-get install build-essential pkg-config libx11-dev \
			libxft-dev libxinerama-dev libxrandr-dev libxext-dev \
			libfontconfig-dev libfreetype-dev git neovim tmux \
			xinit x11-xserver-utils xss-lock ksh fonts-liberation \
			flameshot mpv yt-dlp pipewire wireplumber wiremix btop \
			python3-venv dfu-util ;;
	*arch*)
		asroot pacman -S --needed base-devel libx11 libxft libxinerama \
			libxrandr libxext fontconfig freetype2 git neovim tmux dfu-util \
			xorg-xinit xorg-xsetroot xss-lock ttf-liberation \
			flameshot mpv yt-dlp pipewire wireplumber wiremix btop ;;
	*fedora*|*rhel*)
		asroot dnf install gcc make pkgconf-pkg-config libX11-devel \
			libXft-devel libXinerama-devel libXrandr-devel libXext-devel \
			fontconfig-devel freetype-devel git neovim tmux dfu-util \
			xorg-x11-xinit xsetroot xss-lock ksh liberation-mono-fonts \
			liberation-sans-fonts flameshot mpv yt-dlp pipewire wireplumber btop ;;
	*void*)
		asroot xbps-install base-devel pkg-config libX11-devel libXft-devel \
			libXinerama-devel libXrandr-devel libXext-devel \
			fontconfig-devel freetype-devel git neovim tmux dfu-util \
			xinit xsetroot xss-lock mksh liberation-fonts-ttf \
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
	pack=${XDG_DATA_HOME:-$HOME/.local/share}/nvim/site/pack/plugins/opt
	for tarball in "$DOTFILES"/archive/nvim-plugins/*.tar.gz; do
		[ -e "$tarball" ] || continue
		name=$(basename "$tarball" .tar.gz)
		[ -e "$pack/$name" ] && continue
		mkdir -p "$pack"
		tar -xzf "$tarball" -C "$pack"
		if [ -d "$pack/$name/doc" ] && command -v nvim >/dev/null 2>&1; then
			nvim --headless -u NONE -c "helptags $pack/$name/doc" -c q >/dev/null 2>&1 || :
		fi
		say "plugin  $name (from archive)"
	done
	command -v nvim >/dev/null 2>&1 || warn "nvim is not installed (needs 0.11+)"
}

install_ksh() {
	link "$DOTFILES/ksh/profile" "$HOME/.profile"
	link "$DOTFILES/ksh/kshrc" "$HOME/.kshrc"
	ksh=$(command -v ksh || command -v mksh || :)
	if [ -z "$ksh" ]; then
		warn "ksh is not installed (try ./install.sh --deps)"
		return
	fi
	# Make ksh the login shell (chsh asks for your password).
	if [ "$(basename "${SHELL:-}")" != "$(basename "$ksh")" ]; then
		grep -qx "$ksh" /etc/shells 2>/dev/null || echo "$ksh" | asroot tee -a /etc/shells >/dev/null
		say "shell   $ksh (takes effect at next login)"
		chsh -s "$ksh"
	fi
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
	for tool in xsetroot xss-lock; do
		command -v $tool >/dev/null 2>&1 || warn "$tool is not installed (try ./install.sh --deps)"
	done
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

# Moonlander: everything goes into your home folder, nothing needs root.
#   ~/.local/share/qmk/zsa_qmk   ZSA's QMK source (firmware25), with qmk.patch
#   ~/.local/share/qmk/venv      Python environment for the qmk command
#   ~/.local/share/qmk/arm-gnu-toolchain   ARM compiler, if none is installed
#   ~/.local/bin/qmk, arm-none-eabi-*      symlinks to the two above
# Each run copies moonlander/canary into QMK fresh (like update.sh did) and
# builds. The built .bin is copied to moonlander/ for Keymapp.
QMK_DIR=${XDG_DATA_HOME:-$HOME/.local/share}/qmk
QMK_BIN=$HOME/.local/bin
QMK_BRANCH=firmware25
QMK_TARGET=zsa/moonlander/reva:canary
ARM_VERSION=14.2.rel1

install_moonlander() {
	fw=$QMK_DIR/zsa_qmk
	mkdir -p "$QMK_DIR" "$QMK_BIN"
	command -v git >/dev/null 2>&1 || die "git not found (try ./install.sh --deps)"
	command -v python3 >/dev/null 2>&1 || die "python3 not found"

	# 1. ZSA's QMK source, only the latest commit to keep it small.
	if [ ! -d "$fw/.git" ]; then
		say "clone   zsa/qmk_firmware ($QMK_BRANCH) -> $fw"
		git clone --depth 1 --branch "$QMK_BRANCH" --recurse-submodules \
			--shallow-submodules https://github.com/zsa/qmk_firmware "$fw"
	fi

	# 2. QMK patch for arbitrary_keycode and the audio timer (applied once).
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

	# 4. ARM compiler: use the system one if there is one, else download.
	if ! command -v arm-none-eabi-gcc >/dev/null 2>&1; then
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
	( cd "$fw" && PATH="$QMK_DIR/venv/bin:$QMK_BIN:$PATH" make "$QMK_TARGET" $nosuffix )
	cp "$fw/zsa_moonlander_reva_canary.bin" "$DOTFILES/moonlander/"
	say "built   moonlander/zsa_moonlander_reva_canary.bin (flash it with Keymapp)"

	case ":$PATH:" in *":$QMK_BIN:"*) ;; *) warn "add $QMK_BIN to PATH to run qmk yourself" ;; esac
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
