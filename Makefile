
include ../Rules.mk

CFLAGS += -I../mtrace
CXXFLAGS += -fno-exceptions -I../mtrace
LD = g++

all: libscg.so scgtest

libscg.so : alloc$(LO) node$(LO) output$(LO) pthread$(LO) symboltable$(LO)
#libscg.so : alloc$(LO) node$(LO) output$(LO) symboltable$(LO)
libscg.so : automatic$(LO) version.ld -lelf -ldl

scgtest: libscg.so

# We pick up symboltable.c from mtrace.
vpath %.c ../mtrace

.PHONY: clean all

clean:
	rm -f libscg.a libscg.so* scgtest *.o .deps/*.d *.s *~

-include .deps/*.d
