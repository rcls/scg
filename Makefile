
OBJECTS = alloc.o node.o output.o symboltable.o pthread.o
POBJECTS = ${OBJECTS:.o=-pic.o} automatic-pic.o

COMPILE = $(CC) $(CFLAGS)
CCOMPILE = $(CXX) $(CXXFLAGS)
CFLAGS = -O2 -Wall -Werror -D_GNU_SOURCE -fno-exceptions -std=gnu99
CXXFLAGS = -O2 -Wall -Werror -D_GNU_SOURCE -fno-exceptions

PIC = -fPIC

all: libscg.so scgtest

# The static library doesn't get auto-start.
libscg.a : $(OBJECTS)
	ar rcv libscg.a $(OBJECTS)

libscg.so : $(POBJECTS) version.ld
	g++ -shared -Wl,-soname=libscg.so -Wl,--version-script=version.ld -o libscg.so $(POBJECTS) -lelf -ldl

scgtest: scgtest.o libscg.so
	$(CCOMPILE) -o scgtest scgtest.o libscg.so -lelf -ldl

-include *.d

# Hack hack hack until we sort the directories out.
symboltable.h: ../mtrace/symboltable.h
	cp $< $@

symboltable.c: ../mtrace/symboltable.c
	cp $< $@


.PHONY: clean all
clean:
	rm -f libscg.a libscg.so scgtest *.o *.d *.s *~


# Wheee.... 2-to-the-power-of-3 is 8!
%.o: %.c
	$(COMPILE) -MMD -c -o $@ $<

%.o: %.cxx
	$(CCOMPILE) -MMD -c -o $@ $<

%-pic.o: %.c
	$(COMPILE) $(PIC) -MMD -c -o $@ $<

%-pic.o: %.cxx
	$(CCOMPILE) $(PIC) -MMD -c -o $@ $<

%.s: %.c
	$(COMPILE) -MMD -S -o $@ $<

%.s: %.cxx
	$(CCOMPILE) -MMD -S -o $@ $<

%-pic.s: %.c
	$(COMPILE) $(PIC) -MMD -S -o $@ $<

%-pic.s: %.cxx
	$(CCOMPILE) $(PIC) -MMD -S -o $@ $<
