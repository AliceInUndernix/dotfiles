# ./install.sh uninstall [--purge] [part|group...] (or ./uninstall.sh):
# takes out what install.sh put in. With no part named, everything, and
# then your home goes back to how programs have it without these
# dotfiles: ~/.system/config back to ~/.config, and ~/.system goes when
# it is empty (data, state and cache there are left alone).
# Sourced by install.sh, not run.
#
# What was in the way when a config was linked (kept in
# ~/.system/backup) is
# put back. Your own data stays: steno's user.dict and the words you
# added, the diary (~/.diary), Principia's old copies; --purge also
# deletes what install.sh made that holds your data (steno's
# directory). The system's packages stay (they may be needed by other
# programs); on Gentoo uninstall --deps takes them out of the world set,
# and emerge -qv --depclean then removes what nothing else needs.
#
# Root's parts (net, slock's setuid copy is in your home so you can
# delete it, the browsers, root's shell settings and your login shell)
# need root: as you, they are skipped and the su -c command is said.

PURGE=no

# links_of <part>: "src dst" of every link install_<part> makes
links_of() {
	for f in "$DOTFILES"/install/*.sh; do
		sed -n "/^install_$1() {/,/^}/p" "$f"
	done | grep '^[[:space:]]*link "$DOTFILES/' | sed 's/^[[:space:]]*link //'
}

# unlink_part <part>: take away its links, put back what was there
unlink_part() {
	links_of "$1" | while IFS= read -r line; do
		eval "set -- $line"
		unlink "$1" "$2"
	done
}

# unbuild <dir>: make uninstall of one of our programs or suckless tools
unbuild() {
	[ -e "$PREFIX/bin/$1" ] || [ -L "$PREFIX/bin/$1" ] || return 0
	if command -v make >/dev/null 2>&1; then
		( cd "$DOTFILES/$1" && make -s PREFIX="$PREFIX" uninstall ) >/dev/null 2>&1 ||
			warn "make uninstall in $1 failed"
	else
		rm -f "$PREFIX/bin/$1" "$PREFIX/share/man/man1/$1.1"
	fi
	say "remove  $1 from $PREFIX"
}

# remove <path>: rm -rf, said
remove() {
	[ -e "$1" ] || [ -L "$1" ] || return 0
	rm -rf "$1"
	say "remove  $1"
}

# needs_root <part> <what>: fine as root; as you, say how and fail
needs_root() {
	[ "$AS_ROOT" = yes ] && return 0
	warn "$1: $2 needs root: su -c '$DOTFILES/install.sh uninstall $1'"
	return 1
}

# root_tidy: root's ~/.system without the directories left empty, and
# gone (and /root closed again) when nothing is left in it
root_tidy() {
	[ -d "$ROOT_SYSTEM" ] || return 0
	find "$ROOT_SYSTEM" -depth -type d -empty -delete
	[ -d "$ROOT_SYSTEM/net" ] || chmod 700 "${ROOT_SYSTEM%/*}"
	[ ! -d "$ROOT_SYSTEM" ] || [ -d "$ROOT_SYSTEM/net" ] ||
		chmod 700 "$ROOT_SYSTEM"
}

uninstall_ksh() {
	unlink_part ksh
	sh_file=$STATE/install/shell.$USER_NAME
	if needs_root ksh "your login shell back, and root's settings"; then
		old=$(cat "$sh_file" 2>/dev/null) || :
		[ -n "$old" ] && [ -x "$old" ] || old=/bin/bash
		[ -x "$old" ] || old=/bin/sh
		login_shell "$USER_NAME" "$old"
		old=$(cat "$ROOT_SYSTEM/state/shell.root" 2>/dev/null) || :
		[ -n "$old" ] && [ -x "$old" ] || old=/bin/bash
		[ -x "$old" ] || old=/bin/sh
		login_shell root "$old"
		unrootcopy "$DOTFILES/ksh/profile" "$(home_of root)/.profile"
		remove "$ROOT_SYSTEM/config/ksh"
		rm -f "$sh_file" "$ROOT_SYSTEM/state/shell.root"
		root_tidy
	fi
}

uninstall_nvim() {
	unlink_part nvim
	[ ! -L "$SYSBIN/vim" ] || case $(readlink "$SYSBIN/vim") in
		*/nvim) rm "$SYSBIN/vim"; say "remove  $SYSBIN/vim" ;;
	esac
	pack=$SYSTEM/nvim/site/pack/plugins/opt
	for t in "$DOTFILES"/archive/nvim-plugins/*.tar.gz; do
		[ -e "$t" ] || continue
		remove "$pack/$(basename "$t" .tar.gz)"
	done
	rmdir_up "$pack"
}

uninstall_st() {
	unbuild st
	remove "$NERD_FONTS"
	! command -v fc-cache >/dev/null 2>&1 || fc-cache "${NERD_FONTS%/*}" 2>/dev/null || :
	rmdir_up "${NERD_FONTS%/*}"
}

uninstall_dmenu() { unbuild dmenu; unlink_part dmenu; }
uninstall_slock() { unbuild slock; }
uninstall_diary() { unbuild diary; unlink_part diary; }

uninstall_steno() {
	unbuild steno
	unlink_part steno
	for d in dictionaries data; do
		[ ! -L "$STENO_DIR/$d" ] || { rm "$STENO_DIR/$d"; say "remove  $STENO_DIR/$d"; }
	done
	src=$STENO_DIR/dotterel
	if [ "$PURGE" = yes ]; then
		remove "$STENO_DIR"
		return
	fi
	# the clone goes only when it has none of your words that aren't
	# pushed: else it stays, and says so
	if [ -d "$src/.git" ]; then
		if [ -z "$(git -C "$src" status --porcelain --untracked-files=no)" ] &&
			[ -z "$(git -C "$src" log --oneline '@{upstream}..' 2>/dev/null)" ]
		then
			remove "$src"
		else
			warn "kept    $src: it has words you added and didn't push (--purge deletes it)"
		fi
	fi
	rmdir "$STENO_DIR" 2>/dev/null ||
		[ ! -d "$STENO_DIR" ] ||
		say "kept    $STENO_DIR: your own (user.dict); --purge deletes it"
}

uninstall_moonlander() {
	for f in "$SYSBIN"/qmk "$SYSBIN"/arm-none-eabi-*; do
		[ -L "$f" ] || continue
		case $(readlink "$f") in "$QMK_DIR"/*) rm "$f"; say "remove  $f" ;; esac
	done
	remove "$ML_SRC"
	remove "$QMK_DIR"
}

uninstall_ksp() {
	if [ -d "$KSPKAN_SRC" ]; then
		if [ -f "$KSPKAN_SRC/Makefile" ]; then
			( cd "$KSPKAN_SRC" && make -s PREFIX="$SYSTEM" uninstall ) \
				>/dev/null 2>&1 || rm -f "$SYSBIN/kspkan"
		fi
		remove "$KSPKAN_SRC"
	fi
	[ ! -d "$DATA/kspkan" ] ||
		say "kept    $DATA/kspkan: kspkan's game folders and mod lists"
	remove "$SYSBIN/kspkan"
	mark=$STATE/ksp/principia-zip
	if [ -f "$mark" ]; then
		dst=$(sed 's/^[0-9]* [0-9]* //' "$mark")
		remove "$dst"
		bak=$(ls -d "$BACKUP"/ksp/Principia.* 2>/dev/null | sort | tail -n 1) || :
		[ -z "$bak" ] || { mv "$bak" "$dst"; say "restore $dst (from $bak)"; }
		rmdir_up "$BACKUP/ksp"
		remove "$STATE/ksp"
	fi
}

uninstall_net() {
	needs_root net "the VPN, Tor and the kill switch" || return 0
	[ -e "$ROOT_SYSTEM/net" ] || [ -e /usr/local/bin/vpn ] || {
		say "ok      net is not installed"; return 0; }
	# vpn off puts back the system's own files (/etc/vpn/orig), takes
	# vpnfw and vpn out of the runlevels and the firewall down
	off=no
	if [ -x /usr/local/bin/vpn ] && /usr/local/bin/vpn off; then off=yes; fi
	if [ "$off" = no ]; then
		warn "vpn off did not work: /etc/vpn and the backups in"
		warn "$ROOT_SYSTEM/state/backup are kept, to put the system's files back by hand"
		return 1
	fi
	for f in /etc/tor/torrc /etc/proxychains.conf; do
		o=/etc/vpn/orig$f
		if [ -L "$f" ]; then
			rm "$f"
			if [ -f "$o" ]; then cp -p "$o" "$f"; say "restore $f"; fi
		fi
	done
	for f in /usr/local/bin/vpn /usr/local/bin/vpnauth /usr/local/bin/vpntop \
		/usr/local/bin/proxychains /etc/init.d/vpnfw /etc/init.d/vpn \
		/etc/sysctl.d/vpn.conf /etc/proxychains.d /etc/vpn
	do
		remove "$f"
	done
	remove "$ROOT_SYSTEM/net"
	# the system's files are back as they were: their backups can go
	remove "$ROOT_SYSTEM/state/backup/etc"
	for g in vpnctl vpnallow; do
		! getent group "$g" >/dev/null || { groupdel "$g"; say "group   $g removed"; }
	done
	root_tidy
	say "net     gone; the packages (openvpn, tor, nftables, proxychains) stay"
}

uninstall_browser() {
	needs_root "$2" "taking out $1" || return 0
	command -v emerge >/dev/null 2>&1 || { say "ok      $2: not Gentoo"; return 0; }
	portageq has_version / "$1" || { say "ok      $1 is not installed"; return 0; }
	( root_env; emerge $EMERGE_OPTS --deselect "$1" &&
		emerge $EMERGE_OPTS --depclean "$1" )
}
uninstall_icecat()   { uninstall_browser www-client/icecat icecat; }
# icecatrc: out of the IceCat profile, settings and all (icecatrc/install.sh
# -u); the profile and the add-ons stay
uninstall_icecatrc() {
	unlink_part icecatrc
	if ! as_user "sh '$DOTFILES/icecatrc/install.sh' -w" >/dev/null 2>&1; then
		say "ok      icecatrc: IceCat has no profile"
		return 0
	fi
	as_user "sh '$DOTFILES/icecatrc/install.sh' -u"
}
uninstall_chromium() { uninstall_browser www-client/ungoogled-chromium chromium; }

# --deps: on Gentoo, out of the world set (nothing is removed yet)
uninstall_deps() {
	needs_root deps "the system's packages" || return 0
	if command -v emerge >/dev/null 2>&1; then
		( root_env; emerge $EMERGE_OPTS --deselect $GENTOO_X $GENTOO_TOOLS \
			$GENTOO_MEDIA $GENTOO_ARCHIVES $GENTOO_NET app-text/sioyek \
			media-video/pipewire media-video/wireplumber )
		say "note    to remove what nothing else needs:"
		say "        emerge $EMERGE_OPTS --depclean"
	else
		say "note    the packages stay; remove the ones you don't want with"
		say "        your package manager (install/packages.sh lists them)"
	fi
}

# x: also the link that takes Caps Lock away (root's)
uninstall_x() {
	f=/etc/X11/xorg.conf.d/90-nocaps.conf
	if [ -L "$f" ] && [ "$AS_ROOT" = yes ]; then
		rm "$f"
		say "remove  $f (Caps Lock is back after X restarts)"
	elif [ -L "$f" ]; then
		say "note    $f stays: su -c '$DOTFILES/install.sh uninstall x'"
	fi
	unlink_part x
}

# claude: the app and its launcher; your login and settings stay
uninstall_claude() {
	remove "$SYSTEM/claude-desktop"
	remove "$SYSBIN/claude-desktop"
	[ ! -d "$CONFIG/Claude" ] ||
		say "note    your login and settings stay in $CONFIG/Claude"
}

# every other part: its links, and its program if it builds one
uninstall_default() {
	case $1 in
	dwm|webarchiver|clean|seal|ballfix) unbuild "$1" ;;
	esac
	unlink_part "$1"
}

# back_home: after a full uninstall, the settings go back to ~/.config.
# Data, state and caches are not touched (they are not ours to move).
back_home() {
	if [ -L "$HOME/.config" ] && [ "$(readlink "$HOME/.config")" = .system/config ]; then
		rm "$HOME/.config"
		if [ -d "$CONFIG" ]; then
			mv "$CONFIG" "$HOME/.config"
			say "move    $CONFIG -> $HOME/.config"
		fi
	fi
	for d in "bin:.local/bin"
	do
		from=$SYSTEM/${d%%:*} to=$HOME/${d#*:}
		[ -d "$from" ] || continue
		rmdir "$from" 2>/dev/null && continue
		if [ -e "$to" ]; then
			warn "kept    $from ($to is there already)"
			continue
		fi
		userdir "$(dirname "$to")"
		mv "$from" "$to"
		say "move    $from -> $to"
	done
	if rmdir "$SYSTEM" 2>/dev/null; then
		say "remove  $SYSTEM"
	elif [ -d "$SYSTEM" ]; then
		warn "kept    $SYSTEM: something else is still in it"
	fi
}

do_uninstall() {
	parts= deps=no
	for a in "$@"; do
		case $a in
		--purge) PURGE=yes ;;
		--deps|0) deps=yes ;;
		-a) EMERGE_OPTS=-qva ;;
		-*) die "unknown option: $a" ;;
		*)
			ps=$(expand "$a") || die "unknown part or group: $a (see --list)"
			parts="$parts $ps" ;;
		esac
	done
	full=no
	if [ -z "$parts" ] && [ "$deps" = no ]; then
		full=yes
		# everything but the browsers, which go only when named
		for p in $PARTS; do
			case " $APPS " in *" $p "*) ;; *) parts="$parts $p" ;; esac
		done
	fi
	# the reverse of the install order
	rev=
	for p in $parts; do rev="$p $rev"; done
	for p in $rev; do
		say "remove  part $p"
		if command -v "uninstall_$p" >/dev/null 2>&1; then
			"uninstall_$p" || warn "part $p did not finish"
		else
			uninstall_default "$p"
		fi
	done
	[ "$deps" = no ] || uninstall_deps
	if [ "$full" = yes ]; then
		# install.sh's own logs and lists, and the backups it put back
		remove "$LOG_DIR"
		rmdir_up "$BACKUP" 2>/dev/null || :
		# and the directories left empty
		[ ! -d "$SYSTEM" ] || find "$SYSTEM" -depth -type d -empty -delete
		back_home
	fi
	say "done; log in again so nothing uses the old settings"
}
