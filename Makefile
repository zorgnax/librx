CC = clang
CFLAGS =
PREFIX ?= $(DESTDIR)/usr/local

all: librx.dylib testsuite example1 example2 example3 example4 example5

librx.dylib: rx.c rx.h
	$(CC) -dynamiclib $(CFLAGS) $(filter %.c, $^) -o $@

testsuite: testsuite.c librx.dylib
	$(CC) $(CFLAGS) $^ -o $@

example1: example1.c librx.dylib
	$(CC) $(CFLAGS) $^ -o $@

example2: example2.c librx.dylib
	$(CC) $(CFLAGS) $^ -o $@

example3: example3.c librx.dylib
	$(CC) $(CFLAGS) $^ -o $@

example4: example4.c librx.dylib
	$(CC) $(CFLAGS) $^ -o $@

example5: example5.c librx.dylib
	$(CC) $(CFLAGS) $^ -o $@

check: testsuite
	./testsuite

install: librx.dylib rx.h
	mkdir -p $(PREFIX)/lib $(PREFIX)/include
	install -c librx.dylib $(PREFIX)/lib
	install -c rx.h $(PREFIX)/include

uninstall:
	rm $(PREFIX)/lib/librx.dylib $(PREFIX)/lib/rx.h

clean:
	rm -rf a.out *.dSYM librx.dylib testsuite example[0-9]

