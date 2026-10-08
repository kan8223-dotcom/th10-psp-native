#include "wrap.h"

#include <pspkernel.h>
#include <pspsdk.h>
#include <pspsysmem_kernel.h>

PSP_MODULE_INFO("th07_ge4_texbw_v1_wrap", PSP_MODULE_KERNEL, 1, 0);
PSP_NO_CREATE_MAIN_THREAD();

extern int sceGeEdramSetSize(int size);
extern unsigned int sceGeEdramGetHwSize(void);

int ge4TexV1GetModel(void)
{
    return sceKernelGetModel();
}

unsigned int ge4TexV1GetEdramHwSize(void)
{
    return sceGeEdramGetHwSize();
}

int ge4TexV1SetEdramSize(unsigned int size)
{
    if (sceKernelGetModel() != 3)
        return -0x3d20;
    if (sceGeEdramGetHwSize() != 0x00400000u)
        return -0x3d21;
    if (size != 0x00200000u && size != 0x00400000u)
        return -0x3d22;
    return sceGeEdramSetSize((int)size);
}

int module_start(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    return 0;
}

int module_stop(void)
{
    return 0;
}
