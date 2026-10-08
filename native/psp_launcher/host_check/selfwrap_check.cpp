// Host check of the XMB self-wrap (build.sh): the launcher's data search,
// ICON0/PIC1 generation and slot commit over a PC folder standing for ms0:.
// usage: selfwrap_check <folder for ms0:> <EBOOT as ms0:/...>
#include "xmb_selfwrap.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char **argv)
{
    if (argc != 3 || std::strncmp(argv[2], "ms0:/", 5) != 0)
    {
        std::fprintf(stderr, "usage: %s <folder for ms0:> <ms0:/.../EBOOT.PBP>\n", argv[0]);
        return 2;
    }
    setenv("TH10_HOST_MS0", argv[1], 1);
    const char *eboot = argv[2];
    char appdir[640];
    std::snprintf(appdir, sizeof(appdir), "%s", eboot);
    *std::strrchr(appdir, '/') = '\0';

    char data_root[640];
    const int found = th10_unified_find_original_data(appdir, "ms0:", data_root,
                                                      sizeof(data_root));
    const char *root = found > 0 ? data_root : nullptr;
    std::printf("find_original_data=%d root=%s\n", found, root ? root : "-");
    std::printf("needs_generation=%d\n",
                th10_unified_selfwrap_needs_generation(eboot, root));
    const int wrapped = th10_unified_try_selfwrap(appdir, eboot, root);
    std::printf("try_selfwrap=%d\n", wrapped);
    std::printf("needs_generation_after=%d\n",
                th10_unified_selfwrap_needs_generation(eboot, root));
    return wrapped < 0 ? 1 : 0;
}
