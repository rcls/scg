##### CONFIGURATION #####

# The default target.
all:

GCC = gcc
CC = $(GCC)
CXX = g++
LD = $(GCC)
CFLAGS = -O2 -Wall -Werror -g3 -D_GNU_SOURCE -std=gnu99
CXXFLAGS = -O2 -Wall -Werror -g3 -D_GNU_SOURCE -std=c++11

# Extension for shared libaries.  HPUX changes this to sl, 'doze to dll.
SO = so

# Extra CFLAGS for making objects for shared libraries.
PICFLAGS = -fPIC

# Gcc flags for automatic dependency generation.
DEPENDENCY_CFLAGS = -MMD -MP -MF.deps/$(subst /,:,$@).d

COMPILE = $(CC) $(DEPENDENCY_CFLAGS) $(CFLAGS)
CCOMPILE = $(CXX) $(DEPENDENCY_CFLAGS) $(CXXFLAGS)
ARCHIVE = ar rcvus
LINK = $(LD) $(CFLAGS) $(LDFLAGS)
LINK.so = $(LD) $(CFLAGS) $(LDFLAGS) $(PICFLAGS) -shared -Wl,-soname=$(@F)$(MAJOR) -Wl,--no-undefined,--error-unresolved-symbols

# If we need special PICFLAGS for building objects for shared libraries, then we
# use TARGET-pic.o instead of TARGET.o for the object file, so we don't get
# confused with other object files.  This allows us to build both static and
# shared libraries in the same directory.
LO = $(if $(PICFLAGS),-pic.o,.o)

# Major version string for shared libs.
MAJOR =
# Minor version string for shared libs.
MINOR =

##### PATTERN RULES #####

lib%.a:
	$(ARCHIVE) $@ $^

lib%.$(SO):
	$(LINK.so) -o $@$(MAJOR)$(MINOR) $^ $(LIBS)
	$(if $(MINOR),ln -sf $@$(MAJOR)$(MINOR) $@$(MAJOR))
	$(if $(MAJOR),ln -sf $@$(MAJOR) $@)

# Wheee.... 2-to-the-power-of-3 is 8!
%.o: %.c
	@test -d .deps || mkdir .deps
	$(COMPILE) -c -o $@ $<

%.o: %.cc
	@test -d .deps || mkdir .deps
	$(CCOMPILE) -c -o $@ $<

%-pic.o: %.c
	@test -d .deps || mkdir .deps
	$(COMPILE) $(PICFLAGS) -c -o $@ $<

%-pic.o: %.cc
	@test -d .deps || mkdir .deps
	$(CCOMPILE) $(PICFLAGS) -c -o $@ $<

# %.s: %.c
# 	$(COMPILE) -S -o $@ $<

# %.s: %.cc
# 	$(CCOMPILE) -S -o $@ $<

# %-pic.s: %.c
# 	$(COMPILE) $(PICFLAGS) -S -o $@ $<

# %-pic.s: %.cc
# 	$(CCOMPILE) $(PICFLAGS) -S -o $@ $<

# For an executable FOO, we must have FOO.o or FOOapp.o.  That stops this rule
# being too nasty!
%: %.o
	$(LINK) -o $@ $^ $(LIBS)

%: %.c
%: %.cc

-include .deps/*.d
