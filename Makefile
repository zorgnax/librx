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

all: $(LIB) test example1 example2 example3 example4 example5 example6

$(LIB): rx.c hash.c rx.h
	$(CC) $(LFLAGS) $(CFLAGS) $(filter %.c, $^) -o $@

test: test.c $(LIB)
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

example6: example6.c hash.c $(LIB)
	$(CC) $(CFLAGS) $^ -o $@
	./example6 example6b.lx
	$(CC) $(CFLAGS) example6b.c $(LIB) -o example6b

check: test
	./test

install: $(LIB) rx.h
	mkdir -p $(PREFIX)/lib $(PREFIX)/include
	install -c $(LIB) $(PREFIX)/lib
	install -c rx.h $(PREFIX)/include

uninstall:
	rm $(PREFIX)/lib/$(LIB) $(PREFIX)/lib/rx.h

clean:
	rm -rf a.out *.dSYM $(LIB) test example[0-9] example6b.c example6b

