# What every part of install.sh shares: who the configs are for, where
# things go, and the small helpers. Sourced by install.sh, not run.

set -eu
# What gets installed system-wide must be readable by everyone, whatever
# umask the shell that started us has (the user's is 077).
umask 022

STAMP=$(date +%Y%m%d-%H%M%S)

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
# login: only settings and a little state live in ~/.system (settings in
# ~/.system/config, ~/.config is a link to it, for programs that don't
# ask). Data stays in ~/.local/share and caches in ~/.cache, where
# programs put them: DATA below is only what this installer puts in
# place, and no XDG_DATA_HOME or XDG_CACHE_HOME redirects programs.
CONFIG=$SYSTEM/config
DATA=$HOME/.local/share
STATE=$SYSTEM
export XDG_CONFIG_HOME="$CONFIG" XDG_STATE_HOME="$STATE"
unset XDG_DATA_HOME XDG_CACHE_HOME
PREFIX=${PREFIX:-$SYSTEM}     # our programs: PREFIX/bin = ~/.system/bin
export HOME
LOG_DIR=$STATE/install

# own <path>...: give files root made in the user's home to the user.
own() { chown -hR "$USER_NAME:$USER_GROUP" "$@"; }

# fix_owner: as root, give the user back what root's programs (fc-cache,
# a package manager) wrote into the user's ~/.system; slock stays
# root's. Done before the user's own runs and at the end. Prints how
# many it changed.
fix_owner() {
	[ "$AS_ROOT" = yes ] || { echo 0; return 0; }
	find "$SYSTEM" "$CONFIG" -xdev ! -path "$SYSBIN/slock" \
		! -user "$USER_NAME" -print \
		-exec chown -h "$USER_NAME:$USER_GROUP" {} + 2>/dev/null | wc -l
}

# userdir <dir>: mkdir -p, with every directory it creates owned by the user.
userdir() {
	[ -d "$1" ] && return
	userdir "$(dirname "$1")"
	mkdir "$1"
	chown "$USER_NAME:$USER_GROUP" "$1"
}

