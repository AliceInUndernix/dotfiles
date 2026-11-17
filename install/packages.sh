#!/bin/sh
# Step 1 of 3: the system's packages (root). ./install.sh --deps runs
# it; run on its own, install/packages.sh [-a] does the same.
#
# Everything these dotfiles use or build with. On Gentoo with emerge -qv
# (-qva with -a, so emerge asks first); apt, pacman, dnf and xbps are
# known too, with the packages of the same names where they have them.
# The big browsers are not here: step 2, install/apps.sh.

# The Gentoo packages, in groups. Atoms only; what needs a keyword, USE
# flag or licence gets it in install_packages.
GENTOO_X="x11-base/xorg-server x11-apps/xinit x11-apps/xsetroot
	x11-apps/xset x11-apps/xprop x11-misc/xss-lock x11-misc/xclip
	x11-libs/libX11 x11-libs/libXft x11-libs/libXinerama
	x11-libs/libXrandr x11-libs/libXext media-libs/fontconfig
	media-libs/freetype media-fonts/liberation-fonts"
GENTOO_TOOLS="app-shells/ksh dev-vcs/git net-misc/curl net-misc/wget
	app-editors/neovim app-misc/tmux sys-process/btop
	app-mobilephone/dfu-util"
GENTOO_MEDIA="media-video/mpv net-misc/yt-dlp media-gfx/feh
	media-gfx/flameshot media-sound/wiremix"
GENTOO_ARCHIVES="app-arch/zip app-arch/unzip app-arch/7zip"
# not free software: each needs its licence accepted (below)
GENTOO_NONFREE="app-arch/rar app-arch/unrar"
GENTOO_NET="net-vpn/openvpn net-vpn/tor"

install_packages() (
	root_env
	id=unknown
	[ -r /etc/os-release ] && id=$(. /etc/os-release; echo "${ID_LIKE:-} ${ID:-}")
	case $id in
	*gentoo*) gentoo_packages ;;
	*debian*|*ubuntu*)
		# rar and unrar are in Debian's non-free section
		apt-get install build-essential pkg-config libx11-dev \
			libxft-dev libxinerama-dev libxrandr-dev libxext-dev \
			libfontconfig-dev libfreetype-dev xserver-xorg xinit \
			x11-xserver-utils x11-utils xss-lock xclip ksh \
			fonts-liberation git curl wget neovim tmux btop dfu-util \
			python3-venv mpv yt-dlp feh flameshot pipewire wireplumber \
			wiremix zip unzip 7zip openvpn tor ;;
	*arch*)
		pacman -S --needed base-devel libx11 libxft libxinerama \
			libxrandr libxext fontconfig freetype2 xorg-server \
			xorg-xinit xorg-xsetroot xorg-xset xorg-xprop xss-lock xclip \
			ttf-liberation git curl wget neovim tmux btop dfu-util mpv \
			yt-dlp feh flameshot pipewire wireplumber wiremix zip unzip \
			7zip unrar openvpn tor ;;
	*fedora*|*rhel*)
		dnf install gcc make pkgconf-pkg-config libX11-devel \
			libXft-devel libXinerama-devel libXrandr-devel libXext-devel \
			fontconfig-devel freetype-devel xorg-x11-server-Xorg \
			xorg-x11-xinit xsetroot xset xprop xss-lock xclip ksh \
			liberation-mono-fonts liberation-sans-fonts git curl wget \
			neovim tmux btop dfu-util mpv yt-dlp feh flameshot pipewire \
			wireplumber zip unzip 7zip openvpn tor ;;
	*void*)
		xbps-install base-devel pkg-config libX11-devel libXft-devel \
			libXinerama-devel libXrandr-devel libXext-devel \
			fontconfig-devel freetype-devel xorg-server xinit xsetroot \
			xset xprop xss-lock xclip mksh liberation-fonts-ttf git curl \
			wget neovim tmux btop dfu-util mpv yt-dlp feh flameshot \
			pipewire wireplumber zip unzip 7zip openvpn tor ;;
	*)
		warn "unknown distro ($id): install a C compiler, make, pkg-config,"
		warn "and headers for X11, Xft, Xinerama, Xrandr, Xext, fontconfig, freetype." ;;
	esac
)

