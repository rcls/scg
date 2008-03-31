##### CONFIGURATION #####

# The default target.
all:

GCC = gcc
CC = $(GCC)
CXX = g++
LD = $(GCC)
CFLAGS = -O2 -Wall -Werror -g3 -D_GNU_SOURCE -std=gnu99 
CXXFLAGS = -O2 -Wall -Werror -g3 -D_GNU_SOURCE

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
LINK.so = $(LD) $(CFLAGS) $(LDFLAGS) $(PICFLAGS) -shared -Wl,-soname=$(@F)$(TT_MAJOR) -Wl,--no-undefined,--error-unresolved-symbols

# If we need special PICFLAGS for building objects for shared libraries, then we
# use TARGET-pic.o instead of TARGET.o for the object file, so we don't get
# confused with other object files.  This allows us to build both static and
# shared libraries in the same directory.
LO = $(if $(PICFLAGS),-pic.o,.o)

# Major version string for shared libs.
MAJOR =
# Minor version string for shared libs.
MINOR =

# "$(call TT,BAR)" is "$($@ BAR)" if that's defined, else "$(BAR)".  This is
# much like target-specific variables, but is not inherited by the
# prerequisites.  Yes, spaces in variable names are valid!
TT = $(if $(is_undefined_p.$(origin ${@F} $1)),$($1),$(${@F} $1))
is_undefined_p.undefined = undefined

# The TT instances we use.
TT_LIBS = $(call TT,LIBS)
TT_MAJOR = $(call TT,MAJOR)
TT_MINOR = $(call TT,MINOR)

##### PATTERN RULES #####

lib%.a:
	$(ARCHIVE) $@ $^

lib%.$(SO):
	$(LINK.so) -o $@$(TT_MAJOR)$(TT_MINOR) $^ $(TT_LIBS)
	$(if $(TT_MINOR),ln -sf $@$(TT_MAJOR)$(TT_MINOR) $@$(TT_MAJOR))
	$(if $(TT_MAJOR),ln -sf $@$(TT_MAJOR) $@)

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
	$(LINK) -o $@ $^ $(TT_LIBS)

%: %.c
%: %.cc

-include .deps/*.d
