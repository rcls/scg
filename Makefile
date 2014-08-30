
include Rules.mk

CFLAGS += -Imtrace -fomit-frame-pointer
CXXFLAGS += -Imtrace -fomit-frame-pointer
LD = g++

all: libscg.so scgtest

libscg.so: alloc$(LO) node$(LO) output$(LO) pthread$(LO)
libscg.so: mtrace/symboltable$(LO)
libscg.so: automatic$(LO) version.ld

libscg.so: private LIBS = -lunwind -lelf -ldl

scgtest: libscgtestfuncs.so libscg.so

libscgtestfuncs.so: scgtestfuncs$(LO)
scgtestfuncs-pic.o scgtestfuncs.o: CFLAGS+=-fno-inline

# We pick up symboltable.c from mtrace.
#vpath %.c ../mtrace

.PHONY: clean all

clean:
	rm -f libscg.a libscg.so* scgtest *.o */*.o .deps/*.d *.s *~

-include .deps/*.d
