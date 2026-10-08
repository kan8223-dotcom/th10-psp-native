#pragma once
/*
 * This port's own declarations of the custom-firmware exports it calls: the
 * KUBridge and SystemCtrlForUser libraries of ARK-5 and the PRO-family CFW.
 * cfw_imports.S imports them. The library names, NIDs, import attributes and
 * the KernelCallArg layout are interface facts of those modules; nothing here
 * comes from the CFW SDK headers.
 */
#include <psptypes.h>
#include <psploadexec_kernel.h> /* SceKernelLoadExecVSHParam (PSPSDK) */

#ifdef __cplusplus
extern "C" {
#endif

/* The argument block kuKernelCall hands to the kernel function: twelve words
 * in, two words back (56 bytes). */
typedef struct KernelCallArg
{
    u32 arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11, arg12;
    u32 ret1, ret2;
} KernelCallArg;

/* KUBridge */
int kuKernelGetModel(void);
int kuKernelCall(void *func_addr, KernelCallArg *args);

/* SystemCtrlForUser */
u32 sctrlHENFindFunction(const char *module, const char *library, u32 nid);
int sctrlKernelLoadExecVSHMs2(const char *file, struct SceKernelLoadExecVSHParam *param);
int sctrlKernelLoadExecVSHEf2(const char *file, struct SceKernelLoadExecVSHParam *param);

#ifdef __cplusplus
}
#endif
