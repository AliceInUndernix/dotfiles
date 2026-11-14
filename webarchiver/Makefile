# WebArchiver - archive whole websites with wget
# See README. Edit config.h (made from config.def.h) to change defaults.
# Builds for the machine it runs on (x86-64 by default); static and
# stripped, so the binary needs nothing but the kernel (and wget to run).

PREFIX = /usr/local
CC = cc
CFLAGS = -std=c89 -pedantic -Wno-long-long -Wall -Wextra -O2
LDFLAGS = -static -s

SRC = main.c fetch.c finish.c store.c page.c serve.c url.c util.c sha256.c
OBJ = $(SRC:.c=.o)

all: webarchiver

config.h:
	cp config.def.h config.h

$(OBJ): webarchiver.h config.h

webarchiver: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

clean:
	rm -f webarchiver $(OBJ)

install: webarchiver
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f webarchiver $(DESTDIR)$(PREFIX)/bin/webarchiver
	chmod 755 $(DESTDIR)$(PREFIX)/bin/webarchiver

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/webarchiver

.PHONY: all clean install uninstall
