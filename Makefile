kokoki: kokoki.c kokoki.h repl.c tgc/tgc.c tgc/tgc.h
	cc -gdwarf  -o kokoki kokoki.c repl.c tgc/tgc.c

run: kokoki
	./kokoki

test: kokoki.c kokoki.h test.c tgc/tgc.c tgc/tgc.h
	cc -o test test.c kokoki.c tgc/tgc.c
	./test
