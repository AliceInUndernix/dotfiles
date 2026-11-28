#!/bin/sh
# The part ksp: things for Kerbal Space Program, only when named
# (./install.sh ksp; not in "all"). Sourced by install.sh, not run.
#
#   kspkan     our own KSP mod manager, from the kspkan repo, cloned into
#              ~/.system/share/kspkan-src, built and installed into
#              ~/.system/bin like the suckless tools. Until that repo has
#              its code (a Makefile), this says so and goes on.
#   Principia  the n-body gravity mod, from a zip you download yourself
#              (its README links the binary; it is not on GitHub's
#              release pages, so nothing here fetches it for you):
#                PRINCIPIA_ZIP=~/Principia-Lie.zip ./install.sh ksp
#              or it finds every Principia*.zip in ~ and asks which.
#              Principia's folder is found inside the zip whatever its
#              layout (principia_root), and goes into GameData/Principia
#              of KSP_DIR, or of the first KSP found
#              in the usual places (Steam, GOG, ~/KSP). An old one is
#              moved to ~/.system/state/backup/ksp/Principia.<date>.
#
# Principia's Linux build needs libc++, libc++abi and libunwind (LLVM's,
# 20 or newer). They are system packages: a root run emerges them
# (su -c './install.sh ksp'), then does the rest as you.

KSPKAN_REPO=${KSPKAN_REPO:-https://github.com/AliceInUndernix/kspkan}
# the source; kspkan keeps its own data (game folders, mod lists,
# downloads) in $DATA/kspkan, so the two stay apart
KSPKAN_SRC=$DATA/kspkan-src
KSP_LIBS="llvm-runtimes/libcxx llvm-runtimes/libcxxabi llvm-runtimes/libunwind"
# a zip that unpacks to more than this is refused (zip bombs)
PRINCIPIA_MAX_BYTES=2000000000

# ksp_dir: KSP_DIR, or the first KSP installation found; nothing if none.
ksp_dir() {
	if [ -n "${KSP_DIR:-}" ]; then
		echo "$KSP_DIR"
		return
	fi
	for d in "$DATA/Steam/steamapps/common/Kerbal Space Program" \
		"$HOME/.steam/steam/steamapps/common/Kerbal Space Program" \
		"$HOME/GOG Games/Kerbal Space Program/game" \
		"$HOME/GOG Games/Kerbal Space Program" \
		"$HOME/KSP" "$HOME/ksp"
	do
		if [ -d "$d/GameData" ]; then
			echo "$d"
			return
		fi
	done
}

# principia_zips: every Principia*.zip anywhere in ~ (one file system),
# the newest first, one a line
principia_zips() {
	find "$HOME" -xdev -type f -iname 'principia*.zip' 2>/dev/null |
		while IFS= read -r f; do
			echo "$(date -r "$f" +%s) $f"
		done | sort -rn | cut -d ' ' -f 2-
}

# principia_ask <zips>: ask which zip to install from, on the terminal.
# Prints the one chosen; nothing for no, or with no terminal to ask on.
principia_ask() {
	# the terminal: stdin when it is one, else /dev/tty if it opens
	if [ -t 0 ]; then tty=
	elif { true < /dev/tty; } 2>/dev/null; then tty=/dev/tty
	else return 0; fi
	echo "Principia zips found in $HOME:" >&2
	n=0
	echo "$1" | while IFS= read -r f; do
		n=$((n + 1))
		mb=$(( $(wc -c < "$f") / 1048576 ))
		printf '  %2d  %s  (%s, %d MB)\n' "$n" "$f" \
			"$(date -r "$f" +%Y-%m-%d)" "$mb" >&2
	done
	count=$(echo "$1" | wc -l)
	while :; do
		printf 'Install Principia from 1? (y, a number, or n): ' >&2
		if [ -z "$tty" ]; then read -r a || a=n
		else read -r a < "$tty" || a=n; fi
		case $a in
		y|Y|yes|'') a=1 ;;
		n|N|no) return 0 ;;
		esac
		case $a in
		*[!0-9]*) ;;
		*)
			if [ "$a" -ge 1 ] && [ "$a" -le "$count" ]; then
				echo "$1" | sed -n "${a}p"
				return 0
			fi ;;
		esac
		echo "  y installs the first, a number that one, n none" >&2
	done
}

# principia_root <dir>: the Principia folder somewhere under an
# unpacked zip, whatever its layout (Principia/, GameData/Principia/,
# a release folder around them, or the files right at the top). It is
# the folder of ksp_plugin_adapter.dll, the part KSP loads; if that is
# missing, the folder named Principia above principia.so. The shallowest
# wins. Nothing if there is none.
principia_root() {
	f=$(find "$1" -type f -iname ksp_plugin_adapter.dll |
		awk -F / '{ print NF " " $0 }' | sort -n | head -n 1 |
		cut -d ' ' -f 2-)
	if [ -n "$f" ]; then
		dirname "$f"
		return
	fi
	f=$(find "$1" -type f -name principia.so |
		awk -F / '{ print NF " " $0 }' | sort -n | head -n 1 |
		cut -d ' ' -f 2-)
	[ -n "$f" ] || return 0
	d=$(dirname "$f")
	while [ "$d" != "$1" ] && [ "$d" != / ]; do
		case $(basename "$d") in
		[Pp]rincipia) echo "$d"; return ;;
		esac
		d=$(dirname "$d")
	done
	# no folder named Principia: the files are right at the top
	echo "$1"
}

