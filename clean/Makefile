# clean: takes metadata out of files (see README). Plain C, only libc;
# static and stripped, so the binary needs nothing but the kernel.
# The program is called clean, so "make clean" can't build it: the
# build makes clean-bin, and install puts it in place as clean.

PREFIX = /usr/local
CC = cc
CFLAGS = -std=c89 -pedantic -Wno-long-long -Wall -Wextra -O2
LDFLAGS = -static -s

SRC = clean.c img.c media.c pdf.c inflate.c deflate.c rand.c ogg.c more.c \
    svg.c xml.c zip.c archive.c
OBJ = $(SRC:.c=.o)

all: clean-bin

$(OBJ): clean.h

clean-bin: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

test: clean-bin
	sh test/run.sh ./clean-bin

clean:
	rm -f clean-bin $(OBJ)

install: clean-bin
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f clean-bin $(DESTDIR)$(PREFIX)/bin/clean
	chmod 755 $(DESTDIR)$(PREFIX)/bin/clean

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/clean

.PHONY: all test clean install uninstall
