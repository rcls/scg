
include ../Rules.mk

all: libmtrace.so elftest linux-gate.so.1

libmtrace_objects = mtrace.o symboltable.o
libmtrace.a: $(libmtrace_objects)
libmtrace.$(SO): $(libmtrace_objects:%.o=%$(LO))
libmtrace.$(SO) LIBS = -lelf

elftest: elftest.o symboltable.o
elftest LIBS = -lelf

.PHONY: clean all
clean:
	rm -f *.o */.deps/*.d *.memlog *.i *.s
	rm -f elftest
	rm -f *.a *.so *.so.*

# Hack hack hack - grab the call gate page from memory...
linux-gate.so.1:
	dd if=/proc/self/mem bs=4096 skip=1048574 count=1 of=$@ || touch $@

-include .deps/*.d
