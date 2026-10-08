.DEFAULT_GOAL := all

override SHELL := /bin/sh
override .SHELLFLAGS := -eu -c
override PATH := /usr/local/pspdev/bin:/usr/bin:/bin
override PSPDEV := /usr/local/pspdev
override PSPSDK := /usr/local/pspdev/psp/sdk
export PATH PSPDEV PSPSDK

TARGET := th10_unified_launcher
include atracdenc.mk
include freetype.mk
OBJS := main.o model_dispatch.o text_table.o xmb_selfwrap.o xmb_sound.o $(ATRAC_OBJS) \
	$(FREETYPE_OBJS)
EXTRA_TARGETS := EBOOT.PBP
BUILD_PRX := 1
PSP_FW_VERSION := 660
# MEMSIZE=1 like both TH10 runtime PBPs (TH07's launcher had 0). PPSSPP sizes
# user memory once, from the booted PBP's PARAM.SFO (PSPLoaders.cpp
# UseLargeMem), and keeps it across sceKernelLoadExec; a PSP-1000 ignores it.
PSP_LARGE_MEMORY := 1
PSP_EBOOT_TITLE := Touhou 10 PSP

# The distributable starts with no copyrighted XMB media.  A runtime hook may
# generate a local self-wrap only after validating the user's original data.
PSP_EBOOT_ICON := NULL
PSP_EBOOT_ICON1 := NULL
PSP_EBOOT_PIC0 := NULL
PSP_EBOOT_PIC1 := NULL
PSP_EBOOT_SND0 := NULL

# This target is the plain launcher input to tools/pack_unified_pbp.py.  The
# audited outer packer adds the reserved XMB area and the dual-runtime PSAR.
PSP_EBOOT_PSAR := NULL

INCDIR := .
CFLAGS := -O2 -G0 -Wall -Wextra -Werror -DPSP
CXXFLAGS := -std=gnu++17 -fno-exceptions -fno-rtti
ASFLAGS := $(CFLAGS)
# KUBridge (the model) and SystemCtrlForUser (LoadExec) from our own stubs.
include ../psp_cfw/cfw_imports.mk
EXTRA_CLEAN := $(CFW_LIBS) $(wildcard $(CFW_OUT)/*.o) $(FREETYPE_OBJS)
LIBS := $(CFW_LIBS) -lpng16 -lz -lstdc++ -lpsppower

include $(PSPSDK)/lib/build.mak
$(TARGET).elf: $(CFW_LIBS)
text_table.o: charset_th10_unicode.inc text_table.hpp
main.o: text_table.hpp