# Backups of what was in the way of a link: in your home they go to
# ~/.system/backup/<path>.<date>, so nothing but links is left
# outside ~/.system; elsewhere <path>.bak.<date>, next to it.
BACKUP=$STATE/backup
bak_path() {
	case $1 in
	"$HOME"/*) echo "$BACKUP/${1#"$HOME"/}.$STAMP" ;;
	*) echo "$1.bak.$STAMP" ;;
	esac
}
# newest_bak <path>: its newest backup, of either kind; nothing if none
newest_bak() {
	case $1 in
	"$HOME"/*) ls -d "$BACKUP/${1#"$HOME"/}".[0-9]*-[0-9]* \
		"$1".bak.* 2>/dev/null | awk -F. '{ print $NF " " $0 }' |
		sort | tail -n 1 | cut -d ' ' -f 2- ;;
	*) ls -d "$1".bak.* 2>/dev/null | sort | tail -n 1 ;;
	esac
}

# link <file or dir in repo> <target>: symlink, backing up what is there.
link() {
	src=$1 dst=$2
	if [ -L "$dst" ] && [ "$(readlink "$dst")" = "$src" ]; then
		say "ok      $dst"
		return
	fi
	if [ -e "$dst" ] || [ -L "$dst" ]; then
		b=$(bak_path "$dst")
		userdir "$(dirname "$b")"
		mv "$dst" "$b"
		say "backup  $dst -> $b"
	fi
	userdir "$(dirname "$dst")"
	ln -s "$src" "$dst"
	own "$dst"
	say "link    $dst -> $src"
}

# unlink <file or dir in repo> <target>: take away a link that link()
# made, and put back what was there before it (the newest .bak.<date>).
unlink() {
	src=$1 dst=$2
	if [ -L "$dst" ] && [ "$(readlink "$dst")" = "$src" ]; then
		rm "$dst"
		say "remove  $dst"
	fi
	bak=$(newest_bak "$dst") || :
	if [ -n "$bak" ] && [ ! -e "$dst" ] && [ ! -L "$dst" ]; then
		userdir "$(dirname "$dst")"
		mv "$bak" "$dst"
		say "restore $dst (from $bak)"
		rmdir_up "$(dirname "$bak")"
	fi
	rmdir_up "$(dirname "$dst")"
}

# rmdir_up <dir>: remove <dir> and its parents while they are empty,
# up to (not including) ~/.system/config, ~/.system and ~.
rmdir_up() {
	d=$1
	while [ "$d" != "$CONFIG" ] && [ "$d" != "$SYSTEM" ] &&
		[ "$d" != "$HOME" ] && [ "$d" != / ] && rmdir "$d" 2>/dev/null
	do
		d=$(dirname "$d")
	done
}

# Root's things are root's own, never links into the user's home (or
# anything running as the user could change what root runs). Like the
# user's, they live in one place, root's ~/.system, and everything
# outside it is a link there:
#   /root/.system/config/ksh/  root's profile and kshrc
#   /root/.system/net/         vpn, its helpers and settings (net)
# /root and /root/.system can be gone through (711) but not listed, so
# the links in /usr/local and /etc work for everyone; only net/ is
# readable, and config/ is root's alone.
ROOT_SYSTEM=$(h=$(getent passwd root 2>/dev/null || grep '^root:' /etc/passwd)
	h=${h%:*}; echo "${h##*:}")/.system

# root_put <file> <path in root's ~/.system> <mode> [group]: a copy of
# <file> there, owned by root (and the group). Prints where it is.
root_put() {
	t=$ROOT_SYSTEM/$2
	[ -d "$ROOT_SYSTEM" ] || install -d -m 700 "$ROOT_SYSTEM"
	mkdir -p "${t%/*}"
	if ! [ -f "$t" ] || ! cmp -s "$1" "$t"; then
		cp "$1" "$t.new"
		chown "root:${4:-$(id -gn root)}" "$t.new"
		chmod "$3" "$t.new"
		mv "$t.new" "$t"
	fi
	chown "root:${4:-$(id -gn root)}" "$t"
	chmod "$3" "$t"
	echo "$t"
}

# root_link <file in root's ~/.system> <target> [keep]: <target> becomes
# a link to it. What was at <target> is replaced; with "keep" (a file
# of the system's, not one of ours) it is kept as <target>.bak.<date>.
root_link() {
	if [ -L "$2" ] && [ "$(readlink "$2")" = "$1" ]; then
		say "ok      $2 -> $1"
		return
	fi
	if [ -e "$2" ] || [ -L "$2" ]; then
		if [ "${3:-}" = keep ] && [ ! -L "$2" ]; then
			b=$ROOT_SYSTEM/state/backup$2.$STAMP
			mkdir -p "${b%/*}"
			chmod 700 "$ROOT_SYSTEM/state"
			mv "$2" "$b"
			say "backup  $2 -> $b"
		else
			rm -rf "$2"
		fi
	fi
	mkdir -p "${2%/*}"
	ln -s "$1" "$2"
	say "link    $2 -> $1"
}

# root_backup <file>: a copy of a system file before it is changed in
# place, in root's ~/.system/state/backup/<file>.<date>. Prints where.
root_backup() {
	b=$ROOT_SYSTEM/state/backup$1.$STAMP
	[ -d "$ROOT_SYSTEM" ] || install -d -m 700 "$ROOT_SYSTEM"
	mkdir -p "${b%/*}"
	chmod 700 "$ROOT_SYSTEM/state"
	cp -p "$1" "$b"
	echo "$b"
}

# root_newest_bak <path>: the newest backup root_link (or an older
# version, <path>.bak.<date>) made of it; nothing if none
root_newest_bak() {
	ls -d "$ROOT_SYSTEM/state/backup$1".[0-9]*-[0-9]* "$1".bak.* 2>/dev/null |
		awk -F. '{ print $NF " " $0 }' | sort | tail -n 1 | cut -d ' ' -f 2-
}

# rootcopy <file in repo> <target>: root's own copy in root's ~/.system
# (config/<repo dir>/<name>), and <target> a link to it; what the system
# had there is kept in root's ~/.system/state/backup/<target>.<date>.
rootcopy() {
	d=${1%/*}
	c=$(root_put "$1" "config/${d##*/}/${1##*/}" 644)
	root_link "$c" "$2" keep
}

