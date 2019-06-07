all: fy test

fy: fy.h fy.cxx linenoise.o
	g++ -o fy -g fy.cxx linenoise.o

linenoise.o: vendor/linenoise/linenoise.h vendor/linenoise/linenoise.c
	gcc -c -g vendor/linenoise/linenoise.c 

i:
	ci -l -m/dev/null -t/dev/null -q *.h *.cxx *.fy Makefile
	indent fy.h fy.cxx
	ci -l -m/dev/null -t/dev/null -q *.h *.cxx *.fy Makefile

test: fy
	./fy test.fy
	echo
	echo OKAY GOOD

clean:
	rm -f fy linenoise.o
