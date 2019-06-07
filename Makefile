all: fy

fy: fy.h fy.cxx linenoise.o
	g++ -o fy -g fy.cxx linenoise.o

linenoise.o: vendor/linenoise/linenoise.h vendor/linenoise/linenoise.c
	gcc -c -g vendor/linenoise/linenoise.c 

i:
	indent forth-yak.h forth-yak.cxx

clean:
	rm -f fy linenoise.o
