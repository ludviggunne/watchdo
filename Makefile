CC=gcc
CFLAGS=-Wall -Wextra -Wpedantic

PREFIX?=/usr/local
DESTDIR?=

all: watchdo

watchdo: main.c
	$(CC) $(CFLAGS) -o $@ $<

install:
	install -Dm755 watchdo $(DESTDIR)$(PREFIX)/bin/watchdo

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/watchdo

clean:
	rm -rf watchdo *.o

.PHONY: all install uninstall clean
