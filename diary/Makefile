# diary: a private diary in one encrypted file (see README). Plain C,
# only libc; static and stripped, so the binary needs nothing but the
# kernel. The archive and the crypto are seal's (../seal), shared, not
# copied.

PREFIX = /usr/local
CC = cc
CFLAGS = -std=c89 -pedantic -Wno-long-long -Wall -Wextra -O2 -I../seal
LDFLAGS = -static -s

SEAL = ../seal/pack.c ../seal/crypto.c ../seal/common.c
SRC = main.c vault.c data.c ui.c term.c
OBJ = $(SRC:.c=.o) pack.o crypto.o common.o

all: diary

$(SRC:.c=.o): diary.h data.h ui.h ../seal/seal.h ../seal/crypto.h

pack.o: ../seal/pack.c ../seal/seal.h ../seal/crypto.h
	$(CC) $(CFLAGS) -c -o $@ ../seal/pack.c
crypto.o: ../seal/crypto.c ../seal/crypto.h
	$(CC) $(CFLAGS) -c -o $@ ../seal/crypto.c
common.o: ../seal/common.c ../seal/seal.h
	$(CC) $(CFLAGS) -c -o $@ ../seal/common.c

diary: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

test: diary
	sh test/run.sh ./diary

clean:
	rm -f diary $(OBJ)

install: diary
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f diary $(DESTDIR)$(PREFIX)/bin/diary
	chmod 755 $(DESTDIR)$(PREFIX)/bin/diary

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/diary

.PHONY: all test clean install uninstall
