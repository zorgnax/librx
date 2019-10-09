all: rx

rx:
	clang rx.c

clean:
	rm -rf a.out *.dSYM

