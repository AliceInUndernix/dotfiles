# ./install.sh check [part...]: only looks, changes nothing. For each
# part what is installed, missing, out of date or not as the repo has
# it. Asks git whether this repo and Dotterel have newer commits (that
# needs the network). Exit status 1 when anything needs a look; each such
# line says what to run. Sourced by install.sh, not run.

# The commands --deps installs, to look for ("a|b": either will do)
TOOLS="git make cc pkg-config curl wget startx nvim tmux xclip ksh|mksh
	mpv yt-dlp feh flameshot sioyek wiremix btop zip unzip 7zz|7z
	dfu-util fc-cache"

# have <cmd|cmd...>: is one of them installed?
have() {
	for c in $(echo "$1" | tr '|' ' '); do
		PATH=$PATH:/usr/sbin:/sbin:/opt/bin command -v "$c" >/dev/null 2>&1 &&
			return 0
	done
	return 1
}

CK_BAD=0
ck_ok() { printf '  ok   %s\n' "$*"; }
ck_bad() { printf '  !!   %s\n' "$*"; CK_BAD=$((CK_BAD + 1)); }
ck_info() { printf '  --   %s\n' "$*"; }

# ck_links <part>: the links install_<part> makes from this repo
ck_links() {
	lines=$(sed -n "/^install_$1() {/,/^}/p" "$DOTFILES/install/configs.sh" |
		grep '^[[:space:]]*link "$DOTFILES/') || :
	[ -n "$lines" ] || return 0
	while IFS= read -r line; do
		eval "set -- ${line#*link }"
		if [ -L "$2" ] && [ "$(readlink "$2")" = "$1" ]; then
			ck_ok "$2"
		elif [ -e "$2" ] || [ -L "$2" ]; then
			ck_bad "$2 is not the repo's: ./install.sh $p"
		else
			ck_bad "$2 is missing: ./install.sh $p"
		fi
	done <<EOF
$lines
EOF
}

ck_cmd() {
	if c=$(PATH=$PATH:/usr/sbin:/sbin command -v "$1"); then
		ck_ok "$1 installed ($c)"
	else
		ck_bad "$1 is not installed: su -c '$DOTFILES/install.sh --deps'"
	fi
}

# ck_built <name>: ~/.system/bin/<name> is there and newer than its source
ck_built() {
	b=$SYSBIN/$1
	if [ ! -x "$b" ]; then
		ck_bad "$1 is not built: ./install.sh $1"
		return
	fi
	n=$(find "$DOTFILES/$1" -type f \( -name '*.[ch]' -o -name config.mk \
		-o -name Makefile \) -newer "$b" | wc -l)
	if [ "$n" -gt 0 ]; then
		ck_bad "$1 is older than its source ($n files changed): ./install.sh $1"
	else
		ck_ok "$1 built from the current source"
	fi
}

# ck_git <dir> <name>: uncommitted changes, newer commits upstream
ck_git() {
	g="git -c safe.directory=$1 -C $1"
	if ! $g rev-parse >/dev/null 2>&1; then
		ck_bad "$2 is not a git checkout ($1)"
		return
	fi
	if $g status --porcelain 2>/dev/null | grep -q .; then
		ck_info "$2 has changes not committed"
	fi
	if ! $g fetch -q 2>/dev/null; then
		ck_info "$2: could not ask upstream for newer commits (network?)"
		return
	fi
	n=$($g rev-list --count 'HEAD..@{upstream}' 2>/dev/null || echo 0)
	if [ "$n" -gt 0 ]; then
		ck_bad "$2 is $n commits behind: git -C $1 pull"
	else
		ck_ok "$2 is up to date ($($g log -1 --format=%h))"
	fi
}

