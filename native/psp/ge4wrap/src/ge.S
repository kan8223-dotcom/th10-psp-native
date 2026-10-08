	.set noreorder

#include "pspstub.s"

	STUB_START "sceGe_driver",0x40090000,0x00020005
	STUB_FUNC  0x547EC5F0,sceGeEdramGetHwSize
	STUB_FUNC  0xD8633888,sceGeEdramSetSize
	STUB_END
