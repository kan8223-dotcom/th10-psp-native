/*
 * Import stubs for cfw_imports.h, written with PSPSDK's pspimport.s macros.
 * Each piece is built on its own (-DF_<piece>, as PSPSDK builds its stub
 * libraries) and archived per library in cfw_imports.mk, so a binary imports
 * only the functions it calls, in the order the CFW SDK stub archives gave.
 */
	.set noreorder

#include "pspimport.s"

#ifdef F_KUBridge_start
	IMPORT_START "KUBridge",0x40090000
#endif
#ifdef F_kuKernelGetModel
	IMPORT_FUNC "KUBridge",0x24331850,kuKernelGetModel
#endif
#ifdef F_kuKernelCall
	IMPORT_FUNC "KUBridge",0x9060F69D,kuKernelCall
#endif

#ifdef F_SystemCtrlForUser_start
	IMPORT_START "SystemCtrlForUser",0x40090000
#endif
#ifdef F_sctrlHENFindFunction
	IMPORT_FUNC "SystemCtrlForUser",0x159AF5CC,sctrlHENFindFunction
#endif
#ifdef F_sctrlKernelLoadExecVSHMs2
	IMPORT_FUNC "SystemCtrlForUser",0xABA7F1B0,sctrlKernelLoadExecVSHMs2
#endif
#ifdef F_sctrlKernelLoadExecVSHEf2
	IMPORT_FUNC "SystemCtrlForUser",0xAF22D576,sctrlKernelLoadExecVSHEf2
#endif
