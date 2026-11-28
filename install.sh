#!/bin/sh
# Install these dotfiles on a Linux system. Run it as yourself; only a
# few steps need root, and then it says so and stops (run those with
# su -c, never sudo or doas):
#   --deps   system packages          slock   must be root's to lock
#   ksh      making ksh the login shell, and root's copies of the files
#   steno    adding you to the serial port's group (dialout), once
#   x        no Caps Lock on any keyboard (/etc/X11/xorg.conf.d)
#   net      Riseup VPN, Tor and the kill switch (Gentoo with OpenRC;
#            net/README)
#   icecat, chromium   the browsers (emerge)
#   ksp      libc++, libc++abi and libunwind for Principia (emerge)
#
# The work is in three steps, each a file in install/ (lib.sh, net.sh
# and check.sh are shared by them):
#   1 packages.sh  the system's packages (--deps)
#   2 apps.sh      the big browsers, only the ones you name
#   3 configs.sh   the configs and our own programs (the parts)
#
#   ./install.sh               every part that needs no root (as "all"):
#                              the configs for what is there, no questions
#   ./install.sh nvim dwm      only the parts named
#   ./install.sh desktop media whole groups of parts (--list shows them)
#   ./install.sh pick          a menu: choose parts and groups with the
#                              keyboard, then install them
#   ./install.sh uninstall [--purge] [part|group...]
#                              take out what it put in (everything when
#                              none is named; ./uninstall.sh is the same)
#   ./install.sh 1 4 19        the parts with those numbers (0: --deps;
#                              ./install.sh --list shows the order)
#   ./install.sh all icecat    every part, and the parts named too
#   ./install.sh --deps        step 1: the system's packages (emerge -qv)
#   ./install.sh -a --deps     the same, but emerge asks first (-qva)
#   ./install.sh icecat chromium   step 2: the browsers IceCat and
#                              ungoogled-chromium (Gentoo, as root, from
#                              source, takes hours; not in "all")
#   ./install.sh ksp           Kerbal Space Program: kspkan and the
#                              Principia mod (install/ksp.sh; not in "all")
#   ./install.sh claude        the Claude desktop app: fetched, checked
#                              against Anthropic's signing key, and
#                              unpacked into ~/.system (run it again to
#                              update; claude-desktop/)
#   ./install.sh --new-pc      a new computer, as root: --deps, all (net
#                              and slock too), and asks which browsers to
#                              build (or name them: --new-pc icecat)
#   ./install.sh check [part]  only look: what is installed, missing or
#                              out of date (changes nothing)
#   ./install.sh update [--deps] [part...]  the newest version: git
#                              pull, then those parts (all when none),
#                              with --deps the packages first, then check
#   ./install.sh --list        show the parts
#
# The configs are for the user who owns this repo directory (or
# INSTALL_USER=name ./install.sh as root). Everything put in that
# user's home belongs to them, not to root. Configs are symlinked, so
# editing them in ~/.system/config edits this repo. Anything already in
# the way is moved to ~/.system/state/backup, never deleted. Every
# program it builds goes into the user's ~/.system (commands in
# ~/.system/bin, first in PATH, settings in ~/.system/config); in ~
# only the links ~/.profile and ~/.config. Outside the home there are
# only links, into /root/.system (see net/README).
# POSIX sh, so it runs on any distro. Each run is logged, with facts
# about the system, to ~/.system/state/install/latest.log.

DOTFILES=$(cd "$(dirname "$0")" && pwd)
for arg in "$@"; do case $arg in -h|--help)
	sed -n '2,/^$/p' "$0" | sed '/^$/d; s/^# \{0,1\}//'; exit 0 ;;
esac; done
. "$DOTFILES/install/lib.sh"
. "$DOTFILES/install/packages.sh"
. "$DOTFILES/install/apps.sh"
. "$DOTFILES/install/configs.sh"
. "$DOTFILES/install/net.sh"
. "$DOTFILES/install/ksp.sh"
. "$DOTFILES/install/check.sh"
. "$DOTFILES/install/groups.sh"
. "$DOTFILES/install/uninstall.sh"

# every part, group by group (install/groups.sh)
PARTS=
for g in $PART_GROUPS; do PARTS="$PARTS $(group_parts "$g")"; done
PARTS=$(echo $PARTS)
# only when named (or numbered): not in "all" or without arguments
ONLY_NAMED="ksp $APPS"

# --- main -----------------------------------------------------------------

