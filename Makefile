CC = clang
CFLAGS = -g
PREFIX ?= $(DESTDIR)/usr/local

all: librx.dylib test

librx.dylib: rx.c rx.h
	$(CC) -dynamiclib $(CFLAGS) $(filter %.c, $^) -o $@

test: test.c librx.dylib
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -rf a.out *.dSYM librx.dylib test

