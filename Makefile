VERSION = 0.4.0
CC = cc
CFLAGS = -std=c89 -pedantic -Wall -Wextra -Wno-long-long \
    -D_POSIX_C_SOURCE=200809L -O2
LDFLAGS = -static
PREFIX = $(HOME)/.system
MANDIR = $(PREFIX)/share/man/man1

OBJS = main.o util.o sha1.o sha256.o crc32.o hashfile.o version.o \
    inflate.o archive.o json.o rx.o registry.o index.o fetch.o \
    instances.o cmd.o resolve.o install.o tui.o query.o modlist.o \
    ckan.o scan.o

all: kspkan

kspkan: $(OBJS)
	$(CC) $(LDFLAGS) -o kspkan $(OBJS)

$(OBJS): kspkan.h

# a stripped static binary for handing out
release: clean
	$(MAKE) LDFLAGS="-static -s"

# the source as a tarball, without user names or local times
dist:
	git archive --format=tar.gz --prefix=kspkan-$(VERSION)/ \
	    -o kspkan-$(VERSION).tar.gz HEAD

install: kspkan
	mkdir -p $(PREFIX)/bin $(MANDIR)
	cp kspkan $(PREFIX)/bin/kspkan.new
	mv $(PREFIX)/bin/kspkan.new $(PREFIX)/bin/kspkan
	cp kspkan.1 $(MANDIR)/kspkan.1

uninstall:
	rm -f $(PREFIX)/bin/kspkan $(MANDIR)/kspkan.1

test: kspkan
	sh tests/instances.sh
	sh tests/evil.sh
	sh tests/modlist.sh
	sh tests/test.sh

clean:
	rm -f kspkan $(OBJS)

.PHONY: all release dist install uninstall test clean
