// S1 check, never shipped: encodes the title-theme clip on the PSP CPU with
// ../xmb_sound.cpp and writes it as a plain .at3 beside this EBOOT with the
// encode time, to compare with the PC build (ffmpeg decode and SNR).
// thbgm.dat is read beside the EBOOT, else from ms0:/PSP/GAME/TH10HLDATA.
#include "../xmb_sound.hpp"

#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <psppower.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

PSP_MODULE_INFO("TH10SOUNDCHECK", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(-1024);

int main(int argc, char **argv)
{
    char dir[512] = "ms0:/PSP/GAME/TH10SND";
    if (argc > 0 && argv[0] && std::strrchr(argv[0], '/'))
        std::snprintf(dir, sizeof(dir), "%.*s", static_cast<int>(std::strrchr(argv[0], '/') - argv[0]), argv[0]);
    char bgm[640], out[640], log[640];
    std::snprintf(bgm, sizeof(bgm), "%s/thbgm.dat", dir);
    SceIoStat st;
    if (sceIoGetstat(bgm, &st) < 0) std::snprintf(bgm, sizeof(bgm), "ms0:/PSP/GAME/TH10HLDATA/thbgm.dat");
    std::snprintf(out, sizeof(out), "%s/sound_check.at3", dir);
    std::snprintf(log, sizeof(log), "%s/sound_check.log", dir);
    scePowerSetClockFrequency(333, 333, 166);
    // th10_02.wav (the title screen's music) from its start: 27 s, 20 ms in, 3 s out.
    const Th10XmbClip clip = {16u, 27u * 44100u / 1024u, 882u, 3u * 44100u};
    const std::size_t capacity = kTh10At3HeaderBytes + clip.frames * kTh10At3FrameBytes;
    auto *buffer = static_cast<unsigned char *>(std::malloc(capacity));
    std::uint32_t us = 0u;
    const std::size_t bytes = buffer ? th10_xmb_encode_clip(bgm, clip, buffer, capacity, &us, nullptr) : 0u;
    if (bytes)
    {
        const SceUID fd = sceIoOpen(out, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
        if (fd >= 0) { sceIoWrite(fd, buffer, bytes); sceIoClose(fd); }
    }
    char line[256];
    const int n = std::snprintf(line, sizeof(line), "bgm=%s bytes=%u frames=%u encode_us=%u cpu=%d bus=%d\n", bgm,
                                static_cast<unsigned>(bytes), static_cast<unsigned>(clip.frames), static_cast<unsigned>(us),
                                scePowerGetCpuClockFrequencyInt(), scePowerGetBusClockFrequencyInt());
    const SceUID fd = sceIoOpen(log, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) { sceIoWrite(fd, line, n); sceIoClose(fd); }
    sceKernelExitGame();
    return 0;
}
