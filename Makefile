CC = clang
CFLAGS =
PREFIX ?= $(DESTDIR)/usr/local

all: librx.dylib test example1 example2 example3

librx.dylib: rx.c rx.h
	$(CC) -dynamiclib $(CFLAGS) $(filter %.c, $^) -o $@

test: test.c librx.dylib
	$(CC) $(CFLAGS) $^ -o $@

example1: example1.c librx.dylib
	$(CC) $(CFLAGS) $^ -o $@

example2: example2.c librx.dylib
	$(CC) $(CFLAGS) $^ -o $@

example3: example3.c librx.dylib
	$(CC) $(CFLAGS) $^ -o $@

check: test
	./test

install: librx.dylib rx.h
	mkdir -p $(PREFIX)/lib $(PREFIX)/include
	install -c librx.dylib $(PREFIX)/lib
	install -c rx.h $(PREFIX)/include

uninstall:
	rm $(PREFIX)/lib/librx.dylib $(PREFIX)/lib/rx.h

clean:
	rm -rf a.out *.dSYM librx.dylib test example1 example2 example3

