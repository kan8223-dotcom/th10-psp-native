#pragma once

#include <cstddef>

enum
{
    // The currently executing outer PBP could not be opened for writing.
    // The launcher may retry from its non-canonical helper copy.
    TH10_UNIFIED_SELFWRAP_DEFERRED = -7,
};

// Locate a complete, unmodified 東方風神録 v1.00a th10.dat/thbgm.dat pair
// where the game runtime will look for it: the `--data` folder named in
// th10run.txt beside the EBOOT, otherwise the EBOOT's own folder. Returns 1
// and writes that folder to out, 0 when no valid pair is there, or a
// negative value for invalid arguments.
extern "C" int th10_unified_find_original_data(
    const char *appdir, const char *launch_device, char *out,
    std::size_t out_size);

// Query the fixed media slots without modifying the PBP. Returns 1 only when
// valid original data is present and ICON0/PIC1/SND0 still need generating,
// 0 when no generation is needed, or a negative self-wrap error. Callers use
// this to show a power-off warning only around real first-run work.
extern "C" int th10_unified_selfwrap_needs_generation(
    const char *eboot_path, const char *data_root);

// Generate ICON0/PIC1 from the user's own th10.dat, and SND0 (the title theme,
// encoded to ATRAC3 from the user's own thbgm.dat), inside immutable fixed PBP
// media slots. The packer must have emitted the TH10XMB3 marker and neutral
// owned placeholders (transparent PNGs, a second of silence). Passing
// null/empty data_root repairs a torn owned slot back to those placeholders
// without original data.
//
// Returns 1 after committing newly generated fixed-slot media, 0 when the PBP was
// already wrapped (or already neutral with no data), and a negative value on
// safe failure. Runtime never writes the PBP header or DATA.PSP/PSAR regions.
extern "C" int th10_unified_try_selfwrap(
    const char *appdir, const char *eboot_path, const char *data_root);

// The microseconds the last generation spent encoding SND0 (0 when it did not
// encode), for the launcher's log.
extern "C" unsigned int th10_unified_last_sound_encode_us(void);

// A display hook for the generation's progress, called with permille (0-1000)
// from inside th10_unified_try_selfwrap. Null removes it.
extern "C" void th10_unified_set_progress(void (*hook)(unsigned int permille));