gentoo_packages() {
	# The first version kept its keywords in package.accept_keywords/
	# dotfiles: they move into zz-dotfiles, where portage() writes.
	old=/etc/portage/package.accept_keywords/dotfiles
	if [ -f "$old" ]; then
		cat "$old" >> /etc/portage/package.accept_keywords/zz-dotfiles
		rm "$old"
		say "move    $old -> zz-dotfiles"
	fi
	# wiremix is only in testing (~amd64). Sioyek is the live ebuild
	# (9999, no keywords at all), so it needs "**".
	keyword media-sound/wiremix
	keyword app-text/sioyek '**'
	# Tor with seccomp, for "Sandbox 1" in net/torrc
	portage use net-vpn/tor seccomp
	portage license app-arch/rar RAR
	portage license app-arch/unrar unRAR
	# PipeWire first, on its own: wiremix needs its libpipewire-0.3 to
	# build, but its ebuild doesn't say so, so emerge may build wiremix
	# before PipeWire is in.
	emerge $EMERGE_OPTS --noreplace media-video/pipewire \
		media-video/wireplumber
	emerge $EMERGE_OPTS --noreplace --changed-use $GENTOO_X $GENTOO_TOOLS \
		$GENTOO_MEDIA $GENTOO_ARCHIVES $GENTOO_NET
	# On their own, so a licence or fetch problem with them doesn't stop
	# the rest
	emerge $EMERGE_OPTS --noreplace $GENTOO_NONFREE ||
		warn "rar and unrar did not install (see emerge's messages above)"
	gentoo_sioyek
}

# Sioyek: the development version (GURU's sioyek-9999, built from the
# newest commit of its development branch), which TypeTrack needs
# (select_current_search_match). A live ebuild is never "out of date",
# so when it is already there it is built again (--oneshot: world
# already has it), to get the newest commit every time --deps runs.
gentoo_sioyek() {
	enable_overlay guru
	# Our patches for it (sioyek/gentoo-patches): its default key
	# bindings from /etc/sioyek, where the ebuild puts them (without it
	# no key works, only the mouse), and its data in ~/.system/share.
	pd=/etc/portage/patches/app-text/sioyek
	mkdir -p "$pd"
	for f in "$DOTFILES"/sioyek/gentoo-patches/*.patch; do
		t=$pd/dotfiles-${f##*/}
		[ "$(stat -c %a "$t" 2>/dev/null)" = 644 ] && cmp -s "$f" "$t" &&
			continue
		# readable by everyone: emerge reads it as user portage, and
		# your repo's files are yours alone (600)
		cp "$f" "$t"
		chmod 644 "$t"
		say "patch   ${f##*/} -> $pd (applied when Sioyek is built)"
	done
	# Its qt3d needs Qt built with OpenGL, and Qt Quick and Qt Multimedia
	# (for TextToSpeech) must match qtbase, so they get it too.
	for atom in dev-qt/qtbase dev-qt/qtdeclarative dev-qt/qtmultimedia; do
		portage use "$atom" opengl
	done
	if grep -rqs -- "dev-qt/qtbase.*[[:space:]]-opengl" /etc/portage/package.use; then
		warn "another package.use file turns OpenGL off for qtbase:"
		grep -rn -- "dev-qt/qtbase.*[[:space:]]-opengl" /etc/portage/package.use >&2 || :
		warn "if emerge still wants qtbase without opengl, delete that line"
	fi
	# Every installed Qt package is updated with it, in one go: updating
	# only Sioyek's own dependencies gave a slot conflict (qtbase 6.11.3
	# for qt3d while qtsvg, qttranslations, ... kept 6.11.2).
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
	# TextToSpeech, which GURU's ebuild doesn't list yet, so they go in
	# first, on their own.
	emerge $EMERGE_OPTS --update --deep --changed-use --noreplace \
		dev-qt/qtdeclarative dev-qt/qtspeech
	had_sioyek=no
	portageq has_version / app-text/sioyek && had_sioyek=yes
	emerge $EMERGE_OPTS --update --deep --changed-use app-text/sioyek
	[ "$had_sioyek" = no ] ||
		emerge $EMERGE_OPTS --oneshot app-text/sioyek
}

# run on its own: the same as ./install.sh --deps
case $0 in */packages.sh|packages.sh)
	exec "$(dirname "$0")/../install.sh" --deps "$@" ;;
esac
