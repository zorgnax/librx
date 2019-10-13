CC = clang
CFLAGS =
PREFIX ?= $(DESTDIR)/usr/local
OS := $(shell uname -s)

ifeq "$(OS)" "Darwin"
    LFLAGS = -dynamiclib
    LIB = librx.dylib
else
    LFLAGS = -shared -fPIC
    LIB = librx.so
endif

all: $(LIB) testsuite example1 example2 example3 example4 example5

$(LIB): rx.c rx.h
	$(CC) $(LFLAGS) $(CFLAGS) $(filter %.c, $^) -o $@

testsuite: testsuite.c $(LIB)
	$(CC) $(CFLAGS) $^ -o $@

example1: example1.c $(LIB)
	$(CC) $(CFLAGS) $^ -o $@

example2: example2.c $(LIB)
	$(CC) $(CFLAGS) $^ -o $@

example3: example3.c $(LIB)
	$(CC) $(CFLAGS) $^ -o $@

example4: example4.c $(LIB)
	$(CC) $(CFLAGS) $^ -o $@

example5: example5.c $(LIB)
	$(CC) $(CFLAGS) $^ -o $@

check: testsuite
	./testsuite

install: $(LIB) rx.h
	mkdir -p $(PREFIX)/lib $(PREFIX)/include
	install -c $(LIB) $(PREFIX)/lib
	install -c rx.h $(PREFIX)/include

uninstall:
	rm $(PREFIX)/lib/$(LIB) $(PREFIX)/lib/rx.h

clean:
	rm -rf a.out *.dSYM $(LIB) testsuite example[0-9]

