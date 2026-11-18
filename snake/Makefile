# snake: terminal snake game, and snaked, the same game playing itself
# on the X desktop (see README). game.c is the game and its autopilot,
# shared by both.
#
# snake is plain C with only libc, no curses; static and stripped, so
# the binary needs nothing but the kernel. snaked needs Xlib, like the
# suckless tools, so it is linked against libX11 the usual way.

.POSIX:

PREFIX = $(HOME)/.system
MANPREFIX = $(PREFIX)/share/man
CC = cc
CFLAGS = -std=c89 -pedantic -Wno-long-long -Wall -Wextra -O2
LDFLAGS = -static -s
X11INC = /usr/include
X11LIB = /usr/lib64
XLDFLAGS = -s
XLIBS = -L$(X11LIB) -lX11

all: snake snaked

snake: snake.c game.c game.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o snake snake.c game.c

snaked: snaked.c game.c game.h
	$(CC) $(CFLAGS) -I$(X11INC) $(XLDFLAGS) -o snaked snaked.c game.c $(XLIBS)

clean:
	rm -f snake snaked

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(MANPREFIX)/man1
	cp -f snake snaked $(DESTDIR)$(PREFIX)/bin/
	chmod 755 $(DESTDIR)$(PREFIX)/bin/snake $(DESTDIR)$(PREFIX)/bin/snaked
	cp -f snake.1 snaked.1 $(DESTDIR)$(MANPREFIX)/man1/
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/snake.1 \
	    $(DESTDIR)$(MANPREFIX)/man1/snaked.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/snake $(DESTDIR)$(PREFIX)/bin/snaked \
	    $(DESTDIR)$(MANPREFIX)/man1/snake.1 \
	    $(DESTDIR)$(MANPREFIX)/man1/snaked.1

.PHONY: all clean install uninstall
