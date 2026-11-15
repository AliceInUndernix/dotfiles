# vpnauth, the password helper of vpn (see vpnauth.c), and vpntop,
# its screen (vpntop.c). Built by install.sh net, which installs
# vpnauth setuid root for group vpnctl. Both are static by default;
# vpnauth needs libcrypt (crypt()) besides libc. On
# Gentoo the static libcrypt comes with sys-libs/libxcrypt[static-libs]:
#   make LDFLAGS=     a dynamic build instead

CC = cc
CFLAGS = -std=c89 -pedantic -Wall -Wextra -Wno-long-long -Os
LDFLAGS = -static
LIBS = -lcrypt

vpnauth: vpnauth.c
	$(CC) $(CFLAGS) -o vpnauth vpnauth.c $(LDFLAGS) $(LIBS)

# vpntop, the screen of vpn: only libc
vpntop: vpntop.c
	$(CC) $(CFLAGS) -o vpntop vpntop.c $(LDFLAGS)

clean:
	rm -f vpnauth vpntop

.PHONY: clean
