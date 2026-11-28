#!/bin/sh
# Step 3 of 3: the configs, and our own programs built from this repo.
# Each part is a function install_<part>; ./install.sh <part> runs it,
# and ./install.sh with no part runs them all. Run on its own,
# install/configs.sh [part...] does the same as ./install.sh [part...].
# Nothing here needs root but slock, net (install/net.sh) and adding you
# to groups or making ksh your shell (ksh, steno).

install_nvim() {
	link "$DOTFILES/nvim" "$CONFIG/nvim"
	# Unpack archived plugins that are not there yet, so the first start
	# works offline. init.lua only downloads plugins that are missing.
	pack=$SYSTEM/nvim/site/pack/plugins/opt
	# A plugin the config no longer uses (plover-vim-tutor: Plover is
	# gone)
	for name in plover-vim-tutor; do
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
			as_user "nvim --headless -u NONE -i NONE -n -c 'helptags $pack/$name/doc' -c q" >/dev/null 2>&1 || :
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
	command -v xclip >/dev/null 2>&1 ||
		warn "xclip is not installed: Neovim can't use the system clipboard (su -c '$DOTFILES/install.sh --deps')"
}

install_ksh() {
	link "$DOTFILES/ksh/profile" "$HOME/.profile"
	link "$DOTFILES/ksh/kshrc" "$CONFIG/ksh/kshrc"
	# ~/.kshrc was the link before (ENV is now in ~/.system/config)
	if [ -L "$HOME/.kshrc" ] && [ "$(readlink "$HOME/.kshrc")" = "$DOTFILES/ksh/kshrc" ]; then
		rm "$HOME/.kshrc"
		say "remove  $HOME/.kshrc (now $CONFIG/ksh/kshrc)"
	fi
	ksh=$(command -v ksh || command -v mksh || :)
	if [ -z "$ksh" ]; then
		warn "ksh is not installed (su -c '$DOTFILES/install.sh --deps')"
		return
	fi
	if [ "$AS_ROOT" = no ]; then
		now=$(getent passwd "$USER_NAME" 2>/dev/null || grep "^$USER_NAME:" /etc/passwd)
		case ${now##*:} in
		*ksh) say "ok      shell ${now##*:} for $USER_NAME" ;;
		*) warn "your login shell is not ksh yet: su -c '$DOTFILES/install.sh ksh'" ;;
		esac
		# root's copies (made by a root run) are only refreshed by root
		for f in profile kshrc; do
			c=$ROOT_SYSTEM/config/ksh/$f
			[ ! -r "$c" ] || cmp -s "$DOTFILES/ksh/$f" "$c" ||
				warn "root's $f is older than ksh/$f: su -c '$DOTFILES/install.sh ksh'"
		done
		return
	fi
	# Make ksh the login shell of both the user and root.
	grep -qx "$ksh" /etc/shells 2>/dev/null || echo "$ksh" >> /etc/shells
	login_shell "$USER_NAME" "$ksh"
	login_shell root "$ksh"
	# root gets the same settings as copies it owns, in its own
	# ~/.system/config/ksh, not links into your home: those would let
	# anything running as you change what root runs. /root/.profile is
	# a link to root's copy. Run ./install.sh ksh again after changing
	# them.
	root_home=$(home_of root)
	[ -d "$ROOT_SYSTEM" ] || install -d -m 700 "$ROOT_SYSTEM"
	install -d -m 700 "$ROOT_SYSTEM/config"
	rootcopy "$DOTFILES/ksh/profile" "$root_home/.profile"
	root_put "$DOTFILES/ksh/kshrc" config/ksh/kshrc 644 >/dev/null
	say "ok      $ROOT_SYSTEM/config/ksh/kshrc"
	# the copy of before, /root/.kshrc (ENV is in ~/.system/config now)
	if [ -f "$root_home/.kshrc" ] && [ ! -L "$root_home/.kshrc" ] &&
		head -n 1 "$root_home/.kshrc" | grep -q '^# Korn shell settings'
	then
		rm "$root_home/.kshrc"
		say "remove  $root_home/.kshrc (now $ROOT_SYSTEM/config/ksh/kshrc)"
	fi
}

