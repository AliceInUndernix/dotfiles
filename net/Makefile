# vpnauth, the password helper of vpn (see vpnauth.c). Built by
# install.sh net, which installs it setuid root for group vpnctl.
# Static by default; it needs libcrypt (crypt()) besides libc. On
# Gentoo the static libcrypt comes with sys-libs/libxcrypt[static-libs]:
#   make LDFLAGS=     a dynamic build instead

CC = cc
CFLAGS = -std=c89 -pedantic -Wall -Wextra -Wno-long-long -Os
LDFLAGS = -static
LIBS = -lcrypt

vpnauth: vpnauth.c
	$(CC) $(CFLAGS) -o vpnauth vpnauth.c $(LDFLAGS) $(LIBS)

clean:
	rm -f vpnauth

.PHONY: clean
