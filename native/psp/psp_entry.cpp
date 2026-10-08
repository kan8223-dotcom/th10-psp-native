// PSP module header for the TH10 headless runner (native/main.cpp).
// 64 MB models: MEMSIZE=1 comes from build.mak (PSP_FW_VERSION > 390).
#include <pspkernel.h>
PSP_MODULE_INFO("TH10PSP", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
#ifdef TH10_MAIN_STACK_KB
PSP_MAIN_THREAD_STACK_SIZE_KB(TH10_MAIN_STACK_KB);   // PSP-1000 lane (TH08's 1000 lane used 512)
#else
PSP_MAIN_THREAD_STACK_SIZE_KB(1024);
#endif
// PSPSDK's _sbrk ignores a negative size: the heap is sceKernelMaxFreeMemSize()
// minus PSP_HEAP_THRESHOLD_SIZE_KB (512 KiB when it is not declared, as here),
// so this line keeps 512 KiB outside the heap, not 2 MiB (libcglue _sbrk.o, 2026-10-03).
PSP_HEAP_SIZE_KB(-2048);  // same as the TH07/TH08 Go builds that shipped
