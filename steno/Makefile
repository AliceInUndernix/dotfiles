# steno: a steno engine for a Gemini PR keyboard, in the way of Plover
# (see README). Plain C, only libc; static and stripped, so the binary
# needs nothing but the kernel. Plover's word list (words.txt) is built
# into it.

PREFIX = /usr/local
CC = cc
CFLAGS = -std=c89 -pedantic -Wno-long-long -Wno-overlength-strings \
	-Wall -Wextra -O2
LDFLAGS = -static -s

SRC = main.c dict.c dotterel.c translate.c format.c ortho.c x11.c \
	tools.c view.c screen.c tui.c gen.c word.c
OBJ = $(SRC:.c=.o) words.o

all: steno

$(OBJ): steno.h gen.h

mkwords: mkwords.c
	$(CC) $(CFLAGS) -o $@ mkwords.c

words.c: words.txt mkwords
	./mkwords words.txt words.c

steno: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

test: steno
	sh test/run.sh

clean:
	rm -f steno mkwords words.c $(OBJ)

install: steno
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f steno $(DESTDIR)$(PREFIX)/bin/steno
	chmod 755 $(DESTDIR)$(PREFIX)/bin/steno

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/steno

.PHONY: all test clean install uninstall
