BINOUT = ./bin/
PATHSRC = ./src/
TARGET_NAME = ge4wrap_texv1
TARGET = $(BINOUT)$(TARGET_NAME)
CC = psp-gcc

PSPSDK = $(shell psp-config --pspsdk-path)

CFLAGS = -Os -G0 -Wall -Wextra -Werror -I$(PSPSDK)/include \
	-D_PSP_FW_VERSION=660
ASFLAGS = -x assembler-with-cpp $(CFLAGS)
LDFLAGS = -Wl,-q,-T$(PSPSDK)/lib/linkfile.prx -nostartfiles \
	-Wl,-zmax-page-size=128 -L$(PSPSDK)/lib

C_FILES = $(wildcard $(PATHSRC)*.c)
S_FILES = $(wildcard $(PATHSRC)*.S)
PATHFILES = $(C_FILES) $(S_FILES)
OBJS = $(notdir $(patsubst %.c, %.o,$(patsubst %.S, %.o,$(PATHFILES))))
OBJS := $(sort $(OBJS:%.o=$(BINOUT)%.o))
OBJS += $(BINOUT)exports.o

LIBS = -lpspsysmem_kernel_660
FINAL_TARGET = $(TARGET).prx

.PHONY: all dirs clean

all: dirs $(FINAL_TARGET)

dirs:
	mkdir -p $(BINOUT)

$(OBJS): | dirs

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)
	psp-fixup-imports $@

%.prx: %.elf
	psp-prxgen $< $@

$(BINOUT)%.o: $(PATHSRC)%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BINOUT)%.o: $(PATHSRC)%.S
	$(CC) $(ASFLAGS) -c $< -o $@

$(PATHSRC)exports.c: $(PATHSRC)exports.exp
	psp-build-exports -b $< > $@

$(BINOUT)exports.o: $(PATHSRC)exports.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm -f $(BINOUT)* $(PATHSRC)exports.c
