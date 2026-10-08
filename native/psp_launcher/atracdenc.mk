# The ATRAC3 encoder subset of atracdenc (../third_party/atracdenc, LGPL-2.1)
# used by xmb_sound.cpp, built into $(ATRAC_OUT) with exceptions and RTTI on
# and its warnings off; everything else stays -fno-exceptions -fno-rtti.
ifndef ATRAC_DIR
ATRAC_DIR := $(dir $(lastword $(MAKEFILE_LIST)))../third_party/atracdenc/src
endif
ATRAC_OUT ?= atracdenc_obj
ATRAC_CXX_SRC := atrac3denc.cpp atrac/at3/atrac3.cpp atrac/at3/atrac3_bitstream.cpp \
	atrac/atrac_scale.cpp atrac/atrac_enc_cache.cpp atrac/atrac_psy_common.cpp \
	atrac/at1/atrac1.cpp atrac/at3p/at3p_tables.cpp lib/mdct/mdct.cpp \
	lib/bitstream/bitstream.cpp lib/bs_encode/encode.cpp qmf/qmf.cpp \
	transient_detector.cpp transient_spectral_upsampler.cpp env.cpp
ATRAC_C_SRC := lib/fft/kissfft_impl/kiss_fft.c lib/fft/kissfft_impl/tools/kiss_fftr.c
ATRAC_OBJS := $(addprefix $(ATRAC_OUT)/,$(ATRAC_CXX_SRC:.cpp=.o) $(ATRAC_C_SRC:.c=.o))
ATRAC_INC := -I$(ATRAC_DIR) -I$(ATRAC_DIR)/lib -I$(ATRAC_DIR)/lib/fft/kissfft_impl \
	-I$(ATRAC_DIR)/lib/fft/kissfft_impl/tools

$(ATRAC_OUT)/%.o: $(ATRAC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(ATRAC_INC) -fexceptions -frtti -w -c -o $@ $<

$(ATRAC_OUT)/%.o: $(ATRAC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(ATRAC_INC) -w -c -o $@ $<

xmb_sound.o: CXXFLAGS += $(ATRAC_INC) -fexceptions -frtti
