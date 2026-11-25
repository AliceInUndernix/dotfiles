# The parts in groups, what each one is, and ./install.sh pick, the
# menu to choose them with the keyboard. Sourced by install.sh, not run.

PART_GROUPS="shell desktop media tools keyboard network games browsers"

# group_parts <group>: its parts, in install order
group_parts() {
	case $1 in
	shell)    echo "ksh xdg nvim tmux" ;;
	desktop)  echo "x dwm st dmenu slock feh flameshot" ;;
	media)    echo "mpv sioyek wiremix btop" ;;
	tools)    echo "webarchiver clean seal ballfix diary" ;;
	keyboard) echo "steno moonlander" ;;
	network)  echo "net switch" ;;
	games)    echo "ksp" ;;
	browsers) echo "$APPS" ;;
	esac
}

# part_info <part>: one line about it; (root) when it needs root
part_info() {
	case $1 in
	deps)        echo "the system's packages (root)" ;;
	ksh)         echo "Korn shell: profile, kshrc, login shell (root)" ;;
	xdg)         echo "Downloads, Music, Pictures ... all in ~" ;;
	nvim)        echo "Neovim config and plugins; vim runs Neovim" ;;
	tmux)        echo "tmux config" ;;
	x)           echo "xinitrc: startx runs dwm" ;;
	dwm)         echo "window manager" ;;
	st)          echo "terminal, with Symbols Nerd Font" ;;
	dmenu)       echo "launcher and its list of programs" ;;
	slock)       echo "screen locker (root: it must be setuid)" ;;
	feh)         echo "picture viewer theme" ;;
	flameshot)   echo "screenshots" ;;
	mpv)         echo "video player config" ;;
	sioyek)      echo "PDF and EPUB reader config" ;;
	wiremix)     echo "sound mixer config" ;;
	btop)        echo "system monitor config and theme" ;;
	webarchiver) echo "archives websites for offline reading" ;;
	clean)       echo "takes metadata out of files" ;;
	seal)        echo "a directory into one encrypted file, and back" ;;
	ballfix)     echo "trackball filter: no false double clicks" ;;
	diary)       echo "a private, encrypted diary" ;;
	steno)       echo "steno engine and Dotterel's dictionaries" ;;
	moonlander)  echo "Moonlander layout: QMK, build and flash" ;;
	net)         echo "VPN, Tor and kill switch, Gentoo with OpenRC (root)" ;;
	switch)      echo "IceCat: settings, add-ons, the switch extension" ;;
	ksp)         echo "Kerbal Space Program: kspkan and Principia" ;;
	icecat)      echo "IceCat browser, hours to build (root, Gentoo)" ;;
	chromium)    echo "ungoogled-chromium, hours to build (root, Gentoo)" ;;
	esac
}

# The parts that only root can install
ROOT_PARTS="slock net $APPS"

# expand <name>...: parts and groups -> parts, in the order given.
# Prints nothing and fails on an unknown name.
expand() {
	out=
	for x in "$@"; do
		case " $PART_GROUPS " in
		*" $x "*) out="$out $(group_parts "$x")" ;;
		*)
			case " $PARTS " in *" $x "*) ;; *) return 1 ;; esac
			out="$out $x" ;;
		esac
	done
	echo $out
}

# --- the menu ----------------------------------------------------------
# It looks like btop and vpntop: one box, its title in the top border,
# the keys in the bottom border (the key in blue), Habamax colours in
# 24-bit, DEC line drawing. Without a terminal that can, plain ASCII.
pick_colours() {
	if [ -t 2 ] && [ "${TERM:-dumb}" != dumb ]; then
		e=$(printf '\033')
		C_BOX="$e[0;38;2;88;88;88m"        # #585858 box lines
		C_KEY="$e[0;38;2;95;135;175m"      # #5f87af keys
		C_TITLE="$e[0;1;38;2;238;238;238m" # #eeeeee titles
		C_TEXT="$e[0;38;2;199;199;199m"    # #c7c7c7 text
		C_LABEL="$e[0;38;2;158;158;158m"   # #9e9e9e labels
		C_DIM="$e[0;38;2;118;118;118m"     # #767676 dim
		C_ON="$e[0;38;2;135;175;135m"      # #87af87 chosen
		C_SEL="$e[48;2;88;88;88m"          # selected grey background
		C_OFF="$e[0m"
		G_ON="$e(0" G_OFF="$e(B"           # DEC line drawing on, off
		H=q V=x TL=l TR=k BL=m BR=j        # in DEC: - | corners
	else
		C_BOX= C_KEY= C_TITLE= C_TEXT= C_LABEL= C_DIM= C_ON= C_SEL= C_OFF=
		G_ON= G_OFF=
		H=- V='|' TL=+ TR=+ BL=+ BR=+
	fi
	W=$(stty size 2>/dev/null </dev/tty | awk '{ print $2 }') || :
	case $W in ''|*[!0-9]*) W=80 ;; esac
	[ "$W" -le 80 ] || W=80
	[ "$W" -ge 50 ] || W=50
}

# line <n>: n box lines
hline() {
	i=0
	while [ "$i" -lt "$1" ]; do printf '%s' "$H"; i=$((i + 1)); done
}

