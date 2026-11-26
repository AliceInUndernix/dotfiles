#!/bin/sh
# Step 2 of 3: the big programs, only the ones you choose (root).
# ./install.sh icecat chromium (or --new-pc, which asks); run on its
# own, install/apps.sh icecat chromium does the same.
#
# IceCat and ungoogled-chromium build from source for hours, so "all"
# and no arguments leave them out. Gentoo only. Neither is in the main
# tree, and both are only in testing (~amd64). Chromium gets no settings
# of ours; IceCat gets the part icecatrc (icecatrc/README) right after.

APPS="icecat chromium"

# browser <overlay> <atom>
browser() (
	if ! command -v emerge >/dev/null 2>&1; then
		warn "${2#*/} is only for Gentoo: skipped"
		return 0
	fi
	root_env
	enable_overlay "$1"
	keyword "$2"
	emerge $EMERGE_OPTS --noreplace "$2"
)
# IceCat, then our settings for it (the part icecatrc)
install_icecat() {
	browser parona-overlay www-client/icecat
	! have_icecat || install_icecatrc
}
install_chromium() { browser pf4public www-client/ungoogled-chromium; }

# ck_browser <atom> <part>: optional, so missing is no fault
ck_browser() {
	if ! command -v portageq >/dev/null 2>&1; then
		ck_info "${1#*/}: only for Gentoo"
	elif portageq has_version / "$1"; then
		ck_ok "${1#*/} $(portageq best_version / "$1" | sed 's/.*-\([0-9]\)/\1/')"
	else
		ck_info "${1#*/} is not installed: su -c '$DOTFILES/install.sh $2'"
	fi
}

# ask_apps: which of them to build, asked once (--new-pc). Prints the
# answer; nothing when there is no terminal to ask on.
ask_apps() {
	[ -t 0 ] || return 0
	printf 'Build which of these? %s (names, all, or none): ' \
		"$APPS" >&2
	read -r a || a=
	case $a in all) a=$APPS ;; none|n|no) a= ;; esac
	for x in $a; do
		case " $APPS " in *" $x "*) echo "$x" ;;
		*) warn "not one of them: $x (left out)" ;; esac
	done
}

# run on its own: the same as ./install.sh <apps>
case $0 in */apps.sh|apps.sh)
	[ $# -gt 0 ] || { echo "usage: $0 $APPS" >&2; exit 2; }
	exec "$(dirname "$0")/../install.sh" "$@" ;;
esac
