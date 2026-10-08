// The XMB title music: a clip of the user's own title theme, encoded on the
// PSP to ATRAC3 LP2 by atracdenc (third_party/atracdenc, LGPL-2.1) in its
// fast mode (no tonal components, no gain control). On this clip the fast
// mode kept the full mode's SNR (20 dB) at 2.4 times the speed (PC, 10-08).
// This file and the atracdenc objects are built with -fexceptions: atracdenc
// throws, and no exception may unwind into the launcher's -fno-exceptions
// code, so every call into it stays inside the try below.
#include "xmb_sound.hpp"

#include "atrac3denc.h"

#include <pspiofilemgr.h>
#include <pspthreadman.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace
{
void PutLe16(unsigned char *p, std::uint32_t value)
{
    p[0] = static_cast<unsigned char>(value);
    p[1] = static_cast<unsigned char>(value >> 8u);
}

void PutLe32(unsigned char *p, std::uint32_t value)
{
    PutLe16(p, value);
    PutLe16(p + 2u, value >> 16u);
}

// Collects the encoder's frames behind the header.
class FrameSink : public ICompressedOutput
{
public:
    FrameSink(unsigned char *data, std::size_t capacity)
        : data_(data), capacity_(capacity)
    {
    }
    std::string GetName() const override { return std::string(); }
    size_t GetChannelNum() const override { return 2u; }
    void WriteFrame(std::vector<char> frame) override
    {
        if (frame.size() != kTh10At3FrameBytes || capacity_ - size_ < frame.size())
        {
            failed_ = true;
            return;
        }
        std::memcpy(data_ + size_, frame.data(), frame.size());
        size_ += frame.size();
        ++frames_;
    }
    bool failed() const { return failed_; }
    std::uint32_t frames() const { return frames_; }

private:
    unsigned char *data_;
    std::size_t capacity_;
    std::size_t size_ = 0u;
    std::uint32_t frames_ = 0u;
    bool failed_ = false;
};

bool ReadAll(SceUID fd, void *destination, std::size_t bytes)
{
    auto *out = static_cast<unsigned char *>(destination);
    std::size_t done = 0u;
    while (done < bytes)
    {
        const int got = sceIoRead(fd, out + done, static_cast<SceSize>(bytes - done));
        if (got <= 0) return false;
        done += static_cast<std::size_t>(got);
    }
    return true;
}

std::size_t Encode(SceUID fd, const Th10XmbClip &clip, unsigned char *out,
                   std::size_t capacity, std::uint32_t *encode_us,
                   Th10XmbProgress progress)
{
    // 16 frames (64 KiB of PCM) per read.
    constexpr std::uint32_t kBlockFrames = 16u;
    const SceOff at = static_cast<SceOff>(clip.pcm_offset);
    if (sceIoLseek(fd, at, PSP_SEEK_SET) != at) return 0u;
    std::vector<std::int16_t> pcm(kBlockFrames * kTh10At3FrameSamples * 2u);
    std::vector<float> samples(kTh10At3FrameSamples * 2u);

    auto *sink = new FrameSink(out + kTh10At3HeaderBytes,
                               capacity - kTh10At3HeaderBytes);
    TCompressedOutputPtr output(sink);
    // Bitrate 0 picks LP2 (132300 bit/s, 384-byte frames, no joint stereo).
    NAtracDEnc::NAtrac3::TAtrac3EncoderSettings settings(
        0u, /*noGainControl*/ true, /*noTonalComponents*/ true, 2u, 0u, nullptr);
    if (settings.ConteinerParams->FrameSz != kTh10At3FrameBytes ||
        settings.ConteinerParams->Js)
    {
        return 0u;
    }
    NAtracDEnc::TAtrac3Encoder encoder(std::move(output), std::move(settings));
    const TPCMEngine::TProcessLambda process = encoder.GetLambda();
    const TPCMEngine::ProcessMeta meta = {2u};

    const std::uint64_t start = sceKernelGetSystemTimeWide();
    const std::uint32_t total = clip.frames * kTh10At3FrameSamples;
    std::uint32_t sample = 0u;
    for (std::uint32_t frame = 0u; frame < clip.frames; frame += kBlockFrames)
    {
        const std::uint32_t count = std::min(kBlockFrames, clip.frames - frame);
        if (!ReadAll(fd, pcm.data(),
                     static_cast<std::size_t>(count) * kTh10At3FrameSamples * 4u))
        {
            return 0u;
        }
        for (std::uint32_t k = 0u; k < count; ++k)
        {
            for (std::uint32_t i = 0u; i < kTh10At3FrameSamples; ++i, ++sample)
            {
                float gain = 1.0f;
                if (sample < clip.fade_in)
                    gain = static_cast<float>(sample) / static_cast<float>(clip.fade_in);
                const std::uint32_t left = total - sample;
                if (left < clip.fade_out)
                    gain *= static_cast<float>(left) / static_cast<float>(clip.fade_out);
                const std::int16_t *in = &pcm[(k * kTh10At3FrameSamples + i) * 2u];
                samples[i * 2u] = static_cast<float>(in[0]) / 32768.0f * gain;
                samples[i * 2u + 1u] = static_cast<float>(in[1]) / 32768.0f * gain;
            }
            process(samples.data(), meta);
        }
        if (progress) progress(frame + count, clip.frames);
    }
    // The encoder holds one frame back as look-ahead; a silent frame flushes it.
    std::fill(samples.begin(), samples.end(), 0.0f);
    process(samples.data(), meta);
    if (encode_us)
        *encode_us = static_cast<std::uint32_t>(sceKernelGetSystemTimeWide() - start);
    if (sink->failed() || sink->frames() != clip.frames) return 0u;
    th10_xmb_at3_header(out, clip.frames);
    return kTh10At3HeaderBytes +
           static_cast<std::size_t>(clip.frames) * kTh10At3FrameBytes;
}
} // namespace