# login_shell <user> <shell>: make <shell> the user's login shell.
login_shell() {
	now=$(getent passwd "$1" 2>/dev/null || grep "^$1:" /etc/passwd)
	now=${now##*:}
	if [ "$(basename "$now")" = "$(basename "$2")" ]; then
		say "ok      shell $now for $1"
		return
	fi
	# the shell of before, for ./install.sh uninstall ksh
	case $1 in
	root) mkdir -p "$ROOT_SYSTEM/state"; chmod 700 "$ROOT_SYSTEM/state"
		f=$ROOT_SYSTEM/state/shell.root ;;
	*) userdir "$STATE/install"; f=$STATE/install/shell.$1 ;;
	esac
	case $(basename "$2") in
	ksh|mksh) [ -e "$f" ] || { echo "$now" > "$f"; [ "$1" = root ] || own "$f"; } ;;
	esac
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
	link "$DOTFILES/x/xinitrc" "$CONFIG/x/xinitrc"
	# ~/.xinitrc was the link before (~/.profile sets XINITRC now)
	if [ -L "$HOME/.xinitrc" ] && [ "$(readlink "$HOME/.xinitrc")" = "$DOTFILES/x/xinitrc" ]; then
		rm "$HOME/.xinitrc"
		say "remove  $HOME/.xinitrc (now $CONFIG/x/xinitrc)"
	fi
	# No Caps Lock on any keyboard (x/nocaps.conf): X's own settings,
	# so it needs root
	nocaps=/etc/X11/xorg.conf.d/90-nocaps.conf
	if [ "$AS_ROOT" = yes ]; then
		f=$(root_put "$DOTFILES/x/nocaps.conf" x/nocaps.conf 644)
		chmod 711 "${ROOT_SYSTEM%/*}" "$ROOT_SYSTEM"
		chmod 755 "$ROOT_SYSTEM/x"
		root_link "$f" "$nocaps"
	elif ! [ -L "$nocaps" ]; then
		warn "Caps Lock is still there: su -c '$DOTFILES/install.sh x' takes it away (then restart X)"
	fi
	for tool in xsetroot xset xss-lock; do
		command -v $tool >/dev/null 2>&1 || warn "$tool is not installed (su -c '$DOTFILES/install.sh --deps')"
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

