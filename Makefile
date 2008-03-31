
include ../Rules.mk

CFLAGS += -Imtrace -fasynchronous-unwind-tables
CXXFLAGS += -Imtrace -fasynchronous-unwind-tables
LD = g++

all: libscg.so scgtest

libscg.so : alloc$(LO) node$(LO) output$(LO) pthread$(LO)
libscg.so :  mtrace/symboltable$(LO)
#libscg.so : alloc$(LO) node$(LO) output$(LO) symboltable$(LO)
libscg.so : automatic$(LO) version.ld -lunwind -lunwind-x86 -lelf -ldl

#scgtest: libscgtestfuncs.so libscg.so
scgtest: scgtestfuncs.o alloc.o node.o output.o mtrace/symboltable.o \
	-lelf -lunwind -lunwind-x86

libscgtestfuncs.so: scgtestfuncs$(LO)
scgtestfuncs-pic.o scgtestfuncs.o: CFLAGS+=-fno-inline

# We pick up symboltable.c from mtrace.
#vpath %.c ../mtrace

.PHONY: clean all

clean:
	rm -f libscg.a libscg.so* scgtest *.o */*.o .deps/*.d *.s *~

-include .deps/*.d
