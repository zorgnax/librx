CC = clang
CFLAGS =
PREFIX ?= $(DESTDIR)/usr/local

all: librx.dylib test

librx.dylib: rx.c rx.h
	$(CC) -dynamiclib $(CFLAGS) $(filter %.c, $^) -o $@

test: test.c librx.dylib
	$(CC) $(CFLAGS) $^ -o $@

check: test
	./test

clean:
	rm -rf a.out *.dSYM librx.dylib test

