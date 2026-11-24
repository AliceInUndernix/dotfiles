# Part net (root): Riseup VPN, Tor and the kill switch, for Gentoo with
# OpenRC. ./install.sh net; net/README explains it. Sourced by
# install.sh, not run.

# Riseup VPN, Tor and the kill switch: net/README. Gentoo with OpenRC,
# root only. Its files are root's own, in /root/.system/net (lib.sh,
# ROOT_SYSTEM), and the system gets links to them:
#   /usr/local/bin/vpn, vpntop, proxychains, and vpnauth (its password
#   helper, setuid root for group vpnctl); /etc/init.d/vpnfw (boot) and
#   vpn (default); /etc/vpn/ (settings, Riseup's list and certificate,
#   vpn's state); /etc/tor/torrc, /etc/proxychains.conf and .d/,
#   /etc/sysctl.d/vpn.conf.
# What it changes in the system's own files, in place: /etc/dhcpcd.conf
# (no hostname, random MAC), hostname "host", time zone UTC. Those are
# backed up (root's ~/.system/state/backup, and /etc/vpn/orig) first; vpn off and
# ./install.sh uninstall net put them back.
install_net() {
	if [ ! -x /sbin/openrc-run ] || ! command -v emerge >/dev/null 2>&1; then
		warn "net is only for Gentoo with OpenRC: skipped"
		return
	fi
	# tor with seccomp, for "Sandbox 1" in torrc; --changed-use builds it
	# again when the installed tor lacks it
	portage use net-vpn/tor seccomp
	emerge $EMERGE_OPTS --noreplace --changed-use net-vpn/openvpn \
		net-vpn/tor net-firewall/nftables net-misc/proxychains
	net=$DOTFILES/net
	N=$ROOT_SYSTEM/net
	# /root and /root/.system: gone through, not listed (the links in
	# /usr/local and /etc must work for you and for tor)
	chmod 711 "${ROOT_SYSTEM%/*}"
	install -d -m 711 "$ROOT_SYSTEM"
	install -d -m 755 "$N" "$N/bin" "$N/init.d" /usr/local/bin
	# /etc/vpn: vpn's settings and state, kept in $N/etc
	if [ -d /etc/vpn ] && [ ! -L /etc/vpn ]; then
		rm -rf "$N/etc"
		mv /etc/vpn "$N/etc"
		say "move    /etc/vpn -> $N/etc"
	fi
	install -d -m 755 "$N/etc"
	root_link "$N/etc" /etc/vpn
	# The system's files as they were before net first changed them, for
	# vpn off (level -1). An older backup of ours (.bak.<date>) is the
	# truer original, so it wins.
	install -d -m 700 /etc/vpn/orig
	for f in /etc/dhcpcd.conf /etc/conf.d/hostname /etc/hostname \
		/etc/hosts /etc/timezone /etc/conf.d/hwclock /etc/tor/torrc \
		/etc/proxychains.conf
	do
		o=/etc/vpn/orig$f
		if [ -e "$o" ] || [ -e "$o.absent" ]; then continue; fi
		mkdir -p "${o%/*}"
		old=$(ls "$ROOT_SYSTEM/state/backup$f".[0-9]*-[0-9]* \
			"$f".bak.* 2>/dev/null | head -n 1) || :
		if [ -n "$old" ]; then cp -p "$old" "$o"
		elif [ -e "$f" ]; then cp -p "$f" "$o"
		else : > "$o.absent"; fi
	done
	[ -e /etc/vpn/orig/localtime.link ] ||
		readlink /etc/localtime > /etc/vpn/orig/localtime.link || :
	echo "$DOTFILES" > /etc/vpn/repo
	rm -f /etc/vpn/off
	root_link "$(root_put "$net/vpn" net/bin/vpn 755)" /usr/local/bin/vpn
	# vpnctl: may run vpn's commands with their own password (vpnauth);
	# vpnallow: what "vpn run" starts, the only programs online in the
	# sandbox
	for g in vpnctl vpnallow; do
		getent group "$g" >/dev/null || { groupadd "$g"; say "group   $g"; }
		case " $(id -nG "$USER_NAME") " in *" $g "*) ;; *)
			usermod -aG "$g" "$USER_NAME"
			say "group   $USER_NAME in $g (log in again)" ;;
		esac
	done
	if ! make -s -C "$net" vpnauth >/dev/null 2>&1; then
		warn "no static libcrypt: vpnauth is built dynamically"
		warn "(for a static one: sys-libs/libxcrypt with USE=static-libs)"
		make -s -C "$net" vpnauth LDFLAGS= ||
			{ warn "vpnauth did not build; vpn will ask for root's password"; }
	fi
	if [ -f "$net/vpnauth" ]; then
		root_link "$(root_put "$net/vpnauth" net/bin/vpnauth 4750 vpnctl)" \
			/usr/local/bin/vpnauth
		say "        (vpnauth: setuid root, group vpnctl)"
	fi
	# vpntop: the screen of "vpn" (plain C, static, no libraries)
	if make -s -C "$net" vpntop >/dev/null 2>&1 ||
		make -s -C "$net" vpntop LDFLAGS= >/dev/null 2>&1
	then
		root_link "$(root_put "$net/vpntop" net/bin/vpntop 755)" \
			/usr/local/bin/vpntop
	else
		warn "vpntop did not build; vpn uses its plain screen"
	fi
	make -s -C "$net" clean
	root_link "$(root_put "$net/vpnfw.initd" net/init.d/vpnfw 755)" \
		/etc/init.d/vpnfw
	root_link "$(root_put "$net/vpn.initd" net/init.d/vpn 755)" \
		/etc/init.d/vpn
	for f in openvpn.conf countries mirrors; do
		install -m 644 "$net/$f" "$N/etc/$f"
	done
	say "install /etc/vpn: openvpn.conf, countries, mirrors"
	root_link "$(root_put "$net/sysctl.conf" net/sysctl.conf 644)" \
		/etc/sysctl.d/vpn.conf
	root_link "$(root_put "$net/proxychains.conf" net/proxychains.conf 644)" \
		/etc/proxychains.conf keep
	# proxychains PROGRAM: each program on a Tor port of its own
	# (9200-9299 in torrc), one config per port
	install -d -m 755 "$N/proxychains.d"
	rm -f "$N"/proxychains.d/[0-9]*.conf
	p=9200
	while [ $p -le 9299 ]; do
		sed "s/^socks5 127\.0\.0\.1 9050\$/socks5 127.0.0.1 $p/" \
			"$net/proxychains.conf" > "$N/proxychains.d/$p.conf"
		chmod 644 "$N/proxychains.d/$p.conf"
		p=$((p + 1))
	done
	root_link "$N/proxychains.d" /etc/proxychains.d
	root_link "$(root_put "$net/proxychains" net/bin/proxychains 755)" \
		/usr/local/bin/proxychains
	# torrc: ours, with the User and DataDirectory lines of the old one
	# (Gentoo's), or Gentoo's defaults
	keep=$(grep -E '^[[:space:]]*(User|DataDirectory)[[:space:]]' \
		/etc/tor/torrc 2>/dev/null) || :
	if [ -z "$keep" ] && [ ! -f /etc/tor/torrc ]; then
		keep=$(printf 'User tor\nDataDirectory /var/lib/tor/data')
	fi
	{
		cat "$net/torrc"
		echo
		echo "# kept from the torrc that was here before"
		echo "$keep"
	} > "$net/.torrc"
	# Sandbox 1 stops a tor built without seccomp from starting at all
	if command -v tor >/dev/null 2>&1 &&
		! tor --verify-config -f "$net/.torrc" >/dev/null 2>&1 &&
		sed '/^Sandbox 1$/d' "$net/.torrc" > "$net/.torrc2" &&
		tor --verify-config -f "$net/.torrc2" >/dev/null 2>&1
	then
		mv "$net/.torrc2" "$net/.torrc"
		warn "tor did not build with seccomp: Sandbox 1 left out for now"
		warn "(emerge -qv --changed-use net-vpn/tor, then install.sh net)"
	fi
	rm -f "$net/.torrc2"
	install -d -m 755 /etc/tor
	root_link "$(root_put "$net/.torrc" net/torrc 644)" /etc/tor/torrc keep
	rm -f "$net/.torrc"
	d=$(echo "$keep" | sed -n 's/^[[:space:]]*DataDirectory[[:space:]]*//p')
	[ -z "$d" ] || [ -d "$d" ] || install -d -o tor -g tor -m 700 "$d"
	# dhcpcd: no hostname, no fixed client ID, and resolv.conf left alone
	# (vpn writes and locks it). The random MAC address is vpnfw's, once
	# at boot: dhcpcd's randomise_hwaddr changed it whenever the link
	# dropped, which broke Wi-Fi (2026-11-15), so it is taken out.
	f=/etc/dhcpcd.conf
	if grep -q '^randomise_hwaddr$' "$f" 2>/dev/null; then
		sed -i '/^randomise_hwaddr$/d; s/^# each time, leave/# leave/' "$f"
		say "dhcpcd  randomise_hwaddr out (it broke Wi-Fi)"
	fi
	if [ -f "$f" ] && ! grep -q '^# vpn (dotfiles' "$f"; then
		b=$(root_backup "$f")
		sed -i 's/^hostname[[:space:]]*$/#hostname  # off: net (vpn)/' "$f"
		cat >> "$f" <<-'EOF'

		# vpn (dotfiles, net/): send no hostname or fixed ID, and leave
		# /etc/resolv.conf alone (vpn sets and locks it). Keep these last:
		# anonymous hides what comes before it.
		nohook resolv.conf
		anonymous
		EOF
		say "dhcpcd  no hostname (backup $b)"
	fi
	v=$(dhcpcd --version 2>/dev/null | sed -n '1s/[^0-9]*\([0-9]*\).*/\1/p')
	[ "${v:-0}" -ge 9 ] 2>/dev/null ||
		warn "dhcpcd ${v:-?} is older than 9: it may send your hostname (emerge -qv dhcpcd)"
	# a hostname that says nothing
	f=/etc/conf.d/hostname
	if [ -f "$f" ] && ! grep -q '^hostname="host"' "$f"; then
		b=$(root_backup "$f")
		if grep -q '^hostname=' "$f"; then
			sed -i 's/^hostname=.*/hostname="host"/' "$f"
		else
			echo 'hostname="host"' >> "$f"
		fi
		say "host    hostname \"host\" at next boot (backup $b)"
	fi
	if [ -f /etc/hostname ] && [ "$(cat /etc/hostname)" != host ]; then
		root_backup /etc/hostname >/dev/null
		echo host > /etc/hostname
	fi
	if ! grep -q '^127\.0\.0\.1[[:space:]].*[[:space:]]host\([[:space:]]\|$\)' /etc/hosts; then
		root_backup /etc/hosts >/dev/null
		printf '127.0.0.1\thost\n' >> /etc/hosts
	fi
	# the clock in UTC, so programs can't tell your time zone
	if [ -f /usr/share/zoneinfo/UTC ] &&
		[ "$(readlink /etc/localtime)" != ../usr/share/zoneinfo/UTC ]
	then
		ln -sfn ../usr/share/zoneinfo/UTC /etc/localtime
		echo UTC > /etc/timezone
		say "time    zone UTC"
	fi
	f=/etc/conf.d/hwclock
	if [ -f "$f" ] && ! grep -q '^clock="UTC"' "$f"; then
		root_backup "$f" >/dev/null
		sed -i 's/^clock=.*/clock="UTC"/' "$f"
		grep -q '^clock=' "$f" || echo 'clock="UTC"' >> "$f"
		say "time    hardware clock in UTC (vpn sets the time after boot)"
	fi
	# Riseup's list and a certificate, straight out while the firewall
	# isn't there yet (later vpn renews them through the VPN)
	if [ ! -s /etc/vpn/gateways ] && ! nft list table inet vpn >/dev/null 2>&1; then
		/usr/local/bin/vpn refresh --install ||
			warn "no server list yet: run vpn refresh before you reboot"
	fi
	/usr/local/bin/vpn firewall closed --check ||
		warn "this kernel can't load the kill switch: it needs nftables (NF_TABLES, NFT_NAT, NFT_REDIR, NFT_CT, NF_TABLES_INET) and TUN"
	# runlevels: the kill switch at boot, the VPN in default. Tor is
	# started by vpn after OpenVPN, so it isn't in a runlevel itself.
	for s in vpnfw:boot vpn:default; do
		rc-update show "${s#*:}" 2>/dev/null | grep -q "^ *${s%:*} |" ||
			rc-update add "${s%:*}" "${s#*:}"
	done
	for l in boot default; do
		for s in tor nftables iptables ip6tables; do
			rc-update show "$l" 2>/dev/null | grep -q "^ *$s |" || continue
			rc-update del "$s" "$l"
			say "runlevel $s out of $l (vpn handles it now)"
		done
		rc-update show "$l" 2>/dev/null | grep -o 'openvpn[^ ]*' |
			while read -r s; do
				warn "$s is in runlevel $l: vpn runs OpenVPN itself (rc-update del $s $l)"
			done
	done
	say "net     ready: reboot, or start it now with"
	say "        rc-service vpnfw start && rc-service vpn start"
}

