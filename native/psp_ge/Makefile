# TH10 for PSP with the GE renderer (psp/GeRenderer.cpp: sceGu display
# lists, no PSPGL/SDL) and a vblank-paced main loop. Same game sources as
# ../psp (the headless probe), built into its own obj/ and EBOOT.PBP here.
TARGET := TH10GE
TH10 := ../../th10_web/cpp
NATIVE := ..
PSPDIR := ../psp
OBJDIR := obj

GAME_SRCS := $(filter-out %/LegacyBridge.cpp %/LegacyCallbacks.cpp %/Exports.cpp %/Freestanding.cpp,$(wildcard $(TH10)/game/*.cpp))
PLATFORM_SRCS := $(wildcard $(TH10)/platform/*.cpp)
HOST_SRCS := HeadlessFiles.cpp HeadlessFonts.cpp HeadlessAudio.cpp main.cpp

OBJS := $(patsubst $(TH10)/game/%.cpp,$(OBJDIR)/game/%.o,$(GAME_SRCS)) \
        $(patsubst $(TH10)/platform/%.cpp,$(OBJDIR)/platform/%.o,$(PLATFORM_SRCS)) \
        $(OBJDIR)/sdl/GraphicsHost.o \
        $(addprefix $(OBJDIR)/host/,$(HOST_SRCS:.cpp=.o)) \
        $(OBJDIR)/psp/psp_entry.o $(OBJDIR)/psp/GeRenderer.o $(OBJDIR)/psp/MeAudio.o $(OBJDIR)/psp/Ge4.o \
        $(OBJDIR)/softfloat.o
# Recording ('EXTRA_CXXFLAGS=... -DTH10_REC=1', psp/Recorder.cpp: SELECT ->
# MJPEG AVI in ms0:/VIDEO/TH10). Without the flag the object list, the
# libraries and the EBOOT are unchanged.
# PSP-1000 lane: the 4 MiB volatile partition as an arena (psp/VolatileArena.cpp).
ifneq ($(findstring -DTH10_VOLATILE_ARENA=1,$(EXTRA_CXXFLAGS)),)
OBJS += $(OBJDIR)/psp/VolatileArena.o
endif
ifneq ($(findstring -DTH10_REC=1,$(EXTRA_CXXFLAGS)),)
REC_OBJ := $(OBJDIR)/psp/Recorder.o
OBJS += $(REC_OBJ)
REC_LIBS := -lpsprtc
endif

# Soft-float census (a PPSSPP measurement build, make CENSUS=1 OBJDIR=obj_census): wraps libgcc's
# binary64 helpers and libm (psp/SoftfloatCensus.cpp; TH08 psp/softfloat_census.cpp fff2bf35).
ifeq ($(CENSUS),1)
OBJS += $(OBJDIR)/psp/SoftfloatCensus.o
CENSUS_WRAPS := __adddf3 __subdf3 __muldf3 __divdf3 __eqdf2 __nedf2 __gtdf2 __gedf2 __ltdf2 __ledf2 __unorddf2 __extendsfdf2 __truncdfsf2 __fixdfsi __fixunsdfsi __floatsidf __floatunsidf sin cos tan atan acos atan2 sqrt floor ceil fmod pow
override EXTRA_CXXFLAGS += -DTH10_SOFTFLOAT_CENSUS=1
endif
# psp-gcc predefines int32_t as long; the game code assumes int (see ../psp).
INT32_AS_INT := -U__INT32_TYPE__ -D__INT32_TYPE__=int -U__UINT32_TYPE__ '-D__UINT32_TYPE__=unsigned int' \
    -U__INT_LEAST32_TYPE__ -D__INT_LEAST32_TYPE__=int -U__UINT_LEAST32_TYPE__ '-D__UINT_LEAST32_TYPE__=unsigned int'
COMMON := -O2 -G0 -march=allegrex -mtune=allegrex -fno-strict-aliasing -ffp-contract=off $(INT32_AS_INT)
CXXFLAGS := $(COMMON) -std=gnu++17 -fno-exceptions -fno-rtti -Wno-attributes \
    -DTH_SDL3=1 -DTH_NATIVE_PLATFORM=1 -DTH10_FILE_CACHE_LIMIT=0 -DTH10_PRELOAD_TRANSITIONS=0 -DTH10_ANM_COMPACT=1 -DTH10_CALL_COUNTERS=0 -DTH10_GE=1 -DTH10_AUDIO_OUT=1 -DTH10_CHAIN_TIMING=1 -DTH10_RENDER_FAST_TRIG=1 -DTH10_RENDER_FAST_CULL=1 -DTH10_RENDER_FAST_PROJECT=1 \
    -I$(NATIVE)/include -I../../portable/sdl -I$(PSPDIR)/third_party/me-custom-core
CFLAGS := $(COMMON) -std=c11 -w -ffunction-sections -fdata-sections -DSOFTFLOAT_FAST_INT64 -DINLINE_LEVEL=5
# Header dependencies (a changed game header must rebuild every user).
CXXFLAGS += -MMD -MP
# Variant builds: make OBJDIR=obj_b EXTRA_CXXFLAGS=-DTH10_EXTENDED_INLINE=0 'PSP_EBOOT_TITLE=...'
CXXFLAGS += $(EXTRA_CXXFLAGS)

# Linker script: psp_hot.ld (tools/gen_hot_ld.py, hot_order.txt) for the hot layout build.
LDSCRIPT ?= $(PSPDIR)/psp_gc_sections.ld
LDFLAGS := -Wl,--gc-sections -Wl,-T,$(LDSCRIPT)
ifeq ($(CENSUS),1)
LDFLAGS += $(foreach w,$(CENSUS_WRAPS),-Wl,--wrap=$(w))
endif
# Media Engine audio: me-custom-core (built into mecc/ by `make mecc`); the CFW
# imports (KUBridge for the model check and kernel calls, SystemCtrlForUser for
# the GE4 wrapper) come from our own stubs, ../psp_cfw.
MECC_LIB := mecc/libme-core.a
CFW_OUT := $(OBJDIR)/cfw
include ../psp_cfw/cfw_imports.mk
LIBS := -Lmecc -lme-core $(CFW_LIBS) -lpspaudio -lpspgu -lpspctrl -lpspdebug -lpspdisplay -lpspge -lpsppower -lpspsdk -lstdc++ -lm
ifneq ($(REC_LIBS),)
LIBS += $(REC_LIBS)
endif

BUILD_PRX := 0
PSP_FW_VERSION := 660
EXTRA_TARGETS := EBOOT.PBP
PSP_EBOOT_TITLE := TH10 PSP (GE)

PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

$(OBJDIR)/game/%.o: $(TH10)/game/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJDIR)/platform/%.o: $(TH10)/platform/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJDIR)/sdl/%.o: $(TH10)/sdl/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJDIR)/host/%.o: $(NATIVE)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJDIR)/psp/%.o: $(PSPDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(MECC_LIB):
	PSPDEV=$${PSPDEV:-/usr/local/pspdev} cmake -S $(PSPDIR)/third_party/me-custom-core -B mecc -DCMAKE_BUILD_TYPE=Release
	cmake --build mecc --target me-core
$(TARGET).elf: $(MECC_LIB) $(CFW_LIBS)
$(OBJDIR)/softfloat.o: $(TH10)/rebuild/third_party/softfloat.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# GE 4 MiB wrapper deployed next to EBOOT.PBP: the Slim+/Go variant of the
# TH07 texbw_v1 wrapper (model >= 1; TH07 tools/build_ge4_slimplus_wrapper.py).
# Its base file (sha256 411e71b3...) accepts model 3 only, and the Go (model 4)
# then refuses with inner 0xffffc2e0.
GE4_WRAPPER := $(PSPDIR)/ge4wrap_texv1.prx
GE4_WRAPPER_SHA256 := 3dc5c753497349d6fb0ab5ae2a819b240cc51e8aa412ded10bb52daa540d841d
.PHONY: ge4-check
ge4-check:
	@echo "$(GE4_WRAPPER_SHA256)  $(GE4_WRAPPER)" | sha256sum -c --quiet - || { echo "wrong $(GE4_WRAPPER): need the Slim+/Go wrapper (sha256 3dc5c753)"; exit 1; }
EBOOT.PBP: ge4-check

-include $(OBJS:.o=.d)

# BUILD_ID (TH08 の TH08_PSP_BUILD_ID の流儀): EBOOT に埋め込み、結果ファイルの先頭に出す。
# 生成ヘッダにしてあるので、ID を変えると main.o が作り直される (TH08 の「clean 必須」の罠を避ける)。
TH10_BUILD_ID ?= dev
BUILD_ID_H := $(OBJDIR)/build_id.h
$(BUILD_ID_H): FORCE
	@mkdir -p $(dir $@)
	@echo '#define TH10_BUILD_ID "$(TH10_BUILD_ID)"' > $@.tmp; cmp -s $@.tmp $@ || mv $@.tmp $@; rm -f $@.tmp
.PHONY: FORCE
FORCE:
$(OBJDIR)/host/main.o: $(BUILD_ID_H)
ifneq ($(REC_OBJ),)
$(REC_OBJ): $(BUILD_ID_H)   # its log's first line is TH10_BUILD_ID=
endif
CXXFLAGS += -I$(OBJDIR)

# Stamps (cmp, like build_id.h). make tracks neither flags nor which OBJDIR
# made the shared TH10GE.elf/EBOOT.PBP, and build.mak's PARAM.SFO has no
# prerequisites: 10-01 an obj_c make after obj_h left the H EBOOT in place,
# and F/G/H all carried the title of an old PARAM.SFO (checklist C-1/C-2).
# Changed flags rebuild this OBJDIR's objects; a changed OBJDIR, linker
# script, BUILD_ID or title rebuilds main.o (so the ELF relinks; build.mak
# links $^, so the stamp itself cannot be an ELF prerequisite) and PARAM.SFO.
$(shell mkdir -p $(OBJDIR))
FLAGS_STAMP := $(OBJDIR)/flags.txt
$(FLAGS_STAMP): FORCE
	$(file >$@.tmp,$(CXXFLAGS) | $(CFLAGS))
	@cmp -s $@.tmp $@ || mv $@.tmp $@; rm -f $@.tmp
$(OBJS): $(FLAGS_STAMP)
LINK_STAMP := link_config.txt
$(LINK_STAMP): FORCE
	$(file >$@.tmp,$(OBJDIR) $(LDSCRIPT) $(TH10_BUILD_ID) $(PSP_EBOOT_TITLE))
	@cmp -s $@.tmp $@ || mv $@.tmp $@; rm -f $@.tmp
$(OBJDIR)/host/main.o: $(LINK_STAMP)
PARAM.SFO: $(LINK_STAMP)
