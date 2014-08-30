
include ../Rules.mk

all: libmtrace.so elftest

libmtrace_objects = mtrace.o symboltable.o
libmtrace.a: $(libmtrace_objects)
libmtrace.$(SO): $(libmtrace_objects:%.o=%$(LO))
libmtrace.$(SO): private LIBS = -lelf

elftest: elftest.o symboltable.o
elftest: private LIBS = -lelf

.PHONY: clean all
clean:
	rm -f *.o */.deps/*.d *.memlog *.i *.s
	rm -f elftest
	rm -f *.a *.so *.so.*

-include .deps/*.d
