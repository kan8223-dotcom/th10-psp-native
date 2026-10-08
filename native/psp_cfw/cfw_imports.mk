# The CFW import stubs of cfw_imports.S as two archives, one object per piece,
# in the member order of the KUBridge and SystemCtrlForUser stub archives that
# came with the CFW SDK, so a binary's import table stays what those gave.
# Link $(CFW_LIBS) where those two libraries were linked.
# Included before build.mak defines `all`: keep the includer's default goal.
CFW_SAVED_GOAL := $(.DEFAULT_GOAL)
ifndef CFW_DIR
CFW_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
endif
CFW_OUT ?= cfw_obj
CFW_KUBRIDGE := KUBridge_start kuKernelGetModel kuKernelCall
CFW_SCTRL := SystemCtrlForUser_start sctrlHENFindFunction sctrlKernelLoadExecVSHMs2 sctrlKernelLoadExecVSHEf2
CFW_LIBS := $(CFW_OUT)/libth10cfw_kubridge.a $(CFW_OUT)/libth10cfw_sctrl.a

$(CFW_OUT)/cfw_%.o: $(CFW_DIR)cfw_imports.S
	@mkdir -p $(dir $@)
	$(CC) -I$(PSPSDK)/include -G0 -DF_$* -c -o $@ $<

$(CFW_OUT)/libth10cfw_kubridge.a: $(addprefix $(CFW_OUT)/cfw_,$(addsuffix .o,$(CFW_KUBRIDGE)))
	rm -f $@
	$(AR) cru $@ $^

$(CFW_OUT)/libth10cfw_sctrl.a: $(addprefix $(CFW_OUT)/cfw_,$(addsuffix .o,$(CFW_SCTRL)))
	rm -f $@
	$(AR) cru $@ $^

.DEFAULT_GOAL := $(CFW_SAVED_GOAL)
