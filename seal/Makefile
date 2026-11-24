# seal: turns a directory into one encrypted file, and back (see
# README). Plain C, only libc; static and stripped, so the binary needs
# nothing but the kernel. unseal is a link to seal.

PREFIX = /usr/local
CC = cc
CFLAGS = -std=c89 -pedantic -Wno-long-long -Wall -Wextra -O2
LDFLAGS = -static -s

SRC = seal.c pack.c crypto.c common.c
OBJ = $(SRC:.c=.o)

all: seal

$(OBJ): seal.h crypto.h

seal: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

test: seal test/vectors
	test/vectors
	sh test/run.sh ./seal

test/vectors: test/vectors.c crypto.c crypto.h
	$(CC) $(CFLAGS) -I. -o $@ test/vectors.c crypto.c

clean:
	rm -f seal $(OBJ) test/vectors

install: seal
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f seal $(DESTDIR)$(PREFIX)/bin/seal
	chmod 755 $(DESTDIR)$(PREFIX)/bin/seal
	ln -sf seal $(DESTDIR)$(PREFIX)/bin/unseal

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/seal $(DESTDIR)$(PREFIX)/bin/unseal

.PHONY: all test clean install uninstall
