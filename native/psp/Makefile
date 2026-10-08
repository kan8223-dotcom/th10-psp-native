# TH10 headless runner for PSP (game logic + platform layer, no GE output).
# Same sources as native/CMakeLists.txt; objects go to obj/, never into the
# upstream tree.
TARGET := TH10PSP
TH10 := ../../th10_web/cpp
NATIVE := ..
OBJDIR := obj

GAME_SRCS := $(filter-out %/LegacyBridge.cpp %/LegacyCallbacks.cpp %/Exports.cpp %/Freestanding.cpp,$(wildcard $(TH10)/game/*.cpp))
PLATFORM_SRCS := $(wildcard $(TH10)/platform/*.cpp)
HOST_SRCS := HeadlessRenderer.cpp HeadlessFiles.cpp HeadlessFonts.cpp HeadlessAudio.cpp main.cpp

OBJS := $(patsubst $(TH10)/game/%.cpp,$(OBJDIR)/game/%.o,$(GAME_SRCS)) \
        $(patsubst $(TH10)/platform/%.cpp,$(OBJDIR)/platform/%.o,$(PLATFORM_SRCS)) \
        $(OBJDIR)/sdl/GraphicsHost.o \
        $(addprefix $(OBJDIR)/host/,$(HOST_SRCS:.cpp=.o)) \
        $(OBJDIR)/psp/psp_entry.o \
        $(OBJDIR)/softfloat.o

# psp-gcc predefines int32_t as long; the game code (like i386/ARM/wasm)
# assumes int. Same size and ABI, so align the typedefs for our objects.
INT32_AS_INT := -U__INT32_TYPE__ -D__INT32_TYPE__=int -U__UINT32_TYPE__ '-D__UINT32_TYPE__=unsigned int' \
    -U__INT_LEAST32_TYPE__ -D__INT_LEAST32_TYPE__=int -U__UINT_LEAST32_TYPE__ '-D__UINT_LEAST32_TYPE__=unsigned int'
COMMON := -O2 -G0 -march=allegrex -mtune=allegrex -fno-strict-aliasing -ffp-contract=off $(INT32_AS_INT)
CXXFLAGS := $(COMMON) -std=gnu++17 -fno-exceptions -fno-rtti -Wno-attributes \
    -DTH_SDL3=1 -DTH_NATIVE_PLATFORM=1 -DTH10_FILE_CACHE_LIMIT=0 -DTH10_PRELOAD_TRANSITIONS=0 -DTH10_ANM_COMPACT=1 -DTH10_CALL_COUNTERS=0 \
    -I$(NATIVE)/include -I../../portable/sdl
CFLAGS := $(COMMON) -std=c11 -w -ffunction-sections -fdata-sections -DSOFTFLOAT_FAST_INT64 -DINLINE_LEVEL=5

# --gc-sections needs KEEP() on the PSP module/NID/stub tables (script taken
# from the TH08 port, same psp-gcc 15.2 toolchain).
LDFLAGS := -Wl,--gc-sections -Wl,-T,psp_gc_sections.ld
LIBS := -lpspdebug -lpspdisplay -lpspge -lpsppower -lstdc++ -lm

BUILD_PRX := 0
PSP_FW_VERSION := 660
EXTRA_TARGETS := EBOOT.PBP
PSP_EBOOT_TITLE := TH10 PSP headless probe

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
$(OBJDIR)/psp/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJDIR)/softfloat.o: $(TH10)/rebuild/third_party/softfloat.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
