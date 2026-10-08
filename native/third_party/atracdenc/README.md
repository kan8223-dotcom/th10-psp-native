# atracdenc (ATRAC3 encoder subset)

The unified launcher (`native/psp_launcher/xmb_sound.cpp`) encodes the XMB
title music (SND0.AT3) on the PSP with this code.

- Upstream: https://github.com/dcherednik/atracdenc
- Commit: `cb170709f836cb43dc0312455188c791159d81dc` (2026-09-08)
- License: GNU LGPL 2.1 (`LICENSE`). The files here keep their notices. The
  launcher links them statically; its complete source, these files included,
  is published with the port, so the binary can be rebuilt and relinked.
- `src/lib/fft/kissfft_impl`: KISS FFT, BSD 3-clause (Mark Borgerding),
  the license text is in `kiss_fft.c`.
- `src/atrac/at3p/ff/atrac3plus_data.h`: tables from FFmpeg (LGPL 2.1 or
  later), as shipped by atracdenc.

## What is here

Only the ATRAC3 encoder and what it links against (`psp_launcher/atracdenc.mk`
lists the compiled files). `atrac/at1/atrac1.cpp` and
`atrac/at3p/at3p_tables.cpp` are included unmodified because
`atrac/atrac_scale.cpp` instantiates its scaler for ATRAC1 and ATRAC3plus too.
The PCM engine, WAV/OMA/RM containers, decoders and the ATRAC3plus encoder are
not included; `xmb_sound.cpp` drives `TAtrac3Encoder` directly and writes the
RIFF header itself (the layout of upstream `src/at3.cpp`).

## Local modifications

Three calls name their template type, because on the PSP toolchain (newlib)
`int32_t` is `long` and `uint32_t` is `unsigned long`, so the deduced types of
the two arguments differed (no change in behaviour):

- `src/atrac/at3/atrac3_bitstream.cpp`: `std::max<int32_t>(std::min<int32_t>(...))`
- `src/transient_detector.cpp`: `std::max<uint32_t>(1u, step / kChunks)` and
  `std::max<uint32_t>(1u, windowLen)`
