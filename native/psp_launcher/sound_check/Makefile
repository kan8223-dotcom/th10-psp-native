# S1 check of xmb_sound.cpp on the PSP CPU (never shipped): make -C sound_check
.DEFAULT_GOAL := all
override PATH := /usr/local/pspdev/bin:/usr/bin:/bin
override PSPDEV := /usr/local/pspdev
override PSPSDK := /usr/local/pspdev/psp/sdk
export PATH PSPDEV PSPSDK
vpath %.cpp ..
TARGET := th10_sound_check
EXTRA_TARGETS := EBOOT.PBP
PSP_EBOOT_TITLE := TH10 sound check
BUILD_PRX := 1
PSP_FW_VERSION := 660
PSP_LARGE_MEMORY := 1
include ../atracdenc.mk
OBJS := main.o xmb_sound.o $(ATRAC_OBJS)
INCDIR := ..
CFLAGS := -O2 -G0 -Wall -Wextra -Werror -DPSP
CXXFLAGS := -std=gnu++17 -fno-exceptions -fno-rtti
LIBS := -lstdc++ -lpsppower
include $(PSPSDK)/lib/build.mak