# pick: the menu; then this script again with what was chosen. Parts
# that need root are left to a root run, and said so.
case ${1:-} in pick|--pick)
	shift
	sel=$(pick) || { echo; say "nothing chosen"; exit 0; }
	mine= rootp=
	for p in $sel; do
		case " --deps $ROOT_PARTS " in
		*" $p "*) [ "$AS_ROOT" = yes ] && mine="$mine $p" || rootp="$rootp $p" ;;
		*) mine="$mine $p" ;;
		esac
	done
	rc=0
	[ -z "$mine" ] || /bin/sh "$DOTFILES/install.sh" "$@" $mine || rc=$?
	[ -z "$rootp" ] ||
		say "these need root: su -c '$DOTFILES/install.sh$rootp'"
	exit $rc ;;
esac
EMERGE_OPTS=-qv  # -a: -qva, so emerge asks before it builds anything
APT_OPTS=-y      # -a: apt and pacman ask too
PACMAN_OPTS=--noconfirm
# uninstall: install/uninstall.sh
case ${1:-} in uninstall|--uninstall)
	shift
	do_uninstall "$@"
	exit $? ;;
esac
# group names stand for their parts
args=
for a in "$@"; do
	case " $PART_GROUPS " in
	*" $a "*) args="$args $(group_parts "$a")" ;;
	*) args="$args $a" ;;
	esac
done
# shellcheck disable=SC2086
set -- $args

want_deps=no
want_all=no
ask=no
asked=
parts=
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
	shift
	# parts, and --deps (and -a) for the packages too
	ckparts=
	for x in "$@"; do
		case $x in --deps|-a) continue ;; esac
		case " $PARTS " in *" $x "*) ckparts="$ckparts $x" ;;
		*) die "unknown part: $x (see --list)" ;; esac
	done
	say "pull    $DOTFILES"
	if [ "$AS_ROOT" = yes ]; then
		su -s /bin/sh "$USER_NAME" -c "git -C '$DOTFILES' pull --ff-only" ||
			die "git pull failed (changes not committed? no network?)"
	else
		git -C "$DOTFILES" pull --ff-only ||
			die "git pull failed (changes not committed? no network?)"
	fi
	rc=0
	/bin/sh "$DOTFILES/install.sh" "${@:-all}" || rc=$?
	echo
	say "check"
	/bin/sh "$DOTFILES/install.sh" check $ckparts || rc=$?
	exit $rc ;;
esac
for arg in "$@"; do
	case $arg in
	all) want_all=yes ;;
	--deps|0) want_deps=yes ;;
	--new-pc) want_deps=yes; want_all=yes; ask=yes ;;
	[1-9]|[1-9][0-9])
		p=$(echo $PARTS | awk -v n="$arg" '{ print $n }')
		[ -n "$p" ] || die "there is no part $arg (see ./install.sh --list)"
		parts="$parts $p" ;;
	-a) EMERGE_OPTS=-qva; APT_OPTS=; PACMAN_OPTS= ;;
	--list)
		n=0
		for g in $PART_GROUPS; do
			printf '%s:' "$g"
			for p in $(group_parts "$g"); do
				n=$((n + 1)); printf ' %s %s' "$n" "$p"
			done
			echo
		done
		exit 0 ;;
	-*) die "unknown option: $arg" ;;
	*)
		case " $PARTS " in *" $arg "*) ;; *) die "unknown part: $arg (see --list)" ;; esac
		parts="$parts $arg" ;;
	esac
done
# --new-pc with no browser named: ask which, before the hours of work
# (only the first run asks; the logged run below gets the answer)
if [ "$ask" = yes ] && [ -z "${INSTALL_LOG:-}" ]; then
	case " $parts " in *" icecat "*|*" chromium "*) ;; *)
		asked=$(ask_apps | tr '\n' ' ')
		parts="$parts $asked" ;;
	esac
fi
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
# the newest. A run started by this one (steno as the user) adds to it.
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
		for t in $TOOLS python3 loginctl; do
			if have "$t"; then found="$found $t"; else missing="$missing $t"; fi
		done
		echo "found   $found"
		echo "missing $missing"
		! command -v nvim >/dev/null 2>&1 || echo "nvim     $(nvim --version | head -n 1)"
		! command -v cc >/dev/null 2>&1 || echo "cc       $(cc --version | head -n 1)"
		echo "------------------------------------------------------------------"
	} >> "$INSTALL_LOG" 2>&1
	exec 3>&1
	rc=$( { { /bin/sh "$DOTFILES/install.sh" "$@" $asked 2>&1 || echo $? >&4; } |
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
	for p in $APPS; do
		case " $parts " in *" $p "*)
			die "$p is built with emerge, which needs root: su -c '$DOTFILES/install.sh $p'" ;;
		esac
	done
elif [ "$want_deps" = no ] && [ -n "$named" ]; then
	case " $named " in *" slock "*|*" ksh "*|*" steno "*|*" x "*|*" net "*|*" ksp "*|*" icecat "*|*" chromium "*) ;; *)
		say "note    these parts don't need root; next time just run ./install.sh$named" ;;
	esac
fi

if [ "$want_deps" = yes ]; then
	say "part    deps ($(date +%H:%M:%S))"
	install_packages
