#pragma once

#include <cstddef>
#include <cstdint>

// SND0.AT3 for the XMB: RIFF/WAVE ATRAC3 LP2 (132 kbps stereo, 384-byte
// frames of 1024 samples) with the header POP-FE writes for the SND0.AT3 it
// makes with atracdenc (sahlberg/pop-fe riff.py, create_riff with loop):
// "fmt ", a "fact" that ends the track 0x2000 samples before the last whole
// 4096-sample block and starts it at 0x800, a "smpl" loop over that range
// (the XMB loops it), then "data". The first device run's header (fact = all
// samples, then 1024; no smpl) showed the icon and background but no music
// (2026-10-08).
constexpr std::size_t kTh10At3HeaderBytes = 144u;
constexpr std::size_t kTh10At3FrameBytes = 384u;
constexpr std::uint32_t kTh10At3FrameSamples = 1024u;

// A clip of thbgm.dat (16-bit stereo 44.1 kHz PCM, located by thbgm.fmt).
struct Th10XmbClip
{
    std::uint32_t pcm_offset; // byte offset of the clip's first sample
    std::uint32_t frames;     // ATRAC3 frames to encode
    std::uint32_t fade_in;    // samples
    std::uint32_t fade_out;   // samples
};

// The header for `frames` frames: "fmt " (WAVE_FORMAT 0x270 with the 14-byte
// ATRAC3 extradata), "fact", "smpl" and the "data" chunk header. The RIFF
// size covers just these frames.
void th10_xmb_at3_header(unsigned char header[kTh10At3HeaderBytes],
                         std::uint32_t frames);

// One frame of silence as the encoder emits it (for the neutral placeholder).
void th10_xmb_at3_silent_frame(unsigned char frame[kTh10At3FrameBytes]);

// Called with the frames encoded so far, after every block of frames.
using Th10XmbProgress = void (*)(std::uint32_t done, std::uint32_t total);

// Encodes the clip into out as header + frames. Returns the byte count, or 0
// on a read error, allocation failure or any exception inside the encoder.
// The encoding time (without the header) goes to *encode_us.
std::size_t th10_xmb_encode_clip(const char *bgm_path, const Th10XmbClip &clip,
                                 unsigned char *out, std::size_t capacity,
                                 std::uint32_t *encode_us,
                                 Th10XmbProgress progress);
