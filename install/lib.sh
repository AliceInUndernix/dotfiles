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
# login: settings in ~/.config, everything else in ~/.system (not ~/.local
# or ~/.cache). Exported, so what this script runs (nvim, fc-cache) uses
# them too, and root's own settings don't leak in.
CONFIG=$HOME/.config
DATA=$SYSTEM/share
STATE=$SYSTEM/state
CACHE=$SYSTEM/cache
export XDG_CONFIG_HOME="$CONFIG" XDG_DATA_HOME="$DATA" \
	XDG_STATE_HOME="$STATE" XDG_CACHE_HOME="$CACHE"
PREFIX=${PREFIX:-$SYSTEM}     # our programs: PREFIX/bin = ~/.system/bin
export HOME
LOG_DIR=$STATE/install

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
