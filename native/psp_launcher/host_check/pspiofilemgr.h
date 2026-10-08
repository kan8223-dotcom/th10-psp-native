#pragma once
// Host build of xmb_selfwrap.cpp (host_check/build.sh): the sceIo calls it
// makes, over POSIX files. "ms0:/x" maps to $TH10_HOST_MS0/x, "ef0:/x" to
// $TH10_HOST_EF0/x.
typedef int SceUID;
typedef long long SceOff;
typedef int SceMode;
typedef unsigned int SceSize;

struct SceIoStat
{
    SceMode st_mode;
    unsigned int st_attr;
    SceOff st_size;
};

#define PSP_O_RDONLY 0x0001
#define PSP_O_WRONLY 0x0002
#define PSP_O_RDWR (PSP_O_RDONLY | PSP_O_WRONLY)
#define PSP_O_APPEND 0x0100
#define PSP_O_CREAT 0x0200
#define PSP_O_TRUNC 0x0400
#define PSP_SEEK_SET 0
#define PSP_SEEK_CUR 1
#define PSP_SEEK_END 2
#define FIO_S_IFMT 0xF000
#define FIO_S_IFDIR 0x1000
#define FIO_S_IFREG 0x2000
#define FIO_S_ISREG(m) (((m) & FIO_S_IFMT) == FIO_S_IFREG)
#define FIO_S_ISDIR(m) (((m) & FIO_S_IFMT) == FIO_S_IFDIR)

SceUID sceIoOpen(const char *file, int flags, SceMode mode);
int sceIoClose(SceUID fd);
int sceIoRead(SceUID fd, void *data, SceSize size);
int sceIoWrite(SceUID fd, const void *data, SceSize size);
SceOff sceIoLseek(SceUID fd, SceOff offset, int whence);
int sceIoGetstat(const char *file, SceIoStat *stat);
int sceIoSync(const char *device, unsigned int unknown);
