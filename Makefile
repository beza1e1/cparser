-include config.mak

GOAL = $(BUILDDIR)/cparser

BUILDDIR ?= build

FIRM_CFLAGS ?= `pkg-config --cflags libfirm`
FIRM_LIBS   ?= `pkg-config --libs libfirm`

CPPFLAGS  = -DHAVE_CONFIG_H -DFIRM_BACKEND
CPPFLAGS += -I.
CPPFLAGS += $(FIRM_CFLAGS)

CFLAGS += -Wall -W -Wstrict-prototypes -Wmissing-prototypes -Werror -std=c99 -pedantic
CFLAGS += -O0 -g3
#CFLAGS += -O3 -march=pentium4 -fomit-frame-pointer -DNDEBUG
#CFLAGS += -pg -O3 -fno-inline
ICC_CFLAGS = -O0 -g3 -std=c99 -Wall -Werror
#LFLAGS += -pg
ICC    ?= true
GCCO1  ?= true

LFLAGS += $(FIRM_LIBS)

SOURCES := \
	adt/hashset.c \
	adt/strset.c \
	adt/obstack.c \
	adt/obstack_printf.c \
	adt/xmalloc.c \
	ast.c \
	ast2firm.c \
	diagnostic.c \
	entitymap.c \
	format_check.c \
	lexer.c \
	main.c \
	parser.c \
	symbol_table.c \
	token.c \
	type.c \
	types.c \
	type_hash.c \
	warning.c \
	write_fluffy.c \
	driver/firm_cmdline.c \
	driver/firm_timing.c \
	driver/firm_codegen.c \
	driver/firm_opt.c \
	driver/gen_firm_asm.c \

OBJECTS = $(SOURCES:%.c=build/%.o)

SPLINTS = $(addsuffix .splint, $(SOURCES))
CPARSERS = $(addsuffix .cparser, $(SOURCES))
CPARSEROS = $(SOURCES:%.c=build/cpb/%.o)
CPARSEROS2 = $(SOURCES:%.c=build/cpb2/%.o)

Q = @

all: $(DIRS) $(GOAL)

.PHONY: all clean dirs bootstrap bootstrap2

ifeq ($(findstring $(MAKECMDGOALS), clean depend),)
-include .depend
endif

%.h:
	@true

.depend: $(SOURCES)
	@echo "#define cparser_REVISION \"`svnversion -n .`\"" > .revision.h
	$(Q)if diff -Nq .revision.h revision.h > /dev/null; then \
	      rm .revision.h;                                    \
	    else                                                 \
	      echo "===> UPDATING revision.h";                   \
	      mv .revision.h revision.h;                         \
	    fi
	@echo "===> DEPEND"
	@rm -f $@ && touch $@ && makedepend -p "$@ build/" -Y -f $@ -- $(CPPFLAGS) -- $(SOURCES) 2> /dev/null && rm $@.bak

DIRS = build build/adt build/driver build/cpb build/cpb/adt build/cpb/driver build/cpb2 build/cpb2/adt build/cpb2/driver
UNUSED := $(shell mkdir -p $(DIRS))

$(GOAL): $(OBJECTS)
	@echo "===> LD $@"
	$(Q)$(CC) $(OBJECTS) $(LFLAGS) -o $(GOAL)

splint: $(SPLINTS)

selfcheck: $(CPARSERS)

bootstrap: build/cpb build/cpb/adt build/cpb/driver $(CPARSEROS) cparser.bootstrap

bootstrap2: build/cpb2 build/cpb2/adt build/cpb2/driver $(CPARSEROS2) cparser.bootstrap2

%.c.splint: %.c
	@echo '===> SPLINT $<'
	$(Q)splint $(CPPFLAGS) $<

%.c.cparser: %.c
	@echo '===> CPARSER $<'
	$(Q)./cparser $(CPPFLAGS) -fsyntax-only $<

$(DIRS):
	@echo "===> MKDIR $@"
	$(Q)mkdir -p $@

build/cpb/%.o: %.c build/cparser
	@echo '===> CPARSER $<'
	$(Q)./build/cparser $(CPPFLAGS) -Wall -g -c $< -o $@

build/cpb2/%.o: %.c cparser.bootstrap
	@echo '===> CPARSER.BOOTSTRAP $<'
	$(Q)./cparser.bootstrap $(CPPFLAGS) -Wall -g -c $< -o $@

cparser.bootstrap: $(CPARSEROS)
	@echo "===> LD $@"
	$(Q)./build/cparser $(CPARSEROS) $(LFLAGS) -o $@

cparser.bootstrap2: $(CPARSEROS2)
	@echo "===> LD $@"
	$(Q)./cparser.bootstrap $(CPARSEROS2) $(LFLAGS) -o $@

build/%.o: %.c
	@echo '===> CC $<'
	$(Q)$(ICC) $(CPPFLAGS) $(ICC_CFLAGS) -c $< -o $@
	$(Q)$(GCCO1) $(CPPFLAGS) $(CFLAGS) -O1 -c $< -o $@
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	@echo '===> CLEAN'
	$(Q)rm -rf build/* $(GOAL) .depend