void th10_xmb_at3_header(unsigned char header[kTh10At3HeaderBytes],
                         std::uint32_t frames)
{
    const std::uint32_t data_bytes =
        frames * static_cast<std::uint32_t>(kTh10At3FrameBytes);
    const std::uint32_t samples = frames * kTh10At3FrameSamples;
    const std::uint32_t end = (samples & ~0xfffu) - 0x2000u;
    std::memset(header, 0, kTh10At3HeaderBytes);
    std::memcpy(header, "RIFF", 4u);
    PutLe32(header + 4u, static_cast<std::uint32_t>(kTh10At3HeaderBytes) - 8u + data_bytes);
    std::memcpy(header + 8u, "WAVEfmt ", 8u);
    PutLe32(header + 16u, 32u);
    PutLe16(header + 20u, 0x0270u); // WAVE_FORMAT ATRAC3
    PutLe16(header + 22u, 2u);
    PutLe32(header + 24u, 44100u);
    PutLe32(header + 28u, static_cast<std::uint32_t>(kTh10At3FrameBytes) * 44100u /
                              kTh10At3FrameSamples);
    PutLe16(header + 32u, static_cast<std::uint32_t>(kTh10At3FrameBytes));
    PutLe16(header + 34u, 0u);
    PutLe16(header + 36u, 14u);
    // Extradata: 1, PCM bytes per frame (1024 samples x 2 channels x 2 bytes),
    // coding mode twice (0: stereo), 1, 0.
    PutLe16(header + 38u, 1u);
    PutLe32(header + 40u, kTh10At3FrameSamples * 4u);
    PutLe16(header + 48u, 1u);
    std::memcpy(header + 52u, "fact", 4u);
    PutLe32(header + 56u, 8u);
    PutLe32(header + 60u, end);
    PutLe32(header + 64u, 0x800u);
    // smpl: manufacturer, product, sample period (ns), MIDI unity note 60,
    // pitch, SMPTE format and offset, one loop, 24 bytes of loop data.
    std::memcpy(header + 68u, "smpl", 4u);
    PutLe32(header + 72u, 36u + 24u);
    PutLe32(header + 84u, 22676u);
    PutLe32(header + 88u, 60u);
    PutLe32(header + 104u, 1u);
    PutLe32(header + 108u, 24u);
    // The loop: cue point 0, type 0, from 0x800 to 0x2801 before the end.
    PutLe32(header + 120u, 0x800u);
    PutLe32(header + 124u, end - 0x2801u);
    std::memcpy(header + 136u, "data", 4u);
    PutLe32(header + 140u, data_bytes);
}

void th10_xmb_at3_silent_frame(unsigned char frame[kTh10At3FrameBytes])
{
    std::memset(frame, 0, kTh10At3FrameBytes);
    frame[0] = frame[192] = 0xa3u;
    frame[3] = frame[195] = 0x02u;
}

std::size_t th10_xmb_encode_clip(const char *bgm_path, const Th10XmbClip &clip,
                                 unsigned char *out, std::size_t capacity,
                                 std::uint32_t *encode_us,
                                 Th10XmbProgress progress)
{
    if (encode_us) *encode_us = 0u;
    // fact and smpl need a few whole 4096-sample blocks (header comment).
    if (!bgm_path || !out || clip.frames < 16u || capacity < kTh10At3HeaderBytes ||
        (capacity - kTh10At3HeaderBytes) / kTh10At3FrameBytes < clip.frames)
    {
        return 0u;
    }
    const SceUID fd = sceIoOpen(bgm_path, PSP_O_RDONLY, 0);
    if (fd < 0) return 0u;
    std::size_t result = 0u;
    try
    {
        result = Encode(fd, clip, out, capacity, encode_us, progress);
    }
    catch (...)
    {
        result = 0u;
    }
    sceIoClose(fd);
    return result;
}
