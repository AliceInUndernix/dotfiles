#!/bin/sh
# The part ksp: things for Kerbal Space Program, only when named
# (./install.sh ksp; not in "all"). Sourced by install.sh, not run.
#
#   kspkan     our own KSP mod manager, from the kspkan repo, cloned into
#              ~/.system/share/kspkan, built and installed into
#              ~/.system/bin like the suckless tools. Until that repo has
#              its code (a Makefile), this says so and goes on.
#   Principia  the n-body gravity mod, from a zip you download yourself
#              (its README links the binary; it is not on GitHub's
#              release pages, so nothing here fetches it for you):
#                PRINCIPIA_ZIP=~/Principia-Lie.zip ./install.sh ksp
#              or the newest ~/[Pp]rincipia*.zip. It goes into
#              GameData/Principia of KSP_DIR, or of the first KSP found
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

# principia_zip: PRINCIPIA_ZIP, or the newest Principia zip in ~.
principia_zip() {
	if [ -n "${PRINCIPIA_ZIP:-}" ]; then
		echo "$PRINCIPIA_ZIP"
		return
	fi
	ls -t "$HOME"/[Pp]rincipia*.zip 2>/dev/null | head -n 1
}

# ksp_libs_missing: the libraries principia.so can't find (ldd), or the
# LLVM runtime packages not installed when there is no principia.so yet.
ksp_libs_missing() {
	so="$(ksp_dir)/GameData/Principia/Linux/x64/principia.so"
	if [ -f "$so" ]; then
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
	z=$(principia_zip)
	if [ -z "$z" ] || [ ! -f "$z" ]; then
		say "note    no Principia zip in $HOME: download it from the link in"
		say "        https://github.com/mockingbirdnest/Principia#readme into ~,"
		say "        then ./install.sh ksp (or PRINCIPIA_ZIP=FILE ./install.sh ksp)"
		return 0
	fi
	# the same zip as last time: nothing to do
	mark=$STATE/ksp/principia-zip
	sum=$(cksum < "$z")
	dst=$k/GameData/Principia
	if [ -d "$dst" ] && [ "$(cat "$mark" 2>/dev/null)" = "$sum $dst" ]; then
		say "ok      $dst (from $(basename "$z"))"
		return 0
	fi
	command -v unzip >/dev/null 2>&1 ||
		die "unzip not found (su -c '$DOTFILES/install.sh --deps')"
	# Look before unpacking: only Principia/..., no absolute paths or
	# "..", and not more than PRINCIPIA_MAX_BYTES in all.
	bad=$(unzip -Z1 "$z" | grep -v -e '^Principia/' -e '^GameData/Principia/' |
		head -n 1) || :
	[ -z "$bad" ] || die "$z has $bad, not only Principia/; not installed"
	! unzip -Z1 "$z" | grep -q -e '^/' -e '\.\./' -e '^\.\.' ||
		die "$z has paths outside its directory; not installed"
	size=$(unzip -Zt "$z" | awk '{ print $3 }')
	case $size in ''|*[!0-9]*) die "can't read the size of $z" ;; esac
	[ "$size" -le "$PRINCIPIA_MAX_BYTES" ] ||
		die "$z unpacks to $size bytes (over $PRINCIPIA_MAX_BYTES); not installed"
	tmp=$CACHE/principia.$STAMP
	userdir "$tmp"
	# and while unpacking: no file bigger than that either
	if ! ( ulimit -f $((PRINCIPIA_MAX_BYTES / 512)); unzip -q "$z" -d "$tmp" )
	then
		rm -rf "$tmp"
		die "unpacking $z failed"
	fi
	new=$tmp/Principia
	[ -d "$new" ] || new=$tmp/GameData/Principia
	# the old one out of GameData (KSP would load both)
	if [ -e "$dst" ]; then
		userdir "$BACKUP/ksp"
		mv "$dst" "$BACKUP/ksp/Principia.$STAMP"
		say "backup  $dst -> $BACKUP/ksp/Principia.$STAMP"
	fi
	mv "$new" "$dst"
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
