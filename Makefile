
CFLAGS=-O2 -std=c99 -Wall -Wsign-compare -Werror -g -D_GNU_SOURCE -fexceptions
PIC = -fPIC

all: libmtrace.so elftest symboltable.o linux-gate.so.1

MTRACE_O = mtrace-pic.o symboltable-pic.o

libmtrace.so: $(MTRACE_O)
	gcc -shared -o libmtrace.so -Wl,-soname=libmtrace.so $(CFLAGS) $(MTRACE_O) -lelf

elftest: elftest.o symboltable.o
	gcc -o elftest $(CFLAGS) elftest.o symboltable.o -lelf

%-pic.o: %.c
	gcc -c -o $@ -MMD $(CFLAGS) $(PIC) $<

%.o: %.c
	gcc -c -o $@ -MMD $(CFLAGS) $<

%.s: %.c
	gcc -S -o $@ $(CFLAGS) $<

%-pic.s: %.c
	gcc -S -o $@ $(CFLAGS) $(PIC) $<

.PHONY: clean all
clean:
	rm -f *.o *.d *.memlog *.i *.s
	rm -f elftest
	rm -f libmtrace.so
	rm -f linux-gate.so

# Hack hack hack - grab the callgate page from memory...
linux-gate.so.1: Makefile
	dd if=/proc/self/mem bs=4096 skip=1048574 count=1 of=linux-gate.so.1 || touch linux-gate.so.1

-include *.d
