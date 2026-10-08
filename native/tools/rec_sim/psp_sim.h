#pragma once
// PC stand-ins for the PSP SDK calls psp/Recorder.cpp makes (native/tools/rec_sim.cpp).
// Time runs `sim_time_scale` times faster than the host clock; files map ms0:/ -> $REC_SIM_ROOT/ms0/.
#include <cstdint>
#include <cstddef>
typedef int SceUID;typedef unsigned int SceSize;typedef long long SceOff;typedef unsigned int SceUInt;typedef long long SceInt64;
struct SceIoStat {int st_mode;int st_attr;SceOff st_size;};
struct SceIoDirent {SceIoStat d_stat;char d_name[256];void* d_private;int dummy;};
struct SceDevInf {unsigned int maxClusters,freeClusters,maxSectors;int sectorSize,sectorCount;};
struct SceDevctlCmd {SceDevInf* pdevinf;};
#define SCE_PR_GETDEV 0x02425818
#define PSP_O_RDONLY 0x0001
#define PSP_O_WRONLY 0x0002
#define PSP_O_RDWR 0x0003
#define PSP_O_APPEND 0x0100
#define PSP_O_CREAT 0x0200
#define PSP_O_TRUNC 0x0400
#define PSP_SEEK_SET 0
#define PSP_SEEK_CUR 1
#define PSP_SEEK_END 2
struct SceCtrlData {unsigned int TimeStamp,Buttons;unsigned char Lx,Ly,Rsrv[6];};
#define PSP_CTRL_SELECT 0x000001
struct ScePspDateTime {unsigned short year,month,day,hour,minute,second;unsigned int microsecond;};
#define PSP_POWER_TICK_ALL 0
#define PSP_THREAD_ATTR_USER 0x80000000
typedef int (*SceKernelThreadEntry)(SceSize,void*);
extern "C" {
uint64_t sceKernelGetSystemTimeWide(void);unsigned int sceKernelGetSystemTimeLow(void);
SceUID sceKernelCreateSema(const char*,unsigned,int,int,void*);int sceKernelWaitSema(SceUID,int,SceUInt*);int sceKernelSignalSema(SceUID,int);
SceUID sceKernelCreateThread(const char*,SceKernelThreadEntry,int,int,SceUInt,void*);int sceKernelStartThread(SceUID,SceSize,void*);
int sceKernelWaitThreadEnd(SceUID,SceUInt*);int sceKernelDeleteThread(SceUID);int sceKernelDelayThread(SceUInt);
void sceKernelDcacheWritebackInvalidateAll(void);void sceKernelDcacheWritebackInvalidateRange(const void*,unsigned);void sceKernelDcacheInvalidateRange(const void*,unsigned);void sceKernelDcacheWritebackRange(const void*,unsigned);
SceUID sceIoOpen(const char*,int,int);int sceIoWrite(SceUID,const void*,SceSize);int sceIoRead(SceUID,void*,SceSize);SceOff sceIoLseek(SceUID,SceOff,int);int sceIoClose(SceUID);
int sceIoRemove(const char*);int sceIoGetstat(const char*,SceIoStat*);int sceIoMkdir(const char*,int);int sceIoDevctl(const char*,unsigned,void*,int,void*,int);
SceUID sceIoDopen(const char*);int sceIoDread(SceUID,SceIoDirent*);int sceIoDclose(SceUID);
int sceCtrlPeekBufferPositive(SceCtrlData*,int);int sceRtcGetCurrentClockLocalTime(ScePspDateTime*);int scePowerTick(int);void pspSdkDisableFPUExceptions(void);
}