# --- check -------------------------------------------------------------

ck_same() {    # ck_same <repo file> <installed file>
	if [ ! -e "$2" ]; then
		ck_bad "$2 is missing: su -c '$DOTFILES/install.sh net'"
	elif cmp -s "$1" "$2"; then
		ck_ok "$2"
	else
		ck_bad "$2 is older than the repo's: su -c '$DOTFILES/install.sh net'"
	fi
}

ck_net() {
	if [ ! -x /sbin/openrc-run ]; then
		ck_info "not OpenRC: net is not for this system"
		return
	fi
	if [ -e /etc/vpn/off ]; then
		ck_info "net is off (vpn level -1): vpn on turns it back on"
		return
	fi
	n=$DOTFILES/net
	ck_same "$n/vpn" /usr/local/bin/vpn
	ck_same "$n/vpnfw.initd" /etc/init.d/vpnfw
	ck_same "$n/vpn.initd" /etc/init.d/vpn
	for f in openvpn.conf countries mirrors; do ck_same "$n/$f" "/etc/vpn/$f"; done
	ck_same "$n/sysctl.conf" /etc/sysctl.d/vpn.conf
	ck_same "$n/proxychains.conf" /etc/proxychains.conf
	ck_same "$n/proxychains" /usr/local/bin/proxychains
	if [ -u /usr/local/bin/vpnauth ]; then
		ck_ok "vpnauth (your password for vpn commands)"
		[ ! -f /usr/local/bin/vpnauth ] ||
			[ -z "$(find "$n/vpnauth.c" -newer /usr/local/bin/vpnauth)" ] ||
			ck_bad "vpnauth is older than vpnauth.c: su -c '$DOTFILES/install.sh net'"
	else
		ck_bad "vpnauth missing: vpn asks root's password (install.sh net)"
	fi
	if [ -x /usr/local/bin/vpntop ]; then
		ck_ok "vpntop (the screen of vpn)"
		[ -z "$(find "$n/vpntop.c" -newer /usr/local/bin/vpntop)" ] ||
			ck_bad "vpntop is older than vpntop.c: su -c '$DOTFILES/install.sh net'"
	else
		ck_bad "vpntop missing: vpn shows its plain screen (install.sh net)"
	fi
	for t in openvpn tor nft proxychains; do ck_cmd "$t"; done
	for g in vpnctl vpnallow; do
		case " $(id -nG "$USER_NAME") " in
		*" $g "*) ck_ok "$USER_NAME is in group $g" ;;
		*) ck_bad "$USER_NAME is not in group $g: su -c '$DOTFILES/install.sh net', log in again" ;;
		esac
	done
	for s in boot/vpnfw default/vpn; do
		if [ -e "/etc/runlevels/$s" ]; then ck_ok "${s#*/} in runlevel ${s%/*}"
		else ck_bad "${s#*/} is not in runlevel ${s%/*}: su -c '$DOTFILES/install.sh net'"; fi
	done
	for l in boot default; do
		[ ! -e "/etc/runlevels/$l/tor" ] ||
			ck_bad "tor is in runlevel $l, so it starts before the VPN: rc-update del tor $l"
	done
	if [ -s /etc/vpn/gateways ] && [ -s /etc/vpn/client.crt ]; then
		ck_ok "$(wc -l < /etc/vpn/gateways) Riseup servers known"
		if openssl x509 -checkend 0 -noout -in /etc/vpn/client.crt >/dev/null 2>&1; then
			ck_ok "certificate valid until $(openssl x509 -enddate -noout -in /etc/vpn/client.crt | sed 's/notAfter=//')"
		else
			ck_bad "the certificate has expired: vpn refresh"
		fi
	else
		ck_bad "no Riseup server list or certificate: vpn refresh"
	fi
	if [ -e /run/openrc/started/vpn ]; then
		read -r st lv 2>/dev/null < /run/vpn/state || st=?
		if [ "$st" = ok ]; then ck_ok "vpn running, level $lv"
		else ck_bad "vpn is $st: vpn status"; fi
	else
		ck_bad "the vpn service is not running (reboot, or rc-service vpn start)"
	fi
	# Tor's DNS, 1.1.1.1 (level 0, sandbox) or at level 1 Riseup's (the
	# one OpenVPN was given)
	rdns=$(sed -n 's/^dns=//p' /run/vpn/up 2>/dev/null)
	if [ "$(cat /etc/resolv.conf 2>/dev/null)" = "nameserver 127.0.0.1" ] ||
		[ "$(cat /etc/resolv.conf 2>/dev/null)" = "nameserver 1.1.1.1" ] ||
		{ [ -n "$rdns" ] &&
		[ "$(cat /etc/resolv.conf 2>/dev/null)" = "nameserver $rdns" ]; }
	then
		case $(lsattr -d /etc/resolv.conf 2>/dev/null) in
		*i*' '*) ck_ok "/etc/resolv.conf locked ($(cat /etc/resolv.conf))" ;;
		*) ck_bad "/etc/resolv.conf is not locked (vpn locks it when it starts)" ;;
		esac
	else
		ck_bad "/etc/resolv.conf is not vpn's (vpn sets it when it starts)"
	fi
	if grep -q '^randomise_hwaddr' /etc/dhcpcd.conf 2>/dev/null; then
		ck_bad "dhcpcd has randomise_hwaddr, which breaks Wi-Fi: su -c '$DOTFILES/install.sh net'"
	fi
	if grep -q '^anonymous' /etc/dhcpcd.conf 2>/dev/null; then ck_ok "dhcpcd: sends no hostname"
	else ck_bad "dhcpcd still sends your hostname and MAC: su -c '$DOTFILES/install.sh net'"; fi
	h=$(cat /proc/sys/kernel/hostname 2>/dev/null) || :
	if [ "$h" = host ]; then ck_ok "hostname host"
	else ck_bad "the hostname is not \"host\" yet (after the next boot)"; fi
	if [ "$(readlink /etc/localtime)" = ../usr/share/zoneinfo/UTC ]; then ck_ok "time zone UTC"
	else ck_bad "the time zone is not UTC: su -c '$DOTFILES/install.sh net'"; fi
	if [ "$(cat /proc/sys/net/ipv6/conf/all/disable_ipv6 2>/dev/null)" = 1 ]; then ck_ok "IPv6 off"
	else ck_bad "IPv6 is on (vpnfw turns it off at boot)"; fi
}
