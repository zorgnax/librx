CC = clang
CFLAGS =
PREFIX ?= $(DESTDIR)/usr/local

all: librx.dylib test example1 example2

librx.dylib: rx.c rx.h
	$(CC) -dynamiclib $(CFLAGS) $(filter %.c, $^) -o $@

test: test.c librx.dylib
	$(CC) $(CFLAGS) $^ -o $@

example1: example1.c librx.dylib
	$(CC) $(CFLAGS) $^ -o $@

example2: example2.c librx.dylib
	$(CC) $(CFLAGS) $^ -o $@

check: test
	./test

clean:
	rm -rf a.out *.dSYM librx.dylib test example1 example2

