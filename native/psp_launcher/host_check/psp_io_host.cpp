// POSIX stand-ins for the sceIo calls in pspiofilemgr.h (host check only).
#include "pspiofilemgr.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
const int kNoEntry = static_cast<int>(0x80010002u);

bool HostPath(const char *file, std::string *out)
{
    const char *root = nullptr;
    if (std::strncmp(file, "ms0:", 4) == 0) root = std::getenv("TH10_HOST_MS0");
    else if (std::strncmp(file, "ef0:", 4) == 0) root = std::getenv("TH10_HOST_EF0");
    if (!root) return false;
    *out = std::string(root) + (file + 4);
    return true;
}
} // namespace

SceUID sceIoOpen(const char *file, int flags, SceMode mode)
{
    std::string path;
    if (!HostPath(file, &path)) return kNoEntry;
    int host = (flags & PSP_O_RDWR) == PSP_O_RDWR ? O_RDWR
               : (flags & PSP_O_WRONLY)           ? O_WRONLY
                                                  : O_RDONLY;
    if (flags & PSP_O_APPEND) host |= O_APPEND;
    if (flags & PSP_O_CREAT) host |= O_CREAT;
    if (flags & PSP_O_TRUNC) host |= O_TRUNC;
    const int fd = open(path.c_str(), host, mode);
    return fd < 0 ? kNoEntry : fd;
}

int sceIoClose(SceUID fd) { return close(fd); }

int sceIoRead(SceUID fd, void *data, SceSize size)
{
    return static_cast<int>(read(fd, data, size));
}

int sceIoWrite(SceUID fd, const void *data, SceSize size)
{
    return static_cast<int>(write(fd, data, size));
}

SceOff sceIoLseek(SceUID fd, SceOff offset, int whence)
{
    return lseek(fd, offset, whence == PSP_SEEK_SET   ? SEEK_SET
                             : whence == PSP_SEEK_CUR ? SEEK_CUR
                                                      : SEEK_END);
}

int sceIoGetstat(const char *file, SceIoStat *stat_out)
{
    std::string path;
    struct stat st;
    if (!HostPath(file, &path) || stat(path.c_str(), &st) != 0) return kNoEntry;
    stat_out->st_mode = S_ISDIR(st.st_mode) ? FIO_S_IFDIR : FIO_S_IFREG;
    stat_out->st_attr = 0u;
    stat_out->st_size = st.st_size;
    return 0;
}

int sceIoSync(const char *, unsigned int)
{
    sync();
    return 0;
}
