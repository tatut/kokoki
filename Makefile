init:
	git submodule update --init --recursive

kokoki: kokoki.c kokoki.h repl.c tgc/tgc.c tgc/tgc.h stdlib.h
	cc -Ires2h -gdwarf  -o kokoki kokoki.c repl.c tgc/tgc.c

run: kokoki
	./kokoki

stdlib.h: stdlib.ki res2h/res2h.c
	cd res2h && make res2h
	./res2h/res2h stdlib.h stdlib.ki

test: kokoki.c kokoki.h test.c tgc/tgc.c tgc/tgc.h stdlib.h
	cc -Ires2h -o test test.c kokoki.c tgc/tgc.c
	./test