# unrootcopy <file in repo> <target>: remove what rootcopy() made (or
# an older copy, still the repo's), and put back the newest .bak.<date>.
unrootcopy() {
	d=${1%/*}
	c=$ROOT_SYSTEM/config/${d##*/}/${1##*/}
	if [ -L "$2" ] && [ "$(readlink "$2")" = "$c" ]; then
		rm "$2"
		say "remove  $2"
	elif [ -f "$2" ] && [ ! -L "$2" ] && cmp -s "$1" "$2"; then
		rm "$2"
		say "remove  $2"
	fi
	rm -f "$c"
	rmdir "${c%/*}" 2>/dev/null || :
	bak=$(root_newest_bak "$2") || :
	if [ -n "$bak" ] && [ ! -e "$2" ]; then
		mv "$bak" "$2"
		say "restore $2 (from $bak)"
	fi
}

# home_of <user>: that user's home directory.
home_of() {
	h=$(getent passwd "$1" 2>/dev/null || grep "^$1:" /etc/passwd)
	h=${h%:*}
	echo "${h##*:}"
}

# as_user <command>: run a shell command as the user (su from root).
as_user() {
	if [ "$AS_ROOT" = yes ]; then
		su -s /bin/sh "$USER_NAME" -c "$1"
	else
		sh -c "$1"
	fi
}

# git_follow <dir>: bring a clone that only follows upstream (moonlander,
# kspkan) up to date. A fast-forward if it can; if upstream's history
# was started again (no commit in common) and the clone has no changes
# of its own, it is reset to upstream. Fails when neither works.
git_follow() {
	rc=0
	as_user "d='$1'
		git -C \"\$d\" fetch --quiet origin || exit 1
		git -C \"\$d\" merge --quiet --ff-only '@{upstream}' \
			>/dev/null 2>&1 && exit 0
		git -C \"\$d\" merge-base HEAD '@{upstream}' >/dev/null 2>&1 &&
			exit 1
		[ -z \"\$(git -C \"\$d\" status --porcelain -uno)\" ] || exit 1
		git -C \"\$d\" reset --quiet --hard '@{upstream}' || exit 1
		exit 3" || rc=$?
	case $rc in
	0) return 0 ;;
	3) say "reset   $1 to upstream (its history was started again)"
		return 0 ;;
	esac
	return 1
}

# serial_group: the group of the Moonlander's serial port (/dev/ttyACM0):
# dialout, or uucp on Arch.
serial_group() {
	g=$(stat -c %G /dev/ttyACM0 2>/dev/null || :)
	case $g in ""|root) g=dialout ;; esac
	echo "$g"
}

# in_group <group>: is the user in it?
in_group() {
	case " $(id -nG "$USER_NAME") " in *" $1 "*) return 0 ;; esac
	return 1
}

# --- Gentoo ------------------------------------------------------------------

# The package managers run as root with root's own home and settings,
# not the user's: otherwise eselect and emerge leave root's caches in the
# user's ~/.system. Call it in a subshell, so the rest keeps the user's.
root_env() {
	HOME=$(home_of root)
	unset XDG_CONFIG_HOME XDG_DATA_HOME XDG_STATE_HOME
}

# enable_overlay <name>: add a Gentoo overlay with eselect repository.
enable_overlay() {
	portageq get_repo_path / "$1" >/dev/null 2>&1 && return
	say "enable  overlay $1"
	emerge $EMERGE_OPTS --noreplace app-eselect/eselect-repository
	eselect repository enable "$1"
	emerge $EMERGE_OPTS --sync "$1"
}

# portage <kind> <atom> <words>: a line in /etc/portage/package.<kind>
# (use, license, accept_keywords), in a file of our own, zz-dotfiles:
# Portage reads such a directory in name order and the last file wins,
# so an older line elsewhere can't undo ours. Nothing is written when
# the atom already has a line there.
portage() {
	f=/etc/portage/package.$1
	if [ -d "$f" ] || [ ! -e "$f" ]; then
		mkdir -p "$f"
		f=$f/zz-dotfiles
	fi
	grep -qs "^$2[[:space:]]" "$f" && return
	echo "$2 $3" >> "$f"
	chmod 644 "$f"
	say "$1 $2 $3 -> $f"
}

# keyword <atom> [keyword]: allow a package from testing (~amd64, or the
# keyword given).
keyword() { portage accept_keywords "$1" "${2:-~$(portageq envvar ARCH)}"; }