# Sioyek itself comes from the system (on Gentoo --deps builds the
# development version, sioyek-9999 from GURU); only its settings are ours.
install_sioyek() {
	link "$DOTFILES/sioyek/prefs_user.config" "$CONFIG/sioyek/prefs_user.config"
	command -v sioyek >/dev/null 2>&1 ||
		warn "sioyek is not installed (on Gentoo: su -c '$DOTFILES/install.sh --deps')"
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
	command -v make >/dev/null 2>&1 || die "make not found (su -c '$DOTFILES/install.sh --deps')"
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

# ballfix: a filter for the Kensington trackball's buttons: stops false
# double clicks and changes buttons (see ballfix/README). Our own C
# program, built like the suckless tools. It needs your user in the
# groups of /dev/uinput and the ball's event file, and starts from
# x/xinitrc.
install_ballfix() { build_suckless ballfix; }

# diary: a private diary in one encrypted file, ~/.diary (see
# diary/README). Our own C program, built like the suckless tools from
# its own code and seal's; attachments go through clean.
install_diary() {
	build_suckless diary
	link "$DOTFILES/diary/config" "$CONFIG/diary/config"
}

# steno: our own steno engine, in C (see steno/README), built like the
# suckless tools. Its dictionaries are Dotterel's, from the dotterel repo,
# cloned into ~/.system/steno/dotterel and updated every run:
#   ~/.system/steno/dictionaries  -> dotterel/dictionaries
#   ~/.system/steno/data          -> dotterel/data
#   ~/.system/steno/user.dict     the strokes you add (steno -a)
#   dotterel/dictionaries/dotterel-*.dict  the words you add (steno -W)
# Reading the keyboard needs your user in the serial port's group; a
# root run adds you.
STENO_DIR=$SYSTEM/steno
DOTTEREL_REPO=${DOTTEREL_REPO:-https://github.com/AliceInUndernix/dotterel}

# Bring the Dotterel clone to exactly the repo's version, plus the
# words you added there (steno -W writes into its dictionaries). Those
# are committed in the clone ("Words added with steno -W") and put on
# top of the new version. If they don't go on top cleanly, the repo's
# version wins whole and your changes are kept in
# ~/.system/steno/kept-TIME.patch. Never a half-done merge.
update_dotterel() {
	d=$1
	kept=$STENO_DIR/kept-$(date -u +%Y%m%d-%H%M%S).patch
	# a merge, rebase or stash left half done by an earlier update
	if [ -n "$(git -C "$d" ls-files -u)" ] || [ -d "$d/.git/rebase-merge" ] ||
		[ -d "$d/.git/rebase-apply" ] || [ -e "$d/.git/MERGE_HEAD" ]
	then
		git -C "$d" rebase --abort >/dev/null 2>&1
		git -C "$d" merge --abort >/dev/null 2>&1
		git -C "$d" diff HEAD >>"$kept" 2>/dev/null
		git -C "$d" reset --quiet --hard
		warn "$d was left half merged; its changes are in $kept"
	fi
	if [ -n "$(git -C "$d" stash list)" ]; then
		git -C "$d" stash list --format=%gd | while read -r s; do
			git -C "$d" stash show -p "$s" >>"$kept"
		done
		git -C "$d" stash clear
		warn "old stashed changes in $d are in $kept"
	fi
	if ! git -C "$d" fetch --quiet origin; then
		warn "could not update $d; steno uses the dictionaries that are there"
		return 0
	fi
	[ "$(git -C "$d" rev-parse --is-shallow-repository)" = false ] ||
		git -C "$d" fetch --quiet --unshallow origin
	up=$(git -C "$d" rev-parse --abbrev-ref '@{upstream}')
	# Dotterel's history was started again (no commit in common): keep
	# your words as a patch, then take the new one whole.
	if ! git -C "$d" merge-base HEAD "$up" >/dev/null 2>&1; then
		git -C "$d" diff HEAD -- dictionaries >>"$kept"
		git -C "$d" log --format=%H --grep='^Words added with steno -W$' |
			while read -r c; do
				git -C "$d" format-patch --stdout -1 "$c" >>"$kept"
			done
		git -C "$d" reset --quiet --hard "$up"
		say "reset   $d to the new Dotterel (its history was started again)"
		[ ! -s "$kept" ] ||
			warn "your words added before are in $kept (git -C $d apply --3way $kept)"
		git -C "$d" clean -fdq -e 'dotterel-*.dict' -- dictionaries data
		return 0
	fi
	if [ -n "$(git -C "$d" status --porcelain --untracked-files=no)" ]; then
		TZ=UTC git -C "$d" -c user.name=user -c user.email=user@example.org \
			commit --quiet -a -m "Words added with steno -W"
	fi
	if ! git -C "$d" rebase --quiet "$up" >/dev/null 2>&1; then
		git -C "$d" rebase --abort >/dev/null 2>&1
		git -C "$d" format-patch --stdout "$up..HEAD" >>"$kept"
		git -C "$d" reset --quiet --hard "$up"
		warn "your words in $d clashed with the new Dotterel; the new one is in, yours are in $kept"
	fi
	git -C "$d" clean -fdq -e 'dotterel-*.dict' -- dictionaries data
}

install_steno() {
	g=$(serial_group)
	if [ "$AS_ROOT" = yes ]; then
		if ! in_group "$g" && getent group "$g" >/dev/null; then
			usermod -aG "$g" "$USER_NAME"
			say "group   $USER_NAME in $g (the Moonlander's serial port; log in again)"
		fi
		fix_owner >/dev/null
		su -s /bin/sh "$USER_NAME" -c "INSTALL_LOG='$INSTALL_LOG' \
INSTALL_NESTED=yes '$DOTFILES/install.sh' steno"
		return
	fi
	in_group "$g" ||
		warn "steno can't read the Moonlander until you are in group $g: su -c '$DOTFILES/install.sh steno', then log in again"
	build_suckless steno
	link "$DOTFILES/steno/config" "$CONFIG/steno/config"
	userdir "$STENO_DIR"
	command -v git >/dev/null 2>&1 || die "git not found (su -c '$DOTFILES/install.sh --deps')"
	src=$STENO_DIR/dotterel
	if [ -d "$src/.git" ]; then
		update_dotterel "$src"
		say "ok      $src ($(git -C "$src" log -1 --format=%h))"
	else
		say "clone   $DOTTEREL_REPO -> $src"
		git clone --quiet "$DOTTEREL_REPO" "$src"
	fi
	# The dictionaries are the clone's. Before, they were made here from
	# Plover's JSON: such a directory (made by this script) is replaced.
	for d in dictionaries data; do
		[ -d "$src/$d" ] || die "$src has no $d/ (an old dotterel? git -C $src pull)"
		if [ "$(readlink "$STENO_DIR/$d" 2>/dev/null)" != "dotterel/$d" ]; then
			rm -rf "${STENO_DIR:?}/$d"
			ln -s "dotterel/$d" "$STENO_DIR/$d"
		fi
		say "ok      $STENO_DIR/$d -> dotterel/$d"
	done
	# strokes added before the plain format: user.json -> user.dict
	if [ -f "$STENO_DIR/user.json" ] && [ ! -e "$STENO_DIR/user.dict" ]; then
		"$SYSBIN/steno" -C "$STENO_DIR" "$STENO_DIR/user.json" >/dev/null &&
			mv "$STENO_DIR/user.json" "$STENO_DIR/user.json.old" &&
			say "convert $STENO_DIR/user.json -> user.dict (the old one: user.json.old)"
	fi
	own "$STENO_DIR"
	plover_leftovers
}

# Plover is no longer part of these dotfiles: steno does its job. What
# install.sh once put in for it goes; its settings and logs stay until
# you delete them.
plover_leftovers() {
	p=$SYSTEM/plover
	if [ -e "$SYSBIN/plover" ] || [ -L "$SYSBIN/plover" ]; then
		rm -f "$SYSBIN/plover"
		say "delete  $SYSBIN/plover (Plover is no longer used)"
	fi
	for d in "$p/app" "$p/dotterel" "$p/plugins"; do
		[ -e "$d" ] || continue
		rm -rf "$d"
		say "delete  $d (Plover is no longer used)"
	done
	[ ! -d "$p" ] ||
		say "note    $p still has Plover's settings and logs: rm -rf $p"
}

# WebArchiver: archives whole websites (see webarchiver/README). Built
# and installed like the suckless tools; it runs wget for every download.
# icecatrc: IceCat's settings, the add-ons in icecatrc/addons.txt and our
# extension "switch" (icecatrc/README), put into the user's IceCat
# profile by icecatrc/install.sh; when IceCat was never started, it makes
# the profile. IceCat itself is the part icecat (root), which runs this
# when it is done. The add-ons come through Tor (127.0.0.1:9050); when
# Tor can't be reached they are left out, and it says so.
# ~/.system/bin/icecat (icecatrc/icecat) starts the real IceCat and runs
# icecatrc/tidy.sh when it quits.
install_icecatrc() {
	# the old name of this part was switch: its link goes, unbacked
	[ "$(readlink "$SYSBIN/icecat" 2>/dev/null)" != "$DOTFILES/switch/icecat" ] ||
		rm -f "$SYSBIN/icecat"
	link "$DOTFILES/icecatrc/icecat" "$SYSBIN/icecat"
	if ! have_icecat; then
		warn "IceCat is not installed: icecatrc is set up when it is (su -c '$DOTFILES/install.sh icecat')"
		return 0
	fi
	as_user "sh '$DOTFILES/icecatrc/install.sh'"
}
# have_icecat: is the real IceCat installed (not only our icecat)?
have_icecat() {
	c=$(PATH=$(echo "$PATH" | sed "s|$SYSBIN:||g; s|:$SYSBIN\$||") \
		command -v icecat 2>/dev/null) || return 1
	[ -n "$c" ]
}
install_webarchiver() {
	build_suckless webarchiver
	command -v wget >/dev/null 2>&1 ||
		warn "wget is not installed; webarchiver needs it (su -c '$DOTFILES/install.sh --deps')"
}
# st's font2 falls back to Symbols Nerd Font Mono for icons and symbols
# Liberation Mono lacks. It goes into your home directory, nothing needs
# root. It comes from archive/fonts/ (no download); to get a newer
# release, put it there and delete the directory below, then run
# ./install.sh st again.
NERD_FONTS=$SYSTEM/fonts/NerdFontsSymbolsOnly

install_st() {
	build_suckless st
	if [ -f "$NERD_FONTS/SymbolsNerdFontMono-Regular.ttf" ]; then
		say "ok      $NERD_FONTS"
	else
		tarball=
		for t in "$DOTFILES"/archive/fonts/NerdFontsSymbolsOnly-*.tar.xz; do
			[ -e "$t" ] && tarball=$t
		done
		say "unpack  Symbols Nerd Font -> $NERD_FONTS"
		userdir "${NERD_FONTS%/*}"
		rm -rf "$NERD_FONTS.tmp" && mkdir -p "$NERD_FONTS.tmp"
		if [ -n "$tarball" ] && tar -xJf "$tarball" -C "$NERD_FONTS.tmp"; then
			rm -rf "$NERD_FONTS" && mv "$NERD_FONTS.tmp" "$NERD_FONTS"
			own "$NERD_FONTS"
		else
			rm -rf "$NERD_FONTS.tmp"
			warn "no Symbols Nerd Font in $DOTFILES/archive/fonts; st still works without it"
		fi
	fi
	# fontconfig looks in ~/.local/share/fonts by default, not here
	userdir "$CONFIG/fontconfig"
	printf '%s\n' '<?xml version="1.0"?>' \
		'<!DOCTYPE fontconfig SYSTEM "fonts.dtd">' '<fontconfig>' \
		"	<dir>${NERD_FONTS%/*}</dir>" '</fontconfig>' \
		> "$CONFIG/fontconfig/fonts.conf.new"
	if [ -f "$CONFIG/fontconfig/fonts.conf" ] &&
		! cmp -s "$CONFIG/fontconfig/fonts.conf.new" "$CONFIG/fontconfig/fonts.conf"
	then
		userdir "$BACKUP/fontconfig"
		cp -p "$CONFIG/fontconfig/fonts.conf" "$BACKUP/fontconfig/fonts.conf.$STAMP"
		say "backup  fonts.conf -> $BACKUP/fontconfig/fonts.conf.$STAMP"
	fi
	mv "$CONFIG/fontconfig/fonts.conf.new" "$CONFIG/fontconfig/fonts.conf"
	own "$CONFIG/fontconfig/fonts.conf"
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
# it. It is cloned into ~/.system/moonlander and its build.sh runs
# as the user, with everything in the user's home:
#   ~/.system/qmk/zsa_qmk   ZSA's QMK source (firmware25), patched
#   ~/.system/qmk/venv      Python environment for the qmk command
#   ~/.system/qmk/arm-gnu-toolchain   ARM compiler
#   ~/.system/bin/qmk, arm-none-eabi-*     links to the two above
#   ~/.system/moonlander/zsa_moonlander_revb_canary.bin   firmware
# Each run updates the clone, builds and flashes (as root: the user
# builds, root flashes, since root reaches the keyboard without udev
# rules). MOONLANDER_FLASH=no ./install.sh moonlander only builds.
ML_REPO=${ML_REPO:-https://github.com/AliceInUndernix/moonlander}
ML_SRC=$SYSTEM/moonlander
QMK_DIR=$SYSTEM/qmk
ML_BIN=zsa_moonlander_revb_canary.bin

install_moonlander() {
	command -v git >/dev/null 2>&1 || die "git not found (su -c '$DOTFILES/install.sh --deps')"
	if [ -d "$ML_SRC/.git" ]; then
		git_follow "$ML_SRC" ||
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

# Claude desktop: Anthropic's .deb, its signature and SHA256 checked,
# unpacked into ~/.system/claude-desktop/app by
# claude-desktop/get-claude-desktop.sh, run as the user (from root too).
# A newer version is fetched when there is one; the launcher
# ~/.system/bin/claude-desktop is written again each time. Your login
# is in ~/.system/config/Claude and is never touched.
install_claude() {
	for t in curl gpg ar tar sha256sum; do
		command -v "$t" >/dev/null 2>&1 ||
			die "$t not found; Claude desktop needs it"
	done
	as_user "sh '$DOTFILES/claude-desktop/get-claude-desktop.sh'"
}

# run on its own: the same as ./install.sh [part...]
case $0 in */configs.sh|configs.sh)
	exec "$(dirname "$0")/../install.sh" "$@" ;;
esac