fi

# migrate_home: what older versions (and programs without these
# dotfiles) left in ~ moves into ~/.system. A run that root starts as
# the user for one part (INSTALL_NESTED) skips it: root's run did it.
migrate_home() {
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
		*/nvim) rm "$f"; continue ;;
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

# What older versions put into ~/.system/share (our own data only:
# Neovim plugins, fonts, steno, kspkan, moonlander, QMK, sioyek) goes
# back to ~/.local/share, where programs keep data. Anything else found
# in ~/.system/share or ~/.system/cache is not ours: it is only listed.
old_share=$SYSTEM/share
for n in nvim fonts steno kspkan kspkan-src moonlander qmk sioyek; do
	[ -e "$old_share/$n" ] && [ ! -L "$old_share/$n" ] || continue
	if [ -e "$DATA/$n" ] || [ -L "$DATA/$n" ]; then
		warn "kept    $old_share/$n ($DATA/$n already exists)"
		continue
	fi
	userdir "$DATA"
	mv "$old_share/$n" "$DATA/$n" && say "move    $old_share/$n -> $DATA/$n"
done
rmdir "$old_share" 2>/dev/null || :
for d in "$old_share" "$SYSTEM/cache"; do
	[ -d "$d" ] || continue
	warn "not ours, please look: $d ($(ls -A "$d" | tr '\n' ' '))"
done
# Only settings are moved: ~/.config goes into ~/.system/config. Data,
# state and caches (~/.local/share with Steam and games, ~/.cache and
# the like) are never moved or copied; programs that follow the XDG
# directories ~/.profile sets write there themselves, and the ones that
# don't stay where they are. Nothing is deleted; a name already taken in
# the new place is kept where it was and reported.
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
			if mv "$f" "$t" 2>/dev/null; then
				echo "move  $f -> $t" >> "$MOVED"
			else
				echo "kept  $f (could not move it to $t)" >> "$MOVED"
			fi
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
# Settings too: ~/.config moves into ~/.system/config, and ~/.config
# becomes a link to it, for programs that don't ask XDG_CONFIG_HOME.
if [ ! -L "$HOME/.config" ]; then
	moveinto "$HOME/.config" "$CONFIG"
	if [ -e "$HOME/.config" ]; then
		warn "kept    $HOME/.config: some of it is already in $CONFIG (list: $MOVED)"
	else
		userdir "$CONFIG"
		ln -s .system/config "$HOME/.config"
		own "$HOME/.config"
		say "link    $HOME/.config -> .system/config"
	fi
elif [ "$(readlink "$HOME/.config")" != .system/config ]; then
	warn "$HOME/.config is a link to $(readlink "$HOME/.config"), not .system/config"
fi
# Backups made in ~ by older versions (~/.profile.bak.<date> ...) go
# to ~/.system/state/backup, with the others.
for f in "$HOME"/.*.bak.[0-9]*-[0-9]*; do
	[ -e "$f" ] || [ -L "$f" ] || continue
	b=${f##*/}
	userdir "$BACKUP"
	mv "$f" "$BACKUP/${b%%.bak.*}.${b##*.bak.}"
	say "move    $f -> $BACKUP/"
done
if [ -f "$HOME/.ksh_history" ] && [ ! -e "$STATE/ksh/history" ]; then
	userdir "$STATE/ksh"
	mv "$HOME/.ksh_history" "$STATE/ksh/history"
	say "move    $HOME/.ksh_history -> $STATE/ksh/history"
fi
}
[ "${INSTALL_NESTED:-}" = yes ] || migrate_home

# Each part on its own: one that fails (no internet for steno, say)
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
# their caches and state (fc-cache, nvim): they belong to the user,
# or the user's programs can't update them. slock stays root's.
if [ "$AS_ROOT" = yes ]; then
	n=$(fix_owner)
	[ "$n" -eq 0 ] || say "owner   $n files root made in $SYSTEM and $CONFIG -> $USER_NAME"
fi
# The user's files are theirs alone, like their umask 077 makes new ones:
# take group and other permissions off everything in their home.
chmod 700 "$HOME"
# slock is left alone: it is root's, so you could no longer run it.
n=$(find "$HOME" -xdev ! -type l ! -path "$SYSBIN/slock" -user "$USER_NAME" \
	-perm /077 -print -exec chmod go= {} + | wc -l)
[ "$n" -eq 0 ] || say "private $n files and directories in $HOME (only $USER_NAME and root)"
[ -z "$skipped" ] ||
	say "skipped $skipped (needs root): su -c '$DOTFILES/install.sh $skipped'"
if [ -n "$failed" ]; then
	warn "these parts did not finish:$failed (the messages above say why;"
	warn "no internet? parts that download: steno, moonlander, net)"
	exit 1
fi