# ksp_libs_missing: the libraries principia.so can't find (ldd), or the
# LLVM runtime packages not installed when there is no principia.so yet.
ksp_libs_missing() {
	so=
	p="$(ksp_dir)/GameData/Principia"
	[ ! -d "$p" ] ||
		so=$(find "$p" -type f -name principia.so | head -n 1)
	if [ -n "$so" ]; then
		ldd "$so" 2>/dev/null | awk '/not found/ { printf " %s", $1 }'
	elif command -v portageq >/dev/null 2>&1; then
		for a in $KSP_LIBS; do
			portageq has_version / "$a" || printf ' %s' "$a"
		done
	fi
}

# kspkan_move_old: older versions cloned the source into $DATA/kspkan,
# where kspkan keeps its data too. Take out only the source and the
# build output there; the data stays.
kspkan_move_old() {
	old=$DATA/kspkan
	[ -d "$old/.git" ] || return 0
	say "move    the kspkan source out of $old (its data stays)"
	git -C "$old" ls-files | while read -r f; do
		rm -f "$old/$f"
	done
	rm -rf "$old/.git"
	rm -f "$old"/*.o "$old/kspkan" "$old/kspkan.new"
	[ ! -d "$old/tests" ] || find "$old/tests" -depth -type d -empty \
		-exec rmdir {} \;
	rmdir "$old" 2>/dev/null || :
}

install_kspkan() {
	command -v git >/dev/null 2>&1 ||
		die "git not found (su -c '$DOTFILES/install.sh --deps')"
	kspkan_move_old
	if [ -d "$KSPKAN_SRC/.git" ]; then
		if git_follow "$KSPKAN_SRC"; then
			say "ok      $KSPKAN_SRC ($(git -C "$KSPKAN_SRC" log -1 --format=%h))"
		else
			warn "could not update $KSPKAN_SRC; building what is there"
		fi
	else
		userdir "$DATA"
		say "clone   $KSPKAN_REPO -> $KSPKAN_SRC"
		git clone --quiet "$KSPKAN_REPO" "$KSPKAN_SRC" || {
			warn "could not clone $KSPKAN_REPO (no network?); kspkan skipped"
			return 0
		}
	fi
	if [ ! -f "$KSPKAN_SRC/Makefile" ]; then
		say "note    kspkan has no code yet (no Makefile in $KSPKAN_REPO);"
		say "        run ./install.sh ksp again once it is pushed"
		return 0
	fi
	userdir "$SYSBIN"
	built=yes
	( cd "$KSPKAN_SRC" && make PREFIX="$SYSTEM" &&
		make PREFIX="$SYSTEM" install ) || built=no
	( cd "$KSPKAN_SRC" && make clean >/dev/null 2>&1 ) || :
	[ "$built" = yes ] || die "building kspkan failed"
	say "install kspkan -> $SYSBIN/kspkan"
}

install_principia() {
	k=$(ksp_dir)
	if [ -z "$k" ] || [ ! -d "$k/GameData" ]; then
		warn "no KSP found (no GameData); Principia skipped:"
		warn "    KSP_DIR=/path/to/KSP ./install.sh ksp"
		return 0
	fi
	mark=$STATE/ksp/principia-zip
	dst=$k/GameData/Principia
	if [ -n "${PRINCIPIA_ZIP:-}" ]; then
		z=$PRINCIPIA_ZIP
		[ -f "$z" ] || die "PRINCIPIA_ZIP: no file $z"
	else
		zips=$(principia_zips)
		if [ -z "$zips" ]; then
			say "note    no Principia zip in $HOME: download it from the link in"
			say "        https://github.com/mockingbirdnest/Principia#readme,"
			say "        then ./install.sh ksp (or PRINCIPIA_ZIP=FILE ./install.sh ksp)"
			return 0
		fi
		# the newest is the one installed last time: nothing to ask
		z=$(echo "$zips" | head -n 1)
		if [ -d "$dst" ] &&
			[ "$(cat "$mark" 2>/dev/null)" = "$(cksum < "$z") $dst" ]
		then
			say "ok      $dst (from $(basename "$z"))"
			return 0
		fi
		z=$(principia_ask "$zips")
		if [ -z "$z" ]; then
			say "note    Principia not installed (asked no, or no terminal"
			say "        to ask on: PRINCIPIA_ZIP=FILE ./install.sh ksp)"
			return 0
		fi
	fi
	# the same zip as last time: nothing to do
	sum=$(cksum < "$z")
	if [ -d "$dst" ] && [ "$(cat "$mark" 2>/dev/null)" = "$sum $dst" ]; then
		say "ok      $dst (from $(basename "$z"))"
		return 0
	fi
	command -v unzip >/dev/null 2>&1 ||
		die "unzip not found (su -c '$DOTFILES/install.sh --deps')"
	# Look before unpacking: no absolute paths or "..", and not more
	# than PRINCIPIA_MAX_BYTES in all. The layout inside the zip is not
	# trusted to stay the same: Principia's folder is found afterwards.
	! unzip -Z1 "$z" | grep -q -e '^/' -e '\.\./' -e '^\.\.' ||
		die "$z has paths outside its directory; not installed"
	size=$(unzip -Zt "$z" | awk '{ print $3 }')
	case $size in ''|*[!0-9]*) die "can't read the size of $z" ;; esac
	[ "$size" -le "$PRINCIPIA_MAX_BYTES" ] ||
		die "$z unpacks to $size bytes (over $PRINCIPIA_MAX_BYTES); not installed"
	tmp=$(mktemp -d "${TMPDIR:-/tmp}/principia.XXXXXX") ||
		die "no temporary directory"
	# and while unpacking: no file bigger than that either
	if ! ( ulimit -f $((PRINCIPIA_MAX_BYTES / 512)); unzip -q "$z" -d "$tmp" )
	then
		rm -rf "$tmp"
		die "unpacking $z failed"
	fi
	if [ -n "$(find "$tmp" -type l | head -n 1)" ]; then
		rm -rf "$tmp"
		die "$z has symbolic links; not installed"
	fi
	new=$(principia_root "$tmp")
	if [ -z "$new" ]; then
		rm -rf "$tmp"
		die "no Principia in $z (no ksp_plugin_adapter.dll or principia.so)"
	fi
	[ -n "$(find "$new" -type f -name principia.so | head -n 1)" ] ||
		warn "$z has no principia.so: is it the Linux build?"
	# whatever else is next to it (other mods, read-me files) stays out
	[ "$new" = "$tmp" ] ||
	for o in "$(dirname "$new")"/*; do
		[ -e "$o" ] && [ "$o" != "$new" ] || continue
		say "skip    $(basename "$o") (also in the zip; not Principia)"
	done
	# the old one out of GameData (KSP would load both)
	if [ -e "$dst" ]; then
		userdir "$BACKUP/ksp"
		mv "$dst" "$BACKUP/ksp/Principia.$STAMP"
		say "backup  $dst -> $BACKUP/ksp/Principia.$STAMP"
	fi
	mv "$new" "$dst" || {
		rm -rf "$tmp"
		die "could not move Principia into $dst"
	}
	rm -rf "$tmp"
	own "$dst"
	userdir "$STATE/ksp"
	echo "$sum $dst" > "$mark"
	own "$mark"
	say "install $(basename "$z") -> $dst"
	say "note    start a new save for Principia; never add it to an old one"
}

install_ksp() {
	if [ "$AS_ROOT" = yes ]; then
		if command -v emerge >/dev/null 2>&1; then
			( root_env; emerge $EMERGE_OPTS --noreplace $KSP_LIBS )
		elif command -v apt-get >/dev/null 2>&1; then
			# the newest LLVM the release has, 20 or newer
			v=
			for n in 23 22 21 20; do
				apt-cache show "libc++1-$n" >/dev/null 2>&1 || continue
				v=$n
				break
			done
			if [ -n "$v" ]; then
				apt-get install $APT_OPTS "libc++1-$v" "libc++abi1-$v" \
					"libunwind-$v"
			else
				warn "this release has no libc++ 20 or newer; Principia needs it"
			fi
		elif command -v pacman >/dev/null 2>&1; then
			pacman -S $PACMAN_OPTS --needed libc++ libc++abi
		else
			warn "install libc++, libc++abi and libunwind (LLVM 20+)" \
				"with your package manager"
		fi
		fix_owner >/dev/null
		su -s /bin/sh "$USER_NAME" -c "INSTALL_LOG='$INSTALL_LOG' \
INSTALL_NESTED=yes KSP_DIR='${KSP_DIR:-}' PRINCIPIA_ZIP='${PRINCIPIA_ZIP:-}' \
'$DOTFILES/install.sh' ksp"
		return
	fi
	install_kspkan
	install_principia
	m=$(ksp_libs_missing)
	[ -z "$m" ] ||
		warn "Principia needs$m: su -c '$DOTFILES/install.sh ksp'"
}

# check: ./install.sh check ksp
ck_ksp() {
	if [ -x "$SYSBIN/kspkan" ]; then ck_ok "kspkan"
	else ck_info "kspkan is not installed: ./install.sh ksp"; fi
	k=$(ksp_dir)
	if [ -z "$k" ]; then
		ck_info "no KSP found (KSP_DIR=/path/to/KSP)"
		return
	fi
	if [ -d "$k/GameData/Principia" ]; then ck_ok "Principia in $k"
	else ck_info "Principia is not in $k: ./install.sh ksp"; fi
	m=$(ksp_libs_missing)
	if [ -z "$m" ]; then ck_ok "Principia's libraries"
	else ck_bad "Principia needs$m: su -c '$DOTFILES/install.sh ksp'"; fi
}