# tag <top|bottom> <key> <text>: btop's tag in a border; the plain
# width is in TAGW afterwards
tag() {
	if [ "$1" = top ]; then a=$TR b=$TL; else a=$BR b=$BL; fi
	[ -n "$G_ON" ] || { a='['; b=']'; }
	printf '%s%s%s%s%s%s%s%s%s%s' "$C_BOX" "$G_ON" "$a" "$G_OFF" \
		"$C_KEY" "$2" "${4:-$C_TEXT}" "$3" "$C_BOX$G_ON" "$b$G_OFF"
	TAGW=$((${#2} + ${#3} + 2))
}

# row <plain text> <coloured text>: a line in the box, padded
row() {
	pad=$((W - 2 - ${#1}))
	[ "$pad" -ge 0 ] || pad=0
	printf '%s%s%s%s%s%*s%s%s%s%s%s\n' "$C_BOX" "$G_ON" "$V" "$G_OFF" \
		"$2" "$pad" '' "$C_BOX" "$G_ON" "$V" "$G_OFF" "$C_OFF"
}

# pick_show <chosen>: the box, every part numbered, chosen ones marked
pick_show() {
	[ -z "$C_OFF" ] || printf '\033[2J\033[H'
	# top border, the title in it
	printf '%s%s%s%s' "$C_BOX" "$G_ON" "$TL" "$G_OFF"
	tag top '' 'dotfiles: what to install' "$C_TITLE"
	printf '%s%s' "$C_BOX$G_ON" "$(hline $((W - 2 - TAGW)))"
	printf '%s%s%s\n' "$TR" "$G_OFF" "$C_OFF"
	dw=$((W - 24))
	n=0
	for g in deps $PART_GROUPS; do
		if [ "$g" = deps ]; then ps=deps
		else
			row " $g" " $C_LABEL$g"
			ps=$(group_parts "$g")
		fi
		for p in $ps; do
			[ "$p" = deps ] || n=$((n + 1))
			k=$n; [ "$p" != deps ] || k=0
			d=$(part_info "$p" | cut -c 1-"$dw")
			case " $1 " in
			*" $p "*) m='[x]' cm="$C_ON[x]" cn="$C_SEL$C_TITLE" ;;
			*) m='[ ]' cm="$C_DIM[ ]" cn="$C_TEXT" ;;
			esac
			pl=$(printf '  %2s %s %-12s %s' "$k" "$m" "$p" "$d")
			row "$pl" "$(printf '  %s%2s %s %s%-12s%s %s%s' "$C_KEY" "$k" \
				"$cm" "$cn" "$p" "$C_OFF" "$C_DIM" "$d")"
		done
	done
	# bottom border, the keys in it
	printf '%s%s%s%s' "$C_BOX" "$G_ON" "$BL" "$G_OFF"
	used=0
	tag bottom 'number' ' or group: choose'; used=$((used + TAGW))
	tag bottom 'a' ' all'; used=$((used + TAGW))
	tag bottom 'n' ' none'; used=$((used + TAGW))
	tag bottom 'Enter' ' install'; used=$((used + TAGW))
	tag bottom 'q' ' quit'; used=$((used + TAGW))
	n=$((W - 2 - used))
	[ "$n" -ge 0 ] || n=0
	printf '%s%s%s%s%s\n' "$C_BOX$G_ON" "$(hline "$n")" "$BR" "$G_OFF" \
		"$C_OFF"
	printf '%s> %s' "$C_KEY" "$C_OFF"
}

# toggle <list> <part>: the list with <part> added, or taken out
toggle() {
	case " $1 " in
	*" $2 "*) echo " $1 " | sed "s/ $2 / /" ;;
	*) echo "$1 $2" ;;
	esac
}

# pick: the menu. Prints the chosen parts (deps first) on stdout; the
# menu itself goes to the terminal.
pick() {
	[ -t 0 ] || die "pick needs a terminal to ask on"
	pick_colours
	chosen=
	while :; do
		pick_show "$chosen" >&2
		read -r line || return 1
		[ -n "$line" ] || break
		for w in $line; do
			case $w in
			q|quit) return 1 ;;
			a|all)	# like install.sh all: ksp and the browsers
				# only when picked by number
				chosen=deps
				for p in $PARTS; do
					case " $ONLY_NAMED " in
					*" $p "*) ;;
					*) chosen="$chosen $p" ;;
					esac
				done ;;
			n|none) chosen= ;;
			0|deps) chosen=$(toggle "$chosen" deps) ;;
			[1-9]|[1-9][0-9])
				p=$(echo $PARTS | awk -v n="$w" '{ print $n }')
				if [ -n "$p" ]; then chosen=$(toggle "$chosen" "$p")
				else warn "there is no $w"; fi ;;
			*)
				if ps=$(expand "$w"); then
					all_in=yes
					for p in $ps; do
						case " $chosen " in *" $p "*) ;; *) all_in=no ;; esac
					done
					for p in $ps; do
						case " $chosen " in
						*" $p "*) [ "$all_in" = no ] ||
							chosen=$(toggle "$chosen" "$p") ;;
						*) chosen=$(toggle "$chosen" "$p") ;;
						esac
					done
				else
					warn "not a part or group: $w"
				fi ;;
			esac
		done
		chosen=$(echo $chosen)
	done
	[ -n "$chosen" ] || return 1
	# in install order
	out=
	case " $chosen " in *" deps "*) out=--deps ;; esac
	for p in $PARTS; do
		case " $chosen " in *" $p "*) out="$out $p" ;; esac
	done
	echo $out
}
