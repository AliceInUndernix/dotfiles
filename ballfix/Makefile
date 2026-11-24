# ballfix: filter for a trackball's buttons: stops false double clicks and
# changes buttons (see README). Plain C, only the kernel's headers; static
# and stripped, so the binary needs nothing but the kernel.

PREFIX = /usr/local
CC = cc
CFLAGS = -std=c89 -pedantic -Wno-long-long -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200112L
LDFLAGS = -static -s

all: ballfix

ballfix: ballfix.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ ballfix.c

clean:
	rm -f ballfix

install: ballfix
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f ballfix $(DESTDIR)$(PREFIX)/bin/ballfix
	chmod 755 $(DESTDIR)$(PREFIX)/bin/ballfix

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/ballfix

.PHONY: all clean install uninstall
