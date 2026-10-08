# The FreeType 2.14.1 subset (../third_party/freetype, FreeType License) used
# by text_table.cpp: the base, the TrueType driver, the SFNT tables and the
# smooth renderer, with the local options in th10/ (freetype/README.md),
# built into $(FREETYPE_OUT) with its warnings off.
ifndef FREETYPE_DIR
FREETYPE_DIR := $(dir $(lastword $(MAKEFILE_LIST)))../third_party/freetype
endif
FREETYPE_OUT ?= freetype_obj
FREETYPE_SRC := src/base/ftbase.c src/base/ftinit.c src/base/ftsystem.c \
	src/base/ftglyph.c src/base/ftbitmap.c src/base/ftdebug.c src/base/ftmm.c \
	src/truetype/truetype.c src/sfnt/sfnt.c src/smooth/smooth.c
FREETYPE_OBJS := $(addprefix $(FREETYPE_OUT)/,$(FREETYPE_SRC:.c=.o))
FREETYPE_INC := -I$(FREETYPE_DIR)/th10 -I$(FREETYPE_DIR)/include

$(FREETYPE_OUT)/%.o: $(FREETYPE_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DFT2_BUILD_LIBRARY $(FREETYPE_INC) -w -c -o $@ $<

text_table.o: CXXFLAGS += $(FREETYPE_INC)
