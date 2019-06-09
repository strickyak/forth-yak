O=-g

all: fy test

fy: fy.h fy.cxx linenoise.o generated-enum.inc generated-creators.inc generated-dispatch-table.inc generated-dispatchers.inc
	g++ -o fy $O fy.cxx linenoise.o

test: fy
	./fy test.fy
	echo
	echo OKAY GOOD

linenoise.o: vendor/linenoise/linenoise.h vendor/linenoise/linenoise.c
	gcc -O2 -c vendor/linenoise/linenoise.c

generated-enum.inc: defs.txt mk-enum.awk
	awk -f mk-enum.awk < defs.txt > generated-enum.inc
generated-creators.inc: defs.txt mk-creators.awk
	awk -f mk-creators.awk < defs.txt > generated-creators.inc
generated-dispatch-table.inc: defs.txt mk-dispatch-table.awk
	awk -f mk-dispatch-table.awk < defs.txt > generated-dispatch-table.inc
generated-dispatchers.inc: defs.txt mk-dispatchers.awk
	awk -f mk-dispatchers.awk < defs.txt > generated-dispatchers.inc

i:
	ci -l -m/dev/null -t/dev/null -q *.h *.cxx defs.txt *.fy Makefile
	indent fy.h fy.cxx
	ci -l -m/dev/null -t/dev/null -q *.h *.cxx defs.txt *.fy Makefile

clean:
	rm -f fy linenoise.o *.inc