ck_part() {
	case $1 in
	ksh)
		ck_links ksh
		sh_now=$(getent passwd "$USER_NAME" 2>/dev/null | cut -d: -f7)
		case $sh_now in
		*ksh) ck_ok "login shell $sh_now" ;;
		*) ck_bad "login shell is $sh_now: su -c '$DOTFILES/install.sh ksh'" ;;
		esac
		for f in profile kshrc; do
			c=$ROOT_SYSTEM/config/ksh/$f
			if [ ! -r "$c" ] && [ "$AS_ROOT" = no ]; then
				ck_info "root's $f: only root can read it (check as root)"
			elif cmp -s "$DOTFILES/ksh/$f" "$c"; then
				ck_ok "root's $f ($c)"
			else
				ck_bad "root's $f is missing or older: su -c '$DOTFILES/install.sh ksh'"
			fi
		done ;;
	nvim)
		ck_links nvim
		if command -v nvim >/dev/null 2>&1; then
			v=$(nvim --version | sed -n '1s/^NVIM v\{0,1\}//p')
			case $v in
			0.[0-9].*|0.10.*) ck_bad "Neovim $v is older than 0.11" ;;
			*) ck_ok "Neovim $v" ;;
			esac
		else
			ck_bad "nvim is not installed: su -c '$DOTFILES/install.sh --deps'"
		fi
		pack=$SYSTEM/nvim/site/pack/plugins/opt
		for t in "$DOTFILES"/archive/nvim-plugins/*.tar.gz; do
			n=$(basename "$t" .tar.gz)
			if [ -d "$pack/$n" ]; then ck_ok "plugin $n"
			else ck_bad "plugin $n is missing: ./install.sh nvim"; fi
		done
		if command -v nvim >/dev/null 2>&1 &&
			[ "$(readlink "$SYSBIN/vim" 2>/dev/null)" = "$(command -v nvim)" ]
		then
			ck_ok "vim runs Neovim"
		else
			ck_bad "$SYSBIN/vim is not Neovim: ./install.sh nvim"
		fi ;;
	xdg|x|feh) ck_links "$1" ;;
	tmux|flameshot|mpv|sioyek|wiremix|btop)
		ck_links "$1"
		ck_cmd "$1" ;;
	dwm|st|webarchiver|clean|seal|ballfix) ck_built "$1" ;;
	diary)
		ck_built diary
		ck_links diary
		;;
	steno)
		ck_built steno
		ck_links steno
		if [ -d "$STENO_DIR/dotterel" ]; then ck_git "$STENO_DIR/dotterel" "steno's Dotterel"
		else ck_bad "steno has no dictionaries: ./install.sh steno"; fi
		g=$(serial_group)
		if in_group "$g"; then ck_ok "$USER_NAME is in group $g (the keyboard's port)"
		else ck_bad "$USER_NAME is not in group $g: su -c '$DOTFILES/install.sh steno'"; fi
		[ ! -d "$SYSTEM/plover" ] ||
			ck_info "$SYSTEM/plover: Plover's old settings and logs (rm -rf it)" ;;
	dmenu)
		ck_built dmenu
		ck_links dmenu ;;
	slock)
		ck_built slock
		if [ -u "$SYSBIN/slock" ] && [ "$(stat -c %U "$SYSBIN/slock" 2>/dev/null)" = root ]; then
			ck_ok "slock is root's and setuid (it can lock)"
		elif [ -e "$SYSBIN/slock" ]; then
			ck_bad "slock is not root's setuid: su -c '$DOTFILES/install.sh slock'"
		fi ;;
	moonlander)
		if [ -x "$SYSBIN/qmk" ]; then ck_ok "qmk"; else ck_bad "qmk is missing: ./install.sh moonlander"; fi
		if [ ! -d "$ML_SRC/.git" ]; then
			ck_bad "the layout is not cloned: ./install.sh moonlander"
		else
			ck_git "$ML_SRC" "the Moonlander layout"
			b=$ML_SRC/$ML_BIN
			if [ ! -f "$b" ]; then
				ck_bad "no firmware built: ./install.sh moonlander"
			elif [ -n "$(find "$ML_SRC/canary" -type f -newer "$b" 2>/dev/null)" ]; then
				ck_bad "the firmware is older than the layout: ./install.sh moonlander"
			else
				ck_ok "firmware $ML_BIN is from the current layout"
			fi
		fi ;;
	claude)
		r=$SYSTEM/claude-desktop/app/.release
		c=$SYSBIN/claude-desktop
		if [ -f "$r" ]; then ck_ok "Claude desktop $(cat "$r")"
		else ck_bad "Claude desktop is missing: ./install.sh claude"; fi
		if [ -x "$c" ]; then ck_ok "$c"
		else ck_bad "$c is missing: ./install.sh claude"; fi ;;
	net) ck_net ;;
	icecatrc) ck_icecatrc ;;
	ksp) ck_ksp ;;
	icecat) ck_browser www-client/icecat icecat ;;
	chromium) ck_browser www-client/ungoogled-chromium chromium ;;
	esac
}

# ck_icecatrc: switch is in the user's IceCat profile, and not older
# than the repo's
ck_icecatrc() {
	ck_links icecatrc
	if ! have_icecat; then
		ck_info "IceCat is not installed: su -c '$DOTFILES/install.sh icecat'"
		return
	fi
	if ! prof=$(as_user "sh '$DOTFILES/icecatrc/install.sh' -w" 2>/dev/null); then
		ck_bad "IceCat has no profile: ./install.sh icecatrc"
		return
	fi
	x=$prof/extensions/switch@example.org.xpi
	v=$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' "$DOTFILES/icecatrc/extension/manifest.json")
	if [ ! -f "$x" ]; then
		ck_bad "switch is not in $prof: ./install.sh icecatrc"
	elif [ -n "$(find "$DOTFILES/icecatrc/extension" "$DOTFILES/icecatrc/icecat.prefs" \
		-type f -newer "$x" 2>/dev/null)" ]
	then
		ck_bad "switch in IceCat is older than the repo's: ./install.sh icecatrc"
	else
		ck_ok "switch $v and the settings are in $prof"
	fi
}

do_check() {
	echo "== repo"
	ck_git "$DOTFILES" "this repo"
	echo "== deps"
	missing=
	for t in $TOOLS; do
		have "$t" || missing="$missing $t"
	done
	if [ -z "$missing" ]; then ck_ok "all the tools are installed"
	else ck_bad "missing:$missing: su -c '$DOTFILES/install.sh --deps'"; fi
	for p in ${1:-$PARTS}; do
		echo "== $p"
		ck_part "$p"
	done
	echo
	if [ "$CK_BAD" -eq 0 ]; then
		echo "everything is as the repo has it"
		return 0
	fi
	echo "$CK_BAD things need a look (the lines with !!)"
	return 1
}
